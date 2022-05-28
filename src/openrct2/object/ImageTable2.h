/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../core/JsonFwd.hpp"
#include "../drawing/ImageId.hpp"
#include "ObjectAsset.h"
#include "ResourceTable.h"

#include <optional>

struct IReadObjectContext;
struct rct_g1_element;

class ImageSource
{
public:
    virtual ~ImageSource() = default;
    virtual std::optional<size_t> LoadImage(rct_g1_element& element, size_t index) = 0;
};

class ImageTable2 : public ResourceTable
{
private:
    struct Entry
    {
        SourceKind Kind{};
        std::optional<ObjectAsset> Asset;
        std::optional<uint32_t> PathIndex;
    };

    std::vector<Entry> _entries;
    ImageIndex _baseIndex = ImageIndexUndefined;
    size_t _loadedImageCount{};
    std::vector<std::unique_ptr<ImageSource>> _imageSources;

public:
    ~ImageTable2();

    std::vector<Entry>& GetEntries();

    /**
     * Read the entries from the given JSON into the table, but do not load anything.
     */
    void ReadFromJson(IReadObjectContext& context, const json_t& root);

    /**
     * Load all available entries from the given table.
     */
    void LoadFrom(const ImageTable2& table, size_t sourceIndex, size_t length);

    /**
     * Load a specific entry from the given table.
     */
    void LoadFrom(const ImageTable2& table, size_t index, size_t sourceIndex, size_t length);

    /**
     * Load all available entries.
     */
    void Load();

    /**
     * Unloads all entries that are currently loaded.
     */
    void Unload();

    /**
     * Load all the images.
     */
    ImageIndex LoadImages();

    size_t GetCount() const;
    ImageIndex GetImage(uint32_t index) const;
    void LoadImage(std::vector<rct_g1_element>& elements, size_t index);

private:
    void ReadEntryFromJson(IReadObjectContext& context, const json_t& jImage);
    ImageSource* GetOrLoadSource(Entry& entry);
};
