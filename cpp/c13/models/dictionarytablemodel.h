#pragma once

#include <database/dictionary.h>

#include <QAbstractTableModel>

class DictionaryTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit DictionaryTableModel(QObject *parent = nullptr);

    void init(Dictionary *dictionary);

    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    // bool insertRows(int row, int count, const QModelIndex &parent) override;
    // bool removeRows(int row, int count, const QModelIndex &parent) override;

private:
    bool relinkSignalId(qint64 signal, qint64 newSignal);
    bool setWord(qint64 signal, const QString &word);
    void refresh();

    Dictionary *dictionary;
};
