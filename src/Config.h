#pragma once
/*
 * Config.h — JSON config living next to the exe. Editable with Notepad,
 * survives rebuilds, auto-created with defaults on first run.
 */

#include <QString>
#include <vector>
#include "ImageService.h"   // PhotoSize

struct CameraCfg {
    QString index   = "auto";   // "auto" or "0", "1", ...
    QString width   = "auto";   // "auto" or pixels
    QString height  = "auto";
    int     fps     = 30;
    bool    preferEdsdk = true; // try Canon EDSDK before OpenCV
    QString edsdkDll = "";      // explicit EDSDK.dll path, empty = auto-search

    int indexInt()  const;      // -1 when "auto"
    int widthInt()  const;      // -1 when "auto"
    int heightInt() const;      // -1 when "auto"
};

struct ThemeCfg {
    QString background = "#2F6DB3";
    QString primary    = "#165AA7";
    QString accent     = "#FFE84A";
    QString orange     = "#E07B1A";
};

class Config {
public:
    QString  language   = "id";
    int      countdown  = 3;
    int      dpi        = 300;
    double   marginMm   = 1.5;      // gap between photo cells
    double   borderGapMm = 1.5;     // gap between photo and its cut border
    int      paperWmm   = 152;      // 4R landscape
    int      paperHmm   = 102;
    QString  output;                // capture folder
    CameraCfg camera;
    ThemeCfg  theme;
    std::vector<PhotoSize> sizes;

    // Folder containing the exe (or project root when running from a build tree)
    static QString baseDir();
    static QString defaultPath();   // baseDir()/config.json

    void load(const QString& path = {});
    bool save(const QString& path = {}) const;

    const QString& loadedFrom() const { return loadedFrom_; }

private:
    void applyDefaults();
    QString loadedFrom_;
};
