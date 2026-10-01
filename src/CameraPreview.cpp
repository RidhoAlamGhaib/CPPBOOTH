#include "CameraPreview.h"
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

CameraPreview::CameraPreview(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setMinimumSize(640, 480);
    setStyleSheet("background: #1A1A1A; border-radius: 8px;");
    setMouseTracking(true);
}

// ══════════════════════════════════════════════════════
//  Frame + guide setup
// ══════════════════════════════════════════════════════

void CameraPreview::setPhotoRatio(int w_mm, int h_mm) {
    if (w_mm > 0 && h_mm > 0)
        photoRatio_ = static_cast<double>(w_mm) / h_mm;
    guideInit_ = false;      // recentre on next frame
    update();
}

void CameraPreview::updateFrame(const QImage& mirrored) {
    if (mirrored.isNull()) return;
    frame_ = QPixmap::fromImage(mirrored)
                 .scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!guideInit_) initGuide(frame_.width(), frame_.height());
    update();
}

void CameraPreview::resizeEvent(QResizeEvent* e) {
    QLabel::resizeEvent(e);
    // Guide is normalised, so it rescales automatically — but the aspect
    // correction depends on the displayed pixel size, so redo it.
    guideInit_ = false;
}

void CameraPreview::initGuide(int pixW, int pixH) {
    if (pixW < 10 || pixH < 10) return;

    const double frameAspect = static_cast<double>(pixW) / pixH;

    // Fit the guide inside ~80% of the frame, honouring the photo aspect.
    if (photoRatio_ < frameAspect) {
        gh_ = 0.80;
        gw_ = gh_ * photoRatio_ / frameAspect;
    } else {
        gw_ = 0.80;
        gh_ = gw_ * frameAspect / photoRatio_;
    }

    gw_ = std::min(gw_, 0.95);
    gh_ = std::min(gh_, 0.95);

    // Exact aspect in pixel space: gw*pixW / (gh*pixH) == photoRatio_
    const double curRatio = (gw_ * pixW) / (gh_ * pixH);
    if (std::abs(curRatio - photoRatio_) > 0.005) {
        if (curRatio > photoRatio_)
            gw_ = gh_ * pixH * photoRatio_ / pixW;
        else
            gh_ = gw_ * pixW / (photoRatio_ * pixH);
    }

    gw_ = std::clamp(gw_, 0.05, 0.98);
    gh_ = std::clamp(gh_, 0.05, 0.98);
    gx_ = (1.0 - gw_) / 2.0;
    gy_ = (1.0 - gh_) / 2.0;
    guideInit_ = true;
}

// ══════════════════════════════════════════════════════
//  Geometry helpers
// ══════════════════════════════════════════════════════

QRect CameraPreview::frameRect() const {
    if (frame_.isNull()) return rect();
    const int ox = (width()  - frame_.width())  / 2;
    const int oy = (height() - frame_.height()) / 2;
    return QRect(ox, oy, frame_.width(), frame_.height());
}

QRect CameraPreview::guidePixelRect() const {
    const QRect fr = frameRect();
    return QRect(fr.x() + static_cast<int>(gx_ * fr.width()),
                 fr.y() + static_cast<int>(gy_ * fr.height()),
                 static_cast<int>(gw_ * fr.width()),
                 static_cast<int>(gh_ * fr.height()));
}

QMap<QString, QRectF> CameraPreview::cornerRects() const {
    const QRect g = guidePixelRect();
    const double h = HANDLE;
    QMap<QString, QRectF> m;
    m["tl"] = QRectF(g.left()  - h, g.top()    - h, h * 2, h * 2);
    m["tr"] = QRectF(g.right() - h, g.top()    - h, h * 2, h * 2);
    m["bl"] = QRectF(g.left()  - h, g.bottom() - h, h * 2, h * 2);
    m["br"] = QRectF(g.right() - h, g.bottom() - h, h * 2, h * 2);
    return m;
}

QString CameraPreview::hitCorner(const QPoint& p) const {
    const auto rects = cornerRects();
    for (auto it = rects.constBegin(); it != rects.constEnd(); ++it)
        if (it.value().contains(p)) return it.key();
    return {};
}

bool CameraPreview::hitGuide(const QPoint& p) const {
    return guidePixelRect().contains(p);
}

cv::Rect CameraPreview::cropRectOnFrame(int frameW, int frameH) const {
    if (frameW <= 0 || frameH <= 0) return {};

    // The preview shows a mirrored image, so the guide's left edge in preview
    // space corresponds to (1 - gx - gw) in raw-frame space.
    const double rawX = 1.0 - gx_ - gw_;

    int x = static_cast<int>(std::lround(rawX * frameW));
    int y = static_cast<int>(std::lround(gy_  * frameH));
    int w = static_cast<int>(std::lround(gw_  * frameW));
    int h = static_cast<int>(std::lround(gh_  * frameH));

    x = std::clamp(x, 0, frameW - 1);
    y = std::clamp(y, 0, frameH - 1);
    w = std::clamp(w, 1, frameW - x);
    h = std::clamp(h, 1, frameH - y);
    return cv::Rect(x, y, w, h);
}

// ══════════════════════════════════════════════════════
//  Mouse interaction
// ══════════════════════════════════════════════════════

void CameraPreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QLabel::mousePressEvent(e); return; }

    const QString c = hitCorner(e->pos());
    if (!c.isEmpty()) {
        resizing_ = true;
        corner_   = c;
    } else if (hitGuide(e->pos())) {
        dragging_ = true;
        const QRect g = guidePixelRect();
        dragOffset_ = QPointF(e->pos().x() - g.x(), e->pos().y() - g.y());
    }
    QLabel::mousePressEvent(e);
}

void CameraPreview::mouseMoveEvent(QMouseEvent* e) {
    const QRect fr = frameRect();
    if (fr.width() < 10 || fr.height() < 10) { QLabel::mouseMoveEvent(e); return; }

    const double pw = fr.width();
    const double ph = fr.height();

    if (dragging_) {
        const double nx = (e->pos().x() - dragOffset_.x() - fr.x()) / pw;
        const double ny = (e->pos().y() - dragOffset_.y() - fr.y()) / ph;
        gx_ = std::clamp(nx, 0.0, 1.0 - gw_);
        gy_ = std::clamp(ny, 0.0, 1.0 - gh_);
        update();

    } else if (resizing_) {
        const double my = (e->pos().y() - fr.y()) / ph;

        double newH = corner_.contains('b') ? (my - gy_)
                                            : ((gy_ + gh_) - my);
        newH = std::max(0.08, newH);

        // Convert the pixel-space aspect lock into normalised space
        const double normRatio = photoRatio_ * ph / pw;
        double newW = newH * normRatio;

        if (newW > 0.98) { newW = 0.98; newH = newW / normRatio; }
        if (newH > 0.98) { newH = 0.98; newW = newH * normRatio; }

        if (corner_.contains('t')) gy_ = (gy_ + gh_) - newH;
        if (corner_.contains('l')) gx_ = (gx_ + gw_) - newW;

        gw_ = newW;
        gh_ = newH;
        gx_ = std::clamp(gx_, 0.0, 1.0 - gw_);
        gy_ = std::clamp(gy_, 0.0, 1.0 - gh_);
        update();

    } else {
        const QString c = hitCorner(e->pos());
        if (c == "tl" || c == "br")      setCursor(Qt::SizeFDiagCursor);
        else if (c == "tr" || c == "bl") setCursor(Qt::SizeBDiagCursor);
        else if (hitGuide(e->pos()))     setCursor(Qt::SizeAllCursor);
        else                              setCursor(Qt::ArrowCursor);
    }
    QLabel::mouseMoveEvent(e);
}

void CameraPreview::mouseReleaseEvent(QMouseEvent* e) {
    dragging_ = false;
    resizing_ = false;
    corner_.clear();
    QLabel::mouseReleaseEvent(e);
}

// ══════════════════════════════════════════════════════
//  Painting
// ══════════════════════════════════════════════════════

void CameraPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor("#1A1A1A"));

    if (frame_.isNull()) {
        p.setPen(QColor(255, 255, 255, 120));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Menghubungkan kamera…"));
        return;
    }

    const QRect fr = frameRect();
    p.drawPixmap(fr.topLeft(), frame_);

    const QRect g = guidePixelRect();

    // ── Dim everything outside the guide ──
    const QColor dim(0, 0, 0, 60);
    p.fillRect(QRect(fr.x(), fr.y(), fr.width(), g.y() - fr.y()), dim);
    p.fillRect(QRect(fr.x(), g.bottom(), fr.width(), fr.bottom() - g.bottom()), dim);
    p.fillRect(QRect(fr.x(), g.y(), g.x() - fr.x(), g.height()), dim);
    p.fillRect(QRect(g.right(), g.y(), fr.right() - g.right(), g.height()), dim);

    // ── Red crop rectangle (outline only) ──
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(220, 40, 40, 210), 2));
    p.drawRect(g);

    // ── Corner handles ──
    p.setPen(QPen(QColor(220, 40, 40, 230), 1));
    p.setBrush(QColor(220, 40, 40, 180));
    for (const QRectF& r : cornerRects())
        p.drawEllipse(r);

    // ── Green head-proportion guides (dashed, no fill) ──
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 200, 0, 140), 1, Qt::DashLine));

    const int cx      = g.x() + g.width() / 2;
    const int headTop = g.y() + static_cast<int>(g.height() * HEAD_TOP_RATIO);
    const int eyeY    = g.y() + static_cast<int>(g.height() * EYE_LINE_RATIO);
    const int chinY   = g.y() + static_cast<int>(g.height() * CHIN_LINE_RATIO);

    p.drawLine(cx, g.y(), cx, g.bottom());
    p.drawLine(g.x(), headTop, g.right(), headTop);
    p.drawLine(g.x(), eyeY,    g.right(), eyeY);
    p.drawLine(g.x(), chinY,   g.right(), chinY);

    // Head oval
    p.setPen(QPen(QColor(0, 200, 0, 100), 1, Qt::DashLine));
    const int ow = static_cast<int>(g.width()  * OVAL_W_RATIO);
    const int oh = static_cast<int>(g.height() * OVAL_H_RATIO);
    const int ox = g.x() + (g.width() - ow) / 2;
    const int oy = g.y() + static_cast<int>(g.height() * OVAL_TOP_RATIO);
    p.drawEllipse(ox, oy, ow, oh);
}
