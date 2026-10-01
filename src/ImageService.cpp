#include "ImageService.h"
#include <QImage>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <vector>
#include <algorithm>
#include <cmath>

// ══════════════════════════════════════════════════════
//  Adjustments
// ══════════════════════════════════════════════════════

cv::Mat ImageService::adjust(const cv::Mat& src, int brightness, int contrast,
                             int saturation, int sharpness, int temperature) {
    if (src.empty()) return src;
    cv::Mat out = src.clone();

    // Brightness + contrast
    if (brightness != 0 || contrast != 0) {
        const double alpha = 1.0 + contrast / 100.0;
        const double beta  = brightness * 2.0;
        cv::Mat tmp;
        out.convertTo(tmp, -1, alpha, beta);
        out = tmp;
    }

    // Saturation (HSV)
    if (saturation != 0) {
        cv::Mat hsv;
        cv::cvtColor(out, hsv, cv::COLOR_BGR2HSV);
        std::vector<cv::Mat> ch;
        cv::split(hsv, ch);
        ch[1].convertTo(ch[1], CV_32F);
        ch[1] *= (1.0 + saturation / 100.0);
        cv::threshold(ch[1], ch[1], 255.0, 255.0, cv::THRESH_TRUNC);
        ch[1].convertTo(ch[1], CV_8U);
        cv::merge(ch, hsv);
        cv::cvtColor(hsv, out, cv::COLOR_HSV2BGR);
    }

    // Colour temperature — warm shifts R up / B down, cool the reverse
    if (temperature != 0) {
        const double t = std::min(40.0, std::abs(temperature) * 1.5);
        std::vector<cv::Mat> bgr;
        cv::split(out, bgr);
        if (temperature > 0) {
            cv::add(bgr[2], cv::Scalar(t), bgr[2]);
            cv::subtract(bgr[0], cv::Scalar(t), bgr[0]);
        } else {
            cv::add(bgr[0], cv::Scalar(t), bgr[0]);
            cv::subtract(bgr[2], cv::Scalar(t), bgr[2]);
        }
        cv::merge(bgr, out);
    }

    // Unsharp mask
    if (sharpness > 0) {
        const double amount = sharpness / 50.0;   // 0..1
        cv::Mat blurred;
        cv::GaussianBlur(out, blurred, cv::Size(0, 0), 1.6);
        cv::Mat sharp;
        cv::addWeighted(out, 1.0 + amount, blurred, -amount, 0, sharp);
        out = sharp;
    }

    return out;
}

// ══════════════════════════════════════════════════════
//  Transforms
// ══════════════════════════════════════════════════════

cv::Mat ImageService::rotate(const cv::Mat& src, int degrees) {
    if (src.empty()) return src;
    cv::Mat out;
    switch (((degrees % 360) + 360) % 360) {
        case 90:  cv::rotate(src, out, cv::ROTATE_90_CLOCKWISE);        return out;
        case 180: cv::rotate(src, out, cv::ROTATE_180);                 return out;
        case 270: cv::rotate(src, out, cv::ROTATE_90_COUNTERCLOCKWISE); return out;
        default:  return src.clone();
    }
}

cv::Mat ImageService::flipH(const cv::Mat& src) {
    if (src.empty()) return src;
    cv::Mat out; cv::flip(src, out, 1); return out;
}

cv::Mat ImageService::flipV(const cv::Mat& src) {
    if (src.empty()) return src;
    cv::Mat out; cv::flip(src, out, 0); return out;
}

cv::Mat ImageService::cropRect(const cv::Mat& src, const cv::Rect& r) {
    if (src.empty()) return src;
    const cv::Rect safe = r & cv::Rect(0, 0, src.cols, src.rows);
    if (safe.width <= 0 || safe.height <= 0) return src.clone();
    return src(safe).clone();
}

