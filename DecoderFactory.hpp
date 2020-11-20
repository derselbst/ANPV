
#pragma once

#include <QString>
#include <memory>

class SmartImageDecoder;
class DocumentView;

class DecoderFactory
{

public:
    DecoderFactory() = delete;

    static std::unique_ptr<SmartImageDecoder> load(QString file, DocumentView*);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
