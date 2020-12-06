#pragma once

#include <QDialog>


namespace Ui
{
    class FileOperationConfig;
}

class QActionGroup;
class QLineEdit;
class ANPV;

class FileOperationConfig : public QDialog
{
    Q_OBJECT

    public:
    explicit FileOperationConfig(QActionGroup* fileOperationActionGroup, ANPV *parent = nullptr);
    ~FileOperationConfig();

    void accept() override;
    
private:
    Ui::FileOperationConfig *ui = nullptr;
    ANPV* anpv = nullptr;
    QActionGroup* fileOperationActionGroup;
    
    void fillDiag();
    void onBrowseClicked(QLineEdit* lineEdit);
};
