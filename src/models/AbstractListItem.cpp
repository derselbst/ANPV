
#include "AbstractListItem.hpp"

#include <QPromise>
#include <QFileInfo>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QDir>

// #include <execution>
#include <algorithm>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstring> // for strverscmp()

#ifdef _WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#endif

#include "SmartImageDecoder.hpp"
#include "DecoderFactory.hpp"
#include "UserCancellation.hpp"
#include "Formatter.hpp"
#include "ExifWrapper.hpp"
#include "xThreadGuard.hpp"
#include "WaitCursor.hpp"
#include "Image.hpp"
#include "ANPV.hpp"
#include "ProgressIndicatorHelper.hpp"

struct AbstractListItem::Impl
{
    ListItemType type;
};

AbstractListItem::~AbstractListItem() = default;

AbstractListItem::AbstractListItem(ListItemType type)
   : d(std::make_unique<Impl>())
{
    d->type = type;
}

ListItemType AbstractListItem::getType() const
{
    return d->type;
}
