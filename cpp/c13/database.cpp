#include "database.h"

#include <QSqlQuery>
#include <QFile>

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
    // https://stackoverflow.com/a/79677694
    this->queryGetOrInsertServer = prepareStatement(this->dbConn, R"(
        INSERT INTO log_servers (host, port)
        VALUES (?, ?)
        ON CONFLICT (host, port)
        DO UPDATE SET host = host
        RETURNING id
    )");
    this->queryInsertAndGetSession = prepareStatement(this->dbConn, R"(
        INSERT INTO log_sessions(server_id, connect_posix_ms)
        VALUES(?, ?)
        RETURNING id
    )");
    this->queryInsertWsMessage = prepareStatement(this->dbConn, R"(
        INSERT INTO log_wsmessages(session_id, time_posix_ms, direction, kind, msg)
        VALUES(?, ?, ?, ?, ?)
    )");
    if(
        !this->queryGetOrInsertServer.has_value() ||
        !this->queryInsertAndGetSession.has_value() ||
        !this->queryInsertWsMessage.has_value()
    ) {
        this->close();
        return false;
    }
    return true;
}

void Database::close() {
    // release prepared statements, then close connection
    this->queryGetOrInsertServer = std::nullopt;
    this->queryInsertAndGetSession = std::nullopt;
    this->queryInsertWsMessage = std::nullopt;
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
