
#pragma once

#include <QObject>
#include <QFileInfo>
#include <QSharedPointer>
#include <memory>
#include "DecodingState.hpp"

class SmartImageDecoder;
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
    QSharedPointer<SmartImageDecoder> getDecoder(const QFileInfo& url);
    bool hasCR2Header(const QFileInfo& url);

private:
    DecoderFactory();
};
