
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"
#include "SmartTiffDecoder.hpp"
#include "DocumentController.hpp"

#include <QFile>



std::unique_ptr<SmartImageDecoder> DecoderFactory::load(QString url, DocumentController* dc)
{
    auto sid = std::make_unique<SmartTiffDecoder>(std::move(url));
    
    QObject::connect(sid.get(), &SmartImageDecoder::decodingStateChanged, dc, &DocumentController::onDecodingStateChanged);
    QObject::connect(sid.get(), &SmartImageDecoder::decodingProgress, dc, &DocumentController::onDecodingProgress);
    QObject::connect(sid.get(), &SmartImageDecoder::imageRefined, dc, &DocumentController::onImageRefinement);
    
    return std::move(sid);
}
