
#pragma once

#include <cstdint>

enum class DecodingState : uint32_t
{
    // Decoder is idle, nothing has been decoded yet
    Ready,
    
    // Metadata is available, something like dimensions of the image and EXIF data (if any) are known at this stage
    // a low resolution thumbnail may also be available
    Metadata,
    
    // This state is selected when a preview image at full resolution can be obtained.
    // Even if many parts are still missing (displayed in black).
    // The preview might have a low degree of detail though.
    // Think of partly decoded progressive JPEGs, etc.
    // This state may be triggered more than once.
    PreviewImage,
    
    // Decoding has finished successfully, the full resolution image is now available.
    FullImage,
    
    // The decoding process has failed.
    Error,
    
    // The decoding was cancelled by the user
    Cancelled
}; 
