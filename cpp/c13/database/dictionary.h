#pragma once

#include "database/database.h"
#include "database/settings.h"

#include "fe56.h"

#include <QObject>
#include <QDateTime>

class Dictionary : public QObject {
    Q_OBJECT
public:
    explicit Dictionary(QObject *parent = nullptr);
    ~Dictionary() override;

    bool init(Database *db, Settings *settings);

    bool upsertDictionary(qint64 signal, const QString &descr, int fmtBefore, int fmtAfter, bool breakOnDouble);
    bool insertTranslationIfDifferent(qint64 signal, const QDateTime &time, const QString &word);

    // FIXME privatize (need to provide Symbolizer and Desymbolizer)
    Fe56ShadowDictionary *dictionary;

signals:

private:
    Database *db;
    Settings *settings;

    QSqlQuery prepUpsertDictionary;
    QSqlQuery prepInsertIfDifferentTranslation;
};
