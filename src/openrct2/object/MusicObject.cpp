/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma warning(disable : 4706) // assignment within conditional expression

#include "MusicObject.h"

#include "../Context.h"
#include "../OpenRCT2.h"
#include "../PlatformEnvironment.h"
#include "../core/IStream.hpp"
#include "../core/Path.hpp"
#include "../localisation/Language.h"
#include "../localisation/StringIds.h"
#include "../ride/Ride.h"
#include "ObjectJsonHelpers.h"

#include <memory>

using namespace OpenRCT2;

void MusicObject::Load()
{
    GetStringTable().Sort();
    NameStringId = language_allocate_object_string(GetName());
}

void MusicObject::Unload()
{
    language_free_object_string(NameStringId);
    NameStringId = 0;
}

void MusicObject::DrawPreview(rct_drawpixelinfo* dpi, int32_t width, int32_t height) const
{
    // Write (no image)
    int32_t x = width / 2;
    int32_t y = height / 2;
    gfx_draw_string_centred(dpi, STR_WINDOW_NO_IMAGE, x, y, COLOUR_BLACK, nullptr);
}

void MusicObject::ReadJson(IReadObjectContext* context, const json_t* root)
{
    _rideTypes.clear();
    _tracks.clear();

    auto properties = json_object_get(root, "properties");
    if (properties != nullptr)
    {
        auto jRideTypes = json_object_get(properties, "rideTypes");
        if (json_is_array(jRideTypes))
        {
            ParseRideTypes(jRideTypes);
        }

        auto jTracks = json_object_get(properties, "tracks");
        if (json_is_array(jTracks))
        {
            ParseTracks(*context, jTracks);
        }
    }

    ObjectJsonHelpers::LoadStrings(root, GetStringTable());
}

void MusicObject::ParseRideTypes(const json_t* jRideTypes)
{
    size_t index;
    const json_t* jRideType;
    json_array_foreach(jRideTypes, index, jRideType)
    {
        auto szRideType = json_string_value(jRideType);
        if (szRideType != nullptr)
        {
            auto rideType = ObjectJsonHelpers::ParseRideType(szRideType);
            if (rideType != RIDE_TYPE_NULL)
            {
                _rideTypes.push_back(rideType);
            }
        }
    }
}

void MusicObject::ParseTracks(IReadObjectContext& context, const json_t* jTracks)
{
    size_t index;
    const json_t* jTrack;
    json_array_foreach(jTracks, index, jTrack)
    {
        if (json_is_object(jTrack))
        {
            MusicObjectTrack track;
            track.Name = ObjectJsonHelpers::GetString(jTrack, "name");
            auto source = ObjectJsonHelpers::GetString(jTrack, "source");
            if (source.empty())
            {
                context.LogError(OBJECT_ERROR_INVALID_PROPERTY, "Invalid audio track definition.");
            }
            else
            {
                track.Asset = GetAsset(context, source);
                _tracks.push_back(std::move(track));
            }
        }
    }
}

bool MusicObject::SupportsRideType(uint8_t rideType)
{
    if (_rideTypes.size() == 0)
    {
        // Default behaviour for music is to only exclude from merry-go-round
        return rideType != RIDE_TYPE_MERRY_GO_ROUND;
    }
    else
    {
        auto it = std::find(_rideTypes.begin(), _rideTypes.end(), rideType);
        return it != _rideTypes.end();
    }
}

size_t MusicObject::GetTrackCount() const
{
    return _tracks.size();
}

const MusicObjectTrack* MusicObject::GetTrack(size_t trackIndex) const
{
    if (_tracks.size() > trackIndex)
    {
        return &_tracks[trackIndex];
    }
    return {};
}

ObjectAsset MusicObject::GetAsset(IReadObjectContext& context, const std::string_view& path)
{
    if (path.find("$RCT2:DATA/") == 0)
    {
        auto platformEnvironment = GetContext()->GetPlatformEnvironment();
        auto dir = platformEnvironment->GetDirectoryPath(DIRBASE::RCT2, DIRID::DATA);
        auto path2 = Path::Combine(dir, std::string(path.substr(11)));
        return ObjectAsset(path2);
    }
    else
    {
        return context.GetAsset(path);
    }
}
