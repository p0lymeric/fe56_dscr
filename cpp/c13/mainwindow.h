#pragma once

#include "database.h"
#include "dscrclient.h"
#include "fe56.h"

#include <optional>
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
    void onDscrClientConnected(const QDateTime &time, const QUrl &url);
    void onDscrClientDscrMessageReceived(const QDateTime &time, qint64 from, qint64 sequenceNum, const QVector<qint64> &symbols);
    void onDscrClientCallSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);
    void onDscrClientTextMessageSent(const QDateTime &time, const QString &text);
    void onDscrClientTextMessageReceived(const QDateTime &time, const QString &text);

private:
    Ui::MainWindow *ui;
    Fe56JsonDictionary *dictionary;
    DscrClient dscrClient;
    Database database;
    std::optional<qint64> sessionId;
};
