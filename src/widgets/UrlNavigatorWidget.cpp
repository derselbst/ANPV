/* Copyright (C) 2022 Martin Pietsch <@pmfoss>
   SPDX-License-Identifier: BSD-3-Clause */
/* Modified by derselbst for ANPV */

#include "UrlNavigatorWidget.hpp"
#include "ANPV.hpp"

#include <QFileSystemModel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStringList>
#include <QTreeView>
#include <QWidget>

#include <QDebug>

struct UrlNavigatorWidget::Impl
{
    UrlNavigatorWidget *q = nullptr;

    /* Initializes all components of the combobox (TreeView, FileSystemModel). */
    void initComboBox()
    {
        this->iCurrentHistoryIndex = -1;
        q->setEditable(true);
        q->setMaxVisibleItems(20);

        this->fsModel = ANPV::globalInstance()->dirModel();
        q->setModel(this->fsModel);

        this->tvView = new QTreeView(q);
        q->setView(this->tvView);
        this->tvView->setSelectionMode(QAbstractItemView::SingleSelection);
        this->tvView->setHeaderHidden(true);
        this->tvView->showColumn(0);
        this->tvView->hideColumn(1);
        this->tvView->hideColumn(2);
        this->tvView->hideColumn(3);
        this->tvView->viewport()->installEventFilter(q);
        q->setRootModelIndex(this->fsModel->index(QDir::rootPath()));

        this->bSkipNextHide = false;
        this->slHistory.clear();
        q->connect(q, &UrlNavigatorWidget::currentIndexChanged, q, &UrlNavigatorWidget::onIndexChanged);
        q->connect(ANPV::globalInstance(), &ANPV::currentDirChanged, q, &UrlNavigatorWidget::setPath);
    }

    QFileSystemModel *fsModel;
    /* Treeview for the popup widget */
    QTreeView *tvView;
    /* prevention of premature closing of the popup widget */
    bool bSkipNextHide;
    /* history list */
    QStringList slHistory;
    /* current index of the history list */
    int iCurrentHistoryIndex;
};


/* class UrlNavigatorWidget */

/* Constructs a combobox for selecting a file system path. */
UrlNavigatorWidget::UrlNavigatorWidget(QWidget *parent)
    : QComboBox(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->initComboBox();
}

/* Constructs a combobox for selecting a file system path with a given file system path (path). */
UrlNavigatorWidget::UrlNavigatorWidget(const QString &path, QWidget *parent)
    : QComboBox(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->initComboBox();
    this->setPath(path);
}

UrlNavigatorWidget::~UrlNavigatorWidget() = default;

/* Sets the path (path) to the combo box and emits the signal pathChanged. If the path is not from the history operations
   (fromhistory == false) the history is rewound to the current history index. */
void UrlNavigatorWidget::navigateTo(const QString &path)
{
    emit this->pathChangedByUser(path);
}

/* Sets the path (newpath) to be used. */
void UrlNavigatorWidget::setPath(const QString &newpath)
{
    this->setCurrentText(newpath);
    QModelIndex curDirIdx = d->fsModel->index(newpath);
    d->tvView->setCurrentIndex(curDirIdx);
    d->tvView->scrollTo(curDirIdx, QAbstractItemView::PositionAtCenter);
}

/* Returns the currently used path. */
QString UrlNavigatorWidget::getPath() const
{
    return this->currentText();
}

/* Filters the Enter- and Return-KeyPressed event for clearing the objects focus. */
void UrlNavigatorWidget::keyPressEvent(QKeyEvent *keyEvent)
{
    switch(keyEvent->key())
    {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        this->clearFocus();
        this->navigateTo(this->currentText());
        keyEvent->accept();
        return;

    case Qt::Key_Escape:
        this->setCurrentText(d->fsModel->filePath(d->tvView->currentIndex()));
        keyEvent->accept();
        return;
    }

    QComboBox::keyPressEvent(keyEvent);
}

/* Filters the MouseButtonPress event on the viewport of the treeview object
   for preventing premature closing of the popup widget. */
bool UrlNavigatorWidget::eventFilter(QObject *object, QEvent *event)
{
    if(event->type() == QEvent::MouseButtonPress && object == view()->viewport())
    {
        QMouseEvent &mouseEvent = dynamic_cast<QMouseEvent &>(*event);
        QModelIndex index = d->tvView->indexAt(mouseEvent.pos());

        if(!d->tvView->visualRect(index).contains(mouseEvent.pos()))
        {
            d->bSkipNextHide = true;
        }
    }

    return false;
}

/* Prepare the treeview object with expanding items to the currently used path
   and popup the treeview. */
void UrlNavigatorWidget::showPopup()
{
    d->tvView->collapseAll();

    QDir expdir = this->currentText();

    do
    {
        d->tvView->setExpanded(d->fsModel->index(expdir.absolutePath()), true);
    }
    while(expdir.cdUp());

    QComboBox::showPopup();

    QModelIndex curDirIdx = d->fsModel->index(this->currentText());
    d->tvView->setCurrentIndex(curDirIdx);
    d->tvView->scrollTo(curDirIdx, QAbstractItemView::PositionAtCenter);

}

/* Prevent the hiding of the popup widget, when an treeview item is clicked. */
void UrlNavigatorWidget::hidePopup()
{
    if(d->bSkipNextHide)
    {
        d->bSkipNextHide = false;
    }
    else
    {
        QComboBox::hidePopup();
    }
}

/* Sets the selected path to the combobox. */
void UrlNavigatorWidget::onIndexChanged(int index)
{
    Q_UNUSED(index)

    this->navigateTo(d->fsModel->filePath(d->tvView->currentIndex()));
}
