
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"
#include "SmartTiffDecoder.hpp"
#include "DocumentView.hpp"

#include <QFile>
#include <QImageReader>



std::unique_ptr<SmartImageDecoder> DecoderFactory::load(QString url, DocumentView* dc)
{
    QImageReader r(url);
    
    std::unique_ptr<SmartImageDecoder> sid;
    if(r.format() == "tiff")
    {
        sid = std::make_unique<SmartTiffDecoder>(std::move(url));
    }
    else if(r.format() == "jpeg")
    {
        sid = std::make_unique<SmartJpegDecoder>(std::move(url));
    }
    
    QObject::connect(sid.get(), &SmartImageDecoder::decodingStateChanged, dc, &DocumentView::onDecodingStateChanged);
    QObject::connect(sid.get(), &SmartImageDecoder::decodingProgress, dc, &DocumentView::onDecodingProgress);
    QObject::connect(sid.get(), &SmartImageDecoder::imageRefined, dc, &DocumentView::onImageRefinement);
    
    return std::move(sid);
}
