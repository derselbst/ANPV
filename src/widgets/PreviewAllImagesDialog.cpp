
#include "PreviewAllImagesDialog.hpp"
#include "ui_PreviewAllImagesDialog.h"
#include "ANPV.hpp"

#include <QString>

struct PreviewAllImagesDialog::Impl
{
    std::unique_ptr<Ui::PreviewAllImagesDialog> ui = std::make_unique<Ui::PreviewAllImagesDialog>();

};

PreviewAllImagesDialog::PreviewAllImagesDialog(QWidget *parent, Qt::WindowFlags f) : QDialog(parent, f), d(std::make_unique<Impl>())
{
    d->ui->setupUi(this);
    d->ui->icon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning")).pixmap(QSize(50, 50)));
}

PreviewAllImagesDialog::~PreviewAllImagesDialog()
{
}

void PreviewAllImagesDialog::setImageHeight(int h)
{
    d->ui->spinBoxImageHeight->setValue(h);
}

int PreviewAllImagesDialog::imageHeight()
{
    return d->ui->spinBoxImageHeight->value();
}
