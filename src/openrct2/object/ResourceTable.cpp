/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ResourceTable.h"

#include "../Context.h"
#include "../PlatformEnvironment.h"
#include "../core/Path.hpp"
#include "../core/String.hpp"

using namespace OpenRCT2;

Range<int32_t> ResourceTable::ParseRange(std::string_view s)
{
    // Currently only supports [###] or [###..###]
    Range<int32_t> result = {};
    if (s.length() >= 3 && s[0] == '[' && s[s.length() - 1] == ']')
    {
        s = s.substr(1, s.length() - 2);
        auto parts = String::Split(s, "..");
        if (parts.size() == 1)
        {
            result = Range<int32_t>(std::stoi(parts[0]));
        }
        else
        {
            auto left = std::stoi(parts[0]);
            auto right = std::stoi(parts[1]);
            if (left <= right)
            {
                result = Range<int32_t>(left, right);
            }
            else
            {
                result = Range<int32_t>(right, left);
            }
        }
    }
    return result;
}

ResourceTable::SourceInfo ResourceTable::ParseSource(std::string_view source)
{
    SourceInfo info;
    if (String::StartsWith(source, "$RCT1:DATA/"))
    {
        auto name = source.substr(11);
        auto rangeStart = name.find('[');
        if (rangeStart != std::string::npos)
        {
            info.SourceRange = ParseRange(name.substr(rangeStart));
            name = name.substr(0, rangeStart);
        }

        auto env = GetContext()->GetPlatformEnvironment();
        info.Path = env->FindFile(DIRBASE::RCT1, DIRID::DATA, name);
    }
    else if (String::StartsWith(source, "$RCT2:DATA/"))
    {
        auto name = source.substr(11);
        auto rangeStart = name.find('[');
        if (rangeStart != std::string::npos)
        {
            info.SourceRange = ParseRange(name.substr(rangeStart));
            name = name.substr(0, rangeStart);
        }

        auto env = GetContext()->GetPlatformEnvironment();
        info.Path = env->FindFile(DIRBASE::RCT2, DIRID::DATA, name);
    }
    else if (String::StartsWith(source, "$RCT2:OBJDATA/"))
    {
        auto name = source.substr(14);
        auto rangeStart = name.find('[');
        if (rangeStart != std::string::npos)
        {
            info.SourceRange = ParseRange(name.substr(rangeStart));
            name = name.substr(0, rangeStart);
        }

        auto env = GetContext()->GetPlatformEnvironment();
        info.Path = env->FindFile(DIRBASE::RCT2, DIRID::OBJECT, name);
    }
    else if (String::StartsWith(source, "$["))
    {
        info.SourceRange = ParseRange(source.substr(1));
    }
    else
    {
        info.Path = source;
    }
    return info;
}
