#include "CameraThread.h"
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QFile>
#include <QCoreApplication>
#include <QDebug>
#include <windows.h>

// Single instance pointer so the C-style EDSDK callback can find us.
static CameraThread* g_self = nullptr;

// ══════════════════════════════════════════════════════
//  ctor / dtor
// ══════════════════════════════════════════════════════

CameraThread::CameraThread(QObject* parent) : QThread(parent) {
    g_self = this;
}

CameraThread::~CameraThread() {
    stop();
    if (g_self == this) g_self = nullptr;
}

void CameraThread::stop() {
    running_ = false;
    if (!wait(5000)) {
        // Thread stuck (e.g. blocked in a slow USB call). Terminate is unsafe
        // for EDSDK state, but leaving the camera locked is worse — force the
        // SDK teardown so the camera doesn't stay busy for the next launch.
        stopEdsdk();
        terminate();
        wait(1000);
    }
}

// ══════════════════════════════════════════════════════
//  thread body
// ══════════════════════════════════════════════════════

void CameraThread::run() {
    running_ = true;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (preferEdsdk_.load() && tryStartEdsdk()) {
        source_ = Source::Edsdk;
    } else if (edsdkCaptureOnly_.load()) {
        // EDSDK session is open and capture works, but EVF live view is dead.
        // Keep the EDSDK session alive for full-res capture; drive the preview
        // from OpenCV (EOS Webcam Utility / any webcam) instead.
        if (openOpenCv()) {
            source_ = Source::OpenCv;
            emit cameraInfo(QStringLiteral("📷 Canon EDSDK capture (OpenCV preview — EVF unavailable)"));
        } else {
            source_ = Source::None;
            emit cameraInfo(QStringLiteral("⚠ EDSDK capture ready but no preview camera found"));
        }
    } else {
        stopEdsdk();
        if (openOpenCv()) source_ = Source::OpenCv;
        else              source_ = Source::None;
    }

    int reconnectCountdown = 0;

    while (running_.load()) {
        cv::Mat raw;
        const Source src = source_.load();

        if (src == Source::Edsdk)       raw = grabEdsdkEvf();
        else if (src == Source::OpenCv) raw = grabOpenCv();

        if (raw.empty()) {
            emit cameraConnected(false);

            if (src == Source::Edsdk) {
                // EVF hiccups are normal; only fall back after a long streak
                if (++evfFailStreak_ > 150) {              // ~5 s at 30 fps
                    emit errorText(QStringLiteral("EDSDK live view stopped — switching to OpenCV"));
                    stopEdsdk();
                    source_ = openOpenCv() ? Source::OpenCv : Source::None;
                    evfFailStreak_ = 0;
                }
                pumpWindowsMessages();
                msleep(20);
                continue;
            }

            // OpenCV / None → periodic reconnect
            if (--reconnectCountdown <= 0) {
                emit cameraInfo(QStringLiteral("⚠ No camera — reconnecting…"));
                closeOpenCv();
                source_ = openOpenCv() ? Source::OpenCv : Source::None;
                reconnectCountdown = 60;                    // retry ~2 s
            }
            msleep(33);
            continue;
        }

        evfFailStreak_ = 0;
        emit cameraConnected(true);

        cv::Mat processed = applyAdjustments(raw);
        {
            QMutexLocker lk(&frameMu_);
            lastProcessed_ = processed.clone();
        }

        cv::Mat mirrored;
        cv::flip(processed, mirrored, 1);
        cv::Mat rgb;
        cv::cvtColor(mirrored, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                   QImage::Format_RGB888);
        emit frameReady(img.copy());

        if (src == Source::Edsdk) {
            if (eds_ && eds_->api().GetEvent) eds_->api().GetEvent();
            pumpWindowsMessages();
        } else if (edsdkCaptureOnly_.load()) {
            // Preview is OpenCV, but the EDSDK session is still open for capture —
            // keep pumping its event queue so the shutter callback can fire.
            if (eds_ && eds_->api().GetEvent) eds_->api().GetEvent();
            pumpWindowsMessages();
        }
        msleep(1000 / fps_.load());
    }

    stopEdsdk();
    closeOpenCv();
    CoUninitialize();
}

