# ID Photo Booth — C++ / Qt6 / Canon EDSDK

## What it does

Full photobooth flow: Home → Photo Size → Guide → Camera → Editor → Print.

- **Camera**: Canon via EDSDK (live view + full-res shutter) with automatic
  OpenCV fallback (webcam / EOS Webcam Utility).
- **Crop guide**: draggable, resizable, aspect-locked to the chosen photo size,
  with ISO/ICAO head-proportion lines (head top, eye line, chin, oval).
- **Editor**: brightness / contrast / saturation, rotate, flip, background
  replacement (white / blue / red via GrabCut).
- **Print**: 4R (4×6 in) sheet, auto-tiled, each photo gets its own dashed
  cut border with an adjustable gap. Save JPG/PNG or send to printer.

---

## Prerequisites

| Tool | Where | Notes |
|---|---|---|
| Visual Studio 2022 Community | https://visualstudio.microsoft.com/ | Workload: **Desktop development with C++** |
| Qt 6.5+ | https://www.qt.io/download-qt-installer-oss | Pick the **MSVC 2022** kit matching your target arch |
| OpenCV 4.x | https://opencv.org/releases/ | Windows prebuilt, extract to e.g. `C:\opencv` |
| Canon EDSDK | Canon Developer Program | Optional — app runs without it |

---

## EDSDK folder layout

Put the SDK next to `CMakeLists.txt` like this:

```
booth/
├── CMakeLists.txt
├── src/
└── EDSDK/
    ├── Header/     EDSDK.h, EDSDKTypes.h, EDSDKErrors.h
    ├── Library/    EDSDK.lib
    └── Dll/        EDSDK.dll, EdsImage.dll
```

Or point CMake elsewhere:

```cmd
cmake -B build -DEDSDK_ROOT=C:/path/to/EDSDK
```

> **Architecture must match the DLL.** Check with:
> ```cmd
> dumpbin /headers EDSDK\Dll\EDSDK.dll | findstr machine
> ```
> `x86` → build Win32. `x64` → build x64.

---

## Build

Open **x64 Native Tools Command Prompt for VS 2022** (or **x86** for 32-bit).

```cmd
cd booth

:: Point CMake at Qt + OpenCV
set CMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2022_64;C:\opencv\build

:: Configure — match your EDSDK arch
cmake -B build -G "Visual Studio 17 2022" -A x64
::   ...or for a 32-bit EDSDK:
:: cmake -B build -G "Visual Studio 17 2022" -A Win32

:: Compile
cmake --build build --config Release
```

Output: `build\Release\IDPhotoBooth.exe`

---

## Deploy the DLLs

```cmd
:: Qt runtime (one-time)
C:\Qt\6.7.3\msvc2022_64\bin\windeployqt.exe build\Release\IDPhotoBooth.exe

:: OpenCV — filename varies by version
copy C:\opencv\build\x64\vc16\bin\opencv_world4*.dll build\Release\
```

EDSDK DLLs are copied automatically by the CMake post-build step.

---

## Run

```cmd
build\Release\IDPhotoBooth.exe
```

First run creates, next to the exe:

```
config.json     ← settings (edit with Notepad)
captures/       ← saved photos, one folder per session
logs/           ← photobooth_YYYYMMDD.log
```

---

## Keyboard

| Key | Action |
|---|---|
| `Esc` | Back to Home; on Home, quit |
| `F12` | Settings |
| `F11` | Toggle fullscreen |

---

## config.json

```json
{
  "language": "id",
  "countdown": 3,
  "dpi": 300,
  "margin_mm": 1.5,
  "border_gap_mm": 1.5,
  "paper_w_mm": 152,
  "paper_h_mm": 102,
  "output": "captures",
  "camera": {
    "index": "auto",
    "width": "auto",
    "height": "auto",
    "fps": 30,
    "prefer_edsdk": true,
    "edsdk_dll": ""
  },
  "theme": {
    "background": "#2F6DB3",
    "primary": "#165AA7",
    "accent": "#FFE84A",
    "orange": "#E07B1A"
  },
  "photo_sizes": [
    { "name": "3x4cm", "w_mm": 30, "h_mm": 40 }
  ]
}
```

| Key | Meaning |
|---|---|
| `camera.index` | `"auto"` scans 0–9, or pin a number |
| `camera.width` / `height` | `"auto"` probes 4K → 1080p → 720p, or set pixels |
| `camera.prefer_edsdk` | `false` forces the OpenCV path |
| `camera.edsdk_dll` | Explicit `EDSDK.dll` path; empty = auto-search |
| `margin_mm` | Gap between photo cells on the sheet |
| `border_gap_mm` | White space between a photo and its dashed cut line |

---

## Session output

```
captures/2026-09-16/14-30-22/
├── original.jpg       full frame from the camera
├── cropped.jpg        cropped to the red guide
├── edited.jpg         after editor adjustments
└── print_layout.jpg   the 4R sheet (written when printing)
```

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `qtpaths` / missing Qt DLL error | Run `windeployqt` on the exe |
| Window opens then closes instantly | Run from a terminal to see the error; check `logs\` |
| `EdsOpenSession failed 0x00000081` | Camera busy — close EOS Utility and EOS Webcam Utility |
| `PROPERTIES_UNAVAILABLE (0x61)` on live view | Body doesn't expose EVF over EDSDK; app falls back to OpenCV automatically |
| `LoadLibrary failed 193` | Arch mismatch — 32-bit DLL with 64-bit build (or vice versa) |
| Black preview, no camera | Set `camera.prefer_edsdk` to `false` and start EOS Webcam Utility |
