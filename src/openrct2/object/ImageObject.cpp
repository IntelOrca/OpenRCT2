/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ImageObject.h"

#include "../core/Json.hpp"

using namespace OpenRCT2;

void ImageObject::Load()
{
    _baseImageIndex = LoadImages();
}

void ImageObject::Unload()
{
    UnloadImages();
    _baseImageIndex = ImageIndexUndefined;
}

void ImageObject::ReadJson(IReadObjectContext* context, json_t& root)
{
    Guard::Assert(root.is_object(), "ImageObject::ReadJson expects parameter root to be object");
    PopulateTablesFromJson(context, root);
}

ImageIndex ImageObject::GetImage(uint32_t index) const
{
    if (_baseImageIndex == ImageIndexUndefined)
        return ImageIndexUndefined;
    return _baseImageIndex + index;
}
