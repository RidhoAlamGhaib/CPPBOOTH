/*
 * main.cpp — entry point.
 * Sets up file logging next to the exe, then shows the booth fullscreen.
 */

#include <QApplication>
#include <QFont>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>
#include <QFontDatabase>
#include <cstdio>

#include "PhotoBoothApp.h"
#include "Config.h"

// ══════════════════════════════════════════════════════
//  Logging — console + rotating daily file
// ══════════════════════════════════════════════════════

static QFile   g_logFile;
static QMutex  g_logMutex;

static void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    const char* level = "INFO ";
    switch (type) {
        case QtDebugMsg:    level = "DEBUG"; break;
        case QtInfoMsg:     level = "INFO "; break;
        case QtWarningMsg:  level = "WARN "; break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg:    level = "FATAL"; break;
    }

    const QString line = QString("%1 %2 %3")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
        .arg(level)
        .arg(msg);

    QMutexLocker lock(&g_logMutex);
    fprintf(stderr, "%s\n", qUtf8Printable(line));
    fflush(stderr);
    if (g_logFile.isOpen()) {
        QTextStream ts(&g_logFile);
        ts << line << '\n';
        ts.flush();
    }
    if (type == QtFatalMsg) abort();
}

static void setupLogging() {
    const QString logDir = QDir(Config::baseDir()).filePath("logs");
    QDir().mkpath(logDir);
    const QString path = QDir(logDir).filePath(
        "photobooth_" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".log");

    g_logFile.setFileName(path);
    g_logFile.open(QIODevice::Append | QIODevice::Text);

    qInstallMessageHandler(messageHandler);

    qInfo() << "==================================================";
    qInfo() << "ID Photo Booth starting";
    qInfo() << "  Base dir:" << Config::baseDir();
    qInfo() << "  Log file:" << path;
    qInfo() << "  Config  :" << Config::defaultPath();
    qInfo() << "==================================================";
}

// ══════════════════════════════════════════════════════
//  main
// ══════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("IDPhotoBooth"));
    app.setOrganizationName(QStringLiteral("PhotoBooth"));

    setupLogging();

    // Segoe UI on Windows, fall back to whatever exists elsewhere
    const QStringList families = QFontDatabase::families();
    const QString family = families.contains(QStringLiteral("Segoe UI"))
                               ? QStringLiteral("Segoe UI")
                               : app.font().family();
    app.setFont(QFont(family, 11));

    PhotoBoothApp window;
    const int rc = app.exec();

    qInfo() << "Exiting with code" << rc;
    if (g_logFile.isOpen()) g_logFile.close();
    return rc;
}
