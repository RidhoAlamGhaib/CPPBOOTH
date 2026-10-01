#include "EdsdkWrapper.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

namespace eds {

std::vector<std::string> Loader::candidatePaths(const std::string& explicitPath) {
    std::vector<std::string> out;
    if (!explicitPath.empty()) out.push_back(explicitPath);

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString srcDir = QDir(exeDir).filePath("../..");  // dev tree fallback

    const QStringList rel = {
        "EDSDK.dll",
        "EDSDK/Dll/EDSDK.dll",
        "edsdk/EDSDK.dll",
        "Dll/EDSDK.dll",
    };
    for (const auto& r : rel) {
        out.push_back(QDir(exeDir).filePath(r).toStdString());
        out.push_back(QDir(srcDir).filePath(r).toStdString());
    }

    // Canon software installs (last resort)
    out.push_back("C:/Program Files/Canon/EOS Webcam Utility Pro/EDSDK.dll");
    out.push_back("C:/Program Files (x86)/Canon/EOS Utility/EU3/EDSDK.dll");
    out.push_back("C:/Program Files (x86)/Canon/EOS Utility/EDSDK.dll");
    return out;
}

bool Loader::load(const std::string& explicitPath) {
    unload();

    // Names of every entry point we cannot work without.
    static const char* kRequired[] = {
        "EdsInitializeSDK", "EdsTerminateSDK",
        "EdsGetCameraList", "EdsGetChildCount", "EdsGetChildAtIndex",
        "EdsOpenSession", "EdsCloseSession", "EdsRelease",
        "EdsSetPropertyData", "EdsSetCapacity", "EdsSendCommand",
        "EdsSetObjectEventHandler",
        "EdsCreateMemoryStream", "EdsCreateFileStream",
        "EdsGetLength", "EdsGetPointer",
        "EdsCreateEvfImageRef", "EdsDownloadEvfImage",
        "EdsGetDirectoryItemInfo", "EdsDownload", "EdsDownloadComplete",
    };

    for (const auto& p : candidatePaths(explicitPath)) {
        const QString qp = QString::fromStdString(p);
        if (!QFileInfo::exists(qp)) continue;

        // EdsImage.dll sits next to EDSDK.dll — make it findable.
        SetDllDirectoryW(reinterpret_cast<LPCWSTR>(QFileInfo(qp).absolutePath().utf16()));

        HMODULE h = LoadLibraryA(p.c_str());
        if (!h) {
            qWarning() << "[EDS] LoadLibrary failed for" << qp << "err" << GetLastError();
            continue;
        }

        // Verify every required symbol exists before committing to this DLL.
        bool complete = true;
        for (const char* sym : kRequired) {
            if (!GetProcAddress(h, sym)) {
                qWarning() << "[EDS]" << qp << "missing" << sym;
                complete = false;
                break;
            }
        }
        if (!complete) { FreeLibrary(h); continue; }

        hDll_ = h;

        #define BIND(field, sym) \
            api_.field = reinterpret_cast<decltype(api_.field)>(GetProcAddress(hDll_, sym))

        BIND(InitializeSDK,         "EdsInitializeSDK");
        BIND(TerminateSDK,          "EdsTerminateSDK");
        BIND(GetCameraList,         "EdsGetCameraList");
        BIND(GetChildCount,         "EdsGetChildCount");
        BIND(GetChildAtIndex,       "EdsGetChildAtIndex");
        BIND(OpenSession,           "EdsOpenSession");
        BIND(CloseSession,          "EdsCloseSession");
        BIND(Release,               "EdsRelease");
        BIND(Retain,                "EdsRetain");
        BIND(SetPropertyData,       "EdsSetPropertyData");
        BIND(GetPropertyData,       "EdsGetPropertyData");
        BIND(SetCapacity,           "EdsSetCapacity");
        BIND(SendCommand,           "EdsSendCommand");
        api_.SendStatusCommand = reinterpret_cast<decltype(api_.SendStatusCommand)>(
            GetProcAddress(hDll_, "EdsSendStatusCommand"));
        BIND(SetObjectEventHandler, "EdsSetObjectEventHandler");
        api_.SetPropertyEventHandler = reinterpret_cast<decltype(api_.SetPropertyEventHandler)>(
            GetProcAddress(hDll_, "EdsSetPropertyEventHandler"));
        api_.SetCameraStateEventHandler = reinterpret_cast<decltype(api_.SetCameraStateEventHandler)>(
            GetProcAddress(hDll_, "EdsSetCameraStateEventHandler"));
        BIND(CreateMemoryStream,    "EdsCreateMemoryStream");
        BIND(CreateFileStream,      "EdsCreateFileStream");
        BIND(GetLength,             "EdsGetLength");
        BIND(GetPointer,            "EdsGetPointer");
        BIND(CreateEvfImageRef,     "EdsCreateEvfImageRef");
        BIND(DownloadEvfImage,      "EdsDownloadEvfImage");
        BIND(GetDirectoryItemInfo,  "EdsGetDirectoryItemInfo");
        BIND(Download,              "EdsDownload");
        BIND(DownloadComplete,      "EdsDownloadComplete");
        BIND(GetEvent,              "EdsGetEvent");

        #undef BIND

        dllPath_ = p;
        qInfo() << "[EDS] loaded" << qp;
        return true;
    }

    qWarning() << "[EDS] no usable EDSDK.dll found";
    return false;
}

void Loader::unload() {
    if (hDll_) { FreeLibrary(hDll_); hDll_ = nullptr; }
    api_ = {};
    dllPath_.clear();
}

const char* errorName(Error e) {
    switch (e) {
        case OK:                      return "OK";
        case DEVICE_NOT_FOUND:        return "DEVICE_NOT_FOUND";
        case DEVICE_BUSY_D:           return "DEVICE_BUSY";
        case DEVICE_INVALID:          return "DEVICE_INVALID";
        case DEVICE_EMERGENCY:        return "DEVICE_EMERGENCY";
        case DEVICE_MEMORY_FULL:      return "DEVICE_MEMORY_FULL";
        case DEVICE_INTERNAL_ERROR:   return "DEVICE_INTERNAL_ERROR";
        case DEVICE_INVALID_PARAM:    return "DEVICE_INVALID_PARAMETER";
        case DEVICE_NO_DISK:          return "DEVICE_NO_DISK";
        case DEVICE_DISK_ERROR:       return "DEVICE_DISK_ERROR";
        case DEVICE_CF_GATE_CHANGED:  return "DEVICE_CF_GATE_CHANGED";
        case DEVICE_DIAL_CHANGED:     return "DEVICE_DIAL_CHANGED";
        case DEVICE_NOT_INSTALLED:    return "DEVICE_NOT_INSTALLED";
        case OBJECT_NOTREADY_EVF: return "OBJECT_NOTREADY";
        case DEVICE_STAY_AWAKE:       return "DEVICE_STAY_AWAKE";
        case DEVICE_NOT_RELEASED:     return "DEVICE_NOT_RELEASED";
        case DEVICE_BUSY:             return "DEVICE_BUSY (comm layer)";
        case OBJECT_NOTREADY:         return "OBJECT_NOTREADY";
        case PROPERTIES_UNAVAILABLE:  return "PROPERTIES_UNAVAILABLE";
        case COMM_DISCONNECTED:       return "COMM_DISCONNECTED";
        case INVALID_HANDLE:          return "INVALID_HANDLE";
        default:                      return "UNKNOWN";
    }
}

const char* errorHint(Error e) {
    switch (e) {
        case DEVICE_NOT_FOUND:
            return "Camera not seen by EDSDK. Check: USB cable plugged in, "
                   "camera powered ON, mode dial NOT on Auto/Video.";
        case DEVICE_BUSY_D:
        case DEVICE_BUSY:
            return "Camera is busy/locked by another program. Close EOS Utility, "
                   "EOS Webcam Utility, or any other Canon software, then retry.";
        case DEVICE_INTERNAL_ERROR:
            return "Camera USB link glitched. Unplug USB, wait 5s, power-cycle "
                   "the camera, plug back in, then retry. Try a different USB port/cable.";
        case DEVICE_NOT_INSTALLED:
            return "No camera driver / camera not recognized by Windows. "
                   "Check Device Manager for a driver warning icon.";
        case OBJECT_NOTREADY_EVF:
            return "Live View engine not ready. Enable 'Live View shoot.' in the "
                   "camera's menu, set the mode dial to P/Av/Tv/M (not Auto/Movie), "
                   "and close any other Canon software holding the camera.";
        case DEVICE_INVALID:
        case DEVICE_INVALID_PARAM:
            return "Camera model/state EDSDK doesn't expect. Set mode dial to "
                   "P, Av, Tv or M (not Auto, not Movie).";
        case DEVICE_MEMORY_FULL:
            return "Camera SD card is full — free space or swap the card.";
        case DEVICE_NO_DISK:
            return "No SD card inserted in the camera.";
        case PROPERTIES_UNAVAILABLE:
            return "This camera body doesn't expose that feature over EDSDK "
                   "(common for live view on some models). App will fall back to OpenCV.";
        case COMM_DISCONNECTED:
            return "USB connection dropped mid-session. Reconnect the cable.";
        default:
            return "Unrecognized code — check Canon's EDSDK error list, or "
                   "just power-cycle the camera and retry.";
    }
}

} // namespace eds