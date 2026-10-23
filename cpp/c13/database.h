#pragma once

#include <functional>
#include <optional>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

class Database : public QObject {
    Q_OBJECT

public:
    static const qint32 APPLICATION_ID = 0x0C13FE56;
    static const qint32 MAX_USER_VERSION = 1;

    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    bool open(const QString &path);
    void close();
    bool isOpen() const;
    QSqlDatabase getConn() const;

    bool scopedTransaction(const std::function<bool(Database &)> &fn);

    std::optional<QVariant> queryGetSetting(const QString &key);
    bool queryUpsertSetting(const QString &key, const QVariant &val);

    std::optional<qint64> queryGetOrInsertServer(const QUrl &url);
    std::optional<qint64> queryInsertAndGetSession(qint64 serverId, const QDateTime &time);
    bool queryInsertWsMessage(qint64 sessionId, const QDateTime &time, const QString &text, int direction);
    bool queryUpsertDictionary(qint64 signal, const QString &descr, int fmtBefore, int fmtAfter, bool breakOnDouble);
    bool queryInsertIfDifferentTranslation(qint64 signal, const QDateTime &time, const QString &word);

signals:

private:
    bool migrate();

    QString connName;
    QSqlDatabase dbConn;

    std::optional<QSqlQuery> prepGetSetting;
    std::optional<QSqlQuery> prepUpsertSetting;

    std::optional<QSqlQuery> prepGetOrInsertServer;
    std::optional<QSqlQuery> prepInsertAndGetSession;
    std::optional<QSqlQuery> prepInsertWsMessage;

    std::optional<QSqlQuery> prepUpsertDictionary;
    std::optional<QSqlQuery> prepInsertIfDifferentTranslation;
};
