/*****************************************************************************
 * Copyright (c) 2014-2021 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../core/IStream.hpp"
#include "../drawing/Drawing.h"

namespace OpenRCT2
{
    class Gx
    {
    private:
        std::vector<rct_g1_element> _elements;
        std::vector<uint8_t> _data;

    public:
        static Gx FromStream(IStream& stream);

        size_t GetCount() const;
        const rct_g1_element* GetImage(size_t i) const;
        rct_g1_element GetImageCopy(size_t i) const;
    };
} // namespace OpenRCT2
