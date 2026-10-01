#pragma once
/*
 * CameraPreview.h — live view widget with a draggable / resizable crop guide.
 *
 * The guide is stored in NORMALISED coordinates (0..1) relative to the
 * displayed frame, so it survives window resizes and resolution changes.
 * Aspect ratio is locked to the selected photo size.
 *
 * The preview image is mirrored, so cropRectOnFrame() mirrors X back when
 * mapping the guide onto the raw (un-mirrored) camera frame.
 */

#include <QLabel>
#include <QPixmap>
#include <QMap>
#include <QRectF>
#include <QPointF>
#include <opencv2/core.hpp>

class CameraPreview : public QLabel {
    Q_OBJECT
public:
    explicit CameraPreview(QWidget* parent = nullptr);

    // Lock the guide to a photo aspect (w_mm : h_mm). Recentres the guide.
    void setPhotoRatio(int w_mm, int h_mm);

    // Feed a new (already mirrored) frame.
    void updateFrame(const QImage& mirrored);

    // Force the guide back to its default centred position/size.
    void resetGuide() { guideInit_ = false; update(); }

    // Map the guide onto raw camera-frame pixels (un-mirrors X).
    cv::Rect cropRectOnFrame(int frameW, int frameH) const;

    // Head-proportion ratios (ISO/ICAO-ish), fractions of guide height.
    static constexpr double HEAD_TOP_RATIO  = 0.08;
    static constexpr double EYE_LINE_RATIO  = 0.46;
    static constexpr double CHIN_LINE_RATIO = 0.82;
    static constexpr double OVAL_W_RATIO    = 0.62;
    static constexpr double OVAL_TOP_RATIO  = 0.08;
    static constexpr double OVAL_H_RATIO    = 0.60;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void  initGuide(int pixW, int pixH);
    QRect frameRect() const;        // where the pixmap sits inside the widget
    QRect guidePixelRect() const;   // guide in widget pixels
    QMap<QString, QRectF> cornerRects() const;
    QString hitCorner(const QPoint& p) const;
    bool    hitGuide(const QPoint& p) const;

    QPixmap frame_;
    double  photoRatio_ = 0.75;     // w / h

    // Guide, normalised to the displayed frame
    double gx_ = 0.0, gy_ = 0.0, gw_ = 0.0, gh_ = 0.0;
    bool   guideInit_ = false;

    // Interaction
    bool    dragging_ = false;
    bool    resizing_ = false;
    QString corner_;
    QPointF dragOffset_;

    static constexpr int HANDLE = 8;
};
