/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "RideAudio.h"

#include "../Context.h"
#include "../OpenRCT2.h"
#include "../audio/AudioMixer.h"
#include "../config/Config.h"
#include "../object/MusicObject.h"
#include "../object/ObjectManager.h"
#include "Ride.h"

#include <algorithm>
#include <vector>

using namespace OpenRCT2;

constexpr size_t MAX_RIDE_MUSIC_CHANNELS = 32;

/**
 * Represents a particular instance of ride music that can be heard in a viewport.
 * These are created each frame via enumerating each ride / viewport.
 */
struct ViewportRideMusicInstance
{
    ride_id_t RideId;
    uint8_t TrackIndex{};

    size_t Offset{};
    int16_t Volume{};
    int16_t Pan{};
    uint16_t Frequency{};
};

/**
 * Represents an audio channel to play a particular ride's music track.
 */
struct RideMusicChannel
{
    ride_id_t RideId{};
    uint8_t TrackIndex{};

    size_t Offset{};
    int16_t Volume{};
    int16_t Pan{};
    uint16_t Frequency{};

    void* Channel{};

    RideMusicChannel(const ViewportRideMusicInstance& instance, void* channel)
    {
        RideId = instance.RideId;
        TrackIndex = instance.TrackIndex;

        Offset = std::max<size_t>(0, instance.Offset - 10000);
        Volume = instance.Volume;
        Pan = instance.Pan;
        Frequency = instance.Frequency;

        Channel = channel;

        Mixer_Channel_SetOffset(channel, Offset);
        Mixer_Channel_Volume(channel, DStoMixerVolume(Volume));
        Mixer_Channel_Pan(channel, DStoMixerPan(Pan));
        Mixer_Channel_Rate(channel, DStoMixerRate(Frequency));
    }

    RideMusicChannel(const RideMusicChannel&) = delete;

    RideMusicChannel(RideMusicChannel&& src) noexcept
    {
        *this = std::move(src);
    }

    RideMusicChannel& operator=(RideMusicChannel&& src) noexcept
    {
        RideId = src.RideId;
        TrackIndex = src.TrackIndex;

        Offset = src.Offset;
        Volume = src.Volume;
        Pan = src.Pan;
        Frequency = src.Frequency;

        if (Channel != nullptr)
        {
            Mixer_Stop_Channel(Channel);
        }
        Channel = src.Channel;
        src.Channel = nullptr;

        return *this;
    }

    ~RideMusicChannel()
    {
        if (Channel != nullptr)
        {
            Mixer_Stop_Channel(Channel);
            Channel = nullptr;
        }
    }

    bool IsPlaying() const
    {
        if (Channel != nullptr)
        {
            return Mixer_Channel_IsPlaying(Channel);
        }
        return false;
    }

    size_t GetOffset() const
    {
        if (Channel != nullptr)
        {
            return Mixer_Channel_GetOffset(Channel);
        }
        return 0;
    }

    void Update(const ViewportRideMusicInstance& instance)
    {
        if (Volume != instance.Volume)
        {
            Volume = instance.Volume;
            if (Channel != nullptr)
            {
                Mixer_Channel_Volume(Channel, DStoMixerVolume(Volume));
            }
        }
        if (Pan != instance.Pan)
        {
            Pan = instance.Pan;
            if (Channel != nullptr)
            {
                Mixer_Channel_Pan(Channel, DStoMixerPan(Pan));
            }
        }
        if (Frequency != instance.Frequency)
        {
            Frequency = instance.Frequency;
            if (Channel != nullptr)
            {
                Mixer_Channel_Rate(Channel, DStoMixerRate(Frequency));
            }
        }
    }
};

static std::vector<ViewportRideMusicInstance> _musicInstances;
static std::vector<RideMusicChannel> _musicChannels;

void RideAudioStopAllChannels()
{
    _musicChannels.clear();
}

void RideAudioClearAllViewportInstances()
{
    _musicInstances.clear();
}

