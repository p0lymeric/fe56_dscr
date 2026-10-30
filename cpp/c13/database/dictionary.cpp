#include "database/dictionary.h"
#include "dictionary.h"
#include "fe56.h"

Dictionary::Dictionary(QObject *parent) : QObject{parent} {
    this->fe56Dictionary = fe56_dictionary_new();
}

Dictionary::~Dictionary() {
    fe56_dictionary_free(this->fe56Dictionary);
}

bool Dictionary::init(Database *db, Settings *settings) {
    this->db = db;
    this->settings = settings;

    if(auto stmt = this->db->prepareStatement(R"(
        INSERT INTO user_dictionary(signal, archived, descr, fmt_before, fmt_after, fmt_break_on_double)
        VALUES(:signal, 0, :descr, :fmt_before, :fmt_after, :fmt_break_on_double)
        ON CONFLICT(signal)
        DO UPDATE SET
            archived=0,
            descr=:descr,
            fmt_before=:fmt_before,
            fmt_after=:fmt_after,
            fmt_break_on_double=:fmt_break_on_double
    )"); stmt.has_value()) {
        this->prepUpsertDictionary = std::move(*stmt);
    } else {
        return false;
    }
    if(auto stmt = this->db->prepareStatement(R"(
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
    )"); stmt.has_value()) {
        this->prepInsertIfDifferentTranslation = std::move(*stmt);
    } else {
        return false;
    }

    QSqlQuery query(this->db->getConn());
    // LEFT JOIN to include dictionary entries without translations
    if(!query.exec(R"(
        SELECT d.signal, t.word, d.fmt_before, d.fmt_after, d.fmt_break_on_double, d.descr, t.revision, t.mtime_posix_ms
        FROM user_dictionary AS d
        LEFT JOIN user_translations AS t
        ON d.signal=t.signal
        AND t.revision=(
            SELECT MAX(revision)
            FROM user_translations
            WHERE signal=d.signal
        )
        WHERE d.archived=0
        ORDER BY d.signal DESC
    )")) {
        return false;
    }
    while(query.next()) {
        qint64 id = query.value(0).toLongLong();
        const QString word = query.value(1).toString();
        int fmtBefore = query.value(2).toInt();
        int fmtAfter = query.value(3).toInt();
        int breakOnDouble = query.value(4).toInt();
        const QString desc = query.value(5).toString();
        // Take advantage of the fact that a nonexistent translation will be revision 0
        qint64 revision = query.value(6).toLongLong();
        qint64 mtimePosixMs = query.value(7).toLongLong();

        // Add to Fe56 dictionary
        if(revision > 0) {
            const QByteArray wordUtf8 = word.toUtf8();
            Fe56Formatting formatting {
                static_cast<Fe56SpacingKind>(fmtBefore),
                static_cast<Fe56SpacingKind>(fmtAfter),
                breakOnDouble != 0,
            };
            fe56_dictionary_add(this->fe56Dictionary, id, reinterpret_cast<const uint8_t*>(wordUtf8.data()), wordUtf8.size(), formatting);
        }

        // Add to flat list
        signalToEntriesFlat[id] = entriesFlat.size();
        entriesFlat.append(Definition {
            id,
            desc,
            static_cast<Spacing>(fmtBefore),
            static_cast<Spacing>(fmtAfter),
            static_cast<bool>(breakOnDouble),
            revision,
            QDateTime::fromMSecsSinceEpoch(mtimePosixMs),
            word
        });
    }
    query.finish();

    // Read global formatting rules from settings
    fe56_dictionary_set_global_format(this->fe56Dictionary, Fe56Formatting {
        static_cast<Fe56SpacingKind>(settings->get("dictionary.beforeUserDefaultMode", Fe56SpacingKind::Space).toInt()),
        static_cast<Fe56SpacingKind>(settings->get("dictionary.afterUserDefaultMode", Fe56SpacingKind::Newline).toInt()),
        false,
    });

    return true;
}

bool Dictionary::upsertDictionary(qint64 signal, const QString &descr, int fmtBefore, int fmtAfter, bool breakOnDouble) {
    this->prepUpsertDictionary.bindValue(":signal", signal);
    this->prepUpsertDictionary.bindValue(":descr", descr);
    this->prepUpsertDictionary.bindValue(":fmt_before", fmtBefore);
    this->prepUpsertDictionary.bindValue(":fmt_after", fmtAfter);
    this->prepUpsertDictionary.bindValue(":fmt_break_on_double", breakOnDouble ? 1 : 0);
    if(!this->prepUpsertDictionary.exec()) {
        this->prepUpsertDictionary.finish();
        return false;
    }
    return true;
}

