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

};

struct DscrTransmission {
    QDateTime time;
    qint64 from;
    qint64 sequenceNum;
    QVector<qint64> symbols;
};
