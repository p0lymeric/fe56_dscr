#include "database/database.h"

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

    return true;
}

void Database::close() {
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

std::optional<QSqlQuery> Database::prepareStatement(const QString &expression) {
    QSqlQuery query(this->dbConn);
    if(query.prepare(expression)) {
        return query;
    }
    return std::nullopt;
}

void Database::bindStatement(QSqlQuery &prepStmt, const QHash<QString, QVariant> &bindings) {
    for(auto [key, val] : bindings.asKeyValueRange()) {
        prepStmt.bindValue(key, val);
    }
}

std::optional<QSqlQuery> Database::oneshotStatement(const QString &expression, const QHash<QString, QVariant> &bindings) {
    std::optional<QSqlQuery> query = this->prepareStatement(expression);

    if(!query.has_value()) {
        return std::nullopt;
    }

    this->bindStatement(*query, bindings);

    if(!query->exec()) {
        return std::nullopt;
    }

    return query;
}

static bool executeTrustedSqlFile(Database &db, const QString &fileName) {
    // Unfortunately QSqlDatabase doesn't support executing multiple SQL
    // statements in one shot. Instead of listing each statement in a
    // C++ array, we provide a dirty parser for embedded text files instead.
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
}

static bool migrate_0001_to_0002(Database &db) {
    return true;
}

// private
bool Database::migrate() {
    // A QUery? Is that Qt's version of a uery? What's a uery?
    QSqlQuery query(this->dbConn);

    query.exec("PRAGMA foreign_keys = ON;");
    if(!query.exec("PRAGMA foreign_keys") || !query.next()) {
        return false;
    }
    if(query.value(0).toInt() != 1) {
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
        // Initialize a fresh database
        return this->scopedTransaction([](Database &db) -> bool {
            return executeTrustedSqlFile(db, ":/sql/init.sql");
        });
    } else if(appId == Database::APPLICATION_ID) {
        // Migrate an existing database
        if(userVersion < 0 || userVersion == Database::MAX_USER_VERSION) {
            // Negatively versioned databases and databases at the current max revision are not migrated
            return true;
        } else if(userVersion == 0 || userVersion > Database::MAX_USER_VERSION) {
            // Version 0 databases and databases above the current max revision cannot be used
            return false;
        }

        QString backupFileNameBase = this->dbConn.databaseName() + QString(".v%1").arg(userVersion, 4, 10, '0');
        QString backupFileName = backupFileNameBase + ".updateattempt0.bak";
        for(int i = 1; QFile::exists(backupFileName); i++) {
            backupFileName = backupFileNameBase + QString(".attempt%1.bak").arg(i);
        }
        if(!query.prepare("VACUUM INTO ?")) {
            return false;
        }
        query.addBindValue(backupFileName);
        if(!query.exec()) {
            return false;
        }

        return this->scopedTransaction([userVersion](Database &db) -> bool {
            if(userVersion == 1) {
                if(!executeTrustedSqlFile(db, ":/sql/migrate_0001_to_0002.sql") || !migrate_0001_to_0002(db)) {
                    return false;
                }
            }
            // Remember to update MAX_USER_VERSION!!!
            return true;
        });
    } else {
        // TODO may be useful to return reason for open failure
        return false;
    }
}
