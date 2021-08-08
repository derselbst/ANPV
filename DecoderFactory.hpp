
#pragma once

#include <QObject>
#include <QFileInfo>
#include <QSharedPointer>
#include <memory>
#include "DecodingState.hpp"

class SmartImageDecoder;
class Image;
class DocumentView;

enum Priority : int
{
    Background = -1,
    Normal = 0,
    Important = 1,
};

class DecoderFactory
{
public:
    static DecoderFactory* globalInstance();

    ~DecoderFactory();
    bool hasCR2Header(const QFileInfo& url);
    QSharedPointer<class Image> makeImage(const QFileInfo& url);
    QSharedPointer<SmartImageDecoder> getDecoder(QSharedPointer<class Image> image);

private:
    DecoderFactory();
};
