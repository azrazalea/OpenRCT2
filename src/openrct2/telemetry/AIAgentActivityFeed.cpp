/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AIAgentActivityFeed.h"

#include <algorithm>

namespace OpenRCT2::Telemetry
{
    AIAgentActivityFeed& AIAgentActivityFeed::Instance()
    {
        static AIAgentActivityFeed feed;
        return feed;
    }

    AIAgentActivityFeed::ListenerId AIAgentActivityFeed::AddListener(Listener listener)
    {
        std::scoped_lock lock(_listenerMutex);
        const auto id = _nextListenerId++;
        _listeners.emplace_back(id, std::move(listener));
        return id;
    }

    void AIAgentActivityFeed::RemoveListener(ListenerId id)
    {
        std::scoped_lock lock(_listenerMutex);
        auto it = std::remove_if(_listeners.begin(), _listeners.end(), [id](const auto& entry) {
            return entry.first == id;
        });
        _listeners.erase(it, _listeners.end());
    }

    void AIAgentActivityFeed::Publish(const AIAgentActivityEvent& event)
    {
        std::vector<Listener> listeners;
        {
            std::scoped_lock lock(_listenerMutex);
            listeners.reserve(_listeners.size());
            for (const auto& entry : _listeners)
            {
                listeners.push_back(entry.second);
            }
        }

        for (const auto& listener : listeners)
        {
            if (listener)
            {
                listener(event);
            }
        }
    }
} // namespace OpenRCT2::Telemetry
