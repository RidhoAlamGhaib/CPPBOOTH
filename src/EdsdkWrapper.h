#pragma once
/*
 * EdsdkWrapper.h — Canon EDSDK wrapper.
 *
 * Works two ways:
 *  1) With official EDSDK headers (HAVE_EDSDK defined by CMake) — full types.
 *  2) Without them — self-declared minimal types, DLL loaded at runtime.
 *
 * All EdsBaseRef handles are RAII-managed (EdsHandle). No manual Release.
 */

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <opencv2/core.hpp>

// ══════════════════════════════════════════════════════
//  Minimal EDSDK type/constant declarations
//  (safe even when official headers are present — we use
//   our own names to avoid clashes)
// ══════════════════════════════════════════════════════

namespace eds {

using Error    = unsigned int;
using BaseRef  = void*;

constexpr Error OK                 = 0x00000000;
constexpr Error DEVICE_NOT_FOUND        = 0x00000002;
constexpr Error DEVICE_BUSY_D           = 0x00000003;   // "device busy" (low-code group)
constexpr Error DEVICE_INVALID          = 0x00000004;
constexpr Error DEVICE_EMERGENCY        = 0x00000005;
constexpr Error DEVICE_MEMORY_FULL      = 0x00000006;
constexpr Error DEVICE_INTERNAL_ERROR   = 0x00000007;
constexpr Error DEVICE_INVALID_PARAM    = 0x00000008;
constexpr Error DEVICE_NO_DISK          = 0x00000009;
constexpr Error DEVICE_DISK_ERROR       = 0x0000000A;
constexpr Error DEVICE_CF_GATE_CHANGED  = 0x0000000B;
constexpr Error DEVICE_DIAL_CHANGED     = 0x0000000C;
constexpr Error DEVICE_NOT_INSTALLED    = 0x0000000D;
// EVF-family error: sensor/live-view engine not ready yet. Seen while the
// camera is still warming up its live-view stream, or when Live View is
// disabled in the camera's own menu.
// 0xA102 is EDS_ERR_OBJECT_NOTREADY: the live-view image set isn't ready yet.
// The SDK explicitly says to RETRY on this — it's expected during warm-up.
constexpr Error OBJECT_NOTREADY_EVF = 0x0000A102;
constexpr Error DEVICE_STAY_AWAKE       = 0x0000000E;
constexpr Error DEVICE_NOT_RELEASED     = 0x0000000F;
constexpr Error DEVICE_BUSY        = 0x00000081;   // "high-code" busy group (comms layer)
constexpr Error OBJECT_NOTREADY    = 0x00000041;
constexpr Error PROPERTIES_UNAVAILABLE = 0x00000061;
constexpr Error COMM_DISCONNECTED  = 0x000000C3;
constexpr Error INVALID_HANDLE     = 0x00000060;

constexpr uint32_t CMD_TakePicture        = 0x00000000;
constexpr uint32_t CMD_PressShutterButton = 0x00000004;
constexpr uint32_t SHUTTER_Halfway          = 0x00000001;
constexpr uint32_t SHUTTER_Completely       = 0x00000003;
constexpr uint32_t SHUTTER_Completely_NonAF = 0x00010003;
constexpr uint32_t SHUTTER_OFF              = 0x00000000;

constexpr uint32_t SaveTo_Host = 2;

constexpr uint32_t PROP_SaveTo            = 0x0000000B;
constexpr uint32_t PROP_Evf_OutputDevice  = 0x00000501;  // SDK: output device for live view
constexpr uint32_t PROP_Evf_Mode          = 0x00000502;  // SDK: status of live view

// EdsSendStatusCommand — camera UI lock. Some bodies won't stream live view
// to the PC until the host locks the camera's own UI first.
constexpr int32_t STATUS_UILock   = 0x00000000;
constexpr int32_t STATUS_UIUnLock = 0x00000001;
constexpr uint32_t PROP_ProductName       = 0x00000002;

constexpr uint32_t EvfOutput_TFT = 1;
constexpr uint32_t EvfOutput_PC  = 2;

constexpr uint32_t OBJEVENT_All                    = 0x00000200;
constexpr uint32_t PROPEVENT_All                   = 0x00000100;
constexpr uint32_t PROPEVENT_PropertyChanged       = 0x00000101;
constexpr uint32_t STATEEVENT_All                  = 0x00000300;
constexpr uint32_t OBJEVENT_DirItemRequestTransfer = 0x00000208;

constexpr int ACCESS_Read        = 0;
constexpr int ACCESS_Write       = 1;
constexpr int ACCESS_ReadWrite   = 2;
constexpr int DISPOSITION_CreateAlways = 2;

// NOTE: no #pragma pack. EDSDK structs use the compiler's default natural
// alignment (8-byte for the uint64 'size'). Forcing pack(1) makes our layout
// disagree with what the DLL reads/writes — the classic cause of garbage
// DirectoryItemInfo and odd download/EVF failures.
struct Capacity {
    int numberOfFreeClusters;
    int bytesPerSector;
    int reset;
};
struct DirectoryItemInfo {
    uint64_t size;
    int      isFolder;
    uint32_t groupID;
    uint32_t option;
    char     szFileName[256];
    uint32_t format;
    uint32_t dateTime;
};

using ObjectEventHandler = Error (__stdcall *)(uint32_t inEvent, BaseRef inRef, void* inContext);
using PropertyEventHandler = Error (__stdcall *)(uint32_t inEvent, uint32_t inPropertyID, uint32_t inParam, void* inContext);
using StateEventHandler = Error (__stdcall *)(uint32_t inEvent, uint32_t inParam, void* inContext);

// EDSDK.dll exports use __stdcall (classic Windows API convention).
// Every member below MUST match, or the very first call corrupts the
// stack and the process dies with no exception, no log line — exactly
// the symptom of "crashes silently right after DLL load".
#define EDSCALL __stdcall

// ══════════════════════════════════════════════════════
//  Function pointer table
// ══════════════════════════════════════════════════════

struct Api {
    Error (EDSCALL *InitializeSDK)();
    Error (EDSCALL *TerminateSDK)();
    Error (EDSCALL *GetCameraList)(BaseRef*);
    Error (EDSCALL *GetChildCount)(BaseRef, int*);
    Error (EDSCALL *GetChildAtIndex)(BaseRef, int, BaseRef*);
    Error (EDSCALL *OpenSession)(BaseRef);
    Error (EDSCALL *CloseSession)(BaseRef);
    Error (EDSCALL *Release)(BaseRef);
    Error (EDSCALL *Retain)(BaseRef);
    Error (EDSCALL *SetPropertyData)(BaseRef, uint32_t, int, int, const void*);
    Error (EDSCALL *GetPropertyData)(BaseRef, uint32_t, int, int, void*);
    Error (EDSCALL *SetCapacity)(BaseRef, Capacity);
    Error (EDSCALL *SendCommand)(BaseRef, uint32_t, int);
    Error (EDSCALL *SendStatusCommand)(BaseRef, uint32_t, int32_t);
    Error (EDSCALL *SetObjectEventHandler)(BaseRef, uint32_t, ObjectEventHandler, void*);
    Error (EDSCALL *SetPropertyEventHandler)(BaseRef, uint32_t, PropertyEventHandler, void*);
    Error (EDSCALL *SetCameraStateEventHandler)(BaseRef, uint32_t, StateEventHandler, void*);
    Error (EDSCALL *CreateMemoryStream)(uint64_t, BaseRef*);
    Error (EDSCALL *CreateFileStream)(const char*, int, int, BaseRef*);
    Error (EDSCALL *GetLength)(BaseRef, uint64_t*);
    Error (EDSCALL *GetPointer)(BaseRef, void**);
    Error (EDSCALL *CreateEvfImageRef)(BaseRef, BaseRef*);
    Error (EDSCALL *DownloadEvfImage)(BaseRef, BaseRef);
    Error (EDSCALL *GetDirectoryItemInfo)(BaseRef, DirectoryItemInfo*);
    Error (EDSCALL *Download)(BaseRef, uint64_t, BaseRef);
    Error (EDSCALL *DownloadComplete)(BaseRef);
    Error (EDSCALL *GetEvent)();
};

// ══════════════════════════════════════════════════════
//  RAII handle — auto Release on scope exit
// ══════════════════════════════════════════════════════

class Handle {
public:
    Handle() = default;
    Handle(BaseRef r, const Api* api) : ref_(r), api_(api) {}
    ~Handle() { reset(); }