// ══════════════════════════════════════════════════════
//  EDSDK — startup
// ══════════════════════════════════════════════════════

bool CameraThread::tryStartEdsdk() {
    // Per-step stopwatch so we can see EXACTLY which EDSDK call is slow or
    // where live-view startup stalls. Every step logs its elapsed ms.
    QElapsedTimer stepTimer; stepTimer.start();
    auto STEP = [&](const char* what, eds::Error e = eds::OK) {
        qInfo().noquote() << QString("[STEP +%1ms] %2%3")
            .arg(stepTimer.restart(), 5)
            .arg(what)
            .arg(e == eds::OK ? "" : QString(" -> 0x%1 (%2)")
                 .arg(e, 8, 16, QChar('0')).arg(eds::errorName(e)));
    };

    QString dllPath;
    { QMutexLocker lk(&cfgMu_); dllPath = edsdkPath_; }

    eds_ = std::make_unique<eds::Loader>();
    if (!eds_->load(dllPath.toStdString())) {
        eds_.reset();
        return false;
    }
    STEP("DLL loaded");
    const auto& api = eds_->api();

    eds::Error err = api.InitializeSDK();
    STEP("InitializeSDK", err);
    if (err != eds::OK) {
        emit errorText(QString("EdsInitializeSDK failed 0x%1").arg(err, 8, 16, QChar('0')));
        eds_.reset();
        return false;
    }
    sdkInited_ = true;

    pumpWindowsMessages();
    msleep(300);

    eds::BaseRef listRaw = nullptr;
    err = api.GetCameraList(&listRaw);
    STEP("GetCameraList", err);
    if (err != eds::OK) {
        emit errorText(QString("EdsGetCameraList failed 0x%1").arg(err, 8, 16, QChar('0')));
        return false;
    }
    eds::Handle list = eds_->wrap(listRaw);

    int count = 0;
    api.GetChildCount(list.get(), &count);
    STEP(count > 0 ? "GetChildCount (camera found)" : "GetChildCount (NONE)");
    if (count <= 0) {
        emit errorText(QStringLiteral("No Canon camera detected by EDSDK"));
        return false;
    }

    eds::BaseRef camRaw = nullptr;
    err = api.GetChildAtIndex(list.get(), 0, &camRaw);
    if (err != eds::OK || !camRaw) {
        emit errorText(QStringLiteral("EdsGetChildAtIndex failed"));
        return false;
    }
    camera_ = camRaw;   // we own it now; released in stopEdsdk()

    // Let the enumerated camera reference settle before opening a session —
    // same reasoning as above, this is what a normal Win32 message-loop app
    // gets "for free" that a bare worker thread does not.
    pumpWindowsMessages();
    msleep(200);

    // Retry OpenSession a few times — a fresh USB enumeration sometimes
    // isn't ready for PTP session negotiation on the very first attempt.
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = api.OpenSession(camera_);
        STEP(QString("OpenSession attempt %1").arg(attempt).toLatin1().constData(), err);
        if (err == eds::OK) break;

        if ((err == eds::DEVICE_INTERNAL_ERROR || err == eds::DEVICE_BUSY_D ||
             err == eds::DEVICE_BUSY) && attempt < 2) {
            pumpWindowsMessages();
            msleep(500 * (attempt + 1));
            continue;
        }
        break;
    }
    if (err != eds::OK) {
        emit errorText(QString("EdsOpenSession failed 0x%1 (%2) — %3")
                           .arg(err, 8, 16, QChar('0'))
                           .arg(eds::errorName(err))
                           .arg(eds::errorHint(err)));
        return false;
    }
    sessionOpen_ = true;

    // Camera model name (nice for the UI)
    QString model = QStringLiteral("Canon");
    if (api.GetPropertyData) {
        char nameBuf[64] = {0};
        if (api.GetPropertyData(camera_, eds::PROP_ProductName, 0, sizeof(nameBuf), nameBuf) == eds::OK)
            model = QString::fromLatin1(nameBuf).trimmed();
    }
    qInfo().noquote() << "[STEP] camera model:" << model;

    // Images come to the PC, not the card
    uint32_t saveTo = eds::SaveTo_Host;
    err = api.SetPropertyData(camera_, eds::PROP_SaveTo, 0, sizeof(saveTo), &saveTo);
    STEP("SetPropertyData(SaveTo=Host)", err);
    eds::Capacity cap{0x7FFFFFFF, 0x1000, 1};
    err = api.SetCapacity(camera_, cap);
    STEP("SetCapacity", err);

    api.SetObjectEventHandler(camera_, eds::OBJEVENT_All, &CameraThread::onObjectEvent, nullptr);
    // Property + state handlers are required for EdsGetEvent to deliver the
    // property-change notification that makes live view "ready". Without them
    // the SDK never advances past OBJECT_NOTREADY. The bodies are no-op stubs;
    // we only need the events to be pumped through.
    if (api.SetPropertyEventHandler)
        api.SetPropertyEventHandler(camera_, eds::PROPEVENT_All, &CameraThread::onPropertyEvent, nullptr);
    if (api.SetCameraStateEventHandler)
        api.SetCameraStateEventHandler(camera_, eds::STATEEVENT_All, &CameraThread::onStateEvent, nullptr);

    // ── Live view start — follows Canon's official EDSDK Sample 10 exactly ──
    // Key differences from a naive approach, all per the SDK reference:
    //   * Do NOT set kEdsPropID_Evf_Mode — the sample never touches it.
    //   * READ the current Evf_OutputDevice, then OR-in the PC bit, so we keep
    //     the camera's existing output flags instead of overwriting them.
    //   * CreateMemoryStream(0, ...) is correct (SDK uses size 0).
    //   * After enabling PC output, the camera fires a property-change event;
    //     live-view frames only start after that, so pump events while polling
    //     and retry on OBJECT_NOTREADY (the SDK explicitly says to retry).
    auto pumpedWait = [&](int ms) {
        const qint64 end = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < end) {
            if (api.GetEvent) api.GetEvent();
            pumpWindowsMessages();
            msleep(50);
        }
    };

    eds::Error e1 = eds::OK, e2 = eds::OK;
    evfActive_ = false;

    // One clean attempt, exactly as Canon Sample 10 describes. Retrying the
    // whole start sequence tends to knock the sensor into stand-by (0xA102) or
    // into shooting mode (0x61), so we don't loop it.
    {
        // STEP 1: Enable the live-view function itself. kEdsPropID_Evf_Mode is
        // the master on/off switch (0=Disable, 1=Enable). Sample 10 skips it
        // assuming it's already on, but 200D-class bodies default it to 0 over
        // EDSDK — so OutputDevice alone gives endless OBJECT_NOTREADY. Set it
        // first, then route output to the PC.
        uint32_t evfMode = 1;
        eds::Error me = api.SetPropertyData(camera_, eds::PROP_Evf_Mode, 0, sizeof(evfMode), &evfMode);
        STEP("SetPropertyData(Evf_Mode=1)", me);
        pumpedWait(500);

        evfOutputChanged_ = false;
        uint32_t device = 0;
        if (api.GetPropertyData) {
            e1 = api.GetPropertyData(camera_, eds::PROP_Evf_OutputDevice, 0, sizeof(device), &device);
        }
        device |= eds::EvfOutput_PC;
        e2 = api.SetPropertyData(camera_, eds::PROP_Evf_OutputDevice, 0, sizeof(device), &device);
        STEP("SetPropertyData(Evf_OutputDevice|=PC)", e2);

        if (e2 != eds::OK) {
            qInfo() << "[EDS] SetPropertyData(OutputDevice) failed"
                    << QString("0x%1").arg(e2, 8, 16, QChar('0'));
            evfActive_ = false;
        } else {

        {
            const qint64 evtDeadline = QDateTime::currentMSecsSinceEpoch() + 8000;
            while (!evfOutputChanged_.load() &&
                   QDateTime::currentMSecsSinceEpoch() < evtDeadline) {
                if (api.GetEvent) api.GetEvent();
                pumpWindowsMessages();
                msleep(30);
            }
            STEP(evfOutputChanged_.load()
                     ? "property-change event RECEIVED"
                     : "property-change event TIMEOUT");
        }

        bool gotFrame = false;
        eds::Error lastGrabErr = eds::OK;
        const qint64 pollDeadline = QDateTime::currentMSecsSinceEpoch() + 30000; // 30s
        int i = 0;
        while (!gotFrame && QDateTime::currentMSecsSinceEpoch() < pollDeadline) {
            for (int e = 0; e < 4; ++e) {
                if (api.GetEvent) api.GetEvent();
                pumpWindowsMessages();
            }

            eds::Error grabErr = eds::OK;
            cv::Mat f = grabEdsdkEvf(&grabErr);
            if (!f.empty()) { gotFrame = true; break; }
            lastGrabErr = grabErr;

            if (i < 5 || i % 20 == 0)
                qInfo() << "[EDS] waiting for live view… attempt" << i
                        << QString("0x%1").arg(grabErr, 8, 16, QChar('0'))
                        << eds::errorName(grabErr);

            if (grabErr != eds::OBJECT_NOTREADY_EVF && grabErr != eds::OBJECT_NOTREADY) {
                qInfo() << "[EDS] hard error while waiting, aborting cycle";
                break;
            }

            ++i;
            msleep(120);
        }
        evfActive_ = gotFrame;
        STEP(gotFrame
                 ? QString("FIRST FRAME after %1 polls").arg(i).toLatin1().constData()
                 : "NO FRAME (gave up)");
        if (!gotFrame)
            emit errorText(QString("Live view not ready after 30s — last error 0x%1 (%2)")
                               .arg(lastGrabErr, 8, 16, QChar('0'))
                               .arg(eds::errorName(lastGrabErr)));
        } // end else (SetPropertyData ok)
    }

    if (evfActive_) {
        emit cameraInfo(QString("📷 %1 — EDSDK live view + full-res capture").arg(model));
        return true;
    }

    // EVF unavailable on this body, but the session is open and capture works.
    // Flag capture-only mode so run() keeps the EDSDK session alive (for
    // full-res shutter) and sources the preview from OpenCV instead of tearing
    // everything down.
    edsdkCaptureOnly_ = sessionOpen_;
    emit errorText(QString("EDSDK live view unavailable (0x%1 / 0x%2) — capture stays full-res, preview via OpenCV")
                       .arg(e1, 8, 16, QChar('0')).arg(e2, 8, 16, QChar('0')));
    return false;
}

