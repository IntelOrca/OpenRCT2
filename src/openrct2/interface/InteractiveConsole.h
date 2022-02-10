/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "PseudoConsole.h"

#include <atomic>
#include <future>
#include <string>

enum class ConsoleInput : uint8_t
{
    None,
    LineClear,
    LineExecute,
    HistoryPrevious,
    HistoryNext,
    ScrollPrevious,
    ScrollNext,
};

class InteractiveConsole : public OutputConsole
{
private:
    PseudoConsole* _pty;

public:
    virtual ~InteractiveConsole()
    {
    }

    void SetPseudoConsole(PseudoConsole* pty);
    std::future<void> ExecuteAsync(const std::string& s);
};

class StdInOutConsole final : public InteractiveConsole
{
private:
    std::atomic<bool> _isPromptShowing{};

public:
    void Start();

    void Clear() override;
    void Close() override;
    void WriteLine(const std::string& s);
    void WriteLineError(const std::string& s);
    void WriteLine(const std::string& s, FormatToken colourFormat) override;
};
