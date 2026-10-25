#include "dictionarytablemodel.h"

// https://doc.qt.io/qt-6/qabstracttablemodel.html

DictionaryTableModel::DictionaryTableModel(QObject *parent) : QAbstractTableModel(parent) {
    this->dictionary = nullptr;
}

void DictionaryTableModel::init(Dictionary *dictionary) {
    this->beginResetModel();
    this->dictionary = dictionary;
    QObject::connect(this->dictionary, &Dictionary::dataChanged, this, [&](qsizetype flatIdx) {
        int row = static_cast<int>(flatIdx);
        // Conservative selection so we don't need dictionary to know which columns to target
        emit this->dataChanged(this->index(row, 0), this->index(row, 1));
    });
    this->endResetModel();
}

int DictionaryTableModel::rowCount(const QModelIndex &parent) const {
    if(parent.isValid()) {
        // "Note: When implementing a table based model, rowCount() should return 0 when the parent is valid."
        return 0;
    }
    if(this->dictionary == nullptr) {
        return 0;
    }
    return this->dictionary->getNumDefinitionsNonArchived();
}

int DictionaryTableModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return 2;
}

QVariant DictionaryTableModel::data(const QModelIndex &index, int role) const {
    // qDebug() << index << role;
    if(role != Qt::DisplayRole && role != Qt::EditRole) {
        // "Note: If you do not have a value to return, return an invalid (default-constructed) QVariant."
        return QVariant();
    }
    const Definition &def = this->dictionary->getDefinitionFlat(index.row());
    switch(index.column()) {
        case 0: return def.signal;
        default: return def.word;
    }
}

QVariant DictionaryTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if(orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch(section) {
        case 0: return "SIGNAL";
        default: return "WORD";
    }
}

Qt::ItemFlags DictionaryTableModel::flags(const QModelIndex &index) const {
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    if (index.column() == 0 || index.column() == 1) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

bool DictionaryTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if(role != Qt::EditRole) {
        return false;
    }

    // TODO eventually support these options:
    // dictionaryEditor.acceptEmptyOrAllWhitespaceWords
    // dictionaryEditor.acceptWordsContainingMixtureOfWhitespaceAndNonWhitespace
    // dictionaryEditor.acceptWordsContaining|@
    // dictionaryEditor.acceptCasefoldedDuplicates
    // dictionaryEditor.acceptExactDuplicates
    // dictionaryEditor.acceptWordsLongerThan16Codepoints
    // dictionaryEditor.ILIKEALLCAPSBUTMYPINKYISTIRED

    // With complicated edit rules, may want a QValidator in the loop
    // to avoid needing to reset a cell due to a rules violation

    qint64 signal = this->dictionary->getDefinitionFlat(index.row()).signal;

    switch(index.column()) {
        case 0: {
            return dictionary->relink(signal, value.toLongLong());
        }
        case 1: {
            QString word = value.toString();
            if(word.trimmed().isEmpty()) {
                 // Empty strings and strings with only whitespace are probably unintentional
                return false;
            }
            return dictionary->rename(signal, word);
        }
        default: {
            return false;
        }
    }
}

// bool insertRows(int row, int count, const QModelIndex &parent) {
// }

// bool removeRows(int row, int count, const QModelIndex &parent) {
// }
