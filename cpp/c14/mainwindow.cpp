#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFile>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    QObject::connect(this->ui->messageEditor, &QTextEdit::textChanged, this, &MainWindow::onMessageEditorTextChanged);
    QObject::connect(&this->dscrClient, &DscrClient::dscrMessageReceived, this, &MainWindow::onDscrClientDscrMessageReceived);
    QObject::connect(&this->dscrClient, &DscrClient::callSignsChanged, this, &MainWindow::onDscrClientCallSignsChanged);

    QFile dict("C:\\Games\\TMDSDecomp\\save\\DICTIONARY-1.save");
    if (dict.open(QIODevice::ReadOnly)) {
        const QByteArray dict_bytes = dict.readAll();
        this->dictionary = fe56_jsondictionary_new(reinterpret_cast<const uint8_t *>(dict_bytes.constData()), dict_bytes.size());
    } else {
        this->dictionary = fe56_jsondictionary_new(nullptr, 0);
    }

    dscrClient.register_(01444, false);
    dscrClient.open(QUrl("wss://dscr-relay.dixonary.co.uk"));
}

void MainWindow::onMessageEditorTextChanged() {
    const QString text = ui->messageEditor->toPlainText();
    const QByteArray textUtf8 = text.toUtf8();

    // FIXME Fe56 returns results in UTF-8 coordinates but QString is UTF-16...
    Fe56RVec_SymbolizerTrace traces = fe56_mwis_symbolize(this->dictionary, reinterpret_cast<const uint8_t *>(textUtf8.constData()), textUtf8.size());

    QString signalVizText;

    QList<QTextEdit::ExtraSelection> selections;
    bool parity = false;
    for(uintptr_t i = 0; i < traces.size; i++) {
        const Fe56SymbolizerTrace &trace = traces.ptr[i];
        QTextCharFormat fmt;
        switch(trace.kind.tag) {
        case Token:
            fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            fmt.setUnderlineColor(parity ? QColor(0xff, 0x00, 0x00) : QColor(0x00, 0x00, 0xff));
            parity ^= true;
            signalVizText += QString("|%1").arg(trace.kind.token.id);
            break;
        case TokenBig:
            fmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            fmt.setUnderlineColor(QColor(0xff, 0x00, 0x00));
            signalVizText += "|~";
            break;
        case TokenAmbiguousAlt:
            fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            fmt.setUnderlineColor(QColor(0xff, 0xff, 0x00));
            signalVizText += QString("|%1?%2").arg(trace.kind.token_ambiguous_alt.id).arg(trace.kind.token_ambiguous_alt.id_alt);
            break;
        case TokenAmbiguous:
            fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            fmt.setUnderlineColor(QColor(0xff, 0xff, 0x00));
            signalVizText += QString("|%1?").arg(trace.kind.token_ambiguous.id);
            break;
        case Unconsumed:
            fmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            fmt.setUnderlineColor(QColor(0xff, 0x00, 0x00));
            signalVizText += "|!";
            break;
        case Whitespace:
            continue;
        }
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(ui->messageEditor->document());
        sel.cursor.setPosition(trace.span.start);
        sel.cursor.setPosition(trace.span.end, QTextCursor::KeepAnchor);
        sel.format = fmt;
        selections.append(sel);
    }
    fe56_rvec_symbolizertrace_free(traces);
    ui->messageEditor->setExtraSelections(selections);
    ui->signalViz->setText(signalVizText);
}

void MainWindow::onDscrClientDscrMessageReceived(const QDateTime &time, qint64 from, qint64 sequenceNum, const QVector<qint64> &symbols) {
    Fe56RDesymbolized desym = fe56_reformatter_desymbolize(this->dictionary, symbols.constData(), symbols.size());
    QString str = QString::fromUtf8(QByteArrayView(desym.text.ptr, desym.text.size));
    this->ui->chatBrowser->append(QString("<span style=\"color:gray\">%1 [%2]</span> <b>%3</b> %4").arg(time.toString()).arg(sequenceNum).arg(from).arg(str.toHtmlEscaped()));
    fe56_rdesymbolized_free(desym);
}

void MainWindow::onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns) {

}

MainWindow::~MainWindow() {
    fe56_jsondictionary_free(this->dictionary);
    delete ui;
}
