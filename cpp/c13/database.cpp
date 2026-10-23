#include "database.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QFile>
#include <QUrl>

// public
Database::Database(QObject *parent) : QObject(parent) {
    this->connName = QString("Database-%1").arg(reinterpret_cast<quintptr>(this));
    QSqlDatabase::addDatabase("QSQLITE", this->connName);
    this->dbConn = QSqlDatabase::database(connName, false);
}

Database::~Database() {
    // https://doc.qt.io/qt-6/qsqldatabase.html
    // "Warning: It is highly recommended that you do not keep a copy of the QSqlDatabase around as a member of a class..."
    // We do so anyway (because this wrapper class will RAII it, and we store prepared statements anyway)
    this->close();
    this->dbConn = QSqlDatabase();
    QSqlDatabase::removeDatabase(this->connName);
}

static std::optional<QSqlQuery> prepareStatement(QSqlDatabase &dbConn, const QString &expression) {
    QSqlQuery query(dbConn);
    if(query.prepare(expression)) {
        return query;
    }
    return std::nullopt;
}

bool Database::open(const QString &path) {
    if(this->isOpen()) {
        this->close();
    }
    this->dbConn.setDatabaseName(path);
    if(!this->dbConn.open()) {
        return false;
    }
    if(!this->migrate()) {
        this->close();
        return false;
    }
    // prepare statements and close if failed
    this->prepGetSetting = prepareStatement(this->dbConn, R"(
        SELECT val
        FROM user_settings
        WHERE key=:key
    )");
    this->prepUpsertSetting = prepareStatement(this->dbConn, R"(
        INSERT OR REPLACE INTO user_settings(key, val)
        VALUES(:key, :val)
    )");
    // https://stackoverflow.com/a/79677694
    this->prepGetOrInsertServer = prepareStatement(this->dbConn, R"(
        INSERT INTO log_servers(host, port)
        VALUES(:host, :port)
        ON CONFLICT(host, port)
        DO UPDATE SET host=host
        RETURNING id
    )");
    this->prepInsertAndGetSession = prepareStatement(this->dbConn, R"(
        INSERT INTO log_sessions(server_id, connect_posix_ms)
        VALUES(:server_id, :connect_posix_ms)
        RETURNING id
    )");
    this->prepInsertWsMessage = prepareStatement(this->dbConn, R"(
        INSERT INTO log_wsmessages(session_id, time_posix_ms, direction, kind, msg)
        VALUES(:session_id, :time_posix_ms, :direction, :kind, :msg)
    )");
    this->prepUpsertDictionary = prepareStatement(this->dbConn, R"(
        INSERT INTO user_dictionary(signal, archived, descr, fmt_before, fmt_after, fmt_break_on_double)
        VALUES(:signal, 0, :descr, :fmt_before, :fmt_after, :fmt_break_on_double)
        ON CONFLICT(signal)
        DO UPDATE SET
            archived=0,
            descr=:descr,
            fmt_before=:fmt_before,
            fmt_after=:fmt_after,
            fmt_break_on_double=:fmt_break_on_double
    )");
    this->prepInsertIfDifferentTranslation = prepareStatement(this->dbConn, R"(
        INSERT INTO user_translations(signal, revision, mtime_posix_ms, word)
        SELECT :signal, COALESCE(MAX(revision), 0) + 1, :mtime, :word
        FROM user_translations
        WHERE signal=:signal
        HAVING(
            SELECT word
            FROM user_translations
            WHERE signal=:signal
            ORDER BY revision DESC
            LIMIT 1
        ) IS NOT :word
    )");
    if(
        !this->prepGetSetting.has_value() ||
        !this->prepUpsertSetting.has_value() ||
        !this->prepGetOrInsertServer.has_value() ||
        !this->prepInsertAndGetSession.has_value() ||
        !this->prepInsertWsMessage.has_value() ||
        !this->prepUpsertDictionary.has_value() ||
        !this->prepInsertIfDifferentTranslation.has_value()
    ) {
        this->close();
        return false;
    }
    return true;
}

void Database::close() {
    // release prepared statements, then close connection
    this->prepGetSetting = std::nullopt;
    this->prepUpsertSetting = std::nullopt;
    this->prepGetOrInsertServer = std::nullopt;
    this->prepInsertAndGetSession = std::nullopt;
    this->prepInsertWsMessage = std::nullopt;
    this->prepUpsertDictionary = std::nullopt;
    this->prepInsertIfDifferentTranslation = std::nullopt;
    if(this->isOpen()) {
        this->dbConn.close();
    }
}

bool Database::isOpen() const {
    return this->dbConn.isOpen();
}

QSqlDatabase Database::getConn() const {
    return this->dbConn;
}

bool Database::scopedTransaction(const std::function<bool(Database &)> &fn) {
    if(!this->dbConn.transaction()) {
        return false;
    }
    if(fn(*this)) {
        if(!this->dbConn.commit()) {
            this->dbConn.rollback();
            return false;
        }
    } else {
        this->dbConn.rollback();
        return false;
    }
    return true;
}

std::optional<QVariant> Database::queryGetSetting(const QString &key) {
    this->prepGetSetting->bindValue(":key", key);
    if(!this->prepGetSetting->exec() || !this->prepGetSetting->next()) {
        this->prepGetSetting->finish();
        return std::nullopt;
    }
    bool ok = false;
    QVariant val = this->prepGetSetting->value(0);
    this->prepGetSetting->finish();
    return val;
}

bool Database::queryUpsertSetting(const QString &key, const QVariant &val) {
    this->prepUpsertSetting->bindValue(":key", key);
    this->prepUpsertSetting->bindValue(":val", val);
    if(!this->prepUpsertSetting->exec()) {
        return false;
    }
    return true;
}

