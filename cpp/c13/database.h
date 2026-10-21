#pragma once

#include <QObject>
#include <QSqlDatabase>

class database : public QObject {
    Q_OBJECT

public:
    explicit database(QObject *parent = nullptr);

signals:

private:
    QSqlDatabase dbConn;
};
