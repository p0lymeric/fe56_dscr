#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QLockFile>
#include <QMessageBox>
#include <QMutex>
#include <QOpenGLContext>
#include <QSurfaceFormat>

static QtMessageHandler originalHandler = nullptr;
static QFile logToFileHandle;
static QMutex logToFileLock;

static void logToFile(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    // https://doc.qt.io/qt-6/qtlogging.html
    QString message = qFormatLogMessage(type, context, msg);
    logToFileLock.lock();
    logToFileHandle.write(qPrintable(message));
    logToFileHandle.write("\n");
    logToFileHandle.flush();
    logToFileLock.unlock();

    if (originalHandler) {
        originalHandler(type, context, msg);
    }
}

static void loadQrcFonts() {
    // In theory these loads aren't fallible as these fonts are packed into the executable
    // Sync this list with assets.qrc
    QFontDatabase::addApplicationFont(":/fonts/B612/B612-Bold.ttf");
    QFontDatabase::addApplicationFont(":/fonts/B612/B612-BoldItalic.ttf");
    QFontDatabase::addApplicationFont(":/fonts/B612/B612-Italic.ttf");
    QFontDatabase::addApplicationFont(":/fonts/B612/B612-Regular.ttf");

    QFontDatabase::addApplicationFont(":/fonts/B612_Mono/B612Mono-Bold.ttf");
    QFontDatabase::addApplicationFont(":/fonts/B612_Mono/B612Mono-BoldItalic.ttf");
    QFontDatabase::addApplicationFont(":/fonts/B612_Mono/B612Mono-Italic.ttf");
    QFontDatabase::addApplicationFont(":/fonts/B612_Mono/B612Mono-Regular.ttf");
}

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    // TODO portable or AppData/XDG_DATA_HOME/Application Support
    QDir appDataRoot(QCoreApplication::applicationDirPath());
    QString filePrefix = "c13";

    // Perform initial styling
    QApplication::setStyle("fusion");
    a.setStyleSheet(R"(
        QWidget { background: #001405; color: #0CED45; selection-background-color: #0CED45; selection-color: #001405; border-radius: 6px; }
        QTextEdit, QTextBrowser, QListView, QLineEdit { border: 1px solid #086921; }
        QScrollBar { background: #001405; }
        QScrollBar::handle { background: #086921; border-radius: 3px; min-height: 20px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QSplitter::handle { background: #086921; }
        QSplitter::handle:hover { background: #0DA635; }
        QSplitter::handle:pressed { background: #0CED45; }
    )");
    loadQrcFonts();
    QApplication::setFont(QFont("B612 Mono", 12));

    // c13.lock protects against concurrent app launches mutually clobbering c13.log and c13.sqlite3
    // This could be generalized in the future if the app were to receive a file prefix argument
    QLockFile processLock(appDataRoot.filePath(QString("%1.lock").arg(filePrefix)));
    if(!processLock.tryLock()) {
        qCritical() << "Could not acquire c13.lock. Maybe another instance of C13 is already running?";
        QMessageBox::critical(nullptr, "C13", "Could not acquire c13.lock. Maybe another instance of C13 is already running?");
        return 1;
    }

    // Try to open a log file
    logToFileHandle.setFileName(appDataRoot.filePath(QString("%1.log").arg(filePrefix)));
    if(logToFileHandle.open(QIODevice::WriteOnly | QIODevice::Text)) {
        originalHandler = qInstallMessageHandler(&logToFile);
    } else {
        qWarning() << "Could not open c13.log for writing. The default log handler will be used.";
    }

    // Initialize the core
    MainCore c;
    if(!c.open(appDataRoot.filePath(QString("%1.sqlite3").arg(filePrefix)))) {
        // TODO give more context around the error (newer schema, error preparing statements, etc.)
        qCritical() << "Could not load or create c13.sqlite3. Maybe the database is malformed or targets a newer version?";
        QMessageBox::critical(nullptr, "C13", "Could not load or create c13.sqlite3. Maybe the database is malformed or targets a newer version?");
        return 1;
    }

    // Set global OpenGL parameters
    // https://doc.qt.io/qt-6/qopenglwidget.html
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    // Initialize and show the main window
    MainWindow w(&c);
    w.show();

    return QApplication::exec();
}
