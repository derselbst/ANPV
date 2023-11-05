#include "FileOperationConfigDialog.hpp"
#include "ui_FileOperationConfigDialog.h"

#include <QDialogButtonBox>
#include <QDialog>
#include <QActionGroup>
#include <QMetaEnum>

FileOperationConfigDialog::FileOperationConfigDialog(QActionGroup *fileOperationActionGroup, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::FileOperationConfigDialog), fileOperationActionGroup(fileOperationActionGroup)
{
    this->ui->setupUi(this);
    this->setAttribute(Qt::WA_DeleteOnClose);

    connect(ui->buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, &FileOperationConfigDialog::accept);
    connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &FileOperationConfigDialog::reject);

    connect(ui->pushButton, &QPushButton::clicked, this, [&]()
    {
        this->onBrowseClicked(ui->lineEdit);
    });
    connect(ui->pushButton_2, &QPushButton::clicked, this, [&]()
    {
        this->onBrowseClicked(ui->lineEdit_2);
    });
    connect(ui->pushButton_3, &QPushButton::clicked, this, [&]()
    {
        this->onBrowseClicked(ui->lineEdit_3);
    });
    connect(ui->pushButton_4, &QPushButton::clicked, this, [&]()
    {
        this->onBrowseClicked(ui->lineEdit_4);
    });

    connect(ui->comboBox, &QComboBox::currentIndexChanged, this, [&](int index)
    {
        ui->lineEdit->setEnabled(index + 1 != ANPV::FileOperation::Delete);
        ui->pushButton->setEnabled(index + 1 != ANPV::FileOperation::Delete);
    });
    connect(ui->comboBox_2, &QComboBox::currentIndexChanged, this, [&](int index)
    {
        ui->lineEdit_2->setEnabled(index + 1 != ANPV::FileOperation::Delete);
        ui->pushButton_2->setEnabled(index + 1 != ANPV::FileOperation::Delete);
    });
    connect(ui->comboBox_3, &QComboBox::currentIndexChanged, this, [&](int index)
    {
        ui->lineEdit_3->setEnabled(index + 1 != ANPV::FileOperation::Delete);
        ui->pushButton_3->setEnabled(index + 1 != ANPV::FileOperation::Delete);
    });
    connect(ui->comboBox_4, &QComboBox::currentIndexChanged, this, [&](int index)
    {
        ui->lineEdit_4->setEnabled(index + 1 != ANPV::FileOperation::Delete);
        ui->pushButton_4->setEnabled(index + 1 != ANPV::FileOperation::Delete);
    });

    this->fillDiag();
}

FileOperationConfigDialog::~FileOperationConfigDialog()
{
    delete ui;
}

ANPV::FileOperation FileOperationConfigDialog::operationFromAction(QAction *a)
{
    QString text = a->text();
    QStringList words = text.split(QStringLiteral(" "));

    if(words.empty())
    {
        throw std::logic_error("This should never happen: words empty!");
    }

    QMetaEnum metaEnumOperation = QMetaEnum::fromType<ANPV::FileOperation>();
    bool ok;
    auto result = static_cast<ANPV::FileOperation>(metaEnumOperation.keyToValue(words[0].remove('&').toLatin1().constData(), &ok));

    if(!ok)
    {
        throw std::runtime_error("Unable to determine FileOperation type");
    }

    return result;
}

void FileOperationConfigDialog::fillDiag()
{
    QMetaEnum metaEnumOperation = QMetaEnum::fromType<ANPV::FileOperation>();
    QList<QAction *> actions = fileOperationActionGroup->actions();

    auto uiBuilder = [&](QAction * action, QComboBox * comboBox, QKeySequenceEdit * seqEdit, QLineEdit * lineEdit)
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
            try
            {
                comboBox->setCurrentText(metaEnumOperation.valueToKey(this->operationFromAction(action)));
            }
            catch(const std::runtime_error &e)
            {
                comboBox->setCurrentIndex(-1);
                comboBox->setCurrentText("unknown");
            }
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
    auto actionBuilder = [&](QComboBox * comboBox, QKeySequenceEdit * seqEdit, QLineEdit * lineEdit)
    {
        QAction *action = nullptr;

        QString title = comboBox->currentText();
        bool isDeleteAction = title == QMetaEnum::fromType<ANPV::FileOperation>().valueToKey(ANPV::FileOperation::Delete);
        QString targetDir = lineEdit->text();
        QFileInfo targetDirInfo = QFileInfo(targetDir);

        if(isDeleteAction || targetDirInfo.isDir())
        {
            QKeySequence seq = seqEdit->keySequence();

            if(!isDeleteAction)
            {
                title += " to ";
                title += targetDir;
            }
            else
            {
                title += " to trash";
            }

            action = new QAction(ANPV::globalInstance());
            action->setText(title);
            action->setShortcut(seq);
            action->setData(targetDir);
            action->setShortcutContext(Qt::WidgetShortcut);

            if(isDeleteAction)
            {
                action->setIcon(QIcon::fromTheme("edit-delete"));
            }

            return action;
        }

        return action;
    };

    QList<QAction *> actions = fileOperationActionGroup->actions();

    for(int i = 0; i < actions.size(); i++)
    {
        fileOperationActionGroup->removeAction(actions[i]);
        actions[i]->deleteLater();
    }

    QAction *a;
    a = actionBuilder(ui->comboBox, ui->keySequenceEdit, ui->lineEdit);

    if(a)
    {
        this->fileOperationActionGroup->addAction(a);
    }

    a = actionBuilder(ui->comboBox_2, ui->keySequenceEdit_2, ui->lineEdit_2);

    if(a)
    {
        this->fileOperationActionGroup->addAction(a);
    }

    a = actionBuilder(ui->comboBox_3, ui->keySequenceEdit_3, ui->lineEdit_3);

    if(a)
    {
        this->fileOperationActionGroup->addAction(a);
    }

    a = actionBuilder(ui->comboBox_4, ui->keySequenceEdit_4, ui->lineEdit_4);

    if(a)
    {
        this->fileOperationActionGroup->addAction(a);
    }

    this->QDialog::accept();
}

void FileOperationConfigDialog::onBrowseClicked(QLineEdit *lineEdit)
{
    QString dirToOpen = lineEdit->text();
    QString dir = ANPV::globalInstance()->getExistingDirectory(this, dirToOpen);

    if(dir.isEmpty())
    {
        return;
    }

    lineEdit->setText(dir);
}
