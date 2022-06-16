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
#include "../core/Imaging.h"
#include "../core/Json.hpp"
#include "../core/Path.hpp"
#include "../drawing/Drawing.h"
#include "../drawing/Image.h"
#include "../drawing/ImageImporter.h"
#include "Object.h"
#include "ObjectFactory.h"

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

namespace OpenRCT2
{
    class ImageSource
    {
    public:
        virtual ~ImageSource() = default;
        virtual std::optional<size_t> LoadImage(const ImageTable2::Entry& entry, rct_g1_element& element, size_t index) = 0;
    };
} // namespace OpenRCT2

ImageTable2::ImageTable2()
{
}

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
        sourceInfo.Kind = SourceKind::Png;
    }

    auto asset = sourceInfo.Path.empty() ? std::nullopt : std::make_optional<ObjectAsset>(context.GetAsset(sourceInfo.Path));
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

void ImageTable2::LoadFrom(const ImageTable2& table, size_t sourceStartIndex, size_t length)
{
    // Ensure we stay in bounds of source table
    if (sourceStartIndex >= table._entries.size())
        return;
    length = std::min(length, table._entries.size() - sourceStartIndex);

    // Asset packs may allocate more images for an object that original, or original object may
    // not allocate any images at all.
    if (_entries.size() < length)
    {
        _entries.resize(length);
    }

    for (size_t i = 0; i < length; i++)
    {
        const auto& sourceEntry = table._entries[sourceStartIndex + i];
        if (sourceEntry.Kind != SourceKind::None)
        {
            _entries[i] = sourceEntry;
        }
    }
}

ImageIndex ImageTable2::Load()
{
    std::vector<rct_g1_element> images;
    images.resize(_entries.size());
    for (size_t i = 0; i < _entries.size(); i++)
    {
        LoadImage(images, i);
    }
    _baseIndex = gfx_object_allocate_images(images.data(), static_cast<uint32_t>(images.size()));
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
            auto el = const_cast<rct_g1_element*>(gfx_get_g1_element(static_cast<ImageIndex>(_baseIndex + i)));
            delete[] el->offset;
            el->offset = nullptr;
        }
        gfx_object_free_images(_baseIndex, static_cast<uint32_t>(_loadedImageCount));
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
    if (source != nullptr)
    {
        auto* element = &elements[index];
        auto nextZoomIndex = source->LoadImage(entry, *element, entry.PathIndex.value_or(0));
        while (nextZoomIndex)
        {
            element->zoomed_offset = -static_cast<int32_t>(elements.size() - index);
            auto& zoomElement = elements.emplace_back();
            nextZoomIndex = source->LoadImage(entry, zoomElement, *nextZoomIndex);
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

    std::optional<size_t> LoadImage(const ImageTable2::Entry& entry, rct_g1_element& element, size_t index) override
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
    ObjectImageSource(const ObjectAsset& asset)
    {
        auto& objRepository = GetContext()->GetObjectRepository();
        _object = ObjectFactory::CreateObjectFromLegacyFile(objRepository, asset.GetPath().c_str(), true);
    }

    std::optional<size_t> LoadImage(const ImageTable2::Entry& entry, rct_g1_element& element, size_t index) override
    {
        const auto& obj = *_object.get();
        auto& gx = obj.GetEmbeddedImages();
        element = gx.GetImageCopy(index);
        return {};
    }
};

class PngImageSource : public ImageSource
{
private:
    Image _image;

public:
    PngImageSource(const ObjectAsset& asset)
    {
        auto buffer = asset.GetData();
        _image = Imaging::ReadFromBuffer(buffer, IMAGE_FORMAT::PNG_32);
    }

    std::optional<size_t> LoadImage(const ImageTable2::Entry& entry, rct_g1_element& element, size_t index) override
    {
        ImageImporter imageImporter;
        auto result = imageImporter.Import(_image, entry.X, entry.Y);

        element = result.Element;
        element.offset = new uint8_t[result.Buffer.size()];
        std::memcpy(element.offset, result.Buffer.data(), result.Buffer.size());
        return {};
    }
};

std::string ImageTable2::GetImageSourceKey(const ImageTable2::Entry& entry)
{
    switch (entry.Kind)
    {
        case SourceKind::G1:
            return "$G1";
        case SourceKind::Csg:
            return "$CSG";
        case SourceKind::None:
        case SourceKind::Gx:
        case SourceKind::ObjData:
        case SourceKind::Png:
            if (!entry.Asset)
                return {};
            return entry.Asset->GetZipPath() + entry.Asset->GetPath();
        default:
            return {};
    }
}

ImageSource* ImageTable2::GetOrLoadSource(Entry& entry)
{
    auto key = GetImageSourceKey(entry);
    if (key == _lastImageSourceKey)
    {
        return _lastImageSource.get();
    }

    _lastImageSourceKey = key;
    _lastImageSource = CreateImageSource(entry);
    return _lastImageSource.get();
}

std::unique_ptr<ImageSource> ImageTable2::CreateImageSource(const Entry& entry)
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
        {
            source = std::make_unique<ObjectImageSource>(*entry.Asset);
            break;
        }
        case SourceKind::Png:
        {
            if (entry.Asset)
            {
                source = std::make_unique<PngImageSource>(*entry.Asset);
            }
            break;
        }
        default:
        {
            break;
        }
    }
    return source;
}