    Handle(Handle&& o) noexcept : ref_(o.ref_), api_(o.api_) { o.ref_ = nullptr; }
    Handle& operator=(Handle&& o) noexcept {
        if (this != &o) { reset(); ref_ = o.ref_; api_ = o.api_; o.ref_ = nullptr; }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    void reset() {
        if (ref_ && api_ && api_->Release) api_->Release(ref_);
        ref_ = nullptr;
    }
    BaseRef  get()  const { return ref_; }
    BaseRef* put()        { reset(); return &ref_; }
    BaseRef  release()    { BaseRef r = ref_; ref_ = nullptr; return r; }
    explicit operator bool() const { return ref_ != nullptr; }

private:
    BaseRef ref_ = nullptr;
    const Api* api_ = nullptr;
};

// ══════════════════════════════════════════════════════
//  DLL loader
// ══════════════════════════════════════════════════════

class Loader {
public:
    Loader() = default;
    ~Loader() { unload(); }
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    // Search order: explicit path → exeDir/EDSDK/Dll → exeDir/edsdk → exeDir → Canon installs
    bool load(const std::string& explicitPath = {});
    void unload();
    bool loaded() const { return hDll_ != nullptr; }
    const Api& api() const { return api_; }
    const std::string& dllPath() const { return dllPath_; }
    Handle wrap(BaseRef raw) { return Handle(raw, &api_); }

private:
    HMODULE     hDll_ = nullptr;
    Api         api_{};
    std::string dllPath_;
    static std::vector<std::string> candidatePaths(const std::string& explicitPath);
};

// Human-readable error name
const char* errorName(Error e);

// One-line plain-language hint on how to fix a given error
const char* errorHint(Error e);

} // namespace eds