#include "PhotoBoothApp.h"
#include "CameraThread.h"
#include "CameraPreview.h"
#include "SizeCard.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QCloseEvent>
#include <thread>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <opencv2/imgproc.hpp>


// ══════════════════════════════════════════════════════
//  Styles
// ══════════════════════════════════════════════════════

QString PhotoBoothApp::screenStyle() const {
    return QString("background: %1;").arg(cfg_.theme.background);
}
QString PhotoBoothApp::titlePillStyle() const {
    return QString("background: %1; color: white; font-size: 28px; font-weight: 700;"
                   "border-radius: 24px; padding: 14px 50px;").arg(cfg_.theme.primary);
}
QString PhotoBoothApp::primaryButtonStyle() const {
    return QString("QPushButton { background: %1; color: %2; font-size: 24px; font-weight: 700;"
                   "  border-radius: 20px; padding: 20px 60px; border: none; }"
                   "QPushButton:hover  { background: #FFF0A0; }"
                   "QPushButton:pressed{ background: #E6CE2E; }")
        .arg(cfg_.theme.accent, cfg_.theme.primary);
}
QString PhotoBoothApp::navButtonStyle() const {
    return QString("QPushButton { background: white; color: %1; font-size: 20px; font-weight: 700;"
                   "  border-radius: 16px; padding: 16px 38px; border: none; min-width: 150px; }"
                   "QPushButton:hover { background: #F0F0F0; }"
                   "QPushButton:disabled { background: #B0B0B0; color: #E8E8E8; }")
        .arg(cfg_.theme.orange);
}
QString PhotoBoothApp::pillButtonStyle() const {
    return "QPushButton { background: rgba(255,255,255,30); color: white; font-size: 16px;"
           "  font-weight: 600; border-radius: 25px; padding: 12px 28px;"
           "  border: 1px solid rgba(255,255,255,60); }"
           "QPushButton:hover { background: rgba(255,80,80,180); border-color: rgba(255,80,80,200); }";
}
QString PhotoBoothApp::toolButtonStyle() const {
    return "QPushButton { background: white; color: #1A1A1A; font-size: 16px; font-weight: 600;"
           "  border-radius: 12px; padding: 12px 18px; border: none; }"
           "QPushButton:hover { background: #E8E8E8; }";
}
QString PhotoBoothApp::panelStyle() const {
    return "QFrame { background: rgba(22,90,167,200); border-radius: 12px; }";
}

// ══════════════════════════════════════════════════════
//  ctor
// ══════════════════════════════════════════════════════

PhotoBoothApp::PhotoBoothApp(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ID Photo Booth"));

    cfg_.load();
    QDir().mkpath(cfg_.output);

    camera_ = new CameraThread(this);
    camera_->setOpenCvIndex(cfg_.camera.indexInt());
    camera_->setRequestedSize(cfg_.camera.widthInt(), cfg_.camera.heightInt());
    camera_->setFps(cfg_.camera.fps);
    camera_->setPreferEdsdk(cfg_.camera.preferEdsdk);
    camera_->setEdsdkDllPath(cfg_.camera.edsdkDll);

    connect(camera_, &CameraThread::frameReady, this, &PhotoBoothApp::onFrameReady);
    connect(camera_, &CameraThread::cameraInfo, this, &PhotoBoothApp::onCameraInfo);
    connect(camera_, &CameraThread::errorText,  this, &PhotoBoothApp::onErrorText);

    countdownTimer_ = new QTimer(this);
    countdownTimer_->setInterval(1000);
    connect(countdownTimer_, &QTimer::timeout, this, &PhotoBoothApp::onCountdownTick);

    stack_ = new QStackedWidget(this);
    setCentralWidget(stack_);

    stack_->addWidget(buildHome());        // 0
    stack_->addWidget(buildSizeSelect());  // 1
    stack_->addWidget(buildGuide());       // 2
    stack_->addWidget(buildCamera());      // 3
    stack_->addWidget(buildEditor());      // 4
    stack_->addWidget(buildPrint());       // 5
    stack_->addWidget(buildSettings());    // 6

    goTo(HOME);
    showFullScreen();
}

PhotoBoothApp::~PhotoBoothApp() {
    if (camera_) camera_->stop();
}

// ══════════════════════════════════════════════════════
//  HOME
// ══════════════════════════════════════════════════════

QWidget* PhotoBoothApp::buildHome() {
    auto* w = new QWidget;
    w->setStyleSheet(screenStyle());
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(60, 60, 60, 40);
    l->addStretch(2);

    auto* logo = new QLabel(QString::fromUtf8("증명\n사진"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(260, 200);
    logo->setStyleSheet("border: 3px solid rgba(255,255,255,110); border-radius: 8px;"
                        "background: transparent; color: white; font-size: 52px; font-weight: 800;");
    l->addWidget(logo, 0, Qt::AlignHCenter);

    auto* sub = new QLabel(QStringLiteral("ID PHOTO"));
    sub->setAlignment(Qt::AlignCenter);
    sub->setStyleSheet("color: white; font-size: 30px; font-weight: 700; letter-spacing: 4px;");
    l->addWidget(sub);
    l->addSpacing(40);

    auto* start = new QPushButton(QStringLiteral("Mulai"));
    start->setFixedSize(420, 80);
    start->setCursor(Qt::PointingHandCursor);
    start->setStyleSheet(primaryButtonStyle());
    connect(start, &QPushButton::clicked, this, [this] { goTo(SIZE_SELECT); });
    l->addWidget(start, 0, Qt::AlignHCenter);
    l->addSpacing(16);

    auto* settings = new QPushButton(QStringLiteral("⚙  Pengaturan"));
    settings->setFixedSize(220, 50);
    settings->setCursor(Qt::PointingHandCursor);
    settings->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,30); color: white; font-size: 16px;"
        "  font-weight: 600; border-radius: 25px; border: 1px solid rgba(255,255,255,60); }"
        "QPushButton:hover { background: rgba(255,255,255,60); }");
    connect(settings, &QPushButton::clicked, this, [this] { goTo(SETTINGS); });
    l->addWidget(settings, 0, Qt::AlignHCenter);
    l->addSpacing(10);

    auto* exitBtn = new QPushButton(QStringLiteral("✕  Keluar"));
    exitBtn->setFixedSize(200, 50);
    exitBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setStyleSheet(pillButtonStyle());
    connect(exitBtn, &QPushButton::clicked, this, [this] { close(); });
    l->addWidget(exitBtn, 0, Qt::AlignHCenter);

    l->addStretch(3);

    auto* footer = new QLabel(QStringLiteral("ID Photo Booth — C++ / Qt6 / EDSDK"));
    footer->setAlignment(Qt::AlignCenter);
    footer->setStyleSheet("color: rgba(255,255,255,140); font-size: 11px;");
    l->addWidget(footer);
    return w;
}

// ══════════════════════════════════════════════════════
//  SIZE SELECT
// ══════════════════════════════════════════════════════

QWidget* PhotoBoothApp::buildSizeSelect() {
    auto* w = new QWidget;
    w->setStyleSheet(screenStyle());
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(60, 40, 60, 40);

    auto* title = new QLabel(QStringLiteral("Pilih tipe foto."));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(titlePillStyle());
    l->addWidget(title, 0, Qt::AlignHCenter);
    l->addSpacing(30);

    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setSpacing(20);

    const int cols = 3;
    for (int i = 0; i < static_cast<int>(cfg_.sizes.size()); ++i) {
        auto* card = new SizeCard(cfg_.sizes[i], i);
        connect(card, &SizeCard::clicked, this, &PhotoBoothApp::onSizeCardClicked);
        sizeCards_.push_back(card);
        grid->addWidget(card, i / cols, i % cols);
    }

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; }"
                          "QScrollBar:vertical { width: 8px; background: rgba(255,255,255,25);"
                          "  border-radius: 4px; }"
                          "QScrollBar::handle:vertical { background: rgba(255,255,255,90);"
                          "  border-radius: 4px; min-height: 40px; }");
    host->setStyleSheet("background: transparent;");
    scroll->setWidget(host);
    l->addWidget(scroll, 1);

    l->addSpacing(20);
    auto* nav = new QHBoxLayout;
    auto* back = new QPushButton(QStringLiteral("Kembali"));
    back->setStyleSheet(navButtonStyle());
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { goTo(HOME); });
    nav->addWidget(back);
    nav->addStretch(1);

    auto* next = new QPushButton(QStringLiteral("Lanjutkan"));
    next->setStyleSheet(navButtonStyle());
    next->setCursor(Qt::PointingHandCursor);
    connect(next, &QPushButton::clicked, this, [this] {
        if (selectedSizeIdx_ < 0) return;
        goTo(GUIDE);
    });
    nav->addWidget(next);
    l->addLayout(nav);
    return w;
}

void PhotoBoothApp::onSizeCardClicked(int index) {
    if (index < 0 || index >= static_cast<int>(cfg_.sizes.size())) return;
    selectedSizeIdx_ = index;
    for (int i = 0; i < sizeCards_.size(); ++i)
        sizeCards_[i]->setSelected(i == index);

    const PhotoSize& s = cfg_.sizes[index];
    if (preview_)    preview_->setPhotoRatio(s.w_mm, s.h_mm);
    if (sizeBadge_)  sizeBadge_->setText(QString("📐 %1").arg(s.name));
}

// ══════════════════════════════════════════════════════
//  GUIDE
// ══════════════════════════════════════════════════════

QWidget* PhotoBoothApp::buildGuide() {
    auto* w = new QWidget;
    w->setStyleSheet(screenStyle());
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(60, 40, 60, 40);

    auto* title = new QLabel(QStringLiteral("Panduan Fotografi"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(titlePillStyle());
    l->addWidget(title, 0, Qt::AlignHCenter);
    l->addSpacing(20);

    auto* panel = new QFrame;
    panel->setStyleSheet("QFrame { background: white; border-radius: 6px; }");
    auto* pl = new QVBoxLayout(panel);
    pl->setContentsMargins(60, 50, 60, 50);
    pl->setSpacing(16);

    const QStringList rules = {
        QStringLiteral("1. Lihat lurus ke arah kamera."),
        QStringLiteral("2. Kedua alis harus terlihat jelas."),
        QStringLiteral("3. Gigi Anda tidak boleh terlihat."),
        QStringLiteral("4. Anda harus melepas kacamata."),
        QStringLiteral("5. Anda harus mengenakan pakaian yang mencolok dari latar belakang."),
    };
    for (const QString& r : rules) {
        auto* lbl = new QLabel(r);
        lbl->setWordWrap(true);
        lbl->setStyleSheet("color: #1A1A1A; font-size: 24px; font-weight: 600;");
        pl->addWidget(lbl);
    }
    pl->addStretch(1);
    l->addWidget(panel, 1);
    l->addSpacing(20);

    auto* nav = new QHBoxLayout;
    auto* back = new QPushButton(QStringLiteral("Kembali"));
    back->setStyleSheet(navButtonStyle());
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { goTo(SIZE_SELECT); });
    nav->addWidget(back);
    nav->addStretch(1);

    auto* next = new QPushButton(QStringLiteral("Lanjutkan"));
    next->setStyleSheet(navButtonStyle());
    next->setCursor(Qt::PointingHandCursor);
    connect(next, &QPushButton::clicked, this, [this] { goTo(CAMERA); });
    nav->addWidget(next);
    l->addLayout(nav);
    return w;
}

// ══════════════════════════════════════════════════════
//  CAMERA
// ══════════════════════════════════════════════════════

QWidget* PhotoBoothApp::buildCamera() {
    auto* w = new QWidget;
    w->setStyleSheet(screenStyle());
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(20, 20, 20, 20);

    auto* body = new QHBoxLayout;
    l->addLayout(body, 1);

    preview_ = new CameraPreview;
    body->addWidget(preview_, 7);

    auto* tools = new QFrame;
    tools->setStyleSheet(panelStyle());
    auto* tl = new QVBoxLayout(tools);
    tl->setContentsMargins(16, 16, 16, 16);
    tl->setSpacing(10);
    body->addWidget(tools, 3);

    camInfoLabel_ = new QLabel(QStringLiteral("📷 Menghubungkan…"));
    camInfoLabel_->setWordWrap(true);
    camInfoLabel_->setStyleSheet("color: #90EE90; font-size: 12px; font-weight: 600; background: transparent;");
    tl->addWidget(camInfoLabel_);

    sizeBadge_ = new QLabel;
    sizeBadge_->setAlignment(Qt::AlignCenter);
    sizeBadge_->setStyleSheet(QString("color: %1; font-size: 18px; font-weight: 700; background: transparent;")
                                  .arg(cfg_.theme.accent));
    tl->addWidget(sizeBadge_);

    auto* hint = new QLabel(QStringLiteral("↕ Geser kotak merah\n↔ Tarik sudut untuk ubah ukuran"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color: rgba(255,255,255,180); font-size: 12px; background: transparent;");
    tl->addWidget(hint);

    // Zoom
    auto* zoomRow = new QHBoxLayout;
    auto* zin  = new QPushButton(QStringLiteral("＋ Zoom"));
    auto* zout = new QPushButton(QStringLiteral("－ Zoom"));
    zin->setStyleSheet(toolButtonStyle());
    zout->setStyleSheet(toolButtonStyle());
    connect(zin,  &QPushButton::clicked, this, [this] { camera_->setZoom(camera_->zoom() + 0.1); });
    connect(zout, &QPushButton::clicked, this, [this] { camera_->setZoom(camera_->zoom() - 0.1); });
    zoomRow->addWidget(zin);
    zoomRow->addWidget(zout);
    tl->addLayout(zoomRow);

    // Rotate
    auto* rotRow = new QHBoxLayout;
    auto* rl = new QPushButton(QStringLiteral("↺"));
    auto* rr = new QPushButton(QStringLiteral("↻"));
    rl->setStyleSheet(toolButtonStyle());
    rr->setStyleSheet(toolButtonStyle());
    connect(rl, &QPushButton::clicked, this, [this] {
        camera_->setRotation(camera_->rotation() - 90);
        preview_->resetGuide();
    });
    connect(rr, &QPushButton::clicked, this, [this] {
        camera_->setRotation(camera_->rotation() + 90);
        preview_->resetGuide();
    });
    rotRow->addWidget(rl);
    rotRow->addWidget(rr);
    tl->addLayout(rotRow);

    auto mkLabel = [](const QString& t) {
        auto* x = new QLabel(t);
        x->setStyleSheet("color: white; font-size: 14px; background: transparent;");
        return x;
    };

    tl->addWidget(mkLabel(QStringLiteral("Kecerahan")));
    camBright_ = new QSlider(Qt::Horizontal);
    camBright_->setRange(-50, 50);
    connect(camBright_, &QSlider::valueChanged, this, [this](int v) { camera_->setBrightness(v); });
    tl->addWidget(camBright_);

    tl->addWidget(mkLabel(QStringLiteral("Kontras")));
    camContrast_ = new QSlider(Qt::Horizontal);
    camContrast_->setRange(-50, 50);
    connect(camContrast_, &QSlider::valueChanged, this, [this](int v) { camera_->setContrast(v); });
    tl->addWidget(camContrast_);

    auto* reset = new QPushButton(QStringLiteral("Reset"));
    reset->setStyleSheet(toolButtonStyle());
    connect(reset, &QPushButton::clicked, this, [this] {
        camBright_->setValue(0);
        camContrast_->setValue(0);
        camera_->resetAdjustments();
        preview_->resetGuide();
    });
    tl->addWidget(reset);
    tl->addStretch(1);

    // Bottom nav
    auto* nav = new QHBoxLayout;
    l->addLayout(nav);
    auto* back = new QPushButton(QStringLiteral("Kembali"));
    back->setStyleSheet(navButtonStyle());
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { goTo(GUIDE); });
    nav->addWidget(back);
    nav->addStretch(1);

    captureBtn_ = new QPushButton(QStringLiteral("Ambil Foto"));
    captureBtn_->setStyleSheet(primaryButtonStyle());
    captureBtn_->setCursor(Qt::PointingHandCursor);
    connect(captureBtn_, &QPushButton::clicked, this, &PhotoBoothApp::startCountdown);
    nav->addWidget(captureBtn_);

    // Overlays (children of w so they can cover the whole screen)
    countdownLbl_ = new QLabel(w);
    countdownLbl_->setAlignment(Qt::AlignCenter);
    countdownLbl_->setStyleSheet("background: rgba(0,0,0,160); color: white;"
                                 "font-size: 180px; font-weight: 800;");
    countdownLbl_->hide();

    flashLbl_ = new QLabel(w);
    flashLbl_->setStyleSheet("background: white;");
    flashLbl_->hide();

    processingLbl_ = new QLabel(QStringLiteral("Memproses foto…"), w);
    processingLbl_->setAlignment(Qt::AlignCenter);
    processingLbl_->setStyleSheet("background: rgba(0,0,0,190); color: white;"
                                  "font-size: 48px; font-weight: 800;");
    processingLbl_->hide();

    return w;
}

void PhotoBoothApp::startCountdown() {
    qInfo() << "[UI] capture button pressed, size idx" << selectedSizeIdx_
            << "countdown" << cfg_.countdown;
    if (selectedSizeIdx_ < 0) { qWarning() << "[UI] no size selected — capture ignored"; return; }
    captureBtn_->setEnabled(false);

    countdownValue_ = cfg_.countdown;
    if (countdownValue_ <= 0) { doCapture(); return; }

    countdownLbl_->setGeometry(stack_->currentWidget()->rect());
    countdownLbl_->setText(QString::number(countdownValue_));
    countdownLbl_->show();
    countdownLbl_->raise();
    countdownTimer_->start();
}

void PhotoBoothApp::onCountdownTick() {
    if (--countdownValue_ > 0) {
        countdownLbl_->setText(QString::number(countdownValue_));
        return;
    }
    countdownTimer_->stop();
    countdownLbl_->hide();
    doCapture();
}

void PhotoBoothApp::doCapture() {
    qInfo() << "[UI] doCapture";
    // Flash
    flashLbl_->setGeometry(stack_->currentWidget()->rect());
    flashLbl_->show();
    flashLbl_->raise();
    QTimer::singleShot(150, this, [this] { flashLbl_->hide(); });

    // Show a "processing" overlay so the UI clearly isn't dead while the
    // shutter fires and the full-res image downloads over USB.
    processingLbl_->setGeometry(stack_->currentWidget()->rect());
    processingLbl_->show();
    processingLbl_->raise();

    // The capture is blocking (shutter + multi-MB USB download) and must NOT
    // run on the UI thread, or the whole window freezes. Run it on a worker
    // thread and hand the result back to the UI thread via a queued signal.
    std::thread([this] {
        cv::Mat frame = camera_->captureFrame();
        // Bounce back onto the Qt UI thread — captureFinished_ handles the rest.
        QMetaObject::invokeMethod(this, [this, frame] {
            finishCapture(frame);
        }, Qt::QueuedConnection);
    }).detach();
}

void PhotoBoothApp::finishCapture(const cv::Mat& frame) {
    qInfo() << "[UI] finishCapture" << frame.cols << "x" << frame.rows;
    processingLbl_->hide();
    captureBtn_->setEnabled(true);

    if (frame.empty()) {
        QMessageBox::warning(this, QStringLiteral("Kamera"),
                             QStringLiteral("Gagal mengambil foto."));
        return;
    }

    sessionDir_ = makeSessionDir();

    const cv::Rect crop = preview_->cropRectOnFrame(frame.cols, frame.rows);
    capturedImage_ = ImageService::cropRect(frame, crop);
    editedImage_   = capturedImage_.clone();

    ImageService::save(frame,          QDir(sessionDir_).filePath("original.jpg"));
    ImageService::save(capturedImage_, QDir(sessionDir_).filePath("cropped.jpg"));

    edBright_->setValue(0);
    edContrast_->setValue(0);
    edSat_->setValue(0);
    bgChoice_   = "original";
    edRotation_ = 0;
    edFlipped_  = false;

    QTimer::singleShot(150, this, [this] { goTo(EDITOR); });
}

// ══════════════════════════════════════════════════════
//  EDITOR
// ══════════════════════════════════════════════════════

QWidget* PhotoBoothApp::buildEditor() {
    auto* w = new QWidget;
    w->setStyleSheet(screenStyle());
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(20, 20, 20, 20);

    auto* body = new QHBoxLayout;
    l->addLayout(body, 1);

    editorPreview_ = new QLabel;
    editorPreview_->setAlignment(Qt::AlignCenter);
    editorPreview_->setMinimumSize(500, 400);
    editorPreview_->setStyleSheet("background: #2A2A2A; border-radius: 8px;");
    body->addWidget(editorPreview_, 7);

    auto* tools = new QFrame;
    tools->setStyleSheet(panelStyle());
    auto* tl = new QVBoxLayout(tools);
    tl->setContentsMargins(16, 16, 16, 16);
    tl->setSpacing(10);
    body->addWidget(tools, 3);

    editorBadge_ = new QLabel;
    editorBadge_->setAlignment(Qt::AlignCenter);
    editorBadge_->setStyleSheet(QString("color: %1; font-size: 16px; font-weight: 700; background: transparent;")
                                    .arg(cfg_.theme.accent));
    tl->addWidget(editorBadge_);

    auto mkLabel = [](const QString& t) {
        auto* x = new QLabel(t);
        x->setStyleSheet("color: white; font-size: 14px; font-weight: 600; background: transparent;");
        return x;
    };

    tl->addWidget(mkLabel(QStringLiteral("Kecerahan")));
    edBright_ = new QSlider(Qt::Horizontal);
    edBright_->setRange(-50, 50);
    connect(edBright_, &QSlider::valueChanged, this, [this] { refreshEditor(); });
    tl->addWidget(edBright_);

    tl->addWidget(mkLabel(QStringLiteral("Kontras")));
    edContrast_ = new QSlider(Qt::Horizontal);
    edContrast_->setRange(-50, 50);
    connect(edContrast_, &QSlider::valueChanged, this, [this] { refreshEditor(); });
    tl->addWidget(edContrast_);

    tl->addWidget(mkLabel(QStringLiteral("Saturasi")));
    edSat_ = new QSlider(Qt::Horizontal);
    edSat_->setRange(-50, 50);
    connect(edSat_, &QSlider::valueChanged, this, [this] { refreshEditor(); });
    tl->addWidget(edSat_);

    // Rotate / flip
    auto* row = new QHBoxLayout;
    auto* rl = new QPushButton(QStringLiteral("↺"));
    auto* rr = new QPushButton(QStringLiteral("↻"));
    auto* fl = new QPushButton(QStringLiteral("⇔"));
    for (auto* b : { rl, rr, fl }) b->setStyleSheet(toolButtonStyle());
    connect(rl, &QPushButton::clicked, this, [this] { edRotation_ = (edRotation_ + 270) % 360; refreshEditor(); });
    connect(rr, &QPushButton::clicked, this, [this] { edRotation_ = (edRotation_ +  90) % 360; refreshEditor(); });
    connect(fl, &QPushButton::clicked, this, [this] { edFlipped_ = !edFlipped_; refreshEditor(); });
    row->addWidget(rl); row->addWidget(rr); row->addWidget(fl);
    tl->addLayout(row);

    // Background
    tl->addWidget(mkLabel(QStringLiteral("Latar Belakang")));
    auto* bgRow = new QHBoxLayout;
    struct BgOpt { const char* key; const char* css; };
    const BgOpt opts[] = {
        { "original", "#808080" },
        { "white",    "#FFFFFF" },
        { "blue",     "#4682B4" },
        { "red",      "#C0392B" },
    };
    for (const auto& o : opts) {
        auto* b = new QPushButton;
        b->setFixedSize(50, 50);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString("QPushButton { background: %1; border-radius: 25px;"
                                 "  border: 3px solid #CCCCCC; }"
                                 "QPushButton:hover { border-color: %2; }")
                             .arg(o.css, cfg_.theme.accent));
        const QString key = QString::fromLatin1(o.key);
        connect(b, &QPushButton::clicked, this, [this, key] {
            bgChoice_ = key;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            refreshEditor();
            QApplication::restoreOverrideCursor();
        });
        bgRow->addWidget(b);
    }
    tl->addLayout(bgRow);

    auto* reset = new QPushButton(QStringLiteral("Reset"));
    reset->setStyleSheet(toolButtonStyle());
    connect(reset, &QPushButton::clicked, this, [this] {
        edBright_->setValue(0);
        edContrast_->setValue(0);
        edSat_->setValue(0);
        bgChoice_   = "original";
        edRotation_ = 0;
        edFlipped_  = false;
        refreshEditor();
    });
    tl->addWidget(reset);
    tl->addStretch(1);

    auto* nav = new QHBoxLayout;
    l->addLayout(nav);
    auto* retake = new QPushButton(QStringLiteral("Ulangi"));
    retake->setStyleSheet(navButtonStyle());
    retake->setCursor(Qt::PointingHandCursor);
    connect(retake, &QPushButton::clicked, this, [this] { goTo(CAMERA); });
    nav->addWidget(retake);
    nav->addStretch(1);

    auto* next = new QPushButton(QStringLiteral("Lanjutkan"));
    next->setStyleSheet(navButtonStyle());
    next->setCursor(Qt::PointingHandCursor);
    connect(next, &QPushButton::clicked, this, [this] {
        if (editedImage_.empty()) return;
        ImageService::save(editedImage_, QDir(sessionDir_).filePath("edited.jpg"));
        goTo(PRINT_VIEW);
    });
    nav->addWidget(next);
    return w;
}

void PhotoBoothApp::refreshEditor() {
    if (capturedImage_.empty()) return;

    cv::Mat img = capturedImage_;
    if (edRotation_ != 0) img = ImageService::rotate(img, edRotation_);
    if (edFlipped_)       img = ImageService::flipH(img);

    img = ImageService::adjust(img,
                               edBright_   ? edBright_->value()   : 0,
                               edContrast_ ? edContrast_->value() : 0,
                               edSat_      ? edSat_->value()      : 0);

    if (bgChoice_ != "original")
        img = ImageService::changeBackground(img, bgChoice_);

    editedImage_ = img;

    if (selectedSizeIdx_ >= 0 && editorBadge_) {
        const PhotoSize& s = cfg_.sizes[selectedSizeIdx_];
        editorBadge_->setText(QString("📐 %1  (%2×%3 mm)").arg(s.name).arg(s.w_mm).arg(s.h_mm));
    }
    editorPreview_->setPixmap(ImageService::toPixmap(editedImage_, editorPreview_->size()));
}

// ══════════════════════════════════════════════════════
//  PRINT
// ══════════════════════════════════════════════════════

QWidget* PhotoBoothApp::buildPrint() {
    auto* w = new QWidget;
    w->setStyleSheet(screenStyle());
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(40, 30, 40, 30);

    auto* title = new QLabel(QStringLiteral("Tata Letak Cetak — 4R (4×6 inci)"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(titlePillStyle());
    l->addWidget(title, 0, Qt::AlignHCenter);
    l->addSpacing(12);

    auto* body = new QHBoxLayout;
    l->addLayout(body, 1);

    printPreview_ = new QLabel;
    printPreview_->setAlignment(Qt::AlignCenter);
    printPreview_->setMinimumSize(600, 420);
    printPreview_->setStyleSheet("background: white; border: 2px solid #CCCCCC; border-radius: 8px;");
    body->addWidget(printPreview_, 7);

    auto* ctrl = new QFrame;
    ctrl->setStyleSheet(panelStyle());
    auto* cl = new QVBoxLayout(ctrl);
    cl->setContentsMargins(20, 20, 20, 20);
    cl->setSpacing(12);
    body->addWidget(ctrl, 3);

    printCount_ = new QLabel;
    printCount_->setAlignment(Qt::AlignCenter);
    printCount_->setStyleSheet(QString("color: %1; font-size: 18px; font-weight: 700; background: transparent;")
                                   .arg(cfg_.theme.accent));
    cl->addWidget(printCount_);

    auto mkLabel = [](const QString& t) {
        auto* x = new QLabel(t);
        x->setStyleSheet("color: white; font-size: 15px; font-weight: 600; background: transparent;");
        return x;
    };
    const QString spinCss =
        "QDoubleSpinBox { padding: 10px; font-size: 16px; border-radius: 8px;"
        "  background: white; border: 2px solid #CCCCCC; }";

    cl->addWidget(mkLabel(QStringLiteral("Jarak antar foto (mm)")));
    marginSpin_ = new QDoubleSpinBox;
    marginSpin_->setRange(0.0, 10.0);
    marginSpin_->setSingleStep(0.5);
    marginSpin_->setDecimals(1);
    marginSpin_->setValue(cfg_.marginMm);
    marginSpin_->setStyleSheet(spinCss);
    connect(marginSpin_, &QDoubleSpinBox::valueChanged, this, [this] { refreshPrint(); });
    cl->addWidget(marginSpin_);

    cl->addWidget(mkLabel(QStringLiteral("Jarak foto ke garis (mm)")));
    gapSpin_ = new QDoubleSpinBox;
    gapSpin_->setRange(0.0, 10.0);
    gapSpin_->setSingleStep(0.5);
    gapSpin_->setDecimals(1);
    gapSpin_->setValue(cfg_.borderGapMm);
    gapSpin_->setStyleSheet(spinCss);
    connect(gapSpin_, &QDoubleSpinBox::valueChanged, this, [this] { refreshPrint(); });
    cl->addWidget(gapSpin_);

    cl->addSpacing(8);

    auto addSave = [&](const QString& text, const QString& ext, const QString& filter) {
        auto* b = new QPushButton(text);
        b->setStyleSheet(toolButtonStyle());
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, ext, filter] {
            if (layoutImage_.empty()) return;
            const QString def = QDir(sessionDir_.isEmpty() ? cfg_.output : sessionDir_)
                                    .filePath("id_photo_4R." + ext);
            const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("Simpan"), def, filter);
            if (p.isEmpty()) return;
            if (ImageService::save(layoutImage_, p))
                QMessageBox::information(this, QStringLiteral("Tersimpan"), p);
            else
                QMessageBox::warning(this, QStringLiteral("Gagal"), QStringLiteral("Tidak bisa menyimpan file."));
        });
        cl->addWidget(b);
    };
    addSave(QStringLiteral("💾  Simpan JPG"), "jpg", QStringLiteral("JPEG (*.jpg)"));
    addSave(QStringLiteral("💾  Simpan PNG"), "png", QStringLiteral("PNG (*.png)"));

    {
        auto* bpdf = new QPushButton(QStringLiteral("💾  Simpan PDF"));
        bpdf->setStyleSheet(toolButtonStyle());
        bpdf->setCursor(Qt::PointingHandCursor);
        connect(bpdf, &QPushButton::clicked, this, [this] {
            if (layoutImage_.empty()) return;
            const QString def = QDir(sessionDir_.isEmpty() ? cfg_.output : sessionDir_)
                                    .filePath("id_photo_4R.pdf");
            const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("Simpan PDF"),
                                                           def, QStringLiteral("PDF (*.pdf)"));
            if (p.isEmpty()) return;
            if (ImageService::savePdf(layoutImage_, p, cfg_.paperWmm, cfg_.paperHmm, cfg_.dpi))
                QMessageBox::information(this, QStringLiteral("Tersimpan"), p);
            else
                QMessageBox::warning(this, QStringLiteral("Gagal"), QStringLiteral("Tidak bisa menyimpan PDF."));
        });
        cl->addWidget(bpdf);
    }

    auto* printBtn = new QPushButton(QStringLiteral("🖨  Cetak"));
    printBtn->setStyleSheet(QString("QPushButton { background: %1; color: %2; font-weight: 700;"
                                    "  font-size: 17px; padding: 14px; border-radius: 12px; border: none; }"
                                    "QPushButton:hover { background: #FFF0A0; }")
                                .arg(cfg_.theme.accent, cfg_.theme.primary));
    printBtn->setCursor(Qt::PointingHandCursor);
    connect(printBtn, &QPushButton::clicked, this, [this] {
        if (layoutImage_.empty()) return;
        const QString tmp = QDir(sessionDir_.isEmpty() ? cfg_.output : sessionDir_)
                                .filePath("print_layout.jpg");
        if (!ImageService::save(layoutImage_, tmp)) {
            QMessageBox::warning(this, QStringLiteral("Cetak"), QStringLiteral("Gagal menyiapkan berkas cetak."));
            return;
        }
        // Hand off to the OS default print verb
        const std::wstring wpath = QDir::toNativeSeparators(tmp).toStdWString();
        ShellExecuteW(nullptr, L"print", wpath.c_str(), nullptr, nullptr, SW_HIDE);
        QMessageBox::information(this, QStringLiteral("Cetak"),
                                 QStringLiteral("Dikirim ke printer."));
    });
    cl->addWidget(printBtn);
    cl->addStretch(1);

    auto* nav = new QHBoxLayout;
    l->addLayout(nav);
    auto* back = new QPushButton(QStringLiteral("Kembali"));
    back->setStyleSheet(navButtonStyle());
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { goTo(EDITOR); });
    nav->addWidget(back);
    nav->addStretch(1);

    auto* done = new QPushButton(QStringLiteral("✓  Selesai"));
    done->setStyleSheet(navButtonStyle());
    done->setCursor(Qt::PointingHandCursor);
    connect(done, &QPushButton::clicked, this, [this] { saveSessionFinal(); resetSession(); goTo(HOME); });
    nav->addWidget(done);
    return w;
}

