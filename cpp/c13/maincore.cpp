#include "maincore.h"
#include "database/wsmessagelog.h"

MainCore::MainCore(QObject *parent) : QObject{parent} {
    QObject::connect(&this->dscrClient, &DscrClient::connected, &this->wsMessageLog, &WsMessageLog::onDscrClientConnected);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageSent, &this->wsMessageLog, &WsMessageLog::onDscrClientTextMessageSent);
    QObject::connect(&this->dscrClient, &DscrClient::textMessageReceived, &this->wsMessageLog, &WsMessageLog::onDscrClientTextMessageReceived);
}

MainCore::~MainCore() {
}

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

    // BEGIN dscrClient compartment
    // FIXME "insertIfNotExistsOrTypeMalformed"
    this->settings.upsert("dscrClient.callsign", 01444);
    this->settings.upsert("dscrClient.url", "wss://dscr-relay.dixonary.co.uk");
    // END dscrClient compartment

    return true;
}

bool MainCore::isOpen() const {
    return this->database.isOpen();
}

// void import_json_dictionary() {
//     QFile dict("C:\\Games\\TMDSDecomp\\save\\DICTIONARY-1.save");
//     if (dict.open(QIODevice::ReadOnly)) {
//         QDateTime importTime = QDateTime::currentDateTime();
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

//         database.scopedTransaction([&](Database &db) {
//             if(!database.queryUpsertSetting("dictionary.beforeUserDefaultMode", beforeUserDefaultMode)) {
//                 return false;
//             }
//             if(!database.queryUpsertSetting("dictionary.afterUserDefaultMode", afterUserDefaultMode)) {
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
//                     database.queryUpsertDictionary(
//                         id,
//                         dDKi["desc"].toString("??? ADD NOTES HERE ???"),
//                         dDKi["formatMode"].toInteger(beforeUserDefaultMode),
//                         dDKi["formatModeAfter"].toInteger(afterUserDefaultMode),
//                         dDKi["breakOnDouble"].toBool(false)
//                     );
//                     database.queryInsertIfDifferentTranslation(id, importTime, word);
//                 }
//             }

//             return true;
//         });

//     } else {
//     }
// }
