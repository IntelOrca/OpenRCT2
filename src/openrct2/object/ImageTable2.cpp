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
#include "../core/FileStream.h"
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
        if (jImage.is_object())
        {
            ReadImageInfoFromJson(context, jImage, entry);
        }
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

void ImageTable2::ReadImageInfoFromJson(IReadObjectContext& context, const json_t& jImage, ImageTable2::Entry& entry)
{
    if (jImage.contains("x"))
        entry.X = jImage.at("x").get<int32_t>();
    if (jImage.contains("y"))
        entry.Y = jImage.at("y").get<int32_t>();
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
    if (sourceEntry.Kind != SourceKind::None)
    {
        entry = sourceEntry;
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
    for (size_t i = 0; i < _entries.size(); i++)
    {
        LoadImage(images, i);
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

void ImageTable2::LoadImage(std::vector<rct_g1_element>& elements, size_t index)
{
    auto& entry = _entries.at(index);
    auto source = GetOrLoadSource(entry);
    if (source != nullptr && entry.PathIndex)
    {
        auto* element = &elements[index];
        auto nextZoomIndex = source->LoadImage(*element, *entry.PathIndex);
        while (nextZoomIndex)
        {
            element->zoomed_offset = -static_cast<int32_t>(elements.size() - index);
            auto& zoomElement = elements.emplace_back();
            nextZoomIndex = source->LoadImage(zoomElement, *nextZoomIndex);
            element = &zoomElement;
            index = elements.size() - 1;
        }
    }
}

class GxImageSource : public ImageSource
{
private:
    GxStream _gx;

public:
    GxImageSource(std::unique_ptr<IStream> stream)
        : _gx(std::move(stream))
    {
    }

    GxImageSource(std::unique_ptr<IStream> streamElements, std::unique_ptr<IStream> streamData)
        : _gx(std::move(streamElements), std::move(streamData))
    {
    }

    std::optional<size_t> LoadImage(rct_g1_element& element, size_t index) override
    {
        element = _gx.GetImageCopy(index);
        return _gx.GetNextZoomImage(index);
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

    std::optional<size_t> LoadImage(rct_g1_element& element, size_t index) override
    {
        const auto& obj = *_object.get();
        auto& gx = obj.GetEmbeddedImages();
        element = gx.GetImageCopy(index);
        return {};
    }
};

ImageSource* ImageTable2::GetOrLoadSource(Entry& entry)
{
    if (_imageSources.size() == 0)
    {
        std::unique_ptr<ImageSource> source;
        switch (entry.Kind)
        {
            case SourceKind::G1:
            {
                auto env = GetContext()->GetPlatformEnvironment();
                auto dataPath = env->FindFile(DIRBASE::RCT2, DIRID::DATA, "g1.dat");
                source = std::make_unique<GxImageSource>(std::make_unique<FileStream>(dataPath, FILE_MODE_OPEN));
                break;
            }
            case SourceKind::Csg:
            {
                auto env = GetContext()->GetPlatformEnvironment();
                auto dataPathElements = env->FindFile(DIRBASE::RCT1, DIRID::DATA, "csg1i.dat");
                auto dataPathData = env->FindFile(DIRBASE::RCT1, DIRID::DATA, "csg1.dat");
                source = std::make_unique<GxImageSource>(
                    std::make_unique<FileStream>(dataPathElements, FILE_MODE_OPEN),
                    std::make_unique<FileStream>(dataPathData, FILE_MODE_OPEN));
                break;
            }
            case SourceKind::Gx:
            {
                if (entry.Asset)
                {
                    auto stream = entry.Asset->GetStream();
                    if (stream != nullptr)
                    {
                        source = std::make_unique<GxImageSource>(std::move(stream));
                    }
                }
                break;
            }
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
