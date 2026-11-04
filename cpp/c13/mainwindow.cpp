#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "dscrtypes.h"

#include <variant>
#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHash>
#include <QSqlQuery>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>

enum UserTextFormatProperty {
    TimeQDateTime = QTextFormat::Property::UserProperty,
    SignalIdqint64,
    OrbPlot,
};

MainWindow::MainWindow(MainCore *core, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    core(core)
{
    this->ui->setupUi(this);

    QObject::connect(this->ui->messageEditor, &QTextEdit::textChanged, this, &MainWindow::onMessageEditorTextChanged);
    QObject::connect(&this->core->transmissionProcessor, &TransmissionProcessor::callSignsChanged, this, &MainWindow::onDscrClientCallSignsChanged);

    QObject::connect(&this->chatModelUnenc, &ChatModel::appendChatMessage, this, [this](const DscrTransmissionDbLinked &trns) {
        this->pushToChatBrowser(trns, this->ui->chatBrowser);
    });
    QObject::connect(&this->chatModelAll, &ChatModel::appendChatMessage, this, [this](const DscrTransmissionDbLinked &trns) {
        this->pushToChatBrowser(trns, this->ui->chatBrowser2);
    });

    this->ui->usersList->setModel(&this->usersModel);
    this->ui->usersList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    this->dictionaryModel.init(&this->core->dictionary); // run before setting dictionaryTable model
    this->ui->dictionaryTable->setModel(&this->dictionaryModel);
    this->ui->dictionaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->ui->dictionaryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    this->ui->dictionaryTable->setEditTriggers(QAbstractItemView::DoubleClicked);
    this->ui->dictionaryTable->verticalHeader()->hide();
    this->ui->dictionaryTable->horizontalHeader()->setStretchLastSection(true);

    // TODO per chat browser instance
    this->ui->chatBrowser->viewport()->setMouseTracking(true);
    this->ui->chatBrowser->viewport()->installEventFilter(this);
    this->ui->chatBrowser2->viewport()->setMouseTracking(true);
    this->ui->chatBrowser2->viewport()->installEventFilter(this);

    this->ui->messageEditor->viewport()->setMouseTracking(true);
    this->ui->messageEditor->viewport()->installEventFilter(this);
    this->ui->messageEditor->installEventFilter(this);
    this->instantMouseoverToolTip.setWindowFlags(Qt::ToolTip);
    this->instantMouseoverToolTip.setAttribute(Qt::WA_TransparentForMouseEvents);
    this->instantGlorp.setWindowFlags(Qt::ToolTip);
    this->instantGlorp.setAttribute(Qt::WA_TransparentForMouseEvents);
    this->instantGlorp.setFixedSize(100, 100);

    // Show in dictionary or populate add box on clicking hyperlink
    QObject::connect(this->ui->addDictionaryButton, &QPushButton::clicked, this, [&](bool) {
        bool id_ok = false;
        qint64 id = this->ui->addDictionaryId->text().toLongLong(&id_ok);
        if(id_ok && id < 0) {
            this->core->dictionary.add(id);
            auto s = this->ui->dictionaryTable->model()->match(this->ui->dictionaryTable->model()->index(0, 0), Qt::DisplayRole, id, 1, Qt::MatchExactly);
            if(!s.isEmpty()) {
                this->ui->dictionaryTable->selectionModel()->setCurrentIndex(s.first(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                this->ui->dictionaryTable->scrollTo(s.first(), QAbstractItemView::PositionAtCenter);
            }
        }
    });

    auto lambdaRetranslate = [&](QTextBrowser *browser) {
        emit this->onMessageEditorTextChanged();
        // we will want to eventually drive the chat browser with a message model
        QScrollBar *vBar = browser->verticalScrollBar();
        int vBarOldPosition = vBar->value();
        bool vBarOldAtBottom = vBarOldPosition == vBar->maximum();
        // QElapsedTimer stopwatch;
        // stopwatch.start();
        QTextCursor cur(browser->document());
        cur.beginEditBlock();
        // browser->clear();
        // qDebug() << "clear time" << stopwatch.durationElapsed();
        // stopwatch.start();
        for(auto &chatMsg : this->chatModelUnenc.getChatMessages()) {
            this->pushToChatBrowser(chatMsg, browser);
        }
        cur.endEditBlock();
        // qDebug() << "retranslate time" << stopwatch.durationElapsed();
        if(vBarOldAtBottom || vBarOldPosition > vBar->maximum()) {
            vBar->setValue(vBar->maximum());
        } else {
            vBar->setValue(vBarOldPosition);
        }
    };

    QObject::connect(&this->chatModelUnenc, &ChatModel::retranslateAll, this, [=]() {
        lambdaRetranslate(this->ui->chatBrowser);
    });
    QObject::connect(&this->chatModelAll, &ChatModel::retranslateAll, this, [=]() {
        lambdaRetranslate(this->ui->chatBrowser2);
    });
    // temp to force a retranslate
    // QObject::connect(this->ui->connectionPill, &QPushButton::clicked, this, [=](bool) {
    //     lambdaRetranslate(this->ui->chatBrowser);
    //     lambdaRetranslate(this->ui->chatBrowser2);
    // });

    this->chatModelUnenc.init(&this->core->transmissionProcessor, &this->core->dictionary, EncKeySelect { true, false, false, 0 });
    this->chatModelAll.init(&this->core->transmissionProcessor, &this->core->dictionary, EncKeySelect { true, true, false, 0 });

    this->core->dscrClient.register_(this->core->settings.get("dscrClient.callsign", 01445).toLongLong(), false);
    this->core->dscrClient.open(QUrl(this->core->settings.get("dscrClient.url", "wss://dscr-relay.dixonary.co.uk").toString()), true);
}

MainWindow::~MainWindow() {
    delete ui;
}

bool MainWindow::eventFilterChatBrowser(QTextBrowser *browser, QEvent *event) {
    switch(event->type()) {
        case QEvent::MouseMove: {
            QMouseEvent *eventMouse = static_cast<QMouseEvent *>(event);
            QPointF documentPosition = eventMouse->pos() + QPointF(browser->horizontalScrollBar()->value(), browser->verticalScrollBar()->value());
            int mousedOverIdx = browser->document()->documentLayout()->hitTest(documentPosition, Qt::HitTestAccuracy::ExactHit);
            if(mousedOverIdx >= 0) {
                QTextCursor cur(browser->document());
                cur.setPosition(mousedOverIdx);
                // need an inclusive selection otherwise hit detection will be offset leftwards
                cur.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                // qDebug() << cur.charFormat();
                QTextCharFormat format = cur.charFormat();
                QVariant timeProp = format.property(UserTextFormatProperty::TimeQDateTime);
                QVariant idProp = format.property(UserTextFormatProperty::SignalIdqint64);
                QVariant plotProp = format.property(UserTextFormatProperty::OrbPlot);
                if(timeProp.isValid()) {
                    // qDebug() << timeProp.toDateTime();
                    this->instantMouseoverToolTip.setText(timeProp.toDateTime().toString("yyyy-MM-dd HH:mm:ss t"));
                    this->instantMouseoverToolTip.adjustSize();
                    this->instantMouseoverToolTip.move(eventMouse->globalPosition().toPoint() + QPoint(15, 15));
                    this->instantMouseoverToolTip.show();
                } else if(idProp.isValid()) {
                    // qDebug() << idProp.toLongLong();
                    this->instantMouseoverToolTip.setText(QString::number(idProp.toLongLong()));
                    this->instantMouseoverToolTip.adjustSize();
                    this->instantMouseoverToolTip.move(eventMouse->globalPosition().toPoint() + QPoint(15, 15));
                    this->instantMouseoverToolTip.show();
                } else if(plotProp.isValid()) {
                    browser->viewport()->setCursor(Qt::PointingHandCursor);
                    this->instantGlorp.load53(plotProp.value<DscrSpecialPlot>());
                    // FIXME forces a redraw every mouseover, should probably perform a value-based dirty check in load53 and define updateIfDirty
                    this->instantGlorp.update();
                    this->instantGlorp.move(eventMouse->globalPosition().toPoint() + QPoint(15, 15));
                    this->instantGlorp.show();
                } else {
                    // FIXME this is slightly incorrect if the cursor were to cross between showing instantMouseoverToolTip and instantGlorp
                    // we would not hide the other in that case
                    this->instantMouseoverToolTip.hide();
                    browser->viewport()->unsetCursor();
                    this->instantGlorp.hide();
                }
            } else {
                // TODO may be edge cases where we don't see a MouseMove but still want to close the tooltip
                this->instantMouseoverToolTip.hide();
                // TODO not all cases where this should be called are covered but the cursor appears to be unset reliably in reality
                browser->viewport()->unsetCursor();
                this->instantGlorp.hide();
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            QMouseEvent *eventMouse = static_cast<QMouseEvent *>(event);
            QPointF documentPosition = eventMouse->pos() + QPointF(browser->horizontalScrollBar()->value(), browser->verticalScrollBar()->value());
            int mousedOverIdx = browser->document()->documentLayout()->hitTest(documentPosition, Qt::HitTestAccuracy::ExactHit);
            if(mousedOverIdx >= 0) {
                QTextCursor cur(browser->document());
                cur.setPosition(mousedOverIdx);
                // need an inclusive selection otherwise hit detection will be offset leftwards
                cur.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                // qDebug() << cur.charFormat();
                QTextCharFormat format = cur.charFormat();
                QVariant idProp = format.property(UserTextFormatProperty::SignalIdqint64);
                QVariant plotProp = format.property(UserTextFormatProperty::OrbPlot);
                if(idProp.isValid()) {
                    // qDebug() << idProp.toLongLong();
                    auto s = this->ui->dictionaryTable->model()->match(this->ui->dictionaryTable->model()->index(0, 0), Qt::DisplayRole, idProp.toLongLong(), 1, Qt::MatchExactly);
                    if(!s.isEmpty()) {
                        this->ui->dictionaryTable->selectionModel()->setCurrentIndex(s.first(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                        this->ui->dictionaryTable->scrollTo(s.first(), QAbstractItemView::PositionAtCenter);
                    } else {
                        this->ui->addDictionaryId->setText(QString::number(idProp.toLongLong()));
                    }
                    // this->dictionaryModel.
                } else if(plotProp.isValid()) {
                    this->ui->glOrPViewer->load53(plotProp.value<DscrSpecialPlot>());
                    this->ui->glOrPViewer->update();
                }
            }
            break;
        }
        case QEvent::Leave: {
            this->instantMouseoverToolTip.hide();
            break;
        }
        default:
            break;
    }
    return false;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // qDebug() << watched->objectName() << event->type();
    if(watched == this->ui->chatBrowser->viewport()) {
        this->eventFilterChatBrowser(this->ui->chatBrowser, event);
    } else if(watched == this->ui->chatBrowser2->viewport()) {
        this->eventFilterChatBrowser(this->ui->chatBrowser2, event);
    } else if(watched == this->ui->messageEditor->viewport()) {
        switch(event->type()) {
            case QEvent::MouseMove: {
                QMouseEvent *eventMouse = static_cast<QMouseEvent *>(event);
                // qDebug() << eventMouse->pos();
                // qDebug() << this->ui->messageEditor->document()->documentLayout()->hitTest(eventMouse->pos(), Qt::HitTestAccuracy::ExactHit);
                QPointF documentPosition = eventMouse->pos() + QPointF(this->ui->messageEditor->horizontalScrollBar()->value(), this->ui->messageEditor->verticalScrollBar()->value());
                int mousedOverIdx = this->ui->messageEditor->document()->documentLayout()->hitTest(documentPosition, Qt::HitTestAccuracy::ExactHit);
                if(mousedOverIdx >= 0) {
                    QVariant idProp;
                    for(auto sel : this->ui->messageEditor->extraSelections()) {
                        if(mousedOverIdx >= sel.cursor.selectionStart() && mousedOverIdx < sel.cursor.selectionEnd()) {
                            idProp = sel.format.property(UserTextFormatProperty::SignalIdqint64);
                        }
                    }
                    if(idProp.isValid()) {
                        // qDebug() << idProp.toLongLong();
                        this->instantMouseoverToolTip.setText(QString::number(idProp.toLongLong()));
                        this->instantMouseoverToolTip.adjustSize();
                        this->instantMouseoverToolTip.move(eventMouse->globalPosition().toPoint() + QPoint(15, 15));
                        this->instantMouseoverToolTip.show();
                    } else {
                        this->instantMouseoverToolTip.hide();
                    }
                } else {
                    // TODO may be edge cases where we don't see a MouseMove but still want to close the tooltip
                    this->instantMouseoverToolTip.hide();
                }
                break;
            }
            case QEvent::MouseButtonPress: {
                QMouseEvent *eventMouse = static_cast<QMouseEvent *>(event);
                QPointF documentPosition = eventMouse->pos() + QPointF(this->ui->messageEditor->horizontalScrollBar()->value(), this->ui->messageEditor->verticalScrollBar()->value());
                int mousedOverIdx = this->ui->messageEditor->document()->documentLayout()->hitTest(documentPosition, Qt::HitTestAccuracy::ExactHit);
                if(mousedOverIdx >= 0) {
                    QVariant idProp;
                    for(auto sel : this->ui->messageEditor->extraSelections()) {
                        if(mousedOverIdx >= sel.cursor.selectionStart() && mousedOverIdx < sel.cursor.selectionEnd()) {
                            idProp = sel.format.property(UserTextFormatProperty::SignalIdqint64);
                        }
                    }
                    if(idProp.isValid()) {
                        // qDebug() << idProp.toLongLong();
                        auto s = this->ui->dictionaryTable->model()->match(this->ui->dictionaryTable->model()->index(0, 0), Qt::DisplayRole, idProp.toLongLong(), 1, Qt::MatchExactly);
                        if(!s.isEmpty()) {
                            this->ui->dictionaryTable->selectionModel()->setCurrentIndex(s.first(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                            this->ui->dictionaryTable->scrollTo(s.first(), QAbstractItemView::PositionAtCenter);
                        } else {
                            this->ui->addDictionaryId->setText(QString::number(idProp.toLongLong()));
                        }
                        // this->dictionaryModel.
                    }
                }
                break;
            }
            case QEvent::Leave: {
                this->instantMouseoverToolTip.hide();
                break;
            }
            default:
                break;
        }
    } else if (watched == this->ui->messageEditor) {
        switch(event->type()) {
            case QEvent::KeyPress: {
                QKeyEvent *eventKey = static_cast<QKeyEvent *>(event);
                if((eventKey->key() == Qt::Key_Enter || eventKey->key() == Qt::Key_Return) && (eventKey->modifiers() & Qt::ControlModifier)) {
                    if(this->canSend) {
                        qDebug() << "Send" << this->signalsToSend;
                        this->core->dscrClient.sendDscrTransmission(this->signalsToSend);
                        this->ui->messageEditor->clear();
                        this->ui->signalViz->clear();
                    } else {
                        qDebug() << "canSend is false";
                    }
                    // prevent Ctrl+Enter from generating a newline
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }
    return false;
}

void MainWindow::onMessageEditorTextChanged() {
    const QString text = ui->messageEditor->toPlainText();
    const QByteArray textUtf8 = text.toUtf8();

    // FIXME Fe56 returns results in UTF-8 coordinates but QString is UTF-16...
    Fe56RSymbolized symbolized = fe56_mwis_symbolize(this->core->dictionary.fe56Dictionary, reinterpret_cast<const uint8_t *>(textUtf8.constData()), textUtf8.size());
    Fe56RVec_SymbolizerToken &tokens = symbolized.tokens;
    Fe56RVec_SymbolizerTrace &traces = symbolized.traces;

    this->canSend = symbolized.tokens.size > 0 && !symbolized.has_error;
    this->signalsToSend.clear();
    for(uintptr_t i = 0; i < tokens.size; i++) {
        auto token = tokens.ptr[i];
        signalsToSend.append(token.id);
    }

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
            fmt.setProperty(UserTextFormatProperty::SignalIdqint64, trace.kind.token.id);
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
    fe56_rsymbolized_free(symbolized);
    ui->messageEditor->setExtraSelections(selections);
    ui->signalViz->setText(signalVizText);
}

void MainWindow::pushToChatBrowser(const DscrTransmissionDbLinked &trns, QTextBrowser *browser) {
    // If the vertical scroll bar is at the bottom, later update the scroll to follow the chat
    QScrollBar *vBar = browser->verticalScrollBar();
    int vBarOldPosition = vBar->value();
    bool vBarOldAtBottom = vBarOldPosition == vBar->maximum();

    // Compute the position of the margin, to align chat text and callsign
    // This method only works with monospaced fonts, and even so may be off by a few pixels
    int avgCharWidth = browser->fontMetrics().averageCharWidth();
    int sequenceWidth = avgCharWidth * (3 + 2);

    // Declare the header and message blocks
    QTextBlockFormat headerBlockFormat;
    headerBlockFormat.setTopMargin(6);
    headerBlockFormat.setBottomMargin(0);

    QTextBlockFormat messageBlockFormat;
    messageBlockFormat.setTopMargin(0);
    messageBlockFormat.setLeftMargin(sequenceWidth);

    // Declare individual text formats to style different parts of the message
    QTextCharFormat sequenceFormat;
    QBrush brush(QColor(41, 199, 227), Qt::SolidPattern);
    sequenceFormat.setForeground(brush);
    sequenceFormat.setProperty(UserTextFormatProperty::TimeQDateTime, trns.time.toLocalTime());

    QTextCharFormat callsignFormat;
    QBrush brush2(QColor(196, 147, 0), Qt::SolidPattern);
    callsignFormat.setForeground(brush2);
    callsignFormat.setFontWeight(QFont::Bold);
    // QBrush brush3(QColor(202, 69, 209), Qt::SolidPattern);

    QTextCharFormat messageFormat;
    // messageFormat.setForeground(brush3);

    QTextCursor cur(browser->document());
    // Surround the whole edit in an edit block. This appears to eliminate a superlinear time penalty
    // that scales with the number of insertText calls in the block.
    cur.beginEditBlock();
    cur.movePosition(QTextCursor::End);

    cur.insertBlock(headerBlockFormat);
    cur.insertText(trns.msg.sequenceNum.has_value() ? QString::number(*trns.msg.sequenceNum).rightJustified(3, '0') : "???", sequenceFormat);
    cur.insertText("  ");
    cur.insertText(QString::number(trns.msg.from, 8).rightJustified(4, '0'), callsignFormat);

    for(qsizetype i = 0; i < trns.specSequences.size(); i++) {
        if(!std::holds_alternative<DscrSpecialPlot>(trns.specSequences[i].attachment)) {
            continue;
        }
        QTextCharFormat plotLinkFormat;
        plotLinkFormat.setProperty(UserTextFormatProperty::OrbPlot, QVariant::fromValue(std::get<DscrSpecialPlot>(trns.specSequences[i].attachment)));

        plotLinkFormat.setForeground(QColor(202, 69, 209));
        plotLinkFormat.setFontUnderline(true);

        cur.insertBlock(messageBlockFormat);
        cur.insertText(this->core->dictionary.translateSimple({-53, -163}), plotLinkFormat); // PLOT SEE
    }

    cur.insertBlock(messageBlockFormat);
    Fe56RDesymbolized desym = fe56_reformatter_desymbolize(this->core->dictionary.fe56Dictionary, trns.msg.symbols.constData(), trns.msg.symbols.size());
    QByteArrayView desymUtf8(desym.text.ptr, desym.text.size);
    uintptr_t prev_end = 0;
    for(uintptr_t i = 0; i < desym.spans.size; i++) {
        Fe56SymbolizerSpan_usize span = desym.spans.ptr[i];
        if(prev_end < span.start) {
            cur.insertText(QString::fromUtf8(desymUtf8.sliced(prev_end, span.start - prev_end)), messageFormat);
        }
        messageFormat.setProperty(UserTextFormatProperty::SignalIdqint64, trns.msg.symbols[i]);
        cur.insertText(QString::fromUtf8(desymUtf8.sliced(span.start, span.end - span.start)), messageFormat);
        messageFormat.clearProperty(UserTextFormatProperty::SignalIdqint64);
        prev_end = span.end;
    }
    if(prev_end < desymUtf8.size()) {
        cur.insertText(QString::fromUtf8(desymUtf8.sliced(prev_end, desymUtf8.size() - prev_end)), messageFormat);
    }
    fe56_rdesymbolized_free(desym);
    cur.endEditBlock();

    if(vBarOldAtBottom) {
        vBar->setValue(vBar->maximum());
    }
}

void MainWindow::onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns) {
    QStringList users;
    for(qint64 callSign : callSigns) {
        users << QString::number(callSign, 8).rightJustified(4, '0');
    }
    users.sort();
    this->usersModel.setStringList(users);
}