void CameraThread::stopEdsdk() {
    if (!eds_) { camera_ = nullptr; sdkInited_ = sessionOpen_ = evfActive_ = false; return; }
    const auto& api = eds_->api();

    if (camera_ && sessionOpen_) {
        // ALWAYS end live view (clear the PC output bit), even if we never got
        // a frame — otherwise the camera thinks the PC still holds live view and
        // stays busy, refusing the next connection until it's power-cycled.
        uint32_t device = 0;
        if (api.GetPropertyData &&
            api.GetPropertyData(camera_, eds::PROP_Evf_OutputDevice, 0, sizeof(device), &device) == eds::OK) {
            device &= ~eds::EvfOutput_PC;
            api.SetPropertyData(camera_, eds::PROP_Evf_OutputDevice, 0, sizeof(device), &device);
        } else {
            uint32_t off = 0;
            api.SetPropertyData(camera_, eds::PROP_Evf_OutputDevice, 0, sizeof(off), &off);
        }
        evfActive_ = false;

        // Turn the live-view function back off too.
        uint32_t evfOff = 0;
        api.SetPropertyData(camera_, eds::PROP_Evf_Mode, 0, sizeof(evfOff), &evfOff);

        // Always release the UI lock we took.
        if (api.SendStatusCommand)
            api.SendStatusCommand(camera_, eds::STATUS_UIUnLock, 0);

        api.CloseSession(camera_);
        sessionOpen_ = false;
    }

    if (camera_)    { api.Release(camera_); camera_ = nullptr; }
    if (sdkInited_) { api.TerminateSDK(); sdkInited_ = false; }

    eds_.reset();
}