static void StartRideMusicChannel(const ViewportRideMusicInstance& instance)
{
    // Create new music channel
    auto ride = get_ride(instance.RideId);
    if (ride->type == RIDE_TYPE_CIRCUS)
    {
        auto channel = Mixer_Play_Music(PATH_ID_CSS24, MIXER_LOOP_NONE, true);
        if (channel != nullptr)
        {
            // Move circus music to the sound mixer group
            Mixer_Channel_SetGroup(channel, Audio::MixerGroup::Sound);

            _musicChannels.emplace_back(instance, channel);
        }
    }
    else
    {
        auto& objManager = GetContext()->GetObjectManager();
        auto musicObj = static_cast<MusicObject*>(objManager.GetLoadedObject(OBJECT_TYPE_MUSIC, ride->music));
        if (musicObj != nullptr)
        {
            auto track = musicObj->GetTrack(instance.TrackIndex);
            if (track != nullptr)
            {
                auto stream = track->Asset.GetStream();
                auto channel = Mixer_Play_Music(std::move(stream), MIXER_LOOP_NONE);
                if (channel != nullptr)
                {
                    _musicChannels.emplace_back(instance, channel);
                }
            }
        }
    }
}

static void StopInactiveRideMusicChannels()
{
    _musicChannels.erase(
        std::remove_if(
            _musicChannels.begin(), _musicChannels.end(),
            [](const auto& channel) {
                auto found = std::any_of(_musicInstances.begin(), _musicInstances.end(), [&channel](const auto& instance) {
                    return instance.RideId == channel.RideId && instance.TrackIndex == channel.TrackIndex;
                });
                if (!found || !channel.IsPlaying())
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }),
        _musicChannels.end());
}

static void UpdateRideMusicChannelForMusicParams(const ViewportRideMusicInstance& instance)
{
    // Find existing music channel
    auto foundChannel = std::find_if(
        _musicChannels.begin(), _musicChannels.end(), [&instance](const RideMusicChannel& channel) {
            return channel.RideId == instance.RideId && channel.TrackIndex == instance.TrackIndex;
        });

    if (foundChannel != _musicChannels.end())
    {
        foundChannel->Update(instance);
    }
    else if (_musicChannels.size() < MAX_RIDE_MUSIC_CHANNELS)
    {
        StartRideMusicChannel(instance);
    }
}

/**
 * Start, update and stop audio channels for each ride music instance that can be heard across all viewports.
 */
void RideUpdateMusicChannels()
{
    if ((gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) != 0 || (gScreenFlags & SCREEN_FLAGS_TITLE_DEMO) != 0)
        return;

    // TODO Allow circus music (CSS24) to play if ride music is disabled (that should be sound)
    if (gGameSoundsOff || !gConfigSound.ride_music_enabled)
        return;

    StopInactiveRideMusicChannels();
    for (const auto& instance : _musicInstances)
    {
        UpdateRideMusicChannelForMusicParams(instance);
    }
}

static std::pair<size_t, size_t> RideMusicGetTuneOffsetLength(const Ride& ride)
{
    auto& objManager = GetContext()->GetObjectManager();
    auto musicObj = static_cast<MusicObject*>(objManager.GetLoadedObject(OBJECT_TYPE_MUSIC, ride.music));
    if (musicObj != nullptr)
    {
        auto numTracks = musicObj->GetTrackCount();
        if (ride.music_tune_id < numTracks)
        {
            auto track = musicObj->GetTrack(ride.music_tune_id);
            return { track->Offset, track->Length };
        }
    }
    return { 0, 0 };
}

static void RideUpdateMusicPosition(Ride& ride)
{
    auto [tuneOffset, tuneLength] = RideMusicGetTuneOffsetLength(ride);
    auto position = ride.music_position + tuneOffset;
    if (position < tuneLength)
    {
        ride.music_position = position;
    }
    else
    {
        ride.music_tune_id = TUNE_ID_NULL;
        ride.music_position = 0;
    }
}

static void RideUpdateMusicPosition(Ride& ride, size_t offset, int16_t volume, int16_t pan, uint16_t sampleRate)
{
    auto [tuneOffset, tuneLength] = RideMusicGetTuneOffsetLength(ride);
    if (offset < tuneLength)
    {
        if (_musicInstances.size() < MAX_RIDE_MUSIC_CHANNELS)
        {
            auto& ride_music_params = _musicInstances.emplace_back();
            ride_music_params.RideId = ride.id;
            ride_music_params.TrackIndex = ride.music_tune_id;
            ride_music_params.Offset = offset;
            ride_music_params.Volume = volume;
            ride_music_params.Pan = pan;
            ride_music_params.Frequency = sampleRate;
        }
        ride.music_position = static_cast<uint32_t>(offset);
    }
    else
    {
        ride.music_tune_id = TUNE_ID_NULL;
        ride.music_position = 0;
    }
}

