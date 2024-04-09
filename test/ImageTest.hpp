
#pragma once

#include <QObject>

class ImageTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testIconHeight();
    void testIconForNonExistingFile();
};
