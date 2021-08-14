#include "FileOperationConfigDialog.hpp"
#include "ui_FileOperationConfigDialog.h"
#include "ANPV.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QShowEvent>
#include <QDebug>
#include <QActionGroup>
#include <QDir>

FileOperationConfigDialog::FileOperationConfigDialog(QActionGroup* fileOperationActionGroup, ANPV *parent)
: QDialog(parent),
  ui(new Ui::FileOperationConfigDialog), anpv(parent), fileOperationActionGroup(fileOperationActionGroup)
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

void FileOperationConfigDialog::fillDiag()
{
    QList<QAction*> actions = fileOperationActionGroup->actions();
    
    auto uiBuilder = [&](QAction* action, QCheckBox* checkBox, QKeySequenceEdit* seqEdit, QLineEdit* lineEdit)
    {
        lineEdit->setText(action->data().toString());
        seqEdit->setKeySequence(action->shortcut());
        checkBox->setCheckState(Qt::PartiallyChecked);
    };
    
    if(actions.size() < 1)
        return;
    
    uiBuilder(actions[0], ui->checkBox, ui->keySequenceEdit, ui->lineEdit);

    if(actions.size() < 2)
        return;
    
    uiBuilder(actions[1], ui->checkBox_2, ui->keySequenceEdit_2, ui->lineEdit_2);

    if(actions.size() < 3)
        return;
    
    uiBuilder(actions[2], ui->checkBox_3, ui->keySequenceEdit_3, ui->lineEdit_3);

    if(actions.size() < 4)
        return;
    
    uiBuilder(actions[3], ui->checkBox_4, ui->keySequenceEdit_4, ui->lineEdit_4);

}

void FileOperationConfigDialog::accept()
{
    auto actionBuilder = [&](QCheckBox* checkBox, QKeySequenceEdit* seqEdit, QLineEdit* lineEdit)
    {
        QAction* action = nullptr;
        
        QString targetDir = lineEdit->text();
        QFileInfo targetDirInfo = QFileInfo(targetDir);
        if(targetDirInfo.isDir())
        {
            QKeySequence seq = seqEdit->keySequence();
            bool isCopy = checkBox->checkState() == Qt::Checked;
            
            QString title = isCopy ? "Copy " : "Move ";
            title += "to ";
            title += targetDir;
            action = new QAction(title);
            action->setShortcut(seq);
            action->setData(targetDir);
            
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
    a = actionBuilder(ui->checkBox, ui->keySequenceEdit, ui->lineEdit);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    a = actionBuilder(ui->checkBox_2, ui->keySequenceEdit_2, ui->lineEdit_2);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    a = actionBuilder(ui->checkBox_3, ui->keySequenceEdit_3, ui->lineEdit_3);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    a = actionBuilder(ui->checkBox_4, ui->keySequenceEdit_4, ui->lineEdit_4);
    if(a)
        this->fileOperationActionGroup->addAction(a);
    
    this->QDialog::accept();
}

void FileOperationConfigDialog::onBrowseClicked(QLineEdit* lineEdit)
{
    QString dirToOpen = lineEdit->text();
    QString dir = QFileDialog::getExistingDirectory(this, "Select Target Directory",
                                        dirToOpen.isEmpty() ? QDir::currentPath() : dirToOpen,
                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    
    if(dir.isEmpty())
    {
        return;
    }
    
    lineEdit->setText(dir);
}
