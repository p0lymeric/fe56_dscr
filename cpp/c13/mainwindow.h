#pragma once

#include "maincore.h"

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
    void onDscrClientDscrMessageReceived(const QDateTime &time, qint64 from, qint64 sequenceNum, const QVector<qint64> &symbols);
    void onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);


private:
    Ui::MainWindow *ui;
    MainCore *core;
    QStringListModel usersModel;
    QSqlQueryModel dictionaryModel;
    QLabel instantMouseoverToolTip;
};
