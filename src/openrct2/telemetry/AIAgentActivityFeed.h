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
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "../Identifiers.h"
#include "../interface/WindowClasses.h"
#include "../world/Location.hpp"

namespace OpenRCT2::Telemetry
{
    enum class AIAgentActivityPhase : uint8_t
    {
        Started = 0,
        Completed = 1,
        Failed = 2,
    };

    enum class AIAgentRideWindowPage : uint8_t
    {
        Main,
        Vehicle,
        Operating,
        Maintenance,
        Colour,
        Music,
        Measurements,
        Graphs,
        Income,
        Customer,
    };

    enum class AIAgentParkWindowPage : uint8_t
    {
        Entrance,
        Rating,
        Guests,
        Price,
        Stats,
        Objective,
        Awards,
    };

    enum class AIAgentFinancesWindowPage : uint8_t
    {
        Summary,
        Marketing,
        Research,
    };

    enum class AIAgentResearchWindowPage : uint8_t
    {
        Development,
        Funding,
    };

    enum class AIAgentConstructRideTab : uint8_t
    {
        Transport,
        Gentle,
        RollerCoaster,
        Thrill,
        Water,
        Shop,
        Research,
    };

    enum class AIAgentRideListFilter : uint8_t
    {
        Rides,      // PAGE_RIDES - All ride types
        Shops,      // PAGE_SHOPS_AND_STALLS - Shops and stalls
        Facilities, // PAGE_KIOSKS_AND_FACILITIES - Information kiosks, toilets, ATMs
    };

    enum class AIAgentStaffListTab : uint8_t
    {
        Handymen,
        Mechanics,
        Security,
        Entertainers,
    };

    // Columns available in the Ride List window
    enum class AIAgentRideListColumn : uint8_t
    {
        Status,
        RideType,
        Popularity,
        Satisfaction,
        Profit,
        TotalCustomers,
        TotalProfit,
        Customers,
        Age,
        Income,
        RunningCost,
        QueueLength,
        QueueTime,
        Reliability,
        DownTime,
        LastInspection,
        GuestsFavourite,
        Excitement,
        Intensity,
        Nausea,
    };

    struct RideWindowIntent
    {
        RideId rideId = RideId::GetNull();
        AIAgentRideWindowPage page = AIAgentRideWindowPage::Main;
    };

    struct StaffWindowIntent
    {
        EntityId staffId = EntityId::GetNull();
    };

    struct GuestWindowIntent
    {
        EntityId guestId = EntityId::GetNull();
    };

    struct ParkWindowIntent
    {
        AIAgentParkWindowPage page = AIAgentParkWindowPage::Entrance;
    };

    struct FinancesWindowIntent
    {
        AIAgentFinancesWindowPage page = AIAgentFinancesWindowPage::Summary;
    };

    struct ResearchWindowIntent
    {
        AIAgentResearchWindowPage page = AIAgentResearchWindowPage::Development;
    };

    struct ConstructRideIntent
    {
        AIAgentConstructRideTab tab = AIAgentConstructRideTab::Shop;
    };

    struct RideListWindowIntent
    {
        AIAgentRideListFilter filter = AIAgentRideListFilter::Rides;
        std::optional<AIAgentRideListColumn> column;  // Which stat column to display
        std::optional<bool> sortDescending;           // Sort direction
    };

    struct StaffListWindowIntent
    {
        std::optional<AIAgentStaffListTab> tab;
    };

    struct GenericWindowIntent
    {
        WindowClass windowClass = WindowClass::null;
    };

    using AIAgentFollowWindowIntent = std::variant<
        std::monostate,
        RideWindowIntent,
        StaffWindowIntent,
        GuestWindowIntent,
        ParkWindowIntent,
        FinancesWindowIntent,
        ResearchWindowIntent,
        ConstructRideIntent,
        RideListWindowIntent,
        StaffListWindowIntent,
        GenericWindowIntent>;

    struct AIAgentFollowHint
    {
        std::string sourceMethod;
        std::string contextLabel;
        std::optional<CoordsXYZ> cameraTarget;
        bool requestCameraFocus = true;
        bool requestWindowFocus = true;
        AIAgentFollowWindowIntent windowIntent{};
    };

    struct AIAgentActivityEvent
    {
        AIAgentActivityPhase phase = AIAgentActivityPhase::Completed;
        std::string method;
        std::string label;
        bool success = false;
        std::optional<AIAgentFollowHint> followHint;
    };

    class AIAgentActivityFeed
    {
    public:
        using ListenerId = uint64_t;
        using Listener = std::function<void(const AIAgentActivityEvent&)>;

        static AIAgentActivityFeed& Instance();

        ListenerId AddListener(Listener listener);
        void RemoveListener(ListenerId id);

        void Publish(const AIAgentActivityEvent& event);

    private:
        AIAgentActivityFeed() = default;

        std::mutex _listenerMutex;
        std::vector<std::pair<ListenerId, Listener>> _listeners;
        std::atomic<ListenerId> _nextListenerId{ 1 };
    };
} // namespace OpenRCT2::Telemetry
