#include "connectionpill.h"
#include "ui_connectionpill.h"

ConnectionPill::ConnectionPill(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConnectionPill)
{
    ui->setupUi(this);
}

ConnectionPill::~ConnectionPill() {
    delete ui;
}
