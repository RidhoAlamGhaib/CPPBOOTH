#pragma once
/*
 * PhotoBoothApp.h — main window. Owns the screen stack, camera thread,
 * session state and config.
 */

#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QFrame>
#include <QTimer>
#include <QVector>
#include <opencv2/core.hpp>

#include "Config.h"
#include "ImageService.h"

class CameraThread;
class CameraPreview;
class SizeCard;

class PhotoBoothApp : public QMainWindow {
    Q_OBJECT
public:
    explicit PhotoBoothApp(QWidget* parent = nullptr);
    ~PhotoBoothApp() override;

protected:
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void closeEvent(QCloseEvent*) override;

private slots:
    void onFrameReady(const QImage& mirrored);
    void onCameraInfo(const QString& text);
    void onErrorText(const QString& text);
    void onCountdownTick();
    void onSizeCardClicked(int index);

private:
    enum Screen { HOME = 0, SIZE_SELECT, GUIDE, CAMERA, EDITOR, PRINT_VIEW, SETTINGS };

    // ── build ──
    QWidget* buildHome();
    QWidget* buildSizeSelect();
    QWidget* buildGuide();
    QWidget* buildCamera();
    QWidget* buildEditor();
    QWidget* buildPrint();
    QWidget* buildSettings();

    // ── flow ──
    void goTo(Screen s);
    void startCountdown();
    void doCapture();
    void finishCapture(const cv::Mat& frame);
    void refreshEditor();
    void refreshPrint();
    void resetSession();
    QString makeSessionDir();

    // ── styles ──
    QString navButtonStyle() const;
    QString primaryButtonStyle() const;
    QString pillButtonStyle() const;
    QString toolButtonStyle() const;
    QString titlePillStyle() const;
    QString panelStyle() const;
    QString screenStyle() const;

    // ── state ──
    Config          cfg_;
    CameraThread*   camera_ = nullptr;
    QStackedWidget* stack_  = nullptr;

    int             selectedSizeIdx_ = -1;
    cv::Mat         capturedImage_;   // auto-cropped to guide
    cv::Mat         editedImage_;     // after adjustments
    cv::Mat         layoutImage_;     // 4R sheet
    QString         sessionDir_;

    // camera screen
    CameraPreview*  preview_       = nullptr;
    QLabel*         camInfoLabel_  = nullptr;
    QLabel*         sizeBadge_     = nullptr;
    QLabel*         countdownLbl_  = nullptr;
    QLabel*         flashLbl_      = nullptr;
    QLabel*         processingLbl_ = nullptr;
    QPushButton*    captureBtn_    = nullptr;
    QSlider*        camBright_     = nullptr;
    QSlider*        camContrast_   = nullptr;
    QTimer*         countdownTimer_ = nullptr;
    int             countdownValue_ = 0;

    // size select
    QVector<SizeCard*> sizeCards_;

    // editor
    QLabel*  editorPreview_ = nullptr;
    QSlider* edBright_   = nullptr;
    QSlider* edContrast_ = nullptr;
    QSlider* edSat_      = nullptr;
    QLabel*  editorBadge_ = nullptr;
    QString  bgChoice_    = "original";
    int      edRotation_  = 0;
    bool     edFlipped_   = false;

    // print
    QLabel*         printPreview_ = nullptr;
    QLabel*         printCount_   = nullptr;
    QDoubleSpinBox* marginSpin_   = nullptr;
    QDoubleSpinBox* gapSpin_      = nullptr;

    // settings
    QComboBox* setCamIndex_  = nullptr;
    QSpinBox*  setCountdown_ = nullptr;
    QSpinBox*  setDpi_       = nullptr;
    QLabel*    setStatus_    = nullptr;
};
