#pragma once

#include "database/database.h"
#include "database/settings.h"
#include "dscrtypes.h"

#include <optional>
#include <QDateTime>
#include <QObject>
#include <QUrl>

class TransmissionProcessor : public QObject {
    Q_OBJECT

public:
    explicit TransmissionProcessor(QObject *parent = nullptr);

    bool init(Database *db, Settings *settings);

public slots:
    void onWsMessageLogConnected(const QDateTime &time, qint64 sessionId);
    void onWsMessageLogDscrTransmissionSent(const QDateTime &time, qint64 msgId, const DscrMessage &msg);
    void onWsMessageLogDscrTransmissionReceived(const QDateTime &time, qint64 msgId, const DscrMessage &msg);

signals:
    void dscrTransmissionReceived(const QDateTime &time, const DscrTransmission &msg);
    void callSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);

private:
    bool processMessage(std::optional<qint64> &desiredCallsign, std::optional<qint64> &actualCallsign, qint64 id, int direction, const DscrMessage &msg);

    Database *db;
    Settings *settings;

    std::optional<qint64> desiredCallsign;
    std::optional<qint64> actualCallsign;

    QSqlQuery prepInsertIntoMemoTransmissions;
    QSqlQuery prepMarkMFromMeMatched;
};
