#pragma once

#include "database/database.h"

#include <QObject>
#include <QVariant>

class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr);

    bool init(Database *db);

    QVariant get(const QString &key);
    QVariant get(const QString &key, const QVariant &default_);
    bool upsert(const QString &key, const QVariant &val);

signals:

private:
    Database *db;

    QSqlQuery prepGetSetting;
    QSqlQuery prepUpsertSetting;
};
