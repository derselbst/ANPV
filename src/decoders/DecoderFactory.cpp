
#include "DecoderFactory.hpp"

#include "SmartImageDecoder.hpp"
#include "SmartJpegDecoder.hpp"
#include "SmartPngDecoder.hpp"
#include "SmartTiffDecoder.hpp"
#include "DocumentView.hpp"
#include "Image.hpp"

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

std::unique_ptr<SmartImageDecoder> DecoderFactory::getDecoder(QSharedPointer<Image> image)
{
    const QFileInfo& info = image->fileInfo();
    if(info.isFile())
    {
        const QString extension = image->fileInfo().fileName().section(QLatin1Char('.'), -1).toLower();

        QByteArray format;
        if (extension.isEmpty())
        {
            qInfo() << "Could not determine file extension for file " << image->fileInfo().fileName();
            QImageReader r(info.absoluteFilePath());
            format = r.format();
            qDebug() << "Determined format " << format << " for file " << image->fileInfo().fileName();
        }
        else
        {
            format = extension.toLocal8Bit();
        }

        if(image->isRaw() || format == "jpeg" || format == "jpg")
        {
            return std::make_unique<SmartJpegDecoder>(image);
        }
        else if (format == "tiff" || format == "tif")
        {
            return std::make_unique<SmartTiffDecoder>(image);
        }
        else if (format == "png")
        {
            return std::make_unique<SmartPngDecoder>(image);
        }
    }
    
    return nullptr;
}

