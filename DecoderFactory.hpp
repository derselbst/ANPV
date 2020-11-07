
#pragma once

#include <QUrl>
#include <memory>

class SmartImageDecoder;
class DocumentController;

class DecoderFactory
{

public:
    DecoderFactory() = delete;

    static std::unique_ptr<SmartImageDecoder> load(QString file, DocumentController*);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
