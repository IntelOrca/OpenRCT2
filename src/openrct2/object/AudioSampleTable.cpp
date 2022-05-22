/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AudioSampleTable.h"

#include "../Context.h"
#include "../PlatformEnvironment.h"
#include "../audio/AudioContext.h"
#include "../core/File.h"
#include "../core/Json.hpp"
#include "../core/Path.hpp"

using namespace OpenRCT2;
using namespace OpenRCT2::Audio;

std::vector<AudioSampleTable::Entry>& AudioSampleTable::GetEntries()
{
    return _entries;
}

void AudioSampleTable::ReadFromJson(IReadObjectContext* context, const json_t& root)
{
    json_t jSamples = root["samples"];
    if (jSamples.is_array())
    {
        for (auto& jSample : jSamples)
        {
            SourceInfo sourceInfo;
            int32_t modifier{};
            if (jSample.is_string())
            {
                sourceInfo = ParseSource(jSample.get<std::string>());
            }
            else if (jSample.is_object())
            {
                sourceInfo = ParseSource(jSample.at("source").get<std::string>());
                if (jSample.contains("modifier"))
                {
                    modifier = jSample.at("modifier").get<int32_t>();
                }
            }

            auto asset = context->GetAsset(sourceInfo.Path);
            if (!sourceInfo.SourceRange)
            {
                auto& entry = _entries.emplace_back();
                entry.Asset = asset;
                entry.Modifier = modifier;
            }
            else
            {
                Range<int32_t> r(1, 5);
                for (auto index : *sourceInfo.SourceRange)
                {
                    auto& entry = _entries.emplace_back();
                    entry.Asset = asset;
                    entry.PathIndex = index;
                    entry.Modifier = modifier;
                }
            }
        }
    }
}

void AudioSampleTable::LoadFrom(const AudioSampleTable& table, size_t sourceIndex, size_t length)
{
    auto numEntries = std::min(_entries.size(), length);
    for (size_t i = 0; i < numEntries; i++)
    {
        LoadFrom(table, i, sourceIndex, length);
    }
}

void AudioSampleTable::LoadFrom(const AudioSampleTable& table, size_t index, size_t sourceStartIndex, size_t length)
{
    auto& entry = _entries[index];
    if (entry.Source != nullptr)
    {
        return;
    }

    auto sourceIndex = sourceStartIndex + index;
    if (sourceIndex >= table._entries.size())
    {
        return;
    }

    const auto& sourceEntry = table._entries[sourceIndex];
    if (sourceEntry.Asset)
    {
        auto stream = sourceEntry.Asset->GetStream();
        if (stream != nullptr)
        {
            entry.Asset = sourceEntry.Asset;
            entry.PathIndex = sourceEntry.PathIndex;
            entry.Modifier = sourceEntry.Modifier;
        }
    }
}

void AudioSampleTable::Load()
{
    LoadFrom(*this, 0, _entries.size());
}

void AudioSampleTable::LoadSamples()
{
    auto audioContext = GetContext()->GetAudioContext();
    for (size_t i = 0; i < _entries.size(); i++)
    {
        auto& entry = _entries[i];
        if (entry.Source == nullptr)
        {
            entry.Source = LoadSample(i);
        }
    }
}

void AudioSampleTable::Unload()
{
    for (auto& entry : _entries)
    {
        if (entry.Source != nullptr)
        {
            entry.Source->Release();
            entry.Source = nullptr;
        }
    }
}

size_t AudioSampleTable::GetCount() const
{
    return _entries.size();
}

IAudioSource* AudioSampleTable::GetSample(uint32_t index) const
{
    if (index < _entries.size())
    {
        return _entries[index].Source;
    }
    return nullptr;
}

IAudioSource* AudioSampleTable::LoadSample(uint32_t index) const
{
    IAudioSource* result{};
    if (index < _entries.size())
    {
        auto& entry = _entries[index];
        if (entry.Asset)
        {
            auto stream = entry.Asset->GetStream();
            if (stream != nullptr)
            {
                auto audioContext = GetContext()->GetAudioContext();
                if (entry.PathIndex)
                {
                    result = audioContext->CreateStreamFromCSS(std::move(stream), *entry.PathIndex);
                }
                else
                {
                    result = audioContext->CreateStreamFromWAV(std::move(stream));
                }
            }
        }
    }
    return result;
}

int32_t AudioSampleTable::GetSampleModifier(uint32_t index) const
{
    if (index < _entries.size())
    {
        return _entries[index].Modifier;
    }
    return 0;
}
