
#pragma once

#include <QObject>

class ImageTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testRawImageHasSilblings();
    void testRawImageHasNoSilblings();
    void testIconHeight();
    void testIconForNonExistingFile();
};
