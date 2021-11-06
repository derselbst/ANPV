#pragma once

#include <QDialog>


namespace Ui
{
    class FileOperationConfigDialog;
}

class QActionGroup;
class QLineEdit;

class FileOperationConfigDialog : public QDialog
{
    Q_OBJECT

    public:
    explicit FileOperationConfigDialog(QActionGroup* fileOperationActionGroup, QWidget *parent = nullptr);
    ~FileOperationConfigDialog() override;

    void accept() override;
    
private:
    Ui::FileOperationConfigDialog *ui = nullptr;
    QActionGroup* fileOperationActionGroup;
    
    void fillDiag();
    void onBrowseClicked(QLineEdit* lineEdit);
};
