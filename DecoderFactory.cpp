
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"

#include <QFile>



std::unique_ptr<SmartImageDecoder> DecoderFactory::load(QString url)
{
    return std::make_unique<SmartJpegDecoder>(std::move(url));
}
