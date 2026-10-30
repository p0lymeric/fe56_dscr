#pragma once

#include <optional>
#include <QDateTime>
#include <QVector>

struct DscrMessage {
    char signifier;
    QVector<qint64> integers;

    QString encodeWsMessage();
    static std::optional<DscrMessage> decodeWsMessage(const QString &text);

    QByteArray encodePacked();
    static std::optional<DscrMessage> decodePacked(const QByteArray &pack);

    static std::optional<DscrMessage> decode(int kind, const QVariant &msg);
};

struct DscrTransmission {
    qint64 from;
    std::optional<qint64> sequenceNum;
    QVector<qint64> symbols;

    static std::optional<DscrTransmission> fromDscrRMessage(const DscrMessage &msg);
    static std::optional<DscrTransmission> fromDscrMMessage(const DscrMessage &msg, qint64 from);
};

struct DscrTransmissionDbLinked {
    QDateTime time;
    qint64 msgId;
    std::optional<qint64> encKey;
    DscrTransmission msg;
};

struct EncKeySelect {
    bool showUnkeyed;
    bool showAllKeyed;
    bool showKeyed;
    qint64 encKey;
};
