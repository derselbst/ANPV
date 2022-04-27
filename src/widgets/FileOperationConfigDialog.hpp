#pragma once

#include <QDialog>
#include "ANPV.hpp"


namespace Ui
{
    class FileOperationConfigDialog;
}

class QActionGroup;
class QLineEdit;
class QAction;

class FileOperationConfigDialog : public QDialog
{
    Q_OBJECT

    public:
    explicit FileOperationConfigDialog(QActionGroup* fileOperationActionGroup, QWidget *parent = nullptr);
    ~FileOperationConfigDialog() override;

    static ANPV::FileOperation operationFromAction(QAction*);
    void accept() override;
    
private:
    Ui::FileOperationConfigDialog *ui = nullptr;
    QActionGroup* fileOperationActionGroup;
    
    void fillDiag();
    void onBrowseClicked(QLineEdit* lineEdit);
};
