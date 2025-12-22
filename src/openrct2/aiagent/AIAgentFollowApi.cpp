/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AIAgentFollowApi.h"

#include "../config/Config.h"
#include "../telemetry/AIAgentActivityFeed.h"

namespace OpenRCT2::AIAgent
{
    namespace
    {
        void PersistConfigIfNeeded(bool persistConfig)
        {
            if (persistConfig)
            {
                Config::Save();
            }
        }
    } // namespace

    bool IsFollowEnabled()
    {
        return Config::Get().aiAgent.followEnabled;
    }

    void SetFollowEnabled(bool enabled, bool persistConfig)
    {
        auto& config = Config::Get();
        if (config.aiAgent.followEnabled == enabled)
        {
            PersistConfigIfNeeded(persistConfig);
            return;
        }

        config.aiAgent.followEnabled = enabled;
        PersistConfigIfNeeded(persistConfig);

        Telemetry::AIAgentActivityEvent event;
        event.phase = Telemetry::AIAgentActivityPhase::Completed;
        event.method = "agent.follow.setMode";
        event.label = enabled ? "Follow mode enabled" : "Follow mode disabled";
        event.success = true;
        Telemetry::AIAgentActivityFeed::Instance().Publish(event);
    }

    void ToggleFollow(bool persistConfig)
    {
        SetFollowEnabled(!IsFollowEnabled(), persistConfig);
    }
} // namespace OpenRCT2::AIAgent
