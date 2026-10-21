#include "serverconnectdialog.h"
#include "ui_serverconnectdialog.h"

ServerConnectDialog::ServerConnectDialog(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ServerConnectDialog)
{
    ui->setupUi(this);
}

ServerConnectDialog::~ServerConnectDialog() {
    delete ui;
}
