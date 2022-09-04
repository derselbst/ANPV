
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
    std::unique_ptr<SmartImageDecoder> getDecoder(const QSharedPointer<Image>& image);
    std::unique_ptr<SmartImageDecoder> getDecoder(const QSharedPointer<Image>& image, const QString& formatHint);

private:
    DecoderFactory();
};
