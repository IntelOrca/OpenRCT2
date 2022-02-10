/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "InteractiveConsole.h"

#include "PseudoConsole.h"

void InteractiveConsole::SetPseudoConsole(PseudoConsole* pty)
{
    if (_pty != nullptr)
    {
        _pty->Detach(this);
    }
    _pty = pty;
    if (_pty != nullptr)
    {
        _pty->Attach(this);
    }
}

std::future<void> InteractiveConsole::ExecuteAsync(const std::string& s)
{
    if (_pty != nullptr)
    {
        return _pty->ExecuteAsync(s);
    }
    else
    {
        std::promise<void> p;
        p.set_value();
        return p.get_future();
    }
}
