#pragma once
/*
 * ImageService.h — stateless image operations.
 * Adjustments, transforms, background replacement, 4R print layout with cut guides.
 */

#include <opencv2/opencv.hpp>
#include <QString>
#include <QImage>
#include <QPixmap>
#include <QSize>

struct PhotoSize {
    QString name;
    int w_mm = 30;
    int h_mm = 40;
    double ratio() const { return h_mm > 0 ? static_cast<double>(w_mm) / h_mm : 0.75; }
};

class ImageService {
public:
    // ── Adjustments ──
    // brightness/contrast/saturation/temperature: -50..+50, sharpness: 0..50
    static cv::Mat adjust(const cv::Mat& src,
                          int brightness = 0,
                          int contrast   = 0,
                          int saturation = 0,
                          int sharpness  = 0,
                          int temperature = 0);

    // ── Transforms ──
    static cv::Mat rotate(const cv::Mat& src, int degrees);   // 90/180/270
    static cv::Mat flipH(const cv::Mat& src);
    static cv::Mat flipV(const cv::Mat& src);
    static cv::Mat cropRect(const cv::Mat& src, const cv::Rect& r);

    // Centre-crop to a target aspect then resize to exactly w×h.
    static cv::Mat cropToAspect(const cv::Mat& src, int outW, int outH);

    // ── Background replacement (GrabCut). color: "white"|"blue"|"red" ──
    static cv::Mat changeBackground(const cv::Mat& src, const QString& color);

    // ── 4R print layout ──
    // Tiles `photo` onto paper, draws a dashed cut-guide rectangle around each
    // photo offset outward by borderGapMm, and centres the whole grid.
    static cv::Mat generatePrintLayout(const cv::Mat& photo,
                                       const PhotoSize& size,
                                       int paperWmm = 152,      // 4R = 6 in
                                       int paperHmm = 102,      // 4R = 4 in
                                       int dpi = 300,
                                       double marginMm = 1.5,     // gap between cells
                                       double borderGapMm = 1.5); // photo → its border

    static int countPhotosOnLayout(const PhotoSize& size,
                                   int paperWmm = 152,
                                   int paperHmm = 102,
                                   double marginMm = 1.5,
                                   double borderGapMm = 1.5);

    // ── I/O ──
    static bool save(const cv::Mat& img, const QString& path, int jpegQuality = 95);

    // Write a single-page PDF sized to the paper (mm), with `img` scaled to fill.
    static bool savePdf(const cv::Mat& img, const QString& path,
                        int paperWmm = 152, int paperHmm = 102, int dpi = 300);

    // ── Qt interop ──
    static QImage  toQImage(const cv::Mat& bgr);
    static QPixmap toPixmap(const cv::Mat& bgr, const QSize& fit = {});
};
