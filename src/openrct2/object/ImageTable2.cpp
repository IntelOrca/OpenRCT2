/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ImageTable2.h"

#include "../Context.h"
#include "../PlatformEnvironment.h"
#include "../audio/AudioContext.h"
#include "../core/Json.hpp"
#include "../core/Path.hpp"
#include "../drawing/Drawing.h"
#include "../drawing/Image.h"
#include "Object.h"
#include "ObjectFactory.h"

using namespace OpenRCT2;

ImageTable2::~ImageTable2()
{
    Unload();
}

void ImageTable2::ReadFromJson(IReadObjectContext& context, const json_t& root)
{
    if (root.contains("images"))
    {
        json_t jImages = root["images"];
        if (jImages.is_string())
        {
            ReadEntryFromJson(context, jImages);
        }
        else if (jImages.is_array())
        {
            for (auto& jImage : jImages)
            {
                ReadEntryFromJson(context, jImage);
            }
        }
    }
}

void ImageTable2::ReadEntryFromJson(IReadObjectContext& context, const json_t& jImage)
{
    SourceInfo sourceInfo;
    if (jImage.is_string())
    {
        sourceInfo = ParseSource(jImage.get<std::string>());
    }
    else if (jImage.is_object())
    {
        sourceInfo = ParseSource(jImage.at("path").get<std::string>());
    }

    auto asset = context.GetAsset(sourceInfo.Path);
    if (!sourceInfo.SourceRange)
    {
        auto& entry = _entries.emplace_back();
        entry.Kind = sourceInfo.Kind;
        entry.Asset = asset;
    }
    else
    {
        for (auto index : *sourceInfo.SourceRange)
        {
            auto& entry = _entries.emplace_back();
            entry.Kind = sourceInfo.Kind;
            entry.Asset = asset;
            entry.PathIndex = index;
        }
    }
}

void ImageTable2::LoadFrom(const ImageTable2& table, size_t sourceIndex, size_t length)
{
    auto numEntries = std::min(_entries.size(), length);
    for (size_t i = 0; i < numEntries; i++)
    {
        LoadFrom(table, i, sourceIndex, length);
    }
}

void ImageTable2::LoadFrom(const ImageTable2& table, size_t index, size_t sourceStartIndex, size_t length)
{
    auto& entry = _entries[index];
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
            entry.Kind = sourceEntry.Kind;
            entry.Asset = sourceEntry.Asset;
            entry.PathIndex = sourceEntry.PathIndex;
        }
    }
}

void ImageTable2::Load()
{
    LoadFrom(*this, 0, _entries.size());
}

ImageIndex ImageTable2::LoadImages()
{
    std::vector<rct_g1_element> images;
    images.resize(_entries.size());
    for (size_t i = 0; i < images.size(); i++)
    {
        LoadImage(images[i], i);
    }
    _baseIndex = gfx_object_allocate_images(images.data(), images.size());
    if (_baseIndex != ImageIndexUndefined)
    {
        _loadedImageCount = images.size();
    }
    return _baseIndex;
}

void ImageTable2::Unload()
{
    if (_baseIndex != ImageIndexUndefined)
    {
        for (size_t i = 0; i < _loadedImageCount; i++)
        {
            auto el = const_cast<rct_g1_element*>(gfx_get_g1_element(_baseIndex + i));
            delete[] el->offset;
            el->offset = nullptr;
        }
        gfx_object_free_images(_baseIndex, _loadedImageCount);
        _baseIndex = ImageIndexUndefined;
        _loadedImageCount = 0;
    }
}

size_t ImageTable2::GetCount() const
{
    return _entries.size();
}

ImageIndex ImageTable2::GetImage(uint32_t index) const
{
    if (_baseIndex != ImageIndexUndefined && index < _loadedImageCount)
    {
        return _baseIndex + index;
    }
    return ImageIndexUndefined;
}

void ImageTable2::LoadImage(rct_g1_element& element, uint32_t sourceIndex)
{
    auto& entry = _entries.at(sourceIndex);
    if (entry.Asset)
    {
        auto source = GetOrLoadSource(entry);
        if (source != nullptr && entry.PathIndex)
        {
            source->LoadImage(element, *entry.PathIndex);
        }
    }
}

class GxImageSource : public ImageSource
{
private:
    Gx _gx;

public:
    GxImageSource(ObjectAsset& asset)
    {
        auto stream = asset.GetStream();
        if (stream != nullptr)
        {
            _gx = Gx::FromStream(*stream);
        }
    }

    void LoadImage(rct_g1_element& element, uint32_t index) override
    {
        element = _gx.GetImageCopy(index);
    }
};

class ObjectImageSource : public ImageSource
{
private:
    std::unique_ptr<Object> _object;

public:
    ObjectImageSource(ObjectAsset& asset)
    {
        auto& objRepository = GetContext()->GetObjectRepository();
        _object = ObjectFactory::CreateObjectFromLegacyFile(objRepository, asset.GetPath().c_str(), true);
    }

    void LoadImage(rct_g1_element& element, uint32_t index) override
    {
        const auto& obj = *_object.get();
        auto& gx = obj.GetEmbeddedImages();
        element = gx.GetImageCopy(index);
    }
};

ImageSource* ImageTable2::GetOrLoadSource(Entry& entry)
{
    if (_imageSources.size() == 0)
    {
        std::unique_ptr<ImageSource> source;
        switch (entry.Kind)
        {
            case SourceKind::Gx:
                source = std::make_unique<GxImageSource>(*entry.Asset);
                break;
            case SourceKind::ObjData:
                source = std::make_unique<ObjectImageSource>(*entry.Asset);
                break;
            default:
                break;
        }
        if (source != nullptr)
        {
            _imageSources.push_back(std::move(source));
        }
        else
        {
            return nullptr;
        }
    }
    return _imageSources.back().get();
}
