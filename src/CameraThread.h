#pragma once
/*
 * CameraThread.h — camera capture thread.
 *
 * Two sources, auto-selected:
 *   A) EDSDK  — Canon direct USB. Live view via EVF, capture via shutter (full-res).
 *   B) OpenCV — webcam / EOS Webcam Utility. Live + capture from the video stream.
 *
 * Preview frames are MIRRORED (like a mirror). captureFrame() returns UN-mirrored.
 */

#include <QThread>
#include <QMutex>
#include <QImage>
#include <QString>
#include <atomic>
#include <memory>
#include <opencv2/opencv.hpp>
#include "EdsdkWrapper.h"

class CameraThread : public QThread {
    Q_OBJECT
public:
    enum class Source { None, Edsdk, OpenCv };

    explicit CameraThread(QObject* parent = nullptr);
    ~CameraThread() override;

    // Config (call before start())
    void setOpenCvIndex(int idx) { QMutexLocker lk(&cfgMu_); cvIndex_ = idx; }
    void setRequestedSize(int w, int h) { reqW_ = w; reqH_ = h; }   // -1 = auto
    void setFps(int f) { fps_ = (f > 0 ? f : 30); }
    void setEdsdkDllPath(const QString& p) { QMutexLocker lk(&cfgMu_); edsdkPath_ = p; }
    void setPreferEdsdk(bool v) { preferEdsdk_ = v; }

    // Live adjustments (thread-safe)
    void setBrightness(int v) { brightness_ = v; }
    void setContrast(int v)   { contrast_   = v; }
    void setZoom(double z)    { zoom_ = (z < 0.5 ? 0.5 : (z > 3.0 ? 3.0 : z)); }
    void setRotation(int deg) { rotation_ = ((deg % 360) + 360) % 360; }
    void resetAdjustments()   { brightness_ = 0; contrast_ = 0; zoom_ = 1.0; rotation_ = 0; }

    double zoom() const     { return zoom_.load(); }
    int    rotation() const { return rotation_.load(); }
    Source source() const   { return source_.load(); }

    void stop();

    // Blocking capture. On EDSDK: fires real shutter → full-res.
    // On OpenCV: returns the latest processed frame.
    cv::Mat captureFrame();

signals:
    void frameReady(const QImage& mirrored);
    void cameraInfo(const QString& text);
    void cameraConnected(bool connected);
    void errorText(const QString& text);

protected:
    void run() override;

private:
    // ── setup / teardown ──
    bool  tryStartEdsdk();
    void  stopEdsdk();
    bool  openOpenCv();
    void  closeOpenCv();

    // ── frame grab ──
    cv::Mat grabEdsdkEvf(eds::Error* outErr = nullptr);
    cv::Mat grabOpenCv();

    // ── capture ──
    cv::Mat captureEdsdk();

    // ── processing ──
    cv::Mat applyAdjustments(const cv::Mat& src) const;

    // EDSDK object-event plumbing
    static eds::Error __stdcall onObjectEvent(uint32_t event, eds::BaseRef ref, void* ctx);
    static eds::Error __stdcall onPropertyEvent(uint32_t event, uint32_t propId, uint32_t param, void* ctx);
    static eds::Error __stdcall onStateEvent(uint32_t event, uint32_t param, void* ctx);
    void handleDirItemTransfer(eds::BaseRef dirItem);
    void pumpWindowsMessages();

    // ── state ──
    std::atomic<bool>   running_{false};
    std::atomic<Source> source_{Source::None};
    std::atomic<int>    brightness_{0}, contrast_{0}, rotation_{0};
    std::atomic<double> zoom_{1.0};
    std::atomic<int>    fps_{30};
    std::atomic<bool>   preferEdsdk_{true};
    std::atomic<int>    reqW_{-1}, reqH_{-1};

    QMutex   cfgMu_;
    int      cvIndex_ = -1;          // -1 = auto
    QString  edsdkPath_;

    QMutex   frameMu_;
    cv::Mat  lastProcessed_;         // un-mirrored, adjustments applied

    // EDSDK
    std::unique_ptr<eds::Loader> eds_;
    eds::BaseRef camera_ = nullptr;  // owned; released in stopEdsdk()
    bool         sdkInited_ = false;
    bool         sessionOpen_ = false;
    bool         evfActive_ = false;
    // Set by the property-change event handler the moment the camera confirms
    // Evf_OutputDevice changed. Per Canon Sample 10, live-view download must
    // only begin AFTER this notification arrives.
    std::atomic<bool> evfOutputChanged_{false};
    // Session is open + capture works, but EVF live view never delivered a
    // frame (common on 200D-class bodies). Preview then comes from OpenCV
    // while capture still goes through EDSDK for full sensor resolution.
    std::atomic<bool> edsdkCaptureOnly_{false};
    int          evfFailStreak_ = 0;

    // capture handoff (written by callback thread, read by capture caller)
    QMutex           capMu_;
    std::atomic<bool> capDone_{false};
    std::atomic<bool> capOk_{false};
    QString          capPath_;

    // OpenCV
    cv::VideoCapture cap_;
    QString          cvBackendName_;
};