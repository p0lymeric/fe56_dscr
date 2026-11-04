#pragma once

#include <QWidget>

namespace Ui {
class DictionaryLongEditor;
}

class DictionaryLongEditor : public QWidget {
    Q_OBJECT

public:
    explicit DictionaryLongEditor(QWidget *parent = nullptr);
    ~DictionaryLongEditor();

private:
    Ui::DictionaryLongEditor *ui;
};
