#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlQuery>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QObject::connect(this->ui->messageEditor, &QTextEdit::textChanged, this, &MainWindow::onMessageEditorTextChanged);
    QObject::connect(&this->dscrClient, &DscrClient::connected, this, &MainWindow::onDscrClientConnected);
    QObject::connect(&this->dscrClient, &DscrClient::dscrMessageReceived, this, &MainWindow::onDscrClientDscrMessageReceived);
    QObject::connect(&this->dscrClient, &DscrClient::callSignsChanged, this, &MainWindow::onDscrClientCallSignsChanged);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageSent, this, &MainWindow::onDscrClientTextMessageSent);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageReceived, this, &MainWindow::onDscrClientTextMessageReceived);

    QFile dict("C:\\Games\\TMDSDecomp\\save\\DICTIONARY-1.save");
    if (dict.open(QIODevice::ReadOnly)) {
        const QByteArray dict_bytes = dict.readAll();
        this->dictionary = fe56_jsondictionary_new(reinterpret_cast<const uint8_t *>(dict_bytes.constData()), dict_bytes.size());
    } else {
        this->dictionary = fe56_jsondictionary_new(nullptr, 0);
    }

    this->database.open(QDir(QCoreApplication::applicationDirPath()).filePath("c13.sqlite3"));

    dscrClient.register_(01444, false);
    dscrClient.open(QUrl("wss://dscr-relay.dixonary.co.uk"));
    ui->chatBrowser->setFont(QFont("B612 Mono", 12));
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
    int avgCharWidth = this->ui->chatBrowser->fontMetrics().averageCharWidth();
    int sequenceWidth = avgCharWidth * (3 + 2);

    QTextBlockFormat headerBlockFormat;
    headerBlockFormat.setTopMargin(6);
    headerBlockFormat.setBottomMargin(0);
    QTextBlockFormat messageBlockFormat;
    messageBlockFormat.setTopMargin(0);
    messageBlockFormat.setLeftMargin(sequenceWidth);

    QTextCharFormat sequenceFormat;
    sequenceFormat.setForeground(Qt::gray);
    sequenceFormat.setToolTip(QLocale().toString(time.toLocalTime(), QLocale::LongFormat));
    QTextCharFormat callsignFormat;
    callsignFormat.setFontWeight(QFont::Bold);
    QTextCharFormat messageFormat;

    QTextCursor cur(this->ui->chatBrowser->document());
    cur.movePosition(QTextCursor::End);

    cur.insertBlock(headerBlockFormat);
    cur.insertText(QString::number(sequenceNum).rightJustified(3, '0'), sequenceFormat);
    cur.insertText("  ");
    cur.insertText(QString::number(from, 8).rightJustified(4, '0'), callsignFormat);

    cur.insertBlock(messageBlockFormat);
    Fe56RDesymbolized desym = fe56_reformatter_desymbolize(this->dictionary, symbols.constData(), symbols.size());
    QString str = QString::fromUtf8(QByteArrayView(desym.text.ptr, desym.text.size));
    cur.insertText(str, messageFormat);
    fe56_rdesymbolized_free(desym);
}

void MainWindow::onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns) {

}

void MainWindow::onDscrClientConnected(const QDateTime &time, const QUrl &url) {
    this->database.scopedTransaction([&](Database &db) {
        this->database.queryGetOrInsertServer->addBindValue(url.host(QUrl::FullyEncoded));
        this->database.queryGetOrInsertServer->addBindValue(url.port(url.scheme() == "wss" ? 443 : 80));
        this->database.queryGetOrInsertServer->exec();
        this->database.queryGetOrInsertServer->next();
        qint64 serverId = this->database.queryGetOrInsertServer->value(0).toLongLong();
        this->database.queryGetOrInsertServer->finish();

        this->database.queryInsertAndGetSession->addBindValue(serverId);
        this->database.queryInsertAndGetSession->addBindValue(time.toMSecsSinceEpoch());
        this->database.queryInsertAndGetSession->exec();
        this->database.queryInsertAndGetSession->next();
        qint64 sessionId = this->database.queryInsertAndGetSession->value(0).toLongLong();
        this->database.queryInsertAndGetSession->finish();

        this->sessionId = sessionId;

        return true;
    });
}

void MainWindow::onDscrClientTextMessageSent(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
    this->database.scopedTransaction([&](Database &db) {
        this->database.queryInsertWsMessage->addBindValue(this->sessionId.value());
        this->database.queryInsertWsMessage->addBindValue(time.toMSecsSinceEpoch());
        this->database.queryInsertWsMessage->addBindValue(1);
        this->database.queryInsertWsMessage->addBindValue(0);
        this->database.queryInsertWsMessage->addBindValue(text);
        this->database.queryInsertWsMessage->exec();
        this->database.queryInsertWsMessage->finish();
        return true;
    });
}

void MainWindow::onDscrClientTextMessageReceived(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
    this->database.scopedTransaction([&](Database &db) {
        this->database.queryInsertWsMessage->addBindValue(this->sessionId.value());
        this->database.queryInsertWsMessage->addBindValue(time.toMSecsSinceEpoch());
        this->database.queryInsertWsMessage->addBindValue(0);
        this->database.queryInsertWsMessage->addBindValue(0);
        this->database.queryInsertWsMessage->addBindValue(text);
        this->database.queryInsertWsMessage->exec();
        this->database.queryInsertWsMessage->finish();
        return true;
    });
}

MainWindow::~MainWindow() {
    fe56_jsondictionary_free(this->dictionary);
    delete ui;
}
