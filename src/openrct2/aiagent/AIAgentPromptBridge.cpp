/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AIAgentPromptBridge.h"

#include "../Diagnostic.h"

#include <mutex>

namespace OpenRCT2::AIAgent
{
    namespace
    {
        std::mutex g_bridgeMutex;
        PromptSender g_promptSender;
        StatusGetter g_statusGetter;
        RestartHandler g_restartHandler;
    } // namespace

    void SetPromptSender(PromptSender sender)
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        g_promptSender = std::move(sender);
    }

    void SetStatusGetter(StatusGetter getter)
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        g_statusGetter = std::move(getter);
    }

    void SetRestartHandler(RestartHandler handler)
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        g_restartHandler = std::move(handler);
    }

    void ClearCallbacks()
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        g_promptSender = nullptr;
        g_statusGetter = nullptr;
        g_restartHandler = nullptr;
    }

    bool SendPrompt(const std::string& text)
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        if (!g_promptSender)
        {
            LOG_WARNING("AIAgentPromptBridge: No prompt sender registered (terminal not open?)");
            return false;
        }
        return g_promptSender(text);
    }

    AgentStatusInfo GetStatus()
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        if (!g_statusGetter)
        {
            // Terminal not open, return not running
            return AgentStatusInfo{ AgentStatus::NotRunning, 0, 0 };
        }
        return g_statusGetter();
    }

    bool Restart()
    {
        std::lock_guard<std::mutex> lock(g_bridgeMutex);
        if (!g_restartHandler)
        {
            LOG_WARNING("AIAgentPromptBridge: No restart handler registered (terminal not open?)");
            return false;
        }
        return g_restartHandler();
    }

} // namespace OpenRCT2::AIAgent
