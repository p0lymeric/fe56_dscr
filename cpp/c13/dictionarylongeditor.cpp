#include "dictionarylongeditor.h"
#include "ui_dictionarylongeditor.h"

DictionaryLongEditor::DictionaryLongEditor(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DictionaryLongEditor)
{
    ui->setupUi(this);
}

DictionaryLongEditor::~DictionaryLongEditor() {
    delete ui;
}
