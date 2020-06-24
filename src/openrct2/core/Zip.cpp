/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef __ANDROID__
#    include "Zip.h"

#    include "IStream.hpp"

#    include <algorithm>
#    include <zip.h>

class ZipArchive final : public IZipArchive
{
private:
    zip_t* _zip;
    ZIP_ACCESS _access;
    std::vector<std::vector<uint8_t>> _writeBuffers;

public:
    ZipArchive(const std::string_view& path, ZIP_ACCESS access)
    {
        auto zipOpenMode = ZIP_RDONLY;
        if (access == ZIP_ACCESS::WRITE)
        {
            zipOpenMode = ZIP_CREATE;
        }

        int32_t error;
        _zip = zip_open(path.data(), zipOpenMode, &error);
        if (_zip == nullptr)
        {
            throw IOException("Unable to open zip file.");
        }

        _access = access;
    }

    ~ZipArchive() override
    {
        zip_close(_zip);
    }

    size_t GetNumFiles() const override
    {
        return zip_get_num_entries(_zip, 0);
    }

    std::string GetFileName(size_t index) const override
    {
        std::string result;
        auto name = zip_get_name(_zip, index, ZIP_FL_ENC_GUESS);
        if (name != nullptr)
        {
            result = name;
        }
        return result;
    }

    uint64_t GetFileSize(size_t index) const override
    {
        zip_stat_t zipFileStat;
        if (zip_stat_index(_zip, index, 0, &zipFileStat) == ZIP_ER_OK)
        {
            return zipFileStat.size;
        }
        else
        {
            return 0;
        }
    }

    std::vector<uint8_t> GetFileData(const std::string_view& path) const override
    {
        std::vector<uint8_t> result;
        auto index = GetIndexFromPath(path);
        auto dataSize = GetFileSize(index);
        if (dataSize > 0 && dataSize < SIZE_MAX)
        {
            auto zipFile = zip_fopen_index(_zip, index, 0);
            if (zipFile != nullptr)
            {
                result.resize(static_cast<size_t>(dataSize));
                uint64_t readBytes = zip_fread(zipFile, result.data(), dataSize);
                if (readBytes != dataSize)
                {
                    result.clear();
                    result.shrink_to_fit();
                }
                zip_fclose(zipFile);
            }
        }
        return result;
    }

    std::unique_ptr<std::istream> GetFileStream(const std::string_view& path) const override
    {
        auto index = GetIndexFromPath(path);
        if (index != -1)
        {
            return std::make_unique<ifilestream>(_zip, index);
        }
        return {};
    }

    void SetFileData(const std::string_view& path, std::vector<uint8_t>&& data) override
    {
        // Push buffer to an internal list as libzip requires access to it until the zip
        // handle is closed.
        _writeBuffers.push_back(std::move(data));
        const auto& writeBuffer = *_writeBuffers.rbegin();

        auto source = zip_source_buffer(_zip, writeBuffer.data(), writeBuffer.size(), 0);
        auto index = GetIndexFromPath(path);
        if (index == -1)
        {
            zip_add(_zip, path.data(), source);
        }
        else
        {
            zip_replace(_zip, index, source);
        }
    }

    void DeleteFile(const std::string_view& path) override
    {
        auto index = GetIndexFromPath(path);
        zip_delete(_zip, index);
    }

    void RenameFile(const std::string_view& path, const std::string_view& newPath) override
    {
        auto index = GetIndexFromPath(path);
        zip_file_rename(_zip, index, newPath.data(), ZIP_FL_ENC_GUESS);
    }

private:
    /**
     * Normalises both the given path and the stored paths and finds the first match.
     */
    zip_int64_t GetIndexFromPath(const std::string_view& path) const
    {
        auto normalisedPath = NormalisePath(path);
        if (!normalisedPath.empty())
        {
            auto numFiles = zip_get_num_entries(_zip, 0);
            for (zip_int64_t i = 0; i < numFiles; i++)
            {
                auto normalisedZipPath = NormalisePath(zip_get_name(_zip, i, ZIP_FL_ENC_GUESS));
                if (normalisedZipPath == normalisedPath)
                {
                    return i;
                }
            }
        }
        return -1;
    }

