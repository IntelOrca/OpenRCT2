/*****************************************************************************
 * Copyright (c) 2014-2021 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Gx.h"

using namespace OpenRCT2;

Gx Gx::FromStream(IStream& stream)
{
    Gx gx;

    // Load header
    auto numImages = stream.ReadValue<uint32_t>();
    auto dataSize = stream.ReadValue<uint32_t>();

    // Load image headers
    gx._elements.resize(numImages);
    for (size_t i = 0; i < numImages; i++)
    {
        auto src = stream.ReadValue<rct_g1_element_32bit>();
        auto& dst = gx._elements[i];
        dst.offset = reinterpret_cast<uint8_t*>(src.offset);
        dst.width = src.width;
        dst.height = src.height;
        dst.x_offset = src.x_offset;
        dst.y_offset = src.y_offset;
        dst.flags = src.flags;
        dst.zoomed_offset = src.zoomed_offset;
    }

    // Load image data
    gx._data.resize(dataSize);
    stream.Read(gx._data.data(), dataSize);

    // Fix image data pointers
    auto dataStart = reinterpret_cast<uintptr_t>(gx._data.data());
    for (size_t i = 0; i < numImages; i++)
    {
        auto& dst = gx._elements[i];
        dst.offset += dataStart;
    }

    return gx;
}

size_t Gx::GetCount() const
{
    return _elements.size();
}

static rct_g1_element DuplicateImageData(const rct_g1_element& src)
{
    rct_g1_element dst = src;
    auto size = g1_calculate_data_size(&src);
    dst.offset = new uint8_t[size];
    std::memcpy(dst.offset, src.offset, size);
    return dst;
}

const rct_g1_element* Gx::GetImage(size_t i) const
{
    const rct_g1_element* result{};
    if (i < _elements.size())
    {
        result = &_elements[i];
    }
    return result;
}

rct_g1_element Gx::GetImageCopy(size_t i) const
{
    rct_g1_element result{};
    if (i < _elements.size())
    {
        result = DuplicateImageData(_elements[i]);
    }
    return result;
}