cv::Mat ImageService::cropToAspect(const cv::Mat& src, int outW, int outH) {
    if (src.empty() || outW <= 0 || outH <= 0) return src;

    const double srcRatio = static_cast<double>(src.cols) / src.rows;
    const double tgtRatio = static_cast<double>(outW) / outH;

    cv::Rect roi;
    if (srcRatio > tgtRatio) {
        const int w = static_cast<int>(std::lround(src.rows * tgtRatio));
        roi = cv::Rect((src.cols - w) / 2, 0, w, src.rows);
    } else {
        const int h = static_cast<int>(std::lround(src.cols / tgtRatio));
        roi = cv::Rect(0, (src.rows - h) / 2, src.cols, h);
    }
    roi &= cv::Rect(0, 0, src.cols, src.rows);

    cv::Mat out;
    cv::resize(src(roi), out, cv::Size(outW, outH), 0, 0, cv::INTER_LANCZOS4);
    return out;
}

// ══════════════════════════════════════════════════════
//  Background replacement
// ══════════════════════════════════════════════════════

cv::Mat ImageService::changeBackground(const cv::Mat& src, const QString& color) {
    if (src.empty()) return src;
    if (color.isEmpty() || color.compare("original", Qt::CaseInsensitive) == 0)
        return src.clone();

    cv::Scalar fill(255, 255, 255);
    if (color.compare("blue", Qt::CaseInsensitive) == 0) fill = cv::Scalar(180, 130, 70);
    else if (color.compare("red", Qt::CaseInsensitive) == 0) fill = cv::Scalar(60, 60, 200);

    const int mx = static_cast<int>(src.cols * 0.05);
    const int my = static_cast<int>(src.rows * 0.05);
    const cv::Rect rect(mx, my, src.cols - 2 * mx, src.rows - 2 * my);
    if (rect.width <= 1 || rect.height <= 1) return src.clone();

    cv::Mat mask, bgdModel, fgdModel;
    try {
        cv::grabCut(src, mask, rect, bgdModel, fgdModel, 5, cv::GC_INIT_WITH_RECT);
    } catch (const cv::Exception&) {
        return src.clone();
    }

    cv::Mat fgMask = (mask == cv::GC_FGD) | (mask == cv::GC_PR_FGD);
    cv::Mat out(src.size(), src.type(), fill);
    src.copyTo(out, fgMask);
    return out;
}

// ══════════════════════════════════════════════════════
//  Print layout (4R) with per-photo dashed cut guides
// ══════════════════════════════════════════════════════

namespace {

void dashedH(cv::Mat& canvas, int y, int x1, int x2,
             const cv::Scalar& color, int thick, int dash, int gap) {
    if (y < 0 || y >= canvas.rows) return;
    for (int x = x1; x < x2; x += dash + gap) {
        const int xe = std::min(x + dash, x2);
        cv::line(canvas, {x, y}, {xe, y}, color, thick, cv::LINE_8);
    }
}

void dashedV(cv::Mat& canvas, int x, int y1, int y2,
             const cv::Scalar& color, int thick, int dash, int gap) {
    if (x < 0 || x >= canvas.cols) return;
    for (int y = y1; y < y2; y += dash + gap) {
        const int ye = std::min(y + dash, y2);
        cv::line(canvas, {x, y}, {x, ye}, color, thick, cv::LINE_8);
    }
}

} // namespace

