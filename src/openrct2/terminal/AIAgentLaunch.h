/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "ShellProcess.h"

#include <string>

namespace OpenRCT2::Terminal
{
    struct AIAgentLaunchPlan
    {
        ShellLaunchOptions options;
        std::string description;
        std::string error;
        bool usesAgent = false;
        bool available = false;
    };

    AIAgentLaunchPlan BuildAIAgentLaunchPlan(int cols, int rows);
} // namespace OpenRCT2::Terminal