void PhotoBoothApp::refreshPrint() {
    if (editedImage_.empty() || selectedSizeIdx_ < 0) return;

    const PhotoSize& s = cfg_.sizes[selectedSizeIdx_];
    const double margin = marginSpin_ ? marginSpin_->value() : cfg_.marginMm;
    const double gap    = gapSpin_    ? gapSpin_->value()    : cfg_.borderGapMm;

    layoutImage_ = ImageService::generatePrintLayout(
        editedImage_, s, cfg_.paperWmm, cfg_.paperHmm, cfg_.dpi, margin, gap);

    if (layoutImage_.empty()) return;

    const int n = ImageService::countPhotosOnLayout(
        s, cfg_.paperWmm, cfg_.paperHmm, margin, gap);
    printCount_->setText(QString("📐 %1  ×%2 lembar").arg(s.name).arg(n));

    cv::Mat thumb;
    cv::resize(layoutImage_, thumb, cv::Size(), 0.3, 0.3, cv::INTER_AREA);
    printPreview_->setPixmap(ImageService::toPixmap(thumb, printPreview_->size()));
}

// ══════════════════════════════════════════════════════
//  SETTINGS
// ══════════════════════════════════════════════════════

QWidget* PhotoBoothApp::buildSettings() {
    auto* w = new QWidget;
    w->setStyleSheet(screenStyle());
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(60, 30, 60, 30);

    auto* title = new QLabel(QStringLiteral("Pengaturan"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(titlePillStyle());
    l->addWidget(title, 0, Qt::AlignHCenter);
    l->addSpacing(20);

    auto* content = new QWidget;
    content->setStyleSheet("QWidget { background: rgba(255,255,255,225); border-radius: 12px; }");
    auto* form = new QFormLayout(content);
    form->setContentsMargins(40, 30, 40, 30);
    form->setSpacing(18);
    form->setLabelAlignment(Qt::AlignRight);

    const QString inputCss =
        "padding: 10px; font-size: 15px; border: 2px solid #CCCCCC;"
        "border-radius: 8px; background: white;";

    setCamIndex_ = new QComboBox;
    setCamIndex_->addItem(QStringLiteral("Otomatis"), QStringLiteral("auto"));
    for (int i = 0; i < 10; ++i)
        setCamIndex_->addItem(QString("Kamera %1").arg(i), QString::number(i));
    const int ci = setCamIndex_->findData(cfg_.camera.index);
    if (ci >= 0) setCamIndex_->setCurrentIndex(ci);
    setCamIndex_->setStyleSheet(inputCss);
    form->addRow(QStringLiteral("Kamera:"), setCamIndex_);

    setCountdown_ = new QSpinBox;
    setCountdown_->setRange(0, 10);
    setCountdown_->setValue(cfg_.countdown);
    setCountdown_->setStyleSheet(inputCss);
    form->addRow(QStringLiteral("Hitungan (detik):"), setCountdown_);

    setDpi_ = new QSpinBox;
    setDpi_->setRange(150, 600);
    setDpi_->setSingleStep(50);
    setDpi_->setValue(cfg_.dpi);
    setDpi_->setStyleSheet(inputCss);
    form->addRow(QStringLiteral("DPI cetak:"), setDpi_);

    auto* outLbl = new QLabel(cfg_.output);
    outLbl->setWordWrap(true);
    outLbl->setStyleSheet("color: #333333; font-size: 13px;");
    form->addRow(QStringLiteral("Folder simpan:"), outLbl);

    auto* cfgLbl = new QLabel(cfg_.loadedFrom());
    cfgLbl->setWordWrap(true);
    cfgLbl->setStyleSheet("color: #666666; font-size: 12px;");
    form->addRow(QStringLiteral("config.json:"), cfgLbl);

    setStatus_ = new QLabel;
    setStatus_->setStyleSheet("color: #1E7A1E; font-size: 13px; font-weight: 600;");
    form->addRow(QString(), setStatus_);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    scroll->setWidget(content);
    l->addWidget(scroll, 1);
    l->addSpacing(16);

    auto* nav = new QHBoxLayout;
    auto* back = new QPushButton(QStringLiteral("Kembali"));
    back->setStyleSheet(navButtonStyle());
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { goTo(HOME); });
    nav->addWidget(back);
    nav->addStretch(1);

    auto* save = new QPushButton(QStringLiteral("💾  Simpan"));
    save->setStyleSheet(navButtonStyle());
    save->setCursor(Qt::PointingHandCursor);
    connect(save, &QPushButton::clicked, this, [this] {
        cfg_.camera.index = setCamIndex_->currentData().toString();
        cfg_.countdown    = setCountdown_->value();
        cfg_.dpi          = setDpi_->value();
        if (marginSpin_) cfg_.marginMm    = marginSpin_->value();
        if (gapSpin_)    cfg_.borderGapMm = gapSpin_->value();

        if (cfg_.save())
            setStatus_->setText(QStringLiteral("✓ Tersimpan. Mulai ulang aplikasi untuk ganti kamera."));
        else
            setStatus_->setText(QStringLiteral("✗ Gagal menyimpan config.json"));
    });
    nav->addWidget(save);
    l->addLayout(nav);
    return w;
}

// ══════════════════════════════════════════════════════
//  Navigation + lifecycle
// ══════════════════════════════════════════════════════

void PhotoBoothApp::goTo(Screen s) {
    // Entering the camera screen: make sure the thread runs and the guide matches
    if (s == CAMERA) {
        if (selectedSizeIdx_ >= 0) {
            const PhotoSize& ps = cfg_.sizes[selectedSizeIdx_];
            preview_->setPhotoRatio(ps.w_mm, ps.h_mm);
            sizeBadge_->setText(QString("📐 %1").arg(ps.name));
        }
        camBright_->setValue(0);
        camContrast_->setValue(0);
        camera_->resetAdjustments();
        if (!camera_->isRunning()) camera_->start();
    }
    if (s == EDITOR)     refreshEditor();
    if (s == PRINT_VIEW) refreshPrint();

    stack_->setCurrentIndex(s);
}

void PhotoBoothApp::resetSession() {
    capturedImage_.release();
    editedImage_.release();
    layoutImage_.release();
    sessionDir_.clear();
    bgChoice_   = "original";
    edRotation_ = 0;
    edFlipped_  = false;
}

QString PhotoBoothApp::makeSessionDir() {
    const QDateTime now = QDateTime::currentDateTime();
    const QString dir = QDir(cfg_.output).filePath(
        now.toString("yyyy-MM-dd") + "/" + now.toString("HH-mm-ss"));
    QDir().mkpath(dir);
    return dir;
}

void PhotoBoothApp::saveSessionFinal() {
    if (sessionDir_.isEmpty() || layoutImage_.empty()) return;
    if (!editedImage_.empty())
        ImageService::save(editedImage_, QDir(sessionDir_).filePath("final_photo.jpg"));
    ImageService::save(layoutImage_, QDir(sessionDir_).filePath("final_layout_4R.jpg"));
    ImageService::savePdf(layoutImage_, QDir(sessionDir_).filePath("final_layout_4R.pdf"),
                          cfg_.paperWmm, cfg_.paperHmm, cfg_.dpi);
    qInfo() << "[SAVE] session deliverables written to" << sessionDir_;
}

void PhotoBoothApp::onFrameReady(const QImage& mirrored) {
    if (stack_->currentIndex() == CAMERA && preview_)
        preview_->updateFrame(mirrored);
}

void PhotoBoothApp::onCameraInfo(const QString& text) {
    if (camInfoLabel_) camInfoLabel_->setText(text);
}

void PhotoBoothApp::onErrorText(const QString& text) {
    qWarning() << "[CAM]" << text;
    if (camInfoLabel_) camInfoLabel_->setText("⚠ " + text);
}

void PhotoBoothApp::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape:
            if (stack_->currentIndex() == HOME) close();
            else { resetSession(); goTo(HOME); }
            break;
        case Qt::Key_F12:
            goTo(SETTINGS);
            break;
        case Qt::Key_F11:
            if (isFullScreen()) showNormal(); else showFullScreen();
            break;
        default:
            QMainWindow::keyPressEvent(e);
    }
}

void PhotoBoothApp::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (countdownLbl_) countdownLbl_->setGeometry(stack_->currentWidget()->rect());
    if (flashLbl_)     flashLbl_->setGeometry(stack_->currentWidget()->rect());
    if (processingLbl_) processingLbl_->setGeometry(stack_->currentWidget()->rect());
}

void PhotoBoothApp::closeEvent(QCloseEvent* e) {
    if (camera_) camera_->stop();
    e->accept();
}
