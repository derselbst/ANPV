
#include "LibRawHelper.hpp"

#include "Formatter.hpp"
#include "rawfiles.h"

// libraw basically include the entire Windoof API, messing up any C++ source code that uses std::min
#include <libraw.h>
#include <libraw_version.h>

#ifdef LIBRAW_HAS_CONFIG
#include <libraw_config.h>
#endif

static const QStringList rawFiles = QString::fromLatin1(raw_file_extentions).remove(QLatin1String("*.")).split(QLatin1Char(' '));

void LibRawHelper::extractThumbnail(QByteArray& encodedThumbnailOut, const void* fileBuf, qint64 buflen)
{
	LibRaw raw;
            
	int ret = raw.open_buffer(const_cast<void*>(fileBuf), buflen);
	if (ret != LIBRAW_SUCCESS)
	{
		throw std::runtime_error(Formatter () << "LibRaw: failed to run open_buffer: " << libraw_strerror(ret));
	}

	auto loadEmbeddedPreview = [](QByteArray& imgData, LibRaw& raw)
	{
		int ret = raw.unpack_thumb();

		if (ret != LIBRAW_SUCCESS)
		{
			throw std::runtime_error(Formatter() << "LibRaw: failed to run unpack_thumb: " << libraw_strerror(ret));
		}

		libraw_processed_image_t* const thumb = raw.dcraw_make_mem_thumb(&ret);
		if (!thumb)
		{
			throw std::runtime_error(Formatter() << "LibRaw: failed to run dcraw_make_mem_thumb: " << libraw_strerror(ret));
		}

		if (thumb->type == LIBRAW_IMAGE_JPEG)
		{
			imgData = QByteArray((const char*)thumb->data, (int)thumb->data_size);
		}
		else
		{
			raw.dcraw_clear_mem(thumb);
			throw std::runtime_error("LibRaw returned a non-JPEG thumbnail, which is currently not supported, sry.");
		}

		raw.dcraw_clear_mem(thumb);
		raw.recycle();

		if (imgData.isEmpty())
		{
			throw std::runtime_error(Formatter() << "JPEG thumb from LibRaw is empty!");
		}
	};

	loadEmbeddedPreview(encodedThumbnailOut, raw);
}

const QStringList& LibRawHelper::rawFilesList()
{
    return rawFiles;
}

