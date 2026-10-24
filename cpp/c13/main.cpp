#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QStyleHints>

void loadFonts() {
    // sync this with assets.qrc
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
    QApplication::setStyle("fusion");
    a.setStyleSheet(R"(
        QWidget { background: #001405; color: #0CED45; selection-background-color: #0CED45; selection-color: #001405; border-radius: 6px; }
        QTextEdit, QTextBrowser, QListView, QLineEdit { border: 1px solid #086921; }
        QScrollBar { background: #001405; }
        QScrollBar::handle { background: #086921; border-radius: 3px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QSplitter::handle { background: #086921; }
        QSplitter::handle:hover { background: #0DA635; }
        QSplitter::handle:pressed { background: #0CED45; }
    )");
    loadFonts();
    QApplication::setFont(QFont("B612 Mono", 12));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
