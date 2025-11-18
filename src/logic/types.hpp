
#pragma once

#include <utility>

#include <QSharedPointer>

enum class ViewMode : int
{
    Unknown,
    None,
    Fit,
};

using ViewFlags_t = unsigned int;
enum class ViewFlag : ViewFlags_t
{
    None = 0,
    CombineRawJpg = 1 << 0,
    ShowAfPoints =  1 << 1,
    RespectExifOrientation = 1 << 2,
    CenterAf = 1 << 3,
    ShowScrollBars = 1 << 4,
    PeriodicBoundary = 1 << 5,
};

class Image;
class SmartImageDecoder;

enum class ListItemType
{
    Image,
    Section
};

enum class SortField
{
    None, // only used for sections
    FileName,
    FileSize,
    DateModified,
    FileType,
    Resolution,
    DateRecorded,
    Aperture,
    Exposure,
    Iso,
    FocalLength,
    Lens,
    CameraModel,
    Last
};

