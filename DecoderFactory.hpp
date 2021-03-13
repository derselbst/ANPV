
#pragma once

#include <QObject>
#include <QFileInfo>
#include <QSharedPointer>
#include <memory>
#include "DecodingState.hpp"

class SmartImageDecoder;
class DocumentView;

class DecoderFactory
{
public:
    static DecoderFactory* globalInstance();

    ~DecoderFactory();
    QSharedPointer<SmartImageDecoder> getDecoder(const QFileInfo& url);
    bool hasCR2Header(const QFileInfo& url);

private:
    DecoderFactory();
};
