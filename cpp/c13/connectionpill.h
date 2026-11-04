#pragma once

#include <QWidget>

namespace Ui {
class ConnectionPill;
}

class ConnectionPill : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionPill(QWidget *parent = nullptr);
    ~ConnectionPill();

private:
    Ui::ConnectionPill *ui;
};
