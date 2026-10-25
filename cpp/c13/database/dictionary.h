#pragma once

#include "database/database.h"
#include "database/settings.h"

#include "fe56.h"

#include <QObject>
#include <QDateTime>
#include <QElapsedTimer>

enum class Spacing {
    None = 0,
    Space = 1,
    Newline = 2,
    TwoNewline = 3,
};

struct Definition {
    qint64 signal;
    QString desc;
    Spacing formatBefore;
    Spacing formatAfter;
    bool breakOnDouble;
    qint64 revision;
    QDateTime mtime;
    QString word;
};

class Dictionary : public QObject {
    Q_OBJECT
public:
    explicit Dictionary(QObject *parent = nullptr);
    ~Dictionary() override;

    bool init(Database *db, Settings *settings);

    bool upsertDictionary(qint64 signal, const QString &descr, int fmtBefore, int fmtAfter, bool breakOnDouble);
    bool insertTranslationIfDifferent(qint64 signal, const QDateTime &time, const QString &word);

    qsizetype getNumDefinitionsNonArchived() const;
    const Definition &getDefinitionFlat(qsizetype flatIdx) const;

    bool relink(qint64 signal, qint64 signalTo);
    bool rename(qint64 signal, QString word);

    // FIXME privatize (need to provide Symbolizer and Desymbolizer)
    Fe56ShadowDictionary *fe56Dictionary;

signals:
    void dataChanged(int flatIdx); // For DictionaryTableModel

private:
    Database *db;
    Settings *settings;

    qint64 lastEditedSignal;
    QElapsedTimer timeSinceLastEdit;

    QVector<Definition> entriesFlat;
    QHash<qint64, qsizetype> signalToEntriesFlat;

    QSqlQuery prepUpsertDictionary;
    QSqlQuery prepInsertIfDifferentTranslation;
};
