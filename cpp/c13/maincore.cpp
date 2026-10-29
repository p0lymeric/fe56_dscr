#include "maincore.h"
#include "database/wsmessagelog.h"

MainCore::MainCore(QObject *parent) : QObject{parent} {
    QObject::connect(&this->dscrClient, &DscrClient::connected, &this->wsMessageLog, &WsMessageLog::onDscrClientConnected);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageSent, &this->wsMessageLog, &WsMessageLog::onDscrClientTextMessageSent);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageReceived, &this->wsMessageLog, &WsMessageLog::onDscrClientTextMessageReceived);

    QObject::connect(&this->wsMessageLog, &WsMessageLog::connected, &this->transmissionProcessor, &TransmissionProcessor::onWsMessageLogConnected);
    QObject::connect(&this->wsMessageLog, &WsMessageLog::dscrTransmissionSent, &this->transmissionProcessor, &TransmissionProcessor::onWsMessageLogDscrTransmissionSent);
    QObject::connect(&this->wsMessageLog, &WsMessageLog::dscrTransmissionReceived, &this->transmissionProcessor, &TransmissionProcessor::onWsMessageLogDscrTransmissionReceived);
}

MainCore::~MainCore() {
}

// #include <QFile>
// #include <QJsonDocument>
// #include <QJsonArray>
// #include <QJsonObject>
// #include <QJsonValue>
// static void import_json_dictionary(MainCore &core, const QString &fileName) {
//     QFile dict(fileName);
//     if (dict.open(QIODevice::ReadOnly)) {
//         QDateTime importTime = dict.fileTime(QFileDevice::FileModificationTime);
//         QJsonDocument json = QJsonDocument::fromJson(dict.readAll());

//         // FIXME replace magic numbers with enum level
//         qint64 beforeUserDefaultMode = json["beforeUserDefaultMode"].toInteger(1);
//         qint64 afterUserDefaultMode = json["afterUserDefaultMode"].toInteger(2);

//         QHash<qint64, QJsonObject> descMap;
//         QJsonArray dDK = json["descDict"]["keys"].toArray();
//         QJsonArray dDV = json["descDict"]["values"].toArray();
//         for(qsizetype i = 0; i < std::min(dDK.size(), dDV.size()); i++) {
//             QJsonValue dDKi = dDK[i];
//             QJsonValue dDVi = dDV[i];
//             if(dDKi.isDouble() && dDVi.isObject()) {
//                 descMap[dDKi.toInteger()] = dDVi.toObject();
//             }
//         }

//         core.database.scopedTransaction([&](Database &db) {
//             if(!core.settings.upsert("dictionary.beforeUserDefaultMode", beforeUserDefaultMode)) {
//                 return false;
//             }
//             if(!core.settings.upsert("dictionary.afterUserDefaultMode", afterUserDefaultMode)) {
//                 return false;
//             }

//             QJsonArray wDK = json["wordDict"]["keys"].toArray();
//             QJsonArray wDV = json["wordDict"]["values"].toArray();
//             for(qsizetype i = 0; i < std::min(wDK.size(), wDV.size()); i++) {
//                 QJsonValue wDKi = wDK[i];
//                 QJsonValue wDVi = wDV[i];
//                 if(wDKi.isDouble() && wDVi.isString()) {
//                     qint64 id = wDKi.toInteger();
//                     QString word = wDVi.toString();
//                     QJsonObject dDKi = descMap[id];;
//                     core.dictionary.upsertDictionary(
//                         id,
//                         dDKi["desc"].toString("??? ADD NOTES HERE ???"),
//                         dDKi["formatMode"].toInteger(beforeUserDefaultMode),
//                         dDKi["formatModeAfter"].toInteger(afterUserDefaultMode),
//                         dDKi["breakOnDouble"].toBool(false)
//                     );
//                     core.dictionary.insertTranslationIfDifferent(id, importTime, word);
//                 }
//             }

//             return true;
//         });

//     } else {
//     }
// }

bool MainCore::open(const QString &dbPath) {
    if(!this->database.open(dbPath)) {
        return false;
    }

    if(!this->settings.init(&this->database)) {
        return false;
    }

    if(!this->dictionary.init(&this->database, &this->settings)) {
        return false;
    }

    if(!this->wsMessageLog.init(&this->database, &this->settings)) {
        return false;
    }

    if(!this->transmissionProcessor.init(&this->database, &this->settings)) {
        return false;
    }

    // BEGIN dscrClient compartment
    // FIXME "insertIfNotExistsOrTypeMalformed"
    // this->settings.upsert("dscrClient.callsign", 01444);
    // this->settings.upsert("dscrClient.url", "wss://dscr-relay.dixonary.co.uk");
    // END dscrClient compartment

    // for(int i = 1; i <= 49; i++) {
    //     import_json_dictionary(*this, QString("C:\\Games\\TMDSDecomp\\save\\DICTIONARY-1 - backup%1.save").arg(i));
    // }
    // import_json_dictionary(*this, QString("C:\\Games\\TMDSDecomp\\save\\DICTIONARY-1.save"));
    // import_json_dictionary(*this, QString("C:\\Games\\TMDSDecomp\\save\\relay3544.txt"));

    return true;
}

bool MainCore::isOpen() const {
    return this->database.isOpen();
}
