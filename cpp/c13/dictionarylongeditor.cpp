#include "dictionarylongeditor.h"
#include "ui_dictionarylongeditor.h"

DictionaryLongEditor::DictionaryLongEditor(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DictionaryLongEditor)
{
    ui->setupUi(this);
    QObject::connect(this->ui->saveButton, &QAbstractButton::clicked, this, &DictionaryLongEditor::onSaveButtonClicked);
    QObject::connect(this->ui->revertButton, &QAbstractButton::clicked, this, &DictionaryLongEditor::onRevertButtonClicked);
}

void DictionaryLongEditor::load(const Definition &def) {
    this->def = def;
    this->revert();
}

void DictionaryLongEditor::revert() {
    if(!this->def.has_value()) {
        return;
    }

    this->ui->signalEdit->setText(QString::number(this->def->signal));
    this->ui->wordEdit->setText(this->def->word);
    this->ui->nlOn2Button->setChecked(this->def->breakOnDouble);
    for(auto button : this->ui->beforeButtonGroup->buttons()) {
        button->setChecked(false);
    }
    switch(this->def->formatBefore) {
        case Spacing::None:
            this->ui->before0Button->setChecked(true);
            break;
        case Spacing::Space:
            this->ui->beforeSpButton->setChecked(true);
            break;
        case Spacing::Newline:
            this->ui->beforeNlButton->setChecked(true);
            break;
        case Spacing::TwoNewline:
            this->ui->before2NlButton->setChecked(true);
            break;
        default:
            break;
    }
    for(auto button : this->ui->afterButtonGroup->buttons()) {
        button->setChecked(false);
    }
    switch(this->def->formatAfter) {
        case Spacing::None:
            this->ui->after0Button->setChecked(true);
            break;
        case Spacing::Space:
            this->ui->afterSpButton->setChecked(true);
            break;
        case Spacing::Newline:
            this->ui->afterNlButton->setChecked(true);
            break;
        case Spacing::TwoNewline:
            this->ui->after2NlButton->setChecked(true);
            break;
        default:
            break;
    }
    this->ui->notesEdit->document()->setPlainText(this->def->desc);
}

void DictionaryLongEditor::clear() {
    this->def = std::nullopt;
}

void DictionaryLongEditor::onRevertButtonClicked(bool /*checked*/) {
    this->revert();
}
void DictionaryLongEditor::onSaveButtonClicked(bool /*checked*/) {
    if(this->def.has_value()) {
        bool ok = false;
        auto buttonsToSpacing = [](QAbstractButton *before0, QAbstractButton *beforeSp, QAbstractButton *beforeNl, QAbstractButton *before2Nl) -> Spacing {
            return
                before0->isChecked() ? Spacing::None :
                beforeSp->isChecked() ? Spacing::Space :
                beforeNl->isChecked() ? Spacing::Newline :
                before2Nl->isChecked() ? Spacing::TwoNewline :
                Spacing::Space
            ;
        };
        Definition newDef = Definition {
            this->ui->signalEdit->text().toLongLong(&ok),
            this->ui->notesEdit->toPlainText(),
            buttonsToSpacing(this->ui->before0Button, this->ui->beforeSpButton, this->ui->beforeNlButton, this->ui->before2NlButton),
            buttonsToSpacing(this->ui->after0Button, this->ui->afterSpButton, this->ui->afterNlButton, this->ui->after2NlButton),
            this->ui->nlOn2Button->isChecked(),
            0, // Unused
            QDateTime(), // Unused
            this->ui->wordEdit->text()
        };
        if(ok) {
            emit this->saveButtonClicked(this->def->signal, newDef);
        }
    }
}

DictionaryLongEditor::~DictionaryLongEditor() {
    delete ui;
}
