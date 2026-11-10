#pragma once

#include "maincore.h"
#include "models/chatmodel.h"
#include "models/dictionarytablemodel.h"
#include "glorpviewer.h"

#include <QLabel>
#include <QMainWindow>
#include <QStringListModel>
#include <QSqlQueryModel>
#include <QTextBrowser>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(MainCore *core, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onMessageEditorTextChanged();
    void onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);

private:
    void pushToChatBrowser(const DscrTransmissionDbLinked &trns, QTextBrowser *browser);
    bool eventFilterChatBrowser(QTextBrowser *browser, QEvent *event);
    bool selectDictionaryEntry(qint64 id);

    Ui::MainWindow *ui;
    MainCore *core;
    QStringListModel usersModel;
    DictionaryTableModel dictionaryModel;
    QLabel instantMouseoverToolTip;
    GlOrPViewer instantGlorp;
    // TODO temp for reacting to dictionary updates
    ChatModel chatModelUnenc;
    ChatModel chatModelAll;
    // TODO loose message editor state
    QVector<qint64> signalsToSend;
    bool canSend;
};
