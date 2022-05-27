/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../drawing/Drawing.h"
#include "../world/Water.h"
#include "Object.h"

#include <tuple>
#include <vector>

class WaterObject final : public Object
{
private:
    std::vector<rct_g1_element> _palettes{};
    rct_water_type _legacyType = {};

public:
    ~WaterObject();

    void* GetLegacyData() override
    {
        return &_legacyType;
    }

    void ReadJson(IReadObjectContext* context, json_t& root) override;
    void ReadLegacy(IReadObjectContext* context, OpenRCT2::IStream* stream) override;
    void Load() override;
    void Unload() override;

    void DrawPreview(rct_drawpixelinfo* dpi, int32_t width, int32_t height) const override;

private:
    void ReadJsonPalette(json_t& jPalette);
    uint32_t ParseColour(const std::string& s) const;
};
