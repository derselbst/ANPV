#include "FileOperationConfigDialog.hpp"
#include "ui_FileOperationConfigDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QShowEvent>
#include <QDebug>
#include <QActionGroup>
#include <QDir>
#include <QMetaEnum>

FileOperationConfigDialog::FileOperationConfigDialog(QActionGroup* fileOperationActionGroup, QWidget *parent)
: QDialog(parent),
  ui(new Ui::FileOperationConfigDialog), fileOperationActionGroup(fileOperationActionGroup)
{
    this->ui->setupUi(this);
    this->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(ui->buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, &FileOperationConfigDialog::accept);
    connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &FileOperationConfigDialog::reject);
    
    connect(ui->pushButton, &QPushButton::clicked, this, [&](){ this->onBrowseClicked(ui->lineEdit); });
    connect(ui->pushButton_2, &QPushButton::clicked, this, [&](){ this->onBrowseClicked(ui->lineEdit_2); });
    connect(ui->pushButton_3, &QPushButton::clicked, this, [&](){ this->onBrowseClicked(ui->lineEdit_3); });
    connect(ui->pushButton_4, &QPushButton::clicked, this, [&](){ this->onBrowseClicked(ui->lineEdit_4); });
    
    this->fillDiag();
}

FileOperationConfigDialog::~FileOperationConfigDialog()
{
    delete ui;
}

ANPV::FileOperation FileOperationConfigDialog::operationFromAction(QAction* a)
{
    QString text = a->text();
    QStringList words = text.split(QStringLiteral(" "));
    if(words.empty())
    {
        throw std::logic_error("This should never happen: words empty!");
    }
    QMetaEnum metaEnumOperation = QMetaEnum::fromType<ANPV::FileOperation>();
    bool ok;
    auto result = static_cast<ANPV::FileOperation>(metaEnumOperation.keyToValue(words[0].toLatin1().constData(), &ok));
    if(!ok)
    {
        throw std::runtime_error("Unable to determine FileOperation type");
    }
    return result;
}

void FileOperationConfigDialog::fillDiag()
{
    QMetaEnum metaEnumOperation = QMetaEnum::fromType<ANPV::FileOperation>();
    QList<QAction*> actions = fileOperationActionGroup->actions();
    
    auto uiBuilder = [&](QAction* action, QComboBox* comboBox, QKeySequenceEdit* seqEdit, QLineEdit* lineEdit)
    {
        comboBox->addItem(metaEnumOperation.valueToKey(ANPV::FileOperation::Move));
        comboBox->addItem(metaEnumOperation.valueToKey(ANPV::FileOperation::HardLink));
        comboBox->addItem(metaEnumOperation.valueToKey(ANPV::FileOperation::Delete));
        if(action != nullptr)
        {
            lineEdit->setText(action->data().toString());
            seqEdit->setKeySequence(action->shortcut());
            // the entries in the combo may not necessarily share the same index as the enum values
            // hence use this complicated way of setting the right index
            comboBox->setCurrentText(metaEnumOperation.valueToKey(this->operationFromAction(action)));
        }
        else
        {
            comboBox->setCurrentIndex(-1);
        }
    };

    uiBuilder(actions.size() < 1 ? nullptr : actions[0], ui->comboBox, ui->keySequenceEdit, ui->lineEdit);
    uiBuilder(actions.size() < 2 ? nullptr : actions[1], ui->comboBox_2, ui->keySequenceEdit_2, ui->lineEdit_2);
    uiBuilder(actions.size() < 3 ? nullptr : actions[2], ui->comboBox_3, ui->keySequenceEdit_3, ui->lineEdit_3);
    uiBuilder(actions.size() < 4 ? nullptr : actions[3], ui->comboBox_4, ui->keySequenceEdit_4, ui->lineEdit_4);

}

void FileOperationConfigDialog::accept()
{
    auto actionBuilder = [&](QComboBox* comboBox, QKeySequenceEdit* seqEdit, QLineEdit* lineEdit)
    {
        QAction* action = nullptr;
        
        QString targetDir = lineEdit->text();
        QFileInfo targetDirInfo = QFileInfo(targetDir);
        if(targetDirInfo.isDir())
        {
            QKeySequence seq = seqEdit->keySequence();
            
            QString title = comboBox->currentText();
            title += " to ";
            title += targetDir;
            action = new QAction(title, ANPV::globalInstance());
            action->setShortcut(seq);
            action->setData(targetDir);
            action->setShortcutContext(Qt::WidgetShortcut);
            
            return action;
        }
        
        return action;
    };
    
    QList<QAction*> actions = fileOperationActionGroup->actions();
    for(int i=0; i<actions.size(); i++)
    {
        fileOperationActionGroup->removeAction(actions[i]);
        actions[i]->deleteLater();
    }
    
    QAction* a;
    a = actionBuilder(ui->comboBox, ui->keySequenceEdit, ui->lineEdit);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    a = actionBuilder(ui->comboBox_2, ui->keySequenceEdit_2, ui->lineEdit_2);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    a = actionBuilder(ui->comboBox_3, ui->keySequenceEdit_3, ui->lineEdit_3);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    a = actionBuilder(ui->comboBox_4, ui->keySequenceEdit_4, ui->lineEdit_4);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    this->QDialog::accept();
}

void FileOperationConfigDialog::onBrowseClicked(QLineEdit* lineEdit)
{
    QString dirToOpen = lineEdit->text();
    QString dir = ANPV::globalInstance()->getExistingDirectory(this, dirToOpen);
    
    if(dir.isEmpty())
    {
        return;
    }
    
    lineEdit->setText(dir);
}
