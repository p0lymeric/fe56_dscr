#pragma once

#include "maincore.h"
#include "models/dictionarytablemodel.h"

#include <QLabel>
#include <QMainWindow>
#include <QStringListModel>
#include <QSqlQueryModel>

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
    void onDscrClientDscrMessageReceived(const QDateTime &time, const DscrMessage &msg);
    void onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);

private:
    void pushToChatBrowser(const DscrMessage &msg);

    Ui::MainWindow *ui;
    MainCore *core;
    QStringListModel usersModel;
    DictionaryTableModel dictionaryModel;
    QLabel instantMouseoverToolTip;
    // TODO temp for reacting to dictionary updates
    QVector<DscrMessage> messages;
    // TODO loose message editor state
    QVector<qint64> signalsToSend;
    bool canSend;
};