uint8_t unkn(int32_t a)
{
    uint8_t result = 255;
    int32_t b = std::min(std::abs(a), 6143) - 2048;
    if (b > 0)
    {
        b = -((b / 4) - 1024) / 4;
        result = static_cast<uint8_t>(std::clamp(b, 0, 255));
    }
    return result;
}

/**
 *
 */
void RideUpdateMusicInstance(Ride& ride, const CoordsXYZ& rideCoords, uint16_t sampleRate)
{
    if (!(gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) && !gGameSoundsOff && g_music_tracking_viewport != nullptr)
    {
        auto rotatedCoords = translate_3d_to_2d_with_z(get_current_rotation(), rideCoords);
        auto viewport = g_music_tracking_viewport;
        int16_t view_width = viewport->view_width;
        int16_t view_width2 = view_width * 2;
        int16_t view_x = viewport->viewPos.x - view_width2;
        int16_t view_y = viewport->viewPos.y - view_width;
        int16_t view_x2 = view_width2 + view_width2 + viewport->view_width + view_x;
        int16_t view_y2 = view_width + view_width + viewport->view_height + view_y;

        if (view_x >= rotatedCoords.x || view_y >= rotatedCoords.y || view_x2 < rotatedCoords.x || view_y2 < rotatedCoords.y)
        {
            RideUpdateMusicPosition(ride);
            return;
        }

        int32_t x2 = viewport->pos.x + ((rotatedCoords.x - viewport->viewPos.x) / viewport->zoom);
        x2 *= 0x10000;
        uint16_t screenwidth = context_get_width();
        if (screenwidth < 64)
        {
            screenwidth = 64;
        }
        int32_t pan_x = ((x2 / screenwidth) - 0x8000) >> 4;

        int32_t y2 = viewport->pos.y + ((rotatedCoords.y - viewport->viewPos.y) / viewport->zoom);
        y2 *= 0x10000;
        uint16_t screenheight = context_get_height();
        if (screenheight < 64)
        {
            screenheight = 64;
        }
        int32_t pan_y = ((y2 / screenheight) - 0x8000) >> 4;

        auto volA = unkn(pan_y);
        auto volB = unkn(pan_x);
        auto volC = std::min(volA, volB);
        if (volC < gVolumeAdjustZoom * 3)
        {
            volC = 0;
        }
        else
        {
            volC = volC - (gVolumeAdjustZoom * 3);
        }

        int16_t newVolume = -((static_cast<uint8_t>(-volC - 1) * static_cast<uint8_t>(-volC - 1)) / 16) - 700;
        if (volC != 0 && newVolume >= -4000)
        {
            auto newPan = std::clamp(pan_x, -10000, 10000);
            auto foundChannel = std::find_if(_musicChannels.begin(), _musicChannels.end(), [&ride](const auto& channel) {
                return channel.RideId == ride.id && channel.TrackIndex == ride.music_tune_id;
            });
            if (foundChannel != _musicChannels.end())
            {
                if (foundChannel->IsPlaying())
                {
                    // Since we have a real music channel, use the offset from that
                    auto newOffset = foundChannel->GetOffset();
                    RideUpdateMusicPosition(ride, newOffset, newVolume, newPan, sampleRate);
                }
                else
                {
                    // We had a real music channel, but it isn't playing anymore, so stop the track
                    ride.music_position = 0;
                    ride.music_tune_id = TUNE_ID_NULL;
                }
            }
            else
            {
                // We do not have a real music channel, so simulate the playing of the music track
                auto [tuneOffset, tuneLength] = RideMusicGetTuneOffsetLength(ride);
                auto newOffset = ride.music_position + tuneOffset;
                RideUpdateMusicPosition(ride, newOffset, newVolume, pan_x, sampleRate);
            }
        }
        else
        {
            RideUpdateMusicPosition(ride);
        }
    }
}
