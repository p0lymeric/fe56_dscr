#include "database.h"

database::database(QObject *parent) : QObject(parent) {
    this->dbConn = QSqlDatabase::addDatabase("QSQLITE");
}