bool Dictionary::insertTranslationIfDifferent(qint64 signal, const QDateTime &time, const QString &word) {
    this->prepInsertIfDifferentTranslation.bindValue(":signal", signal);
    this->prepInsertIfDifferentTranslation.bindValue(":mtime", time.toMSecsSinceEpoch());
    this->prepInsertIfDifferentTranslation.bindValue(":word", word);
    if(!this->prepInsertIfDifferentTranslation.exec()) {
        this->prepInsertIfDifferentTranslation.finish();
        return false;
    }
    return true;
}

qsizetype Dictionary::getNumDefinitionsNonArchived() const {
    return this->entriesFlat.size();
}

const Definition &Dictionary::getDefinitionFlat(qsizetype flatIdx) const {
    return this->entriesFlat[flatIdx];
}

bool Dictionary::relink(qint64 signal, qint64 signalTo) {
    // Lookup
    auto it = this->signalToEntriesFlat.constFind(signal);
    if(it == this->signalToEntriesFlat.cend()) {
        return false;
    }
    qsizetype flatIdx = it.value();
    Definition &def = this->entriesFlat[flatIdx];
    if(signal == signalTo) {
        // no-op
        emit this->dataChanged(flatIdx, flatIdx);
        return true;
    }
    // Not allowed to overwrite an existing signal
    if(this->signalToEntriesFlat.contains(signalTo)) {
        return false;
    }

    // SQL
    if(!this->db->scopedTransaction([&](Database &db) {
        QSqlQuery query(db.getConn());

        // TODO this will fail if the destination is archived
        if(!this->db->oneshotStatement(R"(
            INSERT INTO user_dictionary(signal, archived, descr, fmt_before, fmt_after, fmt_break_on_double)
            SELECT :to, archived, descr, fmt_before, fmt_after, fmt_break_on_double
            FROM user_dictionary
            WHERE signal=:from
        )", {
            {":from", signal},
            {":to", signalTo},
        }).has_value()) {
            return false;
        }

        if(!this->db->oneshotStatement(R"(
            UPDATE user_translations
            SET signal=:to
            WHERE signal=:from
        )", {
            {":from", signal},
            {":to", signalTo},
        }).has_value()) {
            return false;
        }

        if(!this->db->oneshotStatement(R"(
            DELETE FROM user_dictionary
            WHERE signal=:from
        )", {
            {":from", signal},
        }).has_value()) {
            return false;
        }

        return true;
    })) {
        return false;
    }

    // Relinks aren't versioned but we want to squash consecutive edits on the same entry only
    if(this->timeSinceLastEdit.isValid() && this->lastEditedSignal == signal) {
        this->lastEditedSignal = signalTo;
    } else {
        this->timeSinceLastEdit.invalidate();
    }

    // Flat
    def.signal = signalTo;
    this->signalToEntriesFlat.remove(signal);
    this->signalToEntriesFlat.insert(signalTo, flatIdx);
    // TODO reorder position in flat map

    // Fe56
    fe56_dictionary_remove(this->fe56Dictionary, signal);
    const QByteArray wordUtf8 = def.word.toUtf8();
    Fe56Formatting formatting {
        static_cast<Fe56SpacingKind>(def.formatBefore),
        static_cast<Fe56SpacingKind>(def.formatAfter),
        def.breakOnDouble,
    };
    fe56_dictionary_add(this->fe56Dictionary, signalTo, reinterpret_cast<const uint8_t*>(wordUtf8.data()), wordUtf8.size(), formatting);

    emit this->dataChanged(flatIdx, flatIdx);
    return true;
}

bool Dictionary::rename(qint64 signal, const QString &word) {
    // Lookup
    auto it = this->signalToEntriesFlat.constFind(signal);
    if(it == this->signalToEntriesFlat.cend()) {
        return false;
    }
    qsizetype flatIdx = it.value();
    Definition &def = this->entriesFlat[flatIdx];
    if(word == def.word) {
        // no-op
        emit this->dataChanged(flatIdx, flatIdx);
        return true;
    }

    // Debounce
    // TODO pop the top revision if it has been renamed to be identical to the second top revision within REVISION_DEBOUNCE_TIME_MS
    const qint64 REVISION_DEBOUNCE_TIME_MS = 30000; // 30 sec
    bool keepExistingRevision = this->timeSinceLastEdit.isValid() && this->lastEditedSignal == signal && this->timeSinceLastEdit.elapsed() <= REVISION_DEBOUNCE_TIME_MS;
    qint64 newRevision = def.revision;
    QDateTime newTime = QDateTime::currentDateTime();
    if(newRevision == 0 || !keepExistingRevision) {
        newRevision++;
    }
    this->timeSinceLastEdit.start();
    this->lastEditedSignal = signal;

    // SQL
    if(!this->db->oneshotStatement(R"(
        INSERT INTO user_translations(signal, revision, mtime_posix_ms, word)
        VALUES(:signal, :revision, :mtime, :word)
        ON CONFLICT(signal, revision)
        DO UPDATE SET
            mtime_posix_ms=:mtime,
            word=:word
    )", {
        {":signal", signal},
        {":revision", newRevision},
        {":mtime", newTime.toMSecsSinceEpoch()},
        {":word", word},
    }).has_value()) {
        return false;
    }

    // Flat
    def.revision = newRevision;
    def.mtime = newTime;
    def.word = word;

    // Fe56
    fe56_dictionary_remove(this->fe56Dictionary, signal);
    const QByteArray wordUtf8 = word.toUtf8();
    Fe56Formatting formatting {
        static_cast<Fe56SpacingKind>(def.formatBefore),
        static_cast<Fe56SpacingKind>(def.formatAfter),
        def.breakOnDouble,
    };
    fe56_dictionary_add(this->fe56Dictionary, signal, reinterpret_cast<const uint8_t*>(wordUtf8.data()), wordUtf8.size(), formatting);

    emit this->dataChanged(flatIdx, flatIdx);
    return true;
}

