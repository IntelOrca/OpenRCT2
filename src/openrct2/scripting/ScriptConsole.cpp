/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

#    include "ScriptConsole.h"

#    include "../Context.h"
#    include "../interface/InteractiveConsole.h"
#    include "../interface/LegacyConsole.h"
#    include "ScriptEngine.h"

using namespace OpenRCT2::Scripting;

ScriptConsole::ScriptConsole(ScriptEngine& scriptEngine)
    : _scriptEngine(scriptEngine)
{
}

void ScriptConsole::Execute(const std::string& command)
{
    auto ctx = _scriptEngine.GetContext();
    if (duk_peval_string(ctx, command.c_str()) != 0)
    {
        std::string result = std::string(duk_safe_to_string(ctx, -1));
        WriteLineError(result);
    }
    else if (duk_get_type(ctx, -1) != DUK_TYPE_UNDEFINED)
    {
        auto result = Stringify(DukValue::copy_from_stack(ctx, -1));
        WriteLine(result);
    }
    duk_pop(ctx);
}

void ScriptConsole::ExecuteLegacy(const std::string& s)
{
    LegacyConsole legacyConsole;
    legacyConsole.Attach(this);
    legacyConsole.Execute(s);
    legacyConsole.Detach(this);

    // auto* context = GetContext();
    // if (context != nullptr)
    // {
    //     auto* legacyConsole = context->GetLegacyConsole();
    //     if (legacyConsole != nullptr)
    //     {
    //         legacyConsole->ExecuteAsync(s);
    //     }
    // }
}

#endif
