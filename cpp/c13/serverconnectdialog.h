#pragma once

#include <QWidget>

namespace Ui {
class ServerConnectDialog;
}

class ServerConnectDialog : public QWidget {
    Q_OBJECT

public:
    explicit ServerConnectDialog(QWidget *parent = nullptr);
    ~ServerConnectDialog();

private:
    Ui::ServerConnectDialog *ui;
};
