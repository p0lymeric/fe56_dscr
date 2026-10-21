#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QStyleHints>

// https://forum.qt.io/post/456261
// #include <QProxyStyle>
// class MyProxyStyle : public QProxyStyle {
// public:
//     using QProxyStyle::QProxyStyle;

//     int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override {
//         if (hint == QStyle::SH_ToolTip_WakeUpDelay) {
//             return 0;
//         }

//         return QProxyStyle::styleHint(hint, option, widget, returnData);
//     }
// };

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
    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    // a.setStyle(new MyProxyStyle(a.style()));
    loadFonts();
    QApplication::setFont(QFont("B612", 12));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
