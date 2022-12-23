
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
};

class Image;
class SmartImageDecoder;
using Entry_t = std::pair<QSharedPointer<Image>, QSharedPointer<SmartImageDecoder>>;

enum ItemModelUserRoles
{
    CheckAlignmentRole = Qt::UserRole,
    DecorationAlignmentRole,
};

enum ListItemType
{
    ImageItem,
    SectionItem
};
