#include "Config.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

// ══════════════════════════════════════════════════════
//  CameraCfg helpers
// ══════════════════════════════════════════════════════

static int toIntOrAuto(const QString& s) {
    if (s.compare("auto", Qt::CaseInsensitive) == 0 || s.isEmpty()) return -1;
    bool ok = false;
    const int v = s.toInt(&ok);
    return ok ? v : -1;
}

int CameraCfg::indexInt()  const { return toIntOrAuto(index);  }
int CameraCfg::widthInt()  const { return toIntOrAuto(width);  }
int CameraCfg::heightInt() const { return toIntOrAuto(height); }

// ══════════════════════════════════════════════════════
//  Paths
// ══════════════════════════════════════════════════════

QString Config::baseDir() {
    const QString exeDir = QCoreApplication::applicationDirPath();

    // Running from a CMake build tree? Walk up to the folder holding CMakeLists.txt
    QDir d(exeDir);
    for (int i = 0; i < 4; ++i) {
        if (QFileInfo::exists(d.filePath("CMakeLists.txt")))
            return d.absolutePath();
        if (!d.cdUp()) break;
    }
    return exeDir;
}

QString Config::defaultPath() {
    return QDir(baseDir()).filePath("config.json");
}

// ══════════════════════════════════════════════════════
//  Defaults
// ══════════════════════════════════════════════════════

void Config::applyDefaults() {
    if (sizes.empty()) {
        sizes = {
            { "2x3cm",      20, 30 },
            { "3x4cm",      30, 40 },
            { "3.3x4.8cm",  33, 48 },
            { "3.5x4.5cm",  35, 45 },
            { "4x6cm",      40, 60 },
            { "4.5x4.5cm",  45, 45 },
            { "5x5cm",      50, 50 },
            { "Passport",   35, 45 },
            { "Visa",       50, 50 },
        };
    }
    if (output.isEmpty())
        output = QDir(baseDir()).filePath("captures");
}

// ══════════════════════════════════════════════════════
//  Load
// ══════════════════════════════════════════════════════

void Config::load(const QString& pathIn) {
    const QString path = pathIn.isEmpty() ? defaultPath() : pathIn;
    loadedFrom_ = path;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo() << "[CFG] no config at" << path << "— writing defaults";
        applyDefaults();
        QDir().mkpath(output);
        save(path);
        return;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[CFG] parse error:" << err.errorString() << "— using defaults";
        applyDefaults();
        QDir().mkpath(output);
        return;
    }

    const QJsonObject o = doc.object();

    language    = o.value("language").toString(language);
    countdown   = o.value("countdown").toInt(countdown);
    dpi         = o.value("dpi").toInt(dpi);
    marginMm    = o.value("margin_mm").toDouble(marginMm);
    borderGapMm = o.value("border_gap_mm").toDouble(borderGapMm);
    paperWmm    = o.value("paper_w_mm").toInt(paperWmm);
    paperHmm    = o.value("paper_h_mm").toInt(paperHmm);
    output      = o.value("output").toString(output);

    if (o.contains("camera") && o.value("camera").isObject()) {
        const QJsonObject c = o.value("camera").toObject();
        // index may be a string ("auto") or a number
        const QJsonValue idx = c.value("index");
        camera.index  = idx.isDouble() ? QString::number(idx.toInt()) : idx.toString(camera.index);
        const QJsonValue w = c.value("width");
        camera.width  = w.isDouble() ? QString::number(w.toInt()) : w.toString(camera.width);
        const QJsonValue h = c.value("height");
        camera.height = h.isDouble() ? QString::number(h.toInt()) : h.toString(camera.height);
        camera.fps         = c.value("fps").toInt(camera.fps);
        camera.preferEdsdk = c.value("prefer_edsdk").toBool(camera.preferEdsdk);
        camera.edsdkDll    = c.value("edsdk_dll").toString(camera.edsdkDll);
    }

    if (o.contains("theme") && o.value("theme").isObject()) {
        const QJsonObject t = o.value("theme").toObject();
        theme.background = t.value("background").toString(theme.background);
        theme.primary    = t.value("primary").toString(theme.primary);
        theme.accent     = t.value("accent").toString(theme.accent);
        theme.orange     = t.value("orange").toString(theme.orange);
    }

    sizes.clear();
    for (const QJsonValue& v : o.value("photo_sizes").toArray()) {
        const QJsonObject s = v.toObject();
        PhotoSize ps;
        ps.name = s.value("name").toString();
        ps.w_mm = s.value("w_mm").toInt();
        ps.h_mm = s.value("h_mm").toInt();
        if (!ps.name.isEmpty() && ps.w_mm > 0 && ps.h_mm > 0)
            sizes.push_back(ps);
    }

    applyDefaults();
    QDir().mkpath(output);
    qInfo() << "[CFG] loaded" << path << "-" << int(sizes.size()) << "sizes";
}

// ══════════════════════════════════════════════════════
//  Save
// ══════════════════════════════════════════════════════

bool Config::save(const QString& pathIn) const {
    const QString path = pathIn.isEmpty()
                             ? (loadedFrom_.isEmpty() ? defaultPath() : loadedFrom_)
                             : pathIn;

    QJsonObject o;
    o["language"]      = language;
    o["countdown"]     = countdown;
    o["dpi"]           = dpi;
    o["margin_mm"]     = marginMm;
    o["border_gap_mm"] = borderGapMm;
    o["paper_w_mm"]    = paperWmm;
    o["paper_h_mm"]    = paperHmm;
    o["output"]        = output;

    QJsonObject c;
    c["index"]        = camera.index;
    c["width"]        = camera.width;
    c["height"]       = camera.height;
    c["fps"]          = camera.fps;
    c["prefer_edsdk"] = camera.preferEdsdk;
    c["edsdk_dll"]    = camera.edsdkDll;
    o["camera"] = c;

    QJsonObject t;
    t["background"] = theme.background;
    t["primary"]    = theme.primary;
    t["accent"]     = theme.accent;
    t["orange"]     = theme.orange;
    o["theme"] = t;

    QJsonArray arr;
    for (const PhotoSize& s : sizes) {
        QJsonObject so;
        so["name"] = s.name;
        so["w_mm"] = s.w_mm;
        so["h_mm"] = s.h_mm;
        arr.append(so);
    }
    o["photo_sizes"] = arr;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[CFG] cannot write" << path;
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}
