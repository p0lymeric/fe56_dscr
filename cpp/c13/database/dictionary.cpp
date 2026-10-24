#include "database/dictionary.h"

Dictionary::Dictionary(QObject *parent) : QObject{parent} {
    this->dictionary = fe56_dictionary_new();
}

Dictionary::~Dictionary() {
    fe56_dictionary_free(this->dictionary);
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
    // INNER JOIN to exclude dictionary entries without translations
    if(!query.exec(R"(
        SELECT d.signal, t.word, d.fmt_before, d.fmt_after, d.fmt_break_on_double
        FROM user_dictionary AS d
        INNER JOIN user_translations as t
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
        const QByteArray word = query.value(1).toString().toUtf8();
        int fmtBefore = query.value(2).toInt();
        int fmtAfter = query.value(3).toInt();
        int breakOnDouble = query.value(4).toInt();
        Fe56Formatting formatting {
            static_cast<Fe56SpacingKind>(fmtBefore),
            static_cast<Fe56SpacingKind>(fmtAfter),
            breakOnDouble != 0,
        };
        fe56_dictionary_add(this->dictionary, id, reinterpret_cast<const uint8_t*>(word.data()), word.size(), formatting);
    }
    query.finish();

    // Read global formatting rules from settings
    fe56_dictionary_set_global_format(this->dictionary, Fe56Formatting {
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
