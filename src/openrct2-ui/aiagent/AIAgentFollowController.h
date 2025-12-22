/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>

#include <openrct2/interface/Window.h>
#include "../../openrct2/telemetry/AIAgentActivityFeed.h"
#include <openrct2/world/Location.hpp>

namespace OpenRCT2::Ui::AIAgent
{
    class AIAgentFollowController
    {
    public:
        AIAgentFollowController();
        ~AIAgentFollowController();

        void Tick();
        void SetEnabled(bool enabled);

    private:
        struct PendingHint
        {
            Telemetry::AIAgentFollowHint hint;
            std::chrono::steady_clock::time_point queuedAt;
        };

        std::mutex _queueMutex;
        std::deque<PendingHint> _pendingHints;
        Telemetry::AIAgentActivityFeed::ListenerId _listenerId{ 0 };
        std::atomic<bool> _enabled{ false };
        std::optional<std::pair<WindowClass, WindowNumber>> _managedWindow;
        std::optional<CoordsXYZ> _lastCameraTarget;
        std::chrono::steady_clock::time_point _lastCameraTime{};

        void EnqueueHint(const Telemetry::AIAgentFollowHint& hint);
        std::optional<Telemetry::AIAgentFollowHint> DequeueHint();
        void ProcessHint(const Telemetry::AIAgentFollowHint& hint);
        bool ShouldDedup(const Telemetry::AIAgentFollowHint& hint) const;
        void UpdateCameraStamp(const Telemetry::AIAgentFollowHint& hint);
        void ApplyWindowIntent(const Telemetry::AIAgentFollowWindowIntent& intent);
        void FocusRideWindow(const Telemetry::RideWindowIntent& intent);
        void FocusStaffWindow(const Telemetry::StaffWindowIntent& intent);
        void FocusGuestWindow(const Telemetry::GuestWindowIntent& intent);
        void FocusParkWindow(const Telemetry::ParkWindowIntent& intent);
        void FocusFinancesWindow(const Telemetry::FinancesWindowIntent& intent);
        void FocusResearchWindow(const Telemetry::ResearchWindowIntent& intent);
        void FocusConstructRideWindow(const Telemetry::ConstructRideIntent& intent);
        void FocusRideListWindow(const Telemetry::RideListWindowIntent& intent);
        void FocusStaffListWindow(const Telemetry::StaffListWindowIntent& intent);
        void FocusGenericWindow(const Telemetry::GenericWindowIntent& intent);
        void CloseManagedWindow();
        void TrackManagedWindow(WindowClass cls, WindowNumber number, bool created);
        bool ShouldMoveCamera(const CoordsXYZ& target) const;
        void MoveCamera(const CoordsXYZ& target) const;
        std::pair<float, float> CalculateUnobscuredViewportCenter(Viewport* viewport) const;
    };
} // namespace OpenRCT2::Ui::AIAgent