// ══════════════════════════════════════════════════════
//  EDSDK — live view frame
// ══════════════════════════════════════════════════════

cv::Mat CameraThread::grabEdsdkEvf(eds::Error* outErr) {
    auto fail = [outErr](eds::Error e) { if (outErr) *outErr = e; return cv::Mat(); };

    if (!eds_ || !camera_) return fail(eds::COMM_DISCONNECTED);
    const auto& api = eds_->api();

    // 2 MB buffer, exactly as Canon's DownloadEvfCommand sample does.
    eds::BaseRef streamRaw = nullptr;
    eds::Error err = api.CreateMemoryStream(2 * 1024 * 1024, &streamRaw);
    if (err != eds::OK) return fail(err);
    eds::Handle stream = eds_->wrap(streamRaw);

    eds::BaseRef evfRaw = nullptr;
    err = api.CreateEvfImageRef(stream.get(), &evfRaw);
    if (err != eds::OK) return fail(err);
    eds::Handle evf = eds_->wrap(evfRaw);

    err = api.DownloadEvfImage(camera_, evf.get());
    if (err != eds::OK) return fail(err);

    uint64_t len = 0;
    api.GetLength(stream.get(), &len);
    if (len == 0) return fail(eds::OBJECT_NOTREADY);

    void* ptr = nullptr;
    api.GetPointer(stream.get(), &ptr);
    if (!ptr) return fail(eds::OBJECT_NOTREADY);

    std::vector<uchar> buf(static_cast<size_t>(len));
    memcpy(buf.data(), ptr, static_cast<size_t>(len));
    if (outErr) *outErr = eds::OK;
    return cv::imdecode(buf, cv::IMREAD_COLOR);
}

