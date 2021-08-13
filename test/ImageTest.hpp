
#pragma once

#include <QObject>

class ImageTest : public QObject
{
    Q_OBJECT
private slots:
    void testRawImageHasSilblings();
    void testRawImageHasNoSilblings();
    void testIconHeight();
};
