
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"
#include "SmartTiffDecoder.hpp"
#include "DocumentView.hpp"
#include "Image.hpp"

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

bool DecoderFactory::hasCR2Header(const QFileInfo& url)
{
    QFile file(url.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    
    char b[12];
    QDataStream istream(&file);
    if(istream.readRawData(b, sizeof(b) / sizeof(*b)) == -1)
    {
        return false;
    }
    
    // endian access switcher for 16bit words
    int e16;
    if(b[0] == 'I' && b[1] == b[0])
    {
        // Intel byte order (little endian)
        e16 = 0;
    }
    else if(b[0] == 'M' && b[1] == b[0])
    {
        // Motorola byte order (big endian)
        e16 = 1;
    }
    else
    {
        return false;
    }
    
    if( b[2 ^ e16] == 0x2A &&
        b[3 ^ e16] == 0x00 &&
        b[8 ^ e16] == 'C' &&
        b[9 ^ e16] == 'R' &&
        b[10 ^ e16] == 0x02 &&
        b[11 ^ e16] == 0x00)
    {
        return true;
    }
    
    return false;
}

QSharedPointer<Image> DecoderFactory::makeImage(const QFileInfo& url)
{
    return QSharedPointer<Image> (new Image(url), &QObject::deleteLater);
}

QSharedPointer<SmartImageDecoder> DecoderFactory::getDecoder(QSharedPointer<Image> image)
{
    const QFileInfo& info = image->fileInfo();
    if(info.isFile())
    {
        QImageReader r(info.absoluteFilePath());
        
        if(image->isRaw())
        {
            QByteArray previewData;

            // use KDcraw for getting the embedded preview
            bool ret = KDcrawIface::KDcraw::loadEmbeddedPreview(previewData, info.absoluteFilePath());

            if (!ret)
            {
                // if the embedded preview loading failed, load half preview instead.
                // That's slower but it works even for images containing
                // small (160x120px) or none embedded preview.
                if (!KDcrawIface::KDcraw::loadHalfPreview(previewData, info.absoluteFilePath()))
                {
                    qWarning() << "unable to get half preview for " << info.absoluteFilePath();
                    return nullptr;
                }
            }
            
            return QSharedPointer<SmartImageDecoder> (new SmartJpegDecoder(image, previewData), &QObject::deleteLater);
        }
        else if(r.format() == "tiff")
        {
            return QSharedPointer<SmartImageDecoder> (new SmartTiffDecoder(image), &QObject::deleteLater);
        }
        else if(r.format() == "jpeg")
        {
            return QSharedPointer<SmartImageDecoder> (new SmartJpegDecoder(image), &QObject::deleteLater);
        }
    }
    
    return nullptr;
}