    static std::string NormalisePath(const std::string_view& path)
    {
        std::string result;
        if (!path.empty())
        {
            // Convert back slashes to forward slashes
            result = std::string(path);
            for (auto ch = result.data(); *ch != '\0'; ch++)
            {
                if (*ch == '\\')
                {
                    *ch = '/';
                }
            }
        }
        return result;
    }

    class ifilestream final : public std::istream
    {
    private:
        class ifilestreambuf final : public std::streambuf
        {
        private:
            zip* _zip;
            zip_int64_t _index;
            zip_file_t* _zipFile{};
            zip_int64_t _pos{};
            zip_int64_t _maxLen{};

        public:
            ifilestreambuf(zip* zip, zip_int64_t index)
                : _zip(zip)
                , _index(index)
            {
            }

            ifilestreambuf(const ifilestreambuf&) = delete;

            ~ifilestreambuf() override
            {
                close();
            }

        private:
            void close()
            {
                if (_zipFile != nullptr)
                {
                    zip_fclose(_zipFile);
                    _zipFile = nullptr;
                }
            }

            bool reset()
            {
                close();

                _pos = 0;
                _maxLen = 0;
                _zipFile = zip_fopen_index(_zip, _index, 0);
                if (_zipFile == nullptr)
                {
                    return false;
                }

                zip_stat_t zipFileStat{};
                if (zip_stat_index(_zip, _index, 0, &zipFileStat) != ZIP_ER_OK)
                {
                    return false;
                }

                _maxLen = zipFileStat.size;
                return true;
            }

            std::streamsize xsgetn(char_type* dst, std::streamsize len) override
            {
                if (_zipFile == nullptr && !reset())
                {
                    return 0;
                }

                auto read = zip_fread(_zipFile, dst, len);
                if (read <= 0)
                {
                    return 0;
                }
                _pos += read;
                return read;
            }

            void skip(zip_int64_t len)
            {
                if (_zipFile != nullptr || reset())
                {
                    char buffer[2048]{};
                    while (len > 0)
                    {
                        auto readLen = std::min<zip_int64_t>(len, sizeof(buffer));
                        auto read = zip_fread(_zipFile, buffer, readLen);
                        if (read <= 0)
                        {
                            break;
                        }
                        _pos += read;
                        len -= read;
                    }
                }
            }

            pos_type seekpos(pos_type pos, ios_base::openmode mode) override final
            {
                if (pos > _pos)
                {
                    // Read to seek fowards
                    skip(pos - _pos);
                }
                else if (pos < _pos)
                {
                    // Can not seek backwards, start from the beginning
                    reset();
                    skip(pos);
                }
                return std::clamp<pos_type>(pos, 0, _maxLen);
            }

            pos_type seekoff(off_type off, ios_base::seekdir dir, ios_base::openmode mode) override
            {
                if (dir == std::ios::beg)
                {
                    return seekpos(off, std::ios::in);
                }
                else if (dir == std::ios::cur)
                {
                    return seekpos(_pos + off, std::ios::in);
                }
                else if (dir == std::ios::end)
                {
                    return seekpos(_maxLen - off, std::ios::in);
                }
                else
                {
                    return std::streampos(-1);
                }
            }
        };

    private:
        ifilestreambuf _streambuf;

    public:
        ifilestream(zip* zip, zip_int64_t index)
            : std::istream(&_streambuf)
            , _streambuf(zip, index)
        {
        }
    };
};

namespace Zip
{
    std::unique_ptr<IZipArchive> Open(const std::string_view& path, ZIP_ACCESS access)
    {
        return std::make_unique<ZipArchive>(path, access);
    }

    std::unique_ptr<IZipArchive> TryOpen(const std::string_view& path, ZIP_ACCESS access)
    {
        std::unique_ptr<IZipArchive> result;
        try
        {
            result = std::make_unique<ZipArchive>(path, access);
        }
        catch (const std::exception&)
        {
        }
        return result;
    }
} // namespace Zip

#endif