cv::Mat ImageService::generatePrintLayout(const cv::Mat& photo, const PhotoSize& size,
                                          int paperWmm, int paperHmm, int dpi,
                                          double marginMm, double borderGapMm) {
    if (photo.empty() || size.w_mm <= 0 || size.h_mm <= 0) return {};

    const double pxPerMm = dpi / 25.4;

    const int pw      = static_cast<int>(std::lround(size.w_mm  * pxPerMm));
    const int ph      = static_cast<int>(std::lround(size.h_mm  * pxPerMm));
    const int paperW  = static_cast<int>(std::lround(paperWmm   * pxPerMm));
    const int paperH  = static_cast<int>(std::lround(paperHmm   * pxPerMm));
    const int margin  = std::max(0, static_cast<int>(std::lround(marginMm * pxPerMm)));
    const int pad     = std::max(1, static_cast<int>(std::lround(borderGapMm * pxPerMm)));

    if (pw <= 0 || ph <= 0 || paperW <= 0 || paperH <= 0) return {};

    // One photo occupies photo + padding on all four sides
    const int cellW = pw + 2 * pad;
    const int cellH = ph + 2 * pad;

    const int cols = std::max(1, (paperW + margin) / (cellW + margin));
    const int rows = std::max(1, (paperH + margin) / (cellH + margin));

    const int gridW = cols * cellW + (cols - 1) * margin;
    const int gridH = rows * cellH + (rows - 1) * margin;
    const int xStart = (paperW - gridW) / 2;
    const int yStart = (paperH - gridH) / 2;

    cv::Mat resized = cropToAspect(photo, pw, ph);
    cv::Mat canvas(paperH, paperW, CV_8UC3, cv::Scalar(255, 255, 255));

    // Place photos, centred inside their padded cells
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int cellX = xStart + c * (cellW + margin);
            const int cellY = yStart + r * (cellH + margin);
            const cv::Rect dst(cellX + pad, cellY + pad, pw, ph);
            if (dst.x < 0 || dst.y < 0 ||
                dst.x + dst.width > paperW || dst.y + dst.height > paperH) continue;
            resized.copyTo(canvas(dst));
        }
    }

    // Dashed cut-guide rectangle around each photo, offset outward by `pad`
    const cv::Scalar guide(180, 180, 180);
    const int thick = std::max(1, static_cast<int>(std::lround(0.3 * pxPerMm)));
    const int dash  = std::max(6, static_cast<int>(std::lround(1.2 * pxPerMm)));
    const int gap   = std::max(4, static_cast<int>(std::lround(0.8 * pxPerMm)));

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int x1 = xStart + c * (cellW + margin);
            const int y1 = yStart + r * (cellH + margin);
            const int x2 = x1 + cellW;
            const int y2 = y1 + cellH;
            if (x1 < 0 || y1 < 0 || x2 > paperW || y2 > paperH) continue;
            dashedH(canvas, y1, x1, x2, guide, thick, dash, gap);
            dashedH(canvas, y2 - 1, x1, x2, guide, thick, dash, gap);
            dashedV(canvas, x1, y1, y2, guide, thick, dash, gap);
            dashedV(canvas, x2 - 1, y1, y2, guide, thick, dash, gap);
        }
    }

    return canvas;
}

int ImageService::countPhotosOnLayout(const PhotoSize& size, int paperWmm, int paperHmm,
                                      double marginMm, double borderGapMm) {
    if (size.w_mm <= 0 || size.h_mm <= 0) return 0;
    const double cellW = size.w_mm + 2 * borderGapMm;
    const double cellH = size.h_mm + 2 * borderGapMm;
    const int cols = std::max(1, static_cast<int>((paperWmm + marginMm) / (cellW + marginMm)));
    const int rows = std::max(1, static_cast<int>((paperHmm + marginMm) / (cellH + marginMm)));
    return cols * rows;
}

// ══════════════════════════════════════════════════════
//  I/O + Qt interop
// ══════════════════════════════════════════════════════

bool ImageService::save(const cv::Mat& img, const QString& path, int jpegQuality) {
    if (img.empty()) return false;
    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString ext = QFileInfo(path).suffix().toLower();
    std::vector<int> params;
    if (ext == "jpg" || ext == "jpeg") {
        params = { cv::IMWRITE_JPEG_QUALITY, jpegQuality };
    } else if (ext == "png") {
        params = { cv::IMWRITE_PNG_COMPRESSION, 3 };
    }
    return cv::imwrite(path.toLocal8Bit().constData(), img, params);
}

QImage ImageService::toQImage(const cv::Mat& bgr) {
    if (bgr.empty()) return {};
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

QPixmap ImageService::toPixmap(const cv::Mat& bgr, const QSize& fit) {
    const QImage img = toQImage(bgr);
    if (img.isNull()) return {};
    QPixmap pm = QPixmap::fromImage(img);
    if (fit.isValid() && !fit.isEmpty())
        pm = pm.scaled(fit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return pm;
}
