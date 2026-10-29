#include "database/wsmessagelog.h"
#include "dscrtypes.h"
#include <limits>

WsMessageLog::WsMessageLog(QObject *parent) : QObject{parent} {
}

bool WsMessageLog::init(Database *db, Settings *settings) {
    this->db = db;
    this->settings = settings;

    // https://stackoverflow.com/a/79677694
    if(auto stmt = this->db->prepareStatement(R"(
        INSERT INTO log_servers(host, port)
        VALUES(:host, :port)
        ON CONFLICT(host, port)
        DO UPDATE SET host=host
        RETURNING id
    )"); stmt.has_value()) {
        this->prepGetOrInsertServer = std::move(*stmt);
    } else {
        return false;
    }
    if(auto stmt = this->db->prepareStatement(R"(
        INSERT INTO log_sessions(server_id, connect_posix_ms)
        VALUES(:server_id, :connect_posix_ms)
        RETURNING id
    )"); stmt.has_value()) {
        this->prepInsertAndGetSession = std::move(*stmt);
    } else {
        return false;
    }
    if(auto stmt = this->db->prepareStatement(R"(
        INSERT INTO log_wsmessages(session_id, time_posix_ms, direction, kind, msg)
        VALUES(:session_id, :time_posix_ms, :direction, :kind, :msg)
        RETURNING id
    )"); stmt.has_value()) {
        this->prepInsertWsMessageAndGetId = std::move(*stmt);
    } else {
        return false;
    }

    if(!this->db->scopedTransaction([&](Database &db) -> bool {
        if(auto flag = settings->testAndSetMetaFlag("wsMessageLog.applyRowCompression"); flag.has_value()) {
            if(!*flag) {
                auto select = db.prepareStatement("SELECT id, msg FROM log_wsmessages WHERE id>:id AND kind=0 ORDER BY id LIMIT 1");
                auto update = db.prepareStatement("UPDATE log_wsmessages SET msg=:msg, kind=1 WHERE id=:id");
                if(!select.has_value() || !update.has_value()) {
                    return false;
                }

                qint64 id = std::numeric_limits<qint64>::min();

                while(true) {
                    select->bindValue(":id", id);
                    if(!select->exec()) {
                        return false;
                    }
                    if(!select->next()) {
                        return true;
                    }
                    id = select->value(0).toLongLong();
                    const QString text = select->value(1).toString();
                    select->finish();

                    if(auto dscrMessage = DscrMessage::decodeWsMessage(text); dscrMessage.has_value()) {
                        update->bindValue(":msg", dscrMessage->encodePacked());
                        update->bindValue(":id", id);
                        if(!update->exec()) {
                            return false;
                        }
                    }
                }
            }
        } else {
            return false;
        }
        return true;
    })) {
        return false;
    }

    return true;
}

void WsMessageLog::onDscrClientConnected(const QDateTime &time, const QUrl &url) {
    this->sessionId = std::nullopt;

    this->db->scopedTransaction([&](Database &) {
        std::optional<qint64> serverId = this->getOrInsertServer(url);
        if(!serverId.has_value()) {
            return false;
        }

        std::optional<qint64> sessionId = this->insertAndGetSession(serverId.value(), time);
        if(!sessionId.has_value()) {
            return false;
        }

        this->sessionId = sessionId;
        emit this->connected(time, *sessionId);

        return true;
    });
}

void WsMessageLog::onDscrClientTextMessageSent(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
    if(auto msg = DscrMessage::decodeWsMessage(text); msg.has_value()) {
        if(auto msgId = this->insertWsMessageAndGetId(this->sessionId.value(), time, 1, 1, msg->encodePacked()); msgId.has_value()) {
            emit dscrTransmissionSent(time, *msgId, *msg);
        }
    } else {
        this->insertWsMessageAndGetId(this->sessionId.value(), time, 1, 0, text);
    }
}

void WsMessageLog::onDscrClientTextMessageReceived(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
    if(auto msg = DscrMessage::decodeWsMessage(text); msg.has_value()) {
        if(auto msgId = this->insertWsMessageAndGetId(this->sessionId.value(), time, 0, 1, msg->encodePacked()); msgId.has_value()) {
            emit dscrTransmissionReceived(time, *msgId, *msg);
        }
    } else {
        this->insertWsMessageAndGetId(this->sessionId.value(), time, 0, 0, text);
    }
}

std::optional<qint64> WsMessageLog::getOrInsertServer(const QUrl &url) {
    this->prepGetOrInsertServer.bindValue(":host", url.host(QUrl::FullyEncoded));
    this->prepGetOrInsertServer.bindValue(":port", url.port(url.scheme() == "wss" ? 443 : 80));
    if(!this->prepGetOrInsertServer.exec() || !this->prepGetOrInsertServer.next()) {
        this->prepGetOrInsertServer.finish();
        return std::nullopt;
    }
    bool ok = false;
    qint64 serverId = this->prepGetOrInsertServer.value(0).toLongLong(&ok);
    this->prepGetOrInsertServer.finish();
    if(ok) {
        return serverId;
    } else {
        return std::nullopt;
    }
}

std::optional<qint64> WsMessageLog::insertAndGetSession(qint64 serverId, const QDateTime &time) {
    this->prepInsertAndGetSession.bindValue(":server_id", serverId);
    this->prepInsertAndGetSession.bindValue(":connect_posix_ms", time.toMSecsSinceEpoch());
    if(!this->prepInsertAndGetSession.exec() || !this->prepInsertAndGetSession.next()) {
        this->prepInsertAndGetSession.finish();
        return std::nullopt;
    }
    bool ok = false;
    qint64 sessionId = this->prepInsertAndGetSession.value(0).toLongLong(&ok);
    this->prepInsertAndGetSession.finish();
    if(ok) {
        return sessionId;
    } else {
        return std::nullopt;
    }
}

std::optional<qint64> WsMessageLog::insertWsMessageAndGetId(qint64 sessionId, const QDateTime &time, int direction, int kind, const QVariant &data) {
    this->prepInsertWsMessageAndGetId.bindValue(":session_id", sessionId);
    this->prepInsertWsMessageAndGetId.bindValue(":time_posix_ms", time.toMSecsSinceEpoch());
    this->prepInsertWsMessageAndGetId.bindValue(":direction", direction);
    this->prepInsertWsMessageAndGetId.bindValue(":kind", kind); // WsText
    this->prepInsertWsMessageAndGetId.bindValue(":msg", data);
    if(!this->prepInsertWsMessageAndGetId.exec() || !this->prepInsertWsMessageAndGetId.next()) {
        this->prepInsertWsMessageAndGetId.finish();
        return std::nullopt;
    }
    bool ok = false;
    qint64 msgId = this->prepInsertWsMessageAndGetId.value(0).toLongLong(&ok);
    this->prepInsertWsMessageAndGetId.finish();
    if(ok) {
        return msgId;
    } else {
        return std::nullopt;
    }
}
