
#pragma once

#include <QByteArray>
#include <QStringList>

class LibRawHelper
{
public:
    LibRawHelper() = delete;
	
    static const QStringList& rawFilesList();
    static void extractThumbnail(QByteArray& encodedThumbnailOut, const void* fileBuf, qint64 buflen);
};
