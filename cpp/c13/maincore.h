#pragma once

#include "database/database.h"
#include "database/settings.h"
#include "database/dictionary.h"
#include "database/wsmessagelog.h"
#include "dscrclient.h"

#include <QObject>

class MainCore : public QObject {
    Q_OBJECT
public:
    explicit MainCore(QObject *parent = nullptr);
    ~MainCore();

    bool open(const QString &dbPath);
    bool isOpen() const;

    Database database;
    Settings settings;
    Dictionary dictionary;
    WsMessageLog wsMessageLog;
    DscrClient dscrClient;

signals:

private:
};