// ══════════════════════════════════════════════════════
//  EDSDK — still capture (full resolution)
// ══════════════════════════════════════════════════════

eds::Error __stdcall CameraThread::onPropertyEvent(uint32_t event, uint32_t propId, uint32_t param, void*) {
    qInfo().noquote() << QString("[PROPEVT] event=0x%1 prop=0x%2 param=0x%3")
        .arg(event, 8, 16, QChar('0'))
        .arg(propId, 8, 16, QChar('0'))
        .arg(param, 8, 16, QChar('0'));
    if (g_self && propId == eds::PROP_Evf_OutputDevice)
        g_self->evfOutputChanged_ = true;
    return eds::OK;
}

eds::Error __stdcall CameraThread::onStateEvent(uint32_t, uint32_t, void*) {
    return eds::OK;
}

eds::Error __stdcall CameraThread::onObjectEvent(uint32_t event, eds::BaseRef ref, void*) {
    if (event == eds::OBJEVENT_DirItemRequestTransfer && g_self)
        g_self->handleDirItemTransfer(ref);
    return eds::OK;
}

void CameraThread::handleDirItemTransfer(eds::BaseRef dirItem) {
    if (!eds_) { capDone_ = true; return; }
    const auto& api = eds_->api();

    eds::DirectoryItemInfo info{};
    if (api.GetDirectoryItemInfo(dirItem, &info) != eds::OK) { capDone_ = true; return; }

    QString target;
    { QMutexLocker lk(&capMu_); target = capPath_; }
    if (target.isEmpty()) {
        target = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                     .filePath(QString("eds_%1.jpg").arg(QDateTime::currentMSecsSinceEpoch()));
        QMutexLocker lk(&capMu_);
        capPath_ = target;
    }

    eds::BaseRef fsRaw = nullptr;
    eds::Error err = api.CreateFileStream(target.toLocal8Bit().constData(),
                                          eds::DISPOSITION_CreateAlways,
                                          eds::ACCESS_ReadWrite, &fsRaw);
    if (err != eds::OK) { capDone_ = true; return; }
    eds::Handle fs = eds_->wrap(fsRaw);

    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    err = api.Download(dirItem, info.size, fs.get());
    if (err == eds::OK) {
        api.DownloadComplete(dirItem);
        capOk_ = true;
        const qint64 dt = QDateTime::currentMSecsSinceEpoch() - t0;
        qInfo() << "[EDS] downloaded" << (info.size / 1024) << "KB in" << dt << "ms";
    }
    capDone_ = true;
}

