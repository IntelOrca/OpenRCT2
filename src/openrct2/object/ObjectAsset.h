/*****************************************************************************
 * Copyright (c) 2014-2021 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace OpenRCT2
{
    class IStream;
}

class ObjectAsset
{
private:
    std::string _zipPath;
    std::string _path;

public:
    ObjectAsset() = default;
    ObjectAsset(std::string_view path)
        : _path(path)
    {
    }
    ObjectAsset(std::string_view zipPath, std::string_view path)
        : _zipPath(zipPath)
        , _path(path)
    {
    }

    [[nodiscard]] bool IsAvailable() const;
    [[nodiscard]] uint64_t GetSize() const;
    [[nodiscard]] std::vector<uint8_t> GetData() const;
    [[nodiscard]] std::unique_ptr<OpenRCT2::IStream> GetStream() const;
    const std::string& GetPath() const;
};
