#include "database/settings.h"

Settings::Settings(QObject *parent) : QObject{parent} {
}

bool Settings::init(Database *db) {
    this->db = db;

    if(auto stmt = this->db->prepareStatement(R"(
        SELECT val
        FROM user_settings
        WHERE key=:key
    )"); stmt.has_value()) {
        this->prepGetSetting = std::move(*stmt);
    } else {
        return false;
    }

    if(auto stmt = this->db->prepareStatement(R"(
        INSERT OR REPLACE INTO user_settings(key, val)
        VALUES(:key, :val)
    )"); stmt.has_value()) {
        this->prepUpsertSetting = std::move(*stmt);
    } else {
        return false;
    }

    return true;
}

QVariant Settings::get(const QString &key) {
    this->prepGetSetting.bindValue(":key", key);
    if(!this->prepGetSetting.exec() || !this->prepGetSetting.next()) {
        this->prepGetSetting.finish();
        return QVariant();
    }
    QVariant val = this->prepGetSetting.value(0);
    this->prepGetSetting.finish();
    return val;
}

QVariant Settings::get(const QString &key, const QVariant &default_) {
    QVariant result = this->get(key);
    if(!result.isValid()) {
        return default_;
    } else {
        return result;
    }
}

bool Settings::upsert(const QString &key, const QVariant &val) {
    this->prepUpsertSetting.bindValue(":key", key);
    this->prepUpsertSetting.bindValue(":val", val);
    if(!this->prepUpsertSetting.exec()) {
        this->prepUpsertSetting.finish();
        return false;
    }
    this->prepUpsertSetting.finish();
    return true;
}
