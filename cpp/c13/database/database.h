#pragma once

#include <functional>
#include <optional>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>

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
    std::optional<QSqlQuery> prepareStatement(const QString &expression);

signals:

private:
    bool migrate();

    QString connName;
    QSqlDatabase dbConn;
};
