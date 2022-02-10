#include "PseudoConsole.h"

#include "../core/String.hpp"
#include "../localisation/FormatCodes.h"
#include "InteractiveConsole.h"

#include <cassert>
#include <varargs.h>

void PseudoConsole::Attach(OutputConsole* console)
{
    assert(console != nullptr);
    auto it = std::find(_consoles.begin(), _consoles.end(), console);
    if (it == _consoles.end())
    {
        _consoles.push_back(console);
    }
}

void PseudoConsole::Detach(OutputConsole* console)
{
    assert(console != nullptr);
    auto it = std::find(_consoles.begin(), _consoles.end(), console);
    if (it != _consoles.end())
    {
        _consoles.erase(it);
    }
}

void PseudoConsole::Execute(const std::string& s)
{
}

std::future<void> PseudoConsole::ExecuteAsync(const std::string& s)
{
    // Push on-demand evaluations onto a queue so that it can be processed deterministically
    // on the main thead at the right time.
    std::promise<void> barrier;
    auto future = barrier.get_future();
    _evalQueue.emplace(std::move(barrier), s);
    return future;
}

void PseudoConsole::Update()
{
    while (_evalQueue.size() > 0)
    {
        auto item = std::move(_evalQueue.front());
        _evalQueue.pop();
        auto promise = std::move(std::get<0>(item));
        auto command = std::move(std::get<1>(item));

        Execute(command);

        // Signal the promise so caller can continue
        promise.set_value();
    }
}

void PseudoConsole::WriteLine(const std::string& s)
{
    WriteLine(s, FormatToken::ColourWindow2);
}

void PseudoConsole::WriteLineError(const std::string& s)
{
    WriteLine(s, FormatToken::ColourRed);
}

void PseudoConsole::WriteLineWarning(const std::string& s)
{
    WriteLine(s, FormatToken::ColourYellow);
}

void PseudoConsole::WriteFormatLine(const char* format, ...)
{
    va_list list;
    va_start(list, format);
    auto buffer = String::Format_VA(format, list);
    va_end(list);

    auto s = std::string(buffer);
    std::free(buffer);
    WriteLine(s);
}

void PseudoConsole::Clear()
{
    for (auto& console : _consoles)
    {
        console->Clear();
    }
}

void PseudoConsole::Close()
{
    for (auto& console : _consoles)
    {
        console->Close();
    }
}

void PseudoConsole::Hide()
{
    for (auto& console : _consoles)
    {
        console->Hide();
    }
}

void PseudoConsole::WriteLine(const std::string& s, FormatToken colourFormat)
{
    for (auto& console : _consoles)
    {
        console->WriteLine(s, colourFormat);
    }
}