cv::Mat CameraThread::captureEdsdk() {
    if (!eds_ || !camera_) return {};
    const auto& api = eds_->api();

    const QString target =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QString("eds_cap_%1.jpg").arg(QDateTime::currentMSecsSinceEpoch()));

    { QMutexLocker lk(&capMu_); capPath_ = target; }
    capDone_ = false;
    capOk_   = false;

    // Fire the shutter. In live view the body does a mirror/exposure cycle then
    // resumes live view — that's the irreducible mechanical delay. We fire with
    // a single Completely + OFF pair (Canon's own threaded sample) and keep the
    // retry window tiny so a ready camera fires instantly.
    bool fired = false;
    for (int attempt = 0; attempt < 4 && !fired; ++attempt) {
        eds::Error err = api.SendCommand(camera_, eds::CMD_PressShutterButton,
                                         eds::SHUTTER_Completely);
        api.SendCommand(camera_, eds::CMD_PressShutterButton, eds::SHUTTER_OFF);
        if (err == eds::OK) { fired = true; break; }
        // AF couldn't lock (low light / low contrast / plain backdrop). Fire
        // anyway without autofocus — focus stays where the lens already is,
        // which for a fixed-distance booth is fine. TakePicture would AF again
        // and fail the same way, so don't fall through to it.
        if (err == eds::TAKE_PICTURE_AF_NG) {
            qWarning() << "[EDS] AF failed to lock — firing without AF";
            err = api.SendCommand(camera_, eds::CMD_PressShutterButton,
                                  eds::SHUTTER_Completely_NonAF);
            api.SendCommand(camera_, eds::CMD_PressShutterButton, eds::SHUTTER_OFF);
            if (err == eds::OK) { fired = true; break; }
        }
        if (err == eds::DEVICE_BUSY || err == eds::OBJECT_NOTREADY) {
            msleep(30);
            continue;
        }
        if (api.SendCommand(camera_, eds::CMD_TakePicture, 0) == eds::OK) {
            fired = true;
            break;
        }
        emit errorText(QString("Shutter failed 0x%1 (%2) — %3")
                           .arg(err, 8, 16, QChar('0'))
                           .arg(eds::errorName(err))
                           .arg(eds::errorHint(err)));
        return {};
    }
    if (!fired) { emit errorText(QStringLiteral("Camera busy — shutter not fired")); return {}; }

    // Poll for the download callback fast.
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 20000;
    while (!capDone_.load() && QDateTime::currentMSecsSinceEpoch() < deadline) {
        pumpWindowsMessages();
        if (api.GetEvent) api.GetEvent();
        msleep(5);
    }

    if (!capOk_.load()) { emit errorText(QStringLiteral("Capture timed out")); return {}; }

    cv::Mat img = cv::imread(target.toLocal8Bit().constData(), cv::IMREAD_COLOR);
    QFile::remove(target);
    if (img.empty()) { emit errorText(QStringLiteral("Downloaded file unreadable")); return {}; }

    return applyAdjustments(img);
}

// ══════════════════════════════════════════════════════
//  public capture entry
// ══════════════════════════════════════════════════════

cv::Mat CameraThread::captureFrame() {
    if (source_.load() == Source::Edsdk || edsdkCaptureOnly_.load()) {
        cv::Mat full = captureEdsdk();
        if (!full.empty()) return full;
    }
    QMutexLocker lk(&frameMu_);
    return lastProcessed_.empty() ? cv::Mat() : lastProcessed_.clone();
}

// ══════════════════════════════════════════════════════
//  OpenCV source
// ══════════════════════════════════════════════════════

