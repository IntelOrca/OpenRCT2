#pragma once

#include "PseudoConsole.h"

class LegacyConsole : public PseudoConsole
{
public:
    void Execute(const std::string& s) override;
    void WriteHelp();
};
