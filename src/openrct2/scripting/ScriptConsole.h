/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_SCRIPTING

#    include "../interface/PseudoConsole.h"

namespace OpenRCT2::Scripting
{
    class ScriptEngine;

    class ScriptConsole : public PseudoConsole
    {
    private:
        ScriptEngine& _scriptEngine;

    protected:
        void Execute(const std::string& s) override;

    public:
        ScriptConsole(ScriptEngine& scriptEngine);
        void ExecuteLegacy(const std::string& s);
    };
} // namespace OpenRCT2::Scripting

#endif