bool CameraThread::openOpenCv() {
    closeOpenCv();
    qputenv("OPENCV_LOG_LEVEL", "SILENT");

    int wanted;
    { QMutexLocker lk(&cfgMu_); wanted = cvIndex_; }

    struct B { int id; const char* name; };
    const B backends[] = {
        { cv::CAP_DSHOW, "DSHOW" },
        { cv::CAP_MSMF,  "MSMF"  },
        { cv::CAP_ANY,   "ANY"   },
    };

    const int first = (wanted >= 0 ? wanted : 0);
    const int last  = (wanted >= 0 ? wanted : 9);

    for (int idx = first; idx <= last; ++idx) {
        for (const auto& b : backends) {
            cv::VideoCapture c(idx, b.id);
            if (!c.isOpened()) continue;
            cv::Mat probe;
            if (!c.read(probe) || probe.empty()) { c.release(); continue; }

            const int rw = reqW_.load(), rh = reqH_.load();
            if (rw > 0 && rh > 0) {
                c.set(cv::CAP_PROP_FRAME_WIDTH,  rw);
                c.set(cv::CAP_PROP_FRAME_HEIGHT, rh);
            } else {
                const int probes[][2] = {{3840,2160},{2560,1440},{1920,1080},{1280,720},{640,480}};
                for (const auto& p : probes) {
                    c.set(cv::CAP_PROP_FRAME_WIDTH,  p[0]);
                    c.set(cv::CAP_PROP_FRAME_HEIGHT, p[1]);
                    if (static_cast<int>(c.get(cv::CAP_PROP_FRAME_WIDTH)) >= p[0] * 0.9) break;
                }
            }
            c.set(cv::CAP_PROP_FPS, fps_.load());

            const int w = static_cast<int>(c.get(cv::CAP_PROP_FRAME_WIDTH));
            const int h = static_cast<int>(c.get(cv::CAP_PROP_FRAME_HEIGHT));

            cap_ = std::move(c);
            cvBackendName_ = QString::fromLatin1(b.name);
            emit cameraInfo(QString("🎥 Camera %1 (%2) [%3×%4]").arg(idx).arg(b.name).arg(w).arg(h));
            emit cameraConnected(true);
            return true;
        }
    }

    emit cameraInfo(QStringLiteral("⚠ No camera found"));
    return false;
}

void CameraThread::closeOpenCv() {
    if (cap_.isOpened()) cap_.release();
}

cv::Mat CameraThread::grabOpenCv() {
    if (!cap_.isOpened()) return {};
    cv::Mat f;
    if (!cap_.read(f)) return {};
    return f;
}

// ══════════════════════════════════════════════════════
//  processing helpers
// ══════════════════════════════════════════════════════

cv::Mat CameraThread::applyAdjustments(const cv::Mat& src) const {
    if (src.empty()) return src;
    cv::Mat out = src;

    const int b = brightness_.load();
    const int c = contrast_.load();
    if (b != 0 || c != 0) {
        const double alpha = 1.0 + c / 100.0;
        cv::Mat tmp;
        out.convertTo(tmp, -1, alpha, b);
        out = tmp;
    }

    switch (rotation_.load()) {
        case 90:  cv::rotate(out, out, cv::ROTATE_90_CLOCKWISE);        break;
        case 180: cv::rotate(out, out, cv::ROTATE_180);                 break;
        case 270: cv::rotate(out, out, cv::ROTATE_90_COUNTERCLOCKWISE); break;
        default: break;
    }

    const double z = zoom_.load();
    if (z > 1.01) {
        const int h = out.rows, w = out.cols;
        const int zh = static_cast<int>(h / z), zw = static_cast<int>(w / z);
        if (zh > 1 && zw > 1) {
            cv::Mat crop = out(cv::Rect((w - zw) / 2, (h - zh) / 2, zw, zh));
            cv::Mat resized;
            cv::resize(crop, resized, cv::Size(w, h), 0, 0, cv::INTER_LINEAR);
            out = resized;
        }
    }
    return out;
}

void CameraThread::pumpWindowsMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}