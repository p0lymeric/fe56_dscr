#pragma once

#include "database/database.h"
#include "database/settings.h"

#include <optional>
#include <QDateTime>
#include <QObject>
#include <QUrl>

class WsMessageLog : public QObject {
    Q_OBJECT
public:
    explicit WsMessageLog(QObject *parent = nullptr);

    bool init(Database *db, Settings *settings);

    std::optional<qint64> getOrInsertServer(const QUrl &url);
    std::optional<qint64> insertAndGetSession(qint64 serverId, const QDateTime &time);
    bool insertWsMessage(qint64 sessionId, const QDateTime &time, const QString &text, int direction);

public slots:
    void onDscrClientConnected(const QDateTime &time, const QUrl &url);
    void onDscrClientTextMessageSent(const QDateTime &time, const QString &text);
    void onDscrClientTextMessageReceived(const QDateTime &time, const QString &text);

signals:

private:
    Database *db;
    Settings *settings;

    QSqlQuery prepGetOrInsertServer;
    QSqlQuery prepInsertAndGetSession;
    QSqlQuery prepInsertWsMessage;

    std::optional<qint64> sessionId;
};