std::optional<qint64> Database::queryGetOrInsertServer(const QUrl &url) {
    this->prepGetOrInsertServer->bindValue(":host", url.host(QUrl::FullyEncoded));
    this->prepGetOrInsertServer->bindValue(":port", url.port(url.scheme() == "wss" ? 443 : 80));
    if(!this->prepGetOrInsertServer->exec() || !this->prepGetOrInsertServer->next()) {
        this->prepGetOrInsertServer->finish();
        return std::nullopt;
    }
    bool ok = false;
    qint64 serverId = this->prepGetOrInsertServer->value(0).toLongLong(&ok);
    this->prepGetOrInsertServer->finish();
    if(ok) {
        return serverId;
    } else {
        return std::nullopt;
    }
}

std::optional<qint64> Database::queryInsertAndGetSession(qint64 serverId, const QDateTime &time) {
    this->prepInsertAndGetSession->bindValue(":server_id", serverId);
    this->prepInsertAndGetSession->bindValue(":connect_posix_ms", time.toMSecsSinceEpoch());
    if(!this->prepInsertAndGetSession->exec() || !this->prepInsertAndGetSession->next()) {
        this->prepInsertAndGetSession->finish();
        return std::nullopt;
    }
    bool ok = false;
    qint64 sessionId = this->prepInsertAndGetSession->value(0).toLongLong(&ok);
    this->prepInsertAndGetSession->finish();
    if(ok) {
        return sessionId;
    } else {
        return std::nullopt;
    }
}

bool Database::queryInsertWsMessage(qint64 sessionId, const QDateTime &time, const QString &text, int direction) {
    this->prepInsertWsMessage->bindValue(":session_id", sessionId);
    this->prepInsertWsMessage->bindValue(":time_posix_ms", time.toMSecsSinceEpoch());
    this->prepInsertWsMessage->bindValue(":direction", direction);
    this->prepInsertWsMessage->bindValue(":kind", 0); // WsText
    this->prepInsertWsMessage->bindValue(":msg", text);
    if(!this->prepInsertWsMessage->exec()) {
        this->prepInsertWsMessage->finish();
        return false;
    }
    return true;
}

bool Database::queryUpsertDictionary(qint64 signal, const QString &descr, int fmtBefore, int fmtAfter, bool breakOnDouble) {
    this->prepUpsertDictionary->bindValue(":signal", signal);
    this->prepUpsertDictionary->bindValue(":descr", descr);
    this->prepUpsertDictionary->bindValue(":fmt_before", fmtBefore);
    this->prepUpsertDictionary->bindValue(":fmt_after", fmtAfter);
    this->prepUpsertDictionary->bindValue(":fmt_break_on_double", breakOnDouble ? 1 : 0);
    if(!this->prepUpsertDictionary->exec()) {
        this->prepUpsertDictionary->finish();
        return false;
    }
    return true;
}

bool Database::queryInsertIfDifferentTranslation(qint64 signal, const QDateTime &time, const QString &word) {
    this->prepInsertIfDifferentTranslation->bindValue(":signal", signal);
    this->prepInsertIfDifferentTranslation->bindValue(":mtime", time.toMSecsSinceEpoch());
    this->prepInsertIfDifferentTranslation->bindValue(":word", word);
    if(!this->prepInsertIfDifferentTranslation->exec()) {
        this->prepInsertIfDifferentTranslation->finish();
        return false;
    }
    return true;
}

static bool executeTrustedSqlFile(Database &db, const QString &fileName) {
    // Unfortunately QSqlDatabase doesn't support executing multiple SQL
    // statements in one shot. Instead of listing each statement in a
    // C++ array, we provide a dirty parser for embedded text files instead.
    return db.scopedTransaction([&fileName](Database &db) -> bool {
        QSqlQuery query(db.getConn());
        QFile sqlFile(fileName);
        if(sqlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString statements;
            while(!sqlFile.atEnd()) {
                QByteArray line = sqlFile.readLine();
                qsizetype commentPos = line.indexOf("--");
                if (commentPos >= 0) {
                    line.truncate(commentPos);
                }
                statements += QString::fromUtf8(line).trimmed() + "\n";
            }
            // Will fail to handle cases where the semicolon is used within a statement.
            // If a semicolon is ever needed, the EOS character would need to be substituted.
            for(const QString &rawStatement : statements.split(';')) {
                QString statement = rawStatement.trimmed();
                if(statement.isEmpty()) {
                    continue;
                }
                if(!query.exec(statement)) {
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
    });
}

// private
bool Database::migrate() {
    // A QUery? Is that Qt's version of a uery? What's a uery?
    QSqlQuery query(this->dbConn);

    query.exec("PRAGMA foreign_keys = ON;");
    if(!query.exec("PRAGMA foreign_keys") || !query.next()) {
        return false;
    }
    if(query.value(0) != 1) {
        return false;
    }

    if(!query.exec("PRAGMA application_id") || !query.next()) {
        return false;
    }
    qint32 appId = query.value(0).toInt();
    if(!query.exec("PRAGMA user_version") || !query.next()) {
        return false;
    }
    qint32 userVersion = query.value(0).toInt();
    if(!query.exec("SELECT COUNT(*) FROM sqlite_schema;") || !query.next()) {
        return false;
    }
    qint32 numTables = query.value(0).toInt();

    query.finish();

    if(appId == 0 && userVersion == 0 && numTables == 0) {
        return executeTrustedSqlFile(*this, ":/sql/init.sql");
    } else if(appId == Database::APPLICATION_ID) {
        if(userVersion > Database::MAX_USER_VERSION) {
            return false;
        }
        // apply migrations here in future
        return true;
    } else {
        // TODO may be useful to return reason for open failure
        return false;
    }
}
