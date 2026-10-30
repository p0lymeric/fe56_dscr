#include "database/transmissionprocessor.h"
#include "dscrtypes.h"

#include <tuple>
#include <QSqlError>

TransmissionProcessor::TransmissionProcessor(QObject *parent) : QObject{parent} {
}

bool TransmissionProcessor::init(Database *db, Settings *settings) {
    this->db = db;
    this->settings = settings;

    if(auto stmt = this->db->prepareStatement(R"(
        INSERT INTO memo_transmissions(wsmessage_id, callsign, seq, enc_key, origin, duplicate)
        VALUES(:wsmessage_id, :callsign, :seq, :enc_key, :origin, :duplicate)
    )"); stmt.has_value()) {
        this->prepInsertIntoMemoTransmissions = std::move(*stmt);
    } else {
        return false;
    }
    if(auto stmt = this->db->prepareStatement(R"(
        UPDATE memo_transmissions SET origin=3 WHERE wsmessage_id=:wsmessage_id
    )"); stmt.has_value()) {
        this->prepMarkMFromMeMatched = std::move(*stmt);
    } else {
        return false;
    }

    if(!this->db->scopedTransaction([&](Database &db) -> bool {
        if(auto flag = settings->testAndSetMetaFlag("transmissionProcessor.regenerateMemoTransmissions"); flag.has_value()) {
            if(!*flag) {
                auto truncate = db.prepareStatement(R"(
                    DELETE FROM memo_transmissions
                )"); // scary!

                auto select = db.prepareStatement(R"(
                    SELECT id, session_id, direction, kind, msg
                    FROM log_wsmessages
                    ORDER BY id
                )");

                if(!truncate.has_value() || !select.has_value()) {
                    return false;
                }

                if(!truncate->exec()) {
                    return false;
                }

                select->setForwardOnly(true);
                if(!select->exec()) {
                    return false;
                }
                if(select->lastError().isValid()) {
                    // "Note: To make sure the forward-only query completed successfully, the application should check lastError()
                    // for an error not only after executing the query, but also after navigating the query results."
                    return false;
                }

                // const int MAX_ITEMS_IN_RX_DEDUPE_BUFFER = 128;
                // const int MAX_R_MSG_BEFORE_DECLARING_TX_UNMATCHED = 16;
                // QSet<std::tuple<qint64, qint64, QVector<qint64>>> rxDeduplicationBuffer;
                // QVector<std::tuple<qint64, QVector<qint64>>> txOutstandingUnmatchedBuffer;
                // bool checkForDuplicates = false;

                std::optional<qint64> desiredCallsign;
                std::optional<qint64> actualCallsign;
                std::optional<qint64> lastSessionId;

                while(select->next()) {
                    qint64 id = select->value(0).toLongLong();
                    qint64 sessionId = select->value(1).toLongLong();
                    int direction = select->value(2).toInt();
                    int kind = select->value(3).toInt();
                    QVariant dbMsg = select->value(4);
                    if(!lastSessionId.has_value() || sessionId != *lastSessionId) {
                        lastSessionId = sessionId;
                        desiredCallsign = std::nullopt;
                        actualCallsign = std::nullopt;
                    }
                    auto msg = DscrMessage::decode(kind, dbMsg);
                    if(msg.has_value() && !processMessage(desiredCallsign, actualCallsign, id, direction, *msg)) {
                        return false;
                    }
                }
                if(select->lastError().isValid()) {
                    return false;
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

void TransmissionProcessor::onWsMessageLogConnected(const QDateTime &/*time*/, qint64 /*sessionId*/) {
    this->desiredCallsign = std::nullopt;
    this->actualCallsign = std::nullopt;
}
void TransmissionProcessor::onWsMessageLogDscrTransmissionSent(const QDateTime &time, qint64 msgId, const DscrMessage &msg) {
    this->processMessage(this->desiredCallsign, this->actualCallsign, msgId, 1, msg);
}
void TransmissionProcessor::onWsMessageLogDscrTransmissionReceived(const QDateTime &time, qint64 msgId, const DscrMessage &msg) {
    this->processMessage(this->desiredCallsign, this->actualCallsign, msgId, 0, msg);
    switch(msg.signifier) {
        case 'R':
            // Received Message
            if(msg.integers.size() >= 2) {
                // Nominally a message should be non-empty (>= 3) but we'll tolerate the empty case (== 2)
                emit this->dscrTransmissionReceived(time, DscrTransmission {
                    time,
                    msg.integers[0],
                    msg.integers[1],
                    msg.integers.mid(2)
                });
            }
            break;
        case 'C':
            // Call signs update
            emit this->callSignsChanged(time, msg.integers);
            break;
    }
}

bool TransmissionProcessor::processMessage(std::optional<qint64> &desiredCallsign, std::optional<qint64> &actualCallsign, qint64 id, int direction, const DscrMessage &msg) {
    qint64 callsign = 0;
    QVariant seq = QVariant();
    QVariant encKey = QVariant();
    int origin = 0;
    bool duplicate = false;
    if(direction == 1) {
        switch(msg.signifier) {
            case 'M': {
                if(!actualCallsign.has_value()) {
                    return true;
                }
                callsign = *actualCallsign;
                // seq = QVariant();
                if(msg.integers.size() >= 2) {
                    if(msg.integers[0] == -65535) {
                        encKey = msg.integers[1];
                    }
                }
                origin = 2;
                // duplicate = false;
                // TODO push into the tx unmatched buffer
                break;
            }
            case 'S':{
                if(msg.integers.size() > 0) {
                    desiredCallsign = msg.integers[0];
                }
                return true;
            }
            default:
                return true;
        }
    } else {
        switch(msg.signifier) {
            case 'R':
                // log_wsmessages.direction: Rx=0, Tx=1
                // memo_transmissions.origin: RFromOther=0, RFromMe=1, MFromMeUnmatched=2, MFromMeMatched=3
                if(msg.integers.size() < 2) {
                    // Nominally a message should be non-empty (>= 3) but we'll tolerate the empty case (== 2)
                    return true;
                }
                callsign = msg.integers[0];
                seq = msg.integers[1];
                if(msg.integers.size() >= 4) {
                    if(msg.integers[2] == -65535) {
                        encKey = msg.integers[3];
                    }
                }
                origin = callsign == actualCallsign ? 1 : 0;
                // TODO check against the rx deduplication buffer
                // duplicate = false;
                // ^^^ TODO
                // TODO push into the rx deduplication buffer
                // TODO check against the tx unmatched buffer
                // TODO evict from the txt tx unmatched buffer
                break;
            case 'K':
                // Call sign OK
                if(msg.integers.size() > 0) {
                    actualCallsign = msg.integers[0];
                }
                return true;
            case 'E':
                // Reconnect OK
                actualCallsign = desiredCallsign;
                return true;
            case 'U':
                // Call sign in use
                return true;
            default:
                return true;
        }
    }
    this->db->bindStatement(this->prepInsertIntoMemoTransmissions, {
        { ":wsmessage_id", id },
        { ":callsign", callsign },
        { ":seq", seq },
        { ":enc_key", encKey },
        { ":origin", origin },
        { ":duplicate", duplicate },
    });
    if(!this->prepInsertIntoMemoTransmissions.exec()) {
        this->prepInsertIntoMemoTransmissions.finish();
        return false;
    }
    this->prepInsertIntoMemoTransmissions.finish();
    return true;
}
