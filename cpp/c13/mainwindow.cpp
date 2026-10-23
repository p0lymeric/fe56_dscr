#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHash>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    this->ui->setupUi(this);
    this->ui->usersList->setModel(&this->usersModel);
    this->ui->usersList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    QObject::connect(this->ui->messageEditor, &QTextEdit::textChanged, this, &MainWindow::onMessageEditorTextChanged);
    QObject::connect(&this->dscrClient, &DscrClient::connected, this, &MainWindow::onDscrClientConnected);
    QObject::connect(&this->dscrClient, &DscrClient::dscrMessageReceived, this, &MainWindow::onDscrClientDscrMessageReceived);
    QObject::connect(&this->dscrClient, &DscrClient::callSignsChanged, this, &MainWindow::onDscrClientCallSignsChanged);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageSent, this, &MainWindow::onDscrClientTextMessageSent);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageReceived, this, &MainWindow::onDscrClientTextMessageReceived);

    this->database.open(QDir(QCoreApplication::applicationDirPath()).filePath("c13.sqlite3"));

    QFile dict("C:\\Games\\TMDSDecomp\\save\\DICTIONARY-1.save");
    if (dict.open(QIODevice::ReadOnly)) {
        QDateTime importTime = QDateTime::currentDateTime();
        QJsonDocument json = QJsonDocument::fromJson(dict.readAll());

        // FIXME replace magic numbers with enum level
        qint64 beforeUserDefaultMode = json["beforeUserDefaultMode"].toInteger(1);
        qint64 afterUserDefaultMode = json["afterUserDefaultMode"].toInteger(2);

        QHash<qint64, QJsonObject> descMap;
        QJsonArray dDK = json["descDict"]["keys"].toArray();
        QJsonArray dDV = json["descDict"]["values"].toArray();
        for(qsizetype i = 0; i < std::min(dDK.size(), dDV.size()); i++) {
            QJsonValue dDKi = dDK[i];
            QJsonValue dDVi = dDV[i];
            if(dDKi.isDouble() && dDVi.isObject()) {
                descMap[dDKi.toInteger()] = dDVi.toObject();
            }
        }

        database.scopedTransaction([&](Database &db) {
            if(!database.queryUpsertSetting("dictionary.beforeUserDefaultMode", beforeUserDefaultMode)) {
                return false;
            }
            if(!database.queryUpsertSetting("dictionary.afterUserDefaultMode", afterUserDefaultMode)) {
                return false;
            }

            QJsonArray wDK = json["wordDict"]["keys"].toArray();
            QJsonArray wDV = json["wordDict"]["values"].toArray();
            for(qsizetype i = 0; i < std::min(wDK.size(), wDV.size()); i++) {
                QJsonValue wDKi = wDK[i];
                QJsonValue wDVi = wDV[i];
                if(wDKi.isDouble() && wDVi.isString()) {
                    qint64 id = wDKi.toInteger();
                    QString word = wDVi.toString();
                    QJsonObject dDKi = descMap[id];;
                    database.queryUpsertDictionary(
                        id,
                        dDKi["desc"].toString("??? ADD NOTES HERE ???"),
                        dDKi["formatMode"].toInteger(beforeUserDefaultMode),
                        dDKi["formatModeAfter"].toInteger(afterUserDefaultMode),
                        dDKi["breakOnDouble"].toBool(false)
                    );
                    database.queryInsertIfDifferentTranslation(id, importTime, word);
                }
            }

            return true;
        });

    } else {
    }

    this->dictionary = fe56_dictionary_new();
    QSqlQuery query(database.getConn());
    // INNER JOIN to exclude dictionary entries without translations
    // LEFT JOIN to include dictionary entries without translations
    // SELECT d.signal, t.word, t.revision, d.descr, d.fmt_before, d.fmt_after, d.fmt_break_on_double
    query.exec(R"(
        SELECT d.signal, t.word, d.fmt_before, d.fmt_after, d.fmt_break_on_double
        FROM user_dictionary AS d
        JOIN user_translations as t
        ON d.signal=t.signal
        AND t.revision=(
            SELECT MAX(revision)
            FROM user_translations
            WHERE signal=d.signal
        )
        WHERE d.archived=0
        ORDER BY d.signal
    )");

    while(query.next()) {
        qint64 id = query.value(0).toLongLong();
        const QByteArray word = query.value(1).toString().toUtf8();
        int fmtBefore = query.value(2).toInt();
        int fmtAfter = query.value(3).toInt();
        int breakOnDouble = query.value(4).toInt();
        Fe56Formatting formatting {
            static_cast<Fe56SpacingKind>(fmtBefore),
            static_cast<Fe56SpacingKind>(fmtAfter),
            breakOnDouble != 0,
        };
        fe56_dictionary_add(this->dictionary, id, reinterpret_cast<const uint8_t*>(word.data()), word.size(), formatting);
    }

    query.finish();

    fe56_dictionary_set_global_format(this->dictionary, Fe56Formatting {
        static_cast<Fe56SpacingKind>(database.queryGetSetting("dictionary.beforeUserDefaultMode").value_or(QVariant(1)).toInt()),
        static_cast<Fe56SpacingKind>(database.queryGetSetting("dictionary.afterUserDefaultMode").value_or(QVariant(2)).toInt()),
        false,
    });

    this->database.queryUpsertSetting("dscrClient.callsign", 01444);
    this->database.queryUpsertSetting("dscrClient.url", "wss://dscr-relay.dixonary.co.uk");

    dscrClient.register_(this->database.queryGetSetting("dscrClient.callsign").value_or(01444).toLongLong(), false);
    dscrClient.open(QUrl(this->database.queryGetSetting("dscrClient.url").value_or("wss://dscr-relay.dixonary.co.uk").toString()));
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
    QStringList users;
    for(qint64 callSign : callSigns) {
        users << QString::number(callSign, 8).rightJustified(4, '0');
    }
    users.sort();
    this->usersModel.setStringList(users);
}

void MainWindow::onDscrClientConnected(const QDateTime &time, const QUrl &url) {
    this->sessionId = std::nullopt;

    this->database.scopedTransaction([&](Database &db) {
        std::optional<qint64> serverId = db.queryGetOrInsertServer(url);
        if(!serverId.has_value()) {
            return false;
        }

        std::optional<qint64> sessionId = db.queryInsertAndGetSession(serverId.value(), time);
        if(!sessionId.has_value()) {
            return false;
        }

        this->sessionId = sessionId.value();

        return true;
    });
}

void MainWindow::onDscrClientTextMessageSent(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
    this->database.scopedTransaction([&](Database &db) {
        db.queryInsertWsMessage(this->sessionId.value(), time, text, 1);
        return true;
    });
}

void MainWindow::onDscrClientTextMessageReceived(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
    this->database.scopedTransaction([&](Database &db) {
        db.queryInsertWsMessage(this->sessionId.value(), time, text, 0);
        return true;
    });
}

MainWindow::~MainWindow() {
    fe56_dictionary_free(this->dictionary);
    delete ui;
}
