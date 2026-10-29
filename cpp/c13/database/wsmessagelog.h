#pragma once

#include "database/database.h"
#include "database/settings.h"
#include "dscrtypes.h"

#include <optional>
#include <QDateTime>
#include <QObject>
#include <QUrl>

class WsMessageLog : public QObject {
    Q_OBJECT

public:
    explicit WsMessageLog(QObject *parent = nullptr);

    bool init(Database *db, Settings *settings);

public slots:
    void onDscrClientConnected(const QDateTime &time, const QUrl &url);
    void onDscrClientTextMessageSent(const QDateTime &time, const QString &text);
    void onDscrClientTextMessageReceived(const QDateTime &time, const QString &text);

signals:
    void connected(const QDateTime &time, qint64 sessionId);
    void dscrTransmissionSent(const QDateTime &time, qint64 msgId, const DscrMessage &msg);
    void dscrTransmissionReceived(const QDateTime &time, qint64 msgId, const DscrMessage &msg);

private:
    std::optional<qint64> getOrInsertServer(const QUrl &url);
    std::optional<qint64> insertAndGetSession(qint64 serverId, const QDateTime &time);
    std::optional<qint64> insertWsMessageAndGetId(qint64 sessionId, const QDateTime &time, int direction, int kind, const QVariant &data);

    Database *db;
    Settings *settings;

    QSqlQuery prepGetOrInsertServer;
    QSqlQuery prepInsertAndGetSession;
    QSqlQuery prepInsertWsMessageAndGetId;

    std::optional<qint64> sessionId;
};
