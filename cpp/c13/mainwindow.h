#pragma once

#include "dscrclient.h"
#include "fe56.h"

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onMessageEditorTextChanged();
    void onDscrClientDscrMessageReceived(const QDateTime &time, qint64 from, qint64 sequenceNum, const QVector<qint64> &symbols);
    void onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);

private:
    Ui::MainWindow *ui;
    Fe56JsonDictionary *dictionary;
    DscrClient dscrClient;
};
