#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QMutex>

static QtMessageHandler originalHandler = nullptr;
static QFile logToFileHandle;
static QMutex logToFileLock;

void logToFile(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
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
    // https://doc.qt.io/qt-6/qtlogging.html
    logToFileHandle.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("c13.log"));
    if(logToFileHandle.open(QIODevice::WriteOnly | QIODevice::Text)) {
        originalHandler = qInstallMessageHandler(&logToFile);
    } else {
        qWarning() << "Could not open c13.log for writing. The default log handler will be used.";
    }

    QApplication a(argc, argv);

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

    // Initialize the core
    MainCore c;
    // TODO portable or AppData/XDG_DATA_HOME/Application Support
    if(!c.open(QDir(QCoreApplication::applicationDirPath()).filePath("c13.sqlite3"))) {
        // TODO error dialog ("Could not load or create c13.sqlite3")
        return 1;
    }

    // Initialize and show the main window
    MainWindow w(&c);
    w.show();

    return QApplication::exec();
}
