#pragma once

#include <QApplication>
#include <QCursor>

class WaitCursor
{
public:
    WaitCursor()
    {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    }
    WaitCursor(const WaitCursor &) = delete;
    WaitCursor(WaitCursor &&) = delete;
    ~WaitCursor()
    {
        QApplication::restoreOverrideCursor();
    }
};