bool Dictionary::add(qint64 signal) {
    // Lookup
    auto it = this->signalToEntriesFlat.constFind(signal);
    if(it != this->signalToEntriesFlat.cend()) {
        return false;
    }

    Definition def = Definition {
        signal,
        "??? ADD NOTES HERE ???",
        static_cast<Spacing>(this->settings->get("dictionary.beforeUserDefaultMode", static_cast<int>(Spacing::Space)).toInt()),
        static_cast<Spacing>(this->settings->get("dictionary.afterUserDefaultMode", static_cast<int>(Spacing::Newline)).toInt()),
        false,
        0,
        QDateTime::fromMSecsSinceEpoch(0),
        ""
    };

    // SQL
    if(!this->db->scopedTransaction([&](Database &db) {
        // Try to get an existing signal
        if(auto stmtD = db.oneshotStatement(R"(
            UPDATE user_dictionary
            SET archived=0
            WHERE signal=:signal AND archived=1
            RETURNING descr, fmt_before, fmt_after, fmt_break_on_double
        )", {
            {":signal", signal},
        }); stmtD.has_value()) {
            if(stmtD->next()) {
                def.desc = stmtD->value(0).toString();
                def.formatBefore = static_cast<Spacing>(stmtD->value(1).toInt());
                def.formatAfter = static_cast<Spacing>(stmtD->value(2).toInt());
                def.breakOnDouble = stmtD->value(3).toBool();
                if(auto stmtT = db.oneshotStatement(R"(
                    SELECT revision, mtime_posix_ms, word
                    FROM user_translations
                    WHERE signal=:signal
                    ORDER BY revision DESC
                    LIMIT 1
                )", {
                    {":signal", signal},
                }); stmtT.has_value()) {
                    if(stmtT->next()) {
                        def.revision = stmtT->value(0).toLongLong();
                        def.mtime = QDateTime::fromMSecsSinceEpoch(stmtT->value(1).toLongLong());
                        def.word = stmtT->value(2).toString();
                    }
                } else {
                    return false;
                }
                return true;
            }
            // fall through to insertion step
        } else {
            // abort due to SQL error
            return false;
        }

        // Insert a new signal
        if(auto stmt = db.oneshotStatement(R"(
            INSERT INTO user_dictionary(signal, archived, descr, fmt_before, fmt_after, fmt_break_on_double)
            VALUES(:signal, 0, :descr, :fmt_before, :fmt_after, :fmt_break_on_double)
        )", {
            {":signal", signal},
            {":descr", def.desc},
            {":fmt_before", static_cast<int>(def.formatBefore)},
            {":fmt_after", static_cast<int>(def.formatAfter)},
            {":fmt_break_on_double", def.breakOnDouble ? 1 : 0},
        }); stmt.has_value()) {
            return true;
        } else {
            // abort due to SQL error
            return false;
        }
    })) {
        // return from function due to abort in lambda
        return false;
    }

    // Debounce
    if(this->timeSinceLastEdit.isValid()) {
        this->timeSinceLastEdit.invalidate();
    }

    // Flat
    qsizetype flatIdx = this->entriesFlat.size();
    this->signalToEntriesFlat[signal] = flatIdx;
    emit this->beginInsertRows(flatIdx, flatIdx);
    this->entriesFlat.append(def);
    emit this->endInsertRows();
    // TODO insort

    // Fe56
    if(def.revision > 0) {
        const QByteArray wordUtf8 = def.word.toUtf8();
        Fe56Formatting formatting {
            static_cast<Fe56SpacingKind>(def.formatBefore),
            static_cast<Fe56SpacingKind>(def.formatAfter),
            def.breakOnDouble,
        };
        fe56_dictionary_add(this->fe56Dictionary, signal, reinterpret_cast<const uint8_t*>(wordUtf8.data()), wordUtf8.size(), formatting);
    }

    return true;
}
