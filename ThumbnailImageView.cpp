#include "ThumbnailImageView.hpp"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFlags>
#include <QTimer>
#include <QGraphicsPixmapItem>
#include <QWindow>
#include <QGuiApplication>
#include <QThreadPool>
#include <QDebug>
#include <QListView>
#include <QMainWindow>
#include <QFileSystemModel>
#include <QString>
#include <QTreeView>
#include <QDockWidget>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QWheelEvent>

#include <vector>
#include <algorithm>
#include <cmath>

#include "AfPointOverlay.hpp"
#include "SmartImageDecoder.hpp"
#include "ImageDecodeTask.hpp"
#include "ANPV.hpp"
#include "DecoderFactory.hpp"
#include "ExifWrapper.hpp"
#include "MessageWidget.hpp"
#include "SortedImageModel.hpp"
#include "MoveFileCommand.hpp"

ThumbnailImageView::ThumbnailImageView(QWidget *parent)
 : QListView(parent)
{
    this->setViewMode(QListView::IconMode);
    this->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setResizeMode(QListView::Adjust);
    this->setWordWrap(true);
    this->setWrapping(true);
    this->setSpacing(2);
    this->setContextMenuPolicy(Qt::ActionsContextMenu);
}

ThumbnailImageView::~ThumbnailImageView() = default;

void ThumbnailImageView::setModel(SortedImageModel* model)
{
    this->model = model;
    this->QListView::setModel(model);
}

void ThumbnailImageView::wheelEvent(QWheelEvent *event)
{
    auto angleDelta = event->angleDelta();

    if (event->modifiers() & Qt::ControlModifier)
    {
        double s = this->model->iconHeight();
        // zoom
        if(angleDelta.y() > 0)
        {
            this->model->setIconHeight(static_cast<int>(std::ceil(s * 1.2)));
            event->accept();
            return;
        }
        else if(angleDelta.y() < 0)
        {
            this->model->setIconHeight(static_cast<int>(std::floor(s / 1.2)));
            event->accept();
            return;
        }
    }

    QListView::wheelEvent(event);
}
