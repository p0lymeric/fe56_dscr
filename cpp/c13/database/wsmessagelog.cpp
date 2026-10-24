#include "database/wsmessagelog.h"

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
    )"); stmt.has_value()) {
        this->prepInsertWsMessage = std::move(*stmt);
    } else {
        return false;
    }

    return true;
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

bool WsMessageLog::insertWsMessage(qint64 sessionId, const QDateTime &time, const QString &text, int direction) {
    this->prepInsertWsMessage.bindValue(":session_id", sessionId);
    this->prepInsertWsMessage.bindValue(":time_posix_ms", time.toMSecsSinceEpoch());
    this->prepInsertWsMessage.bindValue(":direction", direction);
    this->prepInsertWsMessage.bindValue(":kind", 0); // WsText
    this->prepInsertWsMessage.bindValue(":msg", text);
    if(!this->prepInsertWsMessage.exec()) {
        this->prepInsertWsMessage.finish();
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

        this->sessionId = sessionId.value();

        return true;
    });
}

void WsMessageLog::onDscrClientTextMessageSent(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
    this->db->scopedTransaction([&](Database &) {
        this->insertWsMessage(this->sessionId.value(), time, text, 1);
        return true;
    });
}

void WsMessageLog::onDscrClientTextMessageReceived(const QDateTime &time, const QString &text) {
    if(!this->sessionId.has_value()) {
        return;
    }
   this->db->scopedTransaction([&](Database &) {
        this->insertWsMessage(this->sessionId.value(), time, text, 0);
        return true;
    });
}
