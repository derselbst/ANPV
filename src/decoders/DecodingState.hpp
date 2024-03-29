
#pragma once

#include <cstdint>
#include <QObject>

enum DecodingState : quint32
{
    // No decoder has been assigned yet
    Unknown,

    // Decoder is assigned and idle, nothing has been decoded yet
    Ready,

    // Metadata is available, something like dimensions of the image and EXIF data (if any) are known at this stage
    // a low resolution thumbnail may also be available
    Metadata,

    // This state is selected when a preview image at potentially lower resolution is available.
    // Even if many parts are still missing (displayed in black) or only a ROI has been decoded.
    // The preview might have a low degree of detail though.
    // Think of partly decoded progressive JPEGs, etc.
    // This state may be triggered more than once.
    PreviewImage,

    // Decoding has finished successfully, the full resolution image has been decoded and is now available.
    FullImage,

    // The decoding process has failed; metadata could be retrieved but file is broken or something.
    Error,

    // Fatal error occurred before we could do anything, next state will be ready
    Fatal,

    // The decoding was cancelled by the user
    Cancelled
};

