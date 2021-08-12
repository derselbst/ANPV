
#pragma once

#include <QObject>
#include <QFileInfo>
#include <QSharedPointer>
#include <memory>
#include "DecodingState.hpp"

class SmartImageDecoder;
class Image;
class DocumentView;

class DecoderFactory
{
public:
    static DecoderFactory* globalInstance();

    ~DecoderFactory();
    bool hasCR2Header(const QFileInfo& url);
    QSharedPointer<Image> makeImage(const QFileInfo& url);
    QSharedPointer<SmartImageDecoder> getDecoder(QSharedPointer<Image> image);

private:
    DecoderFactory();
};
