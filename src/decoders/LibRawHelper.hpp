
#pragma once

#include <QByteArray>

class LibRawHelper
{
public:
    LibRawHelper() = delete;
	
    static void extractThumbnail(QByteArray& encodedThumbnailOut, const void* fileBuf, qint64 buflen);
};
