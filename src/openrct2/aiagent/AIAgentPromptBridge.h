/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace OpenRCT2::AIAgent
{
    // Agent terminal status
    enum class AgentStatus
    {
        NotRunning,  // Terminal window not open
        Running,     // Agent process is running
        Exited       // Agent process has exited
    };

    // Status information returned by GetStatus
    struct AgentStatusInfo
    {
        AgentStatus status = AgentStatus::NotRunning;
        int32_t exitCode = 0;
        uint64_t lastOutputTimestamp = 0;  // Unix timestamp of last PTY output
        bool turnComplete = false;          // True if Claude finished its turn (autoplay detection)
        uint64_t lastTurnCompleteTimestamp = 0;  // Unix timestamp of last turn completion
    };

    // Callback types that UI layer registers
    using PromptSender = std::function<bool(const std::string& text)>;
    using StatusGetter = std::function<AgentStatusInfo()>;
    using RestartHandler = std::function<bool()>;

    // Register callbacks from the UI layer (AIAgentTerminal)
    void SetPromptSender(PromptSender sender);
    void SetStatusGetter(StatusGetter getter);
    void SetRestartHandler(RestartHandler handler);

    // Clear callbacks when terminal window closes
    void ClearCallbacks();

    // Functions called by JSON-RPC server
    bool SendPrompt(const std::string& text);
    AgentStatusInfo GetStatus();
    bool Restart();

} // namespace OpenRCT2::AIAgent
