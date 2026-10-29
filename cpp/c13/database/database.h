#pragma once

#include <functional>
#include <optional>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>

class Database : public QObject {
    Q_OBJECT

public:
    static const qint32 APPLICATION_ID = 0x0C13FE56;
    static const qint32 MAX_USER_VERSION = 2;

    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    bool open(const QString &path);
    void close();
    bool isOpen() const;
    QSqlDatabase getConn() const;

    bool scopedTransaction(const std::function<bool(Database &)> &fn);

    std::optional<QSqlQuery> prepareStatement(const QString &expression);
    void bindStatement(QSqlQuery &prepStmt, const QHash<QString, QVariant> &bindings);
    std::optional<QSqlQuery> oneshotStatement(const QString &expression, const QHash<QString, QVariant> &bindings);

signals:

private:
    bool migrate();

    QString connName;
    QSqlDatabase dbConn;
};
