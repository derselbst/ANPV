
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"
#include "SmartTiffDecoder.hpp"
#include "DocumentView.hpp"

#include <KDCRAW/KDcraw>
#include <QFile>
#include <QImageReader>
#include <QDebug>


DecoderFactory::DecoderFactory() = default;

DecoderFactory::~DecoderFactory() = default;

DecoderFactory* DecoderFactory::globalInstance()
{
    static DecoderFactory fac;
    return &fac;
}

QSharedPointer<SmartImageDecoder> DecoderFactory::getDecoder(const QFileInfo& url)
{
    const QByteArray formatHint = url.fileName().section(QLatin1Char('.'), -1).toLocal8Bit().toLower();
    
    QSharedPointer<SmartImageDecoder> sid(nullptr, &QObject::deleteLater);
    QImageReader r(url.absoluteFilePath());
    
    if(KDcrawIface::KDcraw::rawFilesList().contains(QString::fromLatin1(formatHint)))
    {
        QByteArray previewData;

        // use KDcraw for getting the embedded preview
        bool ret = KDcrawIface::KDcraw::loadEmbeddedPreview(previewData, url.absoluteFilePath());

        if (!ret)
        {
            // if the embedded preview loading failed, load half preview instead.
            // That's slower but it works even for images containing
            // small (160x120px) or none embedded preview.
            if (!KDcrawIface::KDcraw::loadHalfPreview(previewData, url.absoluteFilePath()))
            {
                qWarning() << "unable to get half preview for " << url.absoluteFilePath();
                return nullptr;
            }
        }
        
        sid.reset(new SmartJpegDecoder(url, previewData));
    }
    else if(r.format() == "tiff")
    {
        sid.reset(new SmartTiffDecoder(url));
    }
    else if(r.format() == "jpeg")
    {
        sid.reset(new SmartJpegDecoder(url));
    }
    
    return sid;
}

