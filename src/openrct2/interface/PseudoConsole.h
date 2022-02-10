/*****************************************************************************
 * Copyright (c) 2014-2022 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <future>
#include <queue>
#include <string>
#include <vector>

class InteractiveConsole;
enum class FormatToken;

class OutputConsole
{
public:
    virtual ~OutputConsole() = default;
    virtual void Clear(){};
    virtual void Close(){};
    virtual void Hide(){};
    virtual void WriteLine(const std::string& s, FormatToken colourFormat)
    {
    }
};

/**
 * Represents an interface to one or more consoles such as stdin/stdout or the in-game console.
 */
class PseudoConsole : public OutputConsole
{
private:
    std::vector<OutputConsole*> _consoles;
    std::queue<std::tuple<std::promise<void>, std::string>> _evalQueue;

protected:
    virtual void Execute(const std::string& s);

public:
    virtual ~PseudoConsole() = default;
    virtual std::future<void> ExecuteAsync(const std::string& s);

    void Attach(OutputConsole* console);
    void Detach(OutputConsole* console);
    void Update();

    void WriteLine(const std::string& s);
    void WriteLineError(const std::string& s);
    void WriteLineWarning(const std::string& s);
    void WriteFormatLine(const char* format, ...);

    void Clear() override;
    void Close() override;
    void Hide() override;
    void WriteLine(const std::string& s, FormatToken colourFormat) override;
};
