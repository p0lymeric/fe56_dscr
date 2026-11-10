#pragma once

#include "database/dictionary.h"

#include <optional>
#include <QWidget>

namespace Ui {
class DictionaryLongEditor;
}

class DictionaryLongEditor : public QWidget {
    Q_OBJECT

public:
    explicit DictionaryLongEditor(QWidget *parent = nullptr);
    ~DictionaryLongEditor();

    void load(const Definition &def);
    void revert();
    void clear();

signals:
    // void revertButtonClicked();
    void saveButtonClicked(qint64 origSignal, const Definition &def);

private slots:
    void onRevertButtonClicked(bool checked = false);
    void onSaveButtonClicked(bool checked = false);

private:
    Ui::DictionaryLongEditor *ui;
    std::optional<Definition> def;
};
