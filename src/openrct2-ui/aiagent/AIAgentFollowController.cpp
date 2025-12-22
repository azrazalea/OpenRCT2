/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AIAgentFollowController.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include <openrct2/aiagent/AIAgentFollowApi.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/entity/EntityList.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2/ui/WindowManager.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/world/MapLimits.h>
#include <openrct2-ui/windows/Windows.h>

using OpenRCT2::Telemetry::AIAgentActivityFeed;
using OpenRCT2::Telemetry::AIAgentFollowHint;
using OpenRCT2::Telemetry::AIAgentFollowWindowIntent;
using OpenRCT2::Telemetry::AIAgentRideWindowPage;
using OpenRCT2::Telemetry::AIAgentParkWindowPage;
using OpenRCT2::Telemetry::AIAgentFinancesWindowPage;
using OpenRCT2::Telemetry::AIAgentResearchWindowPage;
using OpenRCT2::Telemetry::AIAgentConstructRideTab;
using OpenRCT2::Telemetry::AIAgentStaffListTab;
namespace UiWindows = OpenRCT2::Ui::Windows;

namespace OpenRCT2::Ui::AIAgent
{
    namespace
    {
        constexpr auto kHintDedupWindow = std::chrono::milliseconds(200);
        constexpr int32_t kCameraDedupeXY = kCoordsXYStep;
        constexpr int32_t kCameraDedupeZ = kCoordsZStep * 4;

        const char* RidePageTokenForUi(AIAgentRideWindowPage page)
        {
            switch (page)
            {
                case AIAgentRideWindowPage::Main:
                    return "MAIN";
                case AIAgentRideWindowPage::Vehicle:
                    return "VEHICLE";
                case AIAgentRideWindowPage::Operating:
                    return "OPERATING";
                case AIAgentRideWindowPage::Maintenance:
                    return "MAINTENANCE";
                case AIAgentRideWindowPage::Colour:
                    return "COLOUR";
                case AIAgentRideWindowPage::Music:
                    return "MUSIC";
                case AIAgentRideWindowPage::Measurements:
                    return "MEASUREMENTS";
                case AIAgentRideWindowPage::Graphs:
                    return "GRAPHS";
                case AIAgentRideWindowPage::Income:
                    return "INCOME";
                case AIAgentRideWindowPage::Customer:
                    return "CUSTOMER";
            }
            return "MAIN";
        }
    } // namespace

    AIAgentFollowController::AIAgentFollowController()
        : _enabled(OpenRCT2::AIAgent::IsFollowEnabled())
    {
        auto listener = [this](const Telemetry::AIAgentActivityEvent& event) {
            if (!event.followHint)
            {
                return;
            }
            EnqueueHint(*event.followHint);
        };
        _listenerId = AIAgentActivityFeed::Instance().AddListener(std::move(listener));
    }

    AIAgentFollowController::~AIAgentFollowController()
    {
        AIAgentActivityFeed::Instance().RemoveListener(_listenerId);
        // Note: Don't call CloseManagedWindow() here. During shutdown, the
        // UiContext (and window manager) may already be destroyed, and all
        // windows are being torn down anyway.
    }

    void AIAgentFollowController::SetEnabled(bool enabled)
    {
        if (_enabled == enabled)
        {
            return;
        }
        _enabled = enabled;
        if (!_enabled)
        {
            std::scoped_lock lock(_queueMutex);
            _pendingHints.clear();
        }
        CloseManagedWindow();
        _lastCameraTarget.reset();
    }

    void AIAgentFollowController::Tick()
    {
        if (!_enabled)
        {
            return;
        }

        auto hint = DequeueHint();
        if (!hint)
        {
            return;
        }

        if (ShouldDedup(*hint))
        {
            return;
        }

        ProcessHint(*hint);
        UpdateCameraStamp(*hint);
    }

    void AIAgentFollowController::EnqueueHint(const AIAgentFollowHint& hint)
    {
        std::scoped_lock lock(_queueMutex);
        if (!_enabled)
        {
            return;
        }
        _pendingHints.push_back(PendingHint{ hint, std::chrono::steady_clock::now() });
    }

    std::optional<AIAgentFollowHint> AIAgentFollowController::DequeueHint()
    {
        std::scoped_lock lock(_queueMutex);
        if (_pendingHints.empty())
        {
            return std::nullopt;
        }
        auto next = std::move(_pendingHints.front().hint);
        _pendingHints.pop_front();
        return next;
    }

    bool AIAgentFollowController::ShouldDedup(const AIAgentFollowHint& hint) const
    {
        if (!hint.cameraTarget || !_lastCameraTarget)
        {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastCameraTime > kHintDedupWindow)
        {
            return false;
        }
        auto dx = std::abs(hint.cameraTarget->x - _lastCameraTarget->x);
        auto dy = std::abs(hint.cameraTarget->y - _lastCameraTarget->y);
        auto dz = std::abs(hint.cameraTarget->z - _lastCameraTarget->z);
        return dx <= kCameraDedupeXY && dy <= kCameraDedupeXY && dz <= kCameraDedupeZ;
    }

    void AIAgentFollowController::UpdateCameraStamp(const AIAgentFollowHint& hint)
    {
        if (!hint.cameraTarget)
        {
            return;
        }
        _lastCameraTarget = hint.cameraTarget;
        _lastCameraTime = std::chrono::steady_clock::now();
    }

    void AIAgentFollowController::ProcessHint(const AIAgentFollowHint& hint)
    {
        // Close managed window for any action with camera movement (view hint)
        if (hint.requestCameraFocus && hint.cameraTarget)
        {
            CloseManagedWindow();
        }

        if (hint.requestWindowFocus)
        {
            ApplyWindowIntent(hint.windowIntent);
        }

        if (hint.requestCameraFocus && hint.cameraTarget)
        {
            if (ShouldMoveCamera(*hint.cameraTarget))
            {
                MoveCamera(*hint.cameraTarget);
            }
        }
    }

    void AIAgentFollowController::ApplyWindowIntent(const AIAgentFollowWindowIntent& intent)
    {
        std::visit(
            [&](const auto& target) {
                using T = std::decay_t<decltype(target)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    return;
                }
                else if constexpr (std::is_same_v<T, Telemetry::RideWindowIntent>)
                {
                    FocusRideWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::StaffWindowIntent>)
                {
                    FocusStaffWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::GuestWindowIntent>)
                {
                    FocusGuestWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::ParkWindowIntent>)
                {
                    FocusParkWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::FinancesWindowIntent>)
                {
                    FocusFinancesWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::ResearchWindowIntent>)
                {
                    FocusResearchWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::ConstructRideIntent>)
                {
                    FocusConstructRideWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::RideListWindowIntent>)
                {
                    FocusRideListWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::StaffListWindowIntent>)
                {
                    FocusStaffListWindow(target);
                }
                else if constexpr (std::is_same_v<T, Telemetry::GenericWindowIntent>)
                {
                    FocusGenericWindow(target);
                }
            },
            intent);
    }

    void AIAgentFollowController::FocusRideWindow(const Telemetry::RideWindowIntent& intent)
    {
        auto* ride = GetRide(intent.rideId);
        if (ride == nullptr)
        {
            return;
        }

        auto* windowMgr = GetWindowManager();
        bool existed = windowMgr != nullptr
            && windowMgr->FindByNumber(WindowClass::ride, ride->id.ToUnderlying()) != nullptr;

        auto* window = UiWindows::RideMainOpen(*ride);
        if (window == nullptr)
        {
            return;
        }
        TrackManagedWindow(window->classification, window->number, !existed);
        if (intent.page != AIAgentRideWindowPage::Main)
        {
            UiWindows::RideWindowSetPageByName(intent.rideId, RidePageTokenForUi(intent.page));
        }
    }

void AIAgentFollowController::FocusStaffWindow(const Telemetry::StaffWindowIntent& intent)
{
    auto& gameState = getGameState();
    auto* staff = gameState.entities.GetEntity<Staff>(intent.staffId);
    if (staff == nullptr)
    {
        return;
    }
    auto* windowMgr = GetWindowManager();
    bool existed = windowMgr != nullptr
        && windowMgr->FindByNumber(WindowClass::peep, staff->Id.ToUnderlying()) != nullptr;
    auto* window = UiWindows::StaffOpen(staff);
    if (window == nullptr)
    {
        return;
    }
    TrackManagedWindow(window->classification, window->number, !existed);
}

void AIAgentFollowController::FocusGuestWindow(const Telemetry::GuestWindowIntent& intent)
{
    auto& gameState = getGameState();
    auto* guest = gameState.entities.GetEntity<Guest>(intent.guestId);
    if (guest == nullptr)
    {
        return;
    }

    auto* windowMgr = GetWindowManager();
    bool existed = windowMgr != nullptr
        && windowMgr->FindByNumber(WindowClass::peep, guest->Id.ToUnderlying()) != nullptr;

    auto* window = UiWindows::GuestOpen(guest);
    if (window == nullptr)
    {
        return;
    }

    TrackManagedWindow(window->classification, window->number, !existed);
}

    void AIAgentFollowController::FocusParkWindow(const Telemetry::ParkWindowIntent& intent)
    {
        auto* windowMgr = GetWindowManager();
        bool existed = windowMgr != nullptr && windowMgr->FindByClass(WindowClass::parkInformation) != nullptr;
        WindowBase* window = nullptr;
        switch (intent.page)
        {
            case AIAgentParkWindowPage::Entrance:
                window = UiWindows::ParkEntranceOpen();
                break;
            case AIAgentParkWindowPage::Rating:
                window = UiWindows::ParkRatingOpen();
                break;
            case AIAgentParkWindowPage::Guests:
                window = UiWindows::ParkGuestsOpen();
                break;
            case AIAgentParkWindowPage::Price:
                window = UiWindows::ParkPriceOpen();
                break;
            case AIAgentParkWindowPage::Stats:
                window = UiWindows::ParkStatsOpen();
                break;
            case AIAgentParkWindowPage::Objective:
                window = UiWindows::ParkObjectiveOpen();
                break;
            case AIAgentParkWindowPage::Awards:
                window = UiWindows::ParkAwardsOpen();
                break;
        }
        if (window != nullptr)
        {
            TrackManagedWindow(window->classification, window->number, !existed);
        }
    }

    void AIAgentFollowController::FocusFinancesWindow(const Telemetry::FinancesWindowIntent& intent)
    {
        auto* windowMgr = GetWindowManager();
        bool existed = windowMgr != nullptr && windowMgr->FindByClass(WindowClass::finances) != nullptr;
        WindowBase* window = nullptr;
        switch (intent.page)
        {
            case AIAgentFinancesWindowPage::Marketing:
                window = UiWindows::FinancesMarketingOpen();
                break;
            case AIAgentFinancesWindowPage::Research:
                window = UiWindows::FinancesResearchOpen();
                break;
            case AIAgentFinancesWindowPage::Summary:
            default:
                window = UiWindows::FinancesOpen();
                break;
        }
        if (window != nullptr)
        {
            TrackManagedWindow(window->classification, window->number, !existed);
        }
    }

    void AIAgentFollowController::FocusResearchWindow(const Telemetry::ResearchWindowIntent& intent)
    {
        auto* windowMgr = GetWindowManager();
        bool existed = windowMgr != nullptr && windowMgr->FindByClass(WindowClass::research) != nullptr;
        WindowBase* window = nullptr;
        switch (intent.page)
        {
            case AIAgentResearchWindowPage::Funding:
                window = UiWindows::ResearchFundingOpen();
                break;
            case AIAgentResearchWindowPage::Development:
            default:
                window = UiWindows::ResearchOpen();
                break;
        }
        if (window != nullptr)
        {
            TrackManagedWindow(window->classification, window->number, !existed);
        }
    }

    void AIAgentFollowController::FocusConstructRideWindow(const Telemetry::ConstructRideIntent& intent)
    {
        auto* windowMgr = GetWindowManager();
        bool existed = windowMgr != nullptr && windowMgr->FindByClass(WindowClass::constructRide) != nullptr;
        WindowBase* window = nullptr;
        switch (intent.tab)
        {
            case AIAgentConstructRideTab::Shop:
                window = UiWindows::NewRideOpenShops();
                break;
            case AIAgentConstructRideTab::Research:
                window = UiWindows::NewRideOpenResearch();
                break;
            default:
                window = UiWindows::NewRideOpen();
                break;
        }
        if (window != nullptr)
        {
            TrackManagedWindow(window->classification, window->number, !existed);
        }
    }

    // Maps AIAgentRideListColumn enum to the window's InformationType (which is an integer)
    // The values match 1:1 since AIAgentRideListColumn was defined to mirror the window enum
    static std::optional<int32_t> MapColumnToInformationType(std::optional<Telemetry::AIAgentRideListColumn> column)
    {
        if (!column.has_value())
        {
            return std::nullopt;
        }
        return static_cast<int32_t>(*column);
    }

    void AIAgentFollowController::FocusRideListWindow(const Telemetry::RideListWindowIntent& intent)
    {
        auto* windowMgr = GetWindowManager();
        bool existed = windowMgr != nullptr && windowMgr->FindByClass(WindowClass::rideList) != nullptr;

        // Determine page index: PAGE_RIDES = 0, PAGE_SHOPS_AND_STALLS = 1, PAGE_KIOSKS_AND_FACILITIES = 2
        int32_t pageIndex = 0;
        switch (intent.filter)
        {
            case Telemetry::AIAgentRideListFilter::Rides:
                pageIndex = 0; // PAGE_RIDES
                break;
            case Telemetry::AIAgentRideListFilter::Shops:
                pageIndex = 1; // PAGE_SHOPS_AND_STALLS
                break;
            case Telemetry::AIAgentRideListFilter::Facilities:
                pageIndex = 2; // PAGE_KIOSKS_AND_FACILITIES
                break;
        }

        // Map column enum to information type
        auto informationType = MapColumnToInformationType(intent.column);

        WindowBase* window = UiWindows::RideListOpenWithConfig(pageIndex, informationType, intent.sortDescending);
        if (window != nullptr)
        {
            TrackManagedWindow(window->classification, window->number, !existed);
        }
    }

    void AIAgentFollowController::FocusStaffListWindow(const Telemetry::StaffListWindowIntent& intent)
    {
        auto* windowMgr = GetWindowManager();
        bool existed = windowMgr != nullptr && windowMgr->FindByClass(WindowClass::staffList) != nullptr;
        WindowBase* window = nullptr;
        if (intent.tab)
        {
            window = UiWindows::StaffListOpenToTab(static_cast<uint8_t>(*intent.tab));
        }
        else
        {
            window = UiWindows::StaffListOpen();
        }
        if (window != nullptr)
        {
            TrackManagedWindow(window->classification, window->number, !existed);
        }
    }

    void AIAgentFollowController::FocusGenericWindow(const Telemetry::GenericWindowIntent& intent)
    {
        auto* windowMgr = GetWindowManager();
        if (windowMgr == nullptr)
        {
            return;
        }

        WindowBase* window = nullptr;
        bool existed = windowMgr->FindByClass(intent.windowClass) != nullptr;

        switch (intent.windowClass)
        {
            case WindowClass::finances:
                window = UiWindows::FinancesOpen();
                break;
            case WindowClass::land:
                window = UiWindows::LandOpen();
                break;
            case WindowClass::water:
                window = UiWindows::WaterOpen();
                break;
            case WindowClass::research:
                window = UiWindows::ResearchOpen();
                break;
            case WindowClass::constructRide:
                window = UiWindows::NewRideOpen();
                break;
            default:
                window = windowMgr->OpenWindow(intent.windowClass);
                break;
        }

        if (window != nullptr)
        {
            TrackManagedWindow(window->classification, window->number, !existed);
        }
    }

    void AIAgentFollowController::CloseManagedWindow()
    {
        if (!_managedWindow)
        {
            return;
        }
        auto* windowMgr = GetWindowManager();
        if (windowMgr == nullptr)
        {
            // During shutdown, window manager may already be destroyed
            _managedWindow.reset();
            return;
        }
        windowMgr->CloseByNumber(_managedWindow->first, _managedWindow->second);
        _managedWindow.reset();
    }

    void AIAgentFollowController::TrackManagedWindow(WindowClass cls, WindowNumber number, bool created)
    {
        if (!created)
        {
            return;
        }
        if (_managedWindow && (_managedWindow->first != cls || _managedWindow->second != number))
        {
            CloseManagedWindow();
        }
        _managedWindow = std::make_pair(cls, number);
    }

    bool AIAgentFollowController::ShouldMoveCamera([[maybe_unused]] const CoordsXYZ& target) const
    {
        // Always re-center the viewport on the target, even if it's already visible.
        // This provides consistent camera behavior for AI agent hint-based navigation.
        auto* mainWindow = WindowGetMain();
        if (mainWindow == nullptr)
        {
            return false;
        }
        auto* viewport = WindowGetViewport(mainWindow);
        if (viewport == nullptr)
        {
            return false;
        }
        return true;
    }

    std::pair<float, float> AIAgentFollowController::CalculateUnobscuredViewportCenter(Viewport* viewport) const
    {
        constexpr float kDefaultCenter = 0.5f;

        if (viewport == nullptr)
        {
            return { kDefaultCenter, kDefaultCenter };
        }

        // Get viewport screen bounds
        const int32_t vpLeft = viewport->pos.x;
        const int32_t vpRight = viewport->pos.x + viewport->width;
        const int32_t vpTop = viewport->pos.y;
        const int32_t vpBottom = viewport->pos.y + viewport->height;

        // Find the AI Agent Terminal window
        WindowBase* terminalWindow = nullptr;
        WindowVisitEach([&terminalWindow](WindowBase* w) {
            if (w != nullptr && w->classification == WindowClass::aiAgentTerminal)
            {
                terminalWindow = w;
            }
        });

        if (terminalWindow == nullptr)
        {
            return { kDefaultCenter, kDefaultCenter };
        }

        // Get terminal window bounds
        const int32_t termLeft = terminalWindow->windowPos.x;
        const int32_t termRight = terminalWindow->windowPos.x + terminalWindow->width;
        const int32_t termTop = terminalWindow->windowPos.y;
        const int32_t termBottom = terminalWindow->windowPos.y + terminalWindow->height;

        // Check for horizontal overlap with viewport
        const bool hasHorizontalOverlap = termLeft < vpRight && termRight > vpLeft;
        const bool hasVerticalOverlap = termTop < vpBottom && termBottom > vpTop;

        if (!hasHorizontalOverlap || !hasVerticalOverlap)
        {
            // No overlap, use default center
            return { kDefaultCenter, kDefaultCenter };
        }

        // Calculate the unobscured horizontal region
        // The terminal typically sits on the right side, so we find the left unobscured portion
        int32_t unobscuredLeft = vpLeft;
        int32_t unobscuredRight = vpRight;

        if (termLeft > vpLeft && termLeft < vpRight)
        {
            // Terminal is on the right side - unobscured is left of terminal
            unobscuredRight = termLeft;
        }
        else if (termRight > vpLeft && termRight < vpRight)
        {
            // Terminal is on the left side - unobscured is right of terminal
            unobscuredLeft = termRight;
        }

        // Ensure we have a valid region
        if (unobscuredRight <= unobscuredLeft)
        {
            return { kDefaultCenter, kDefaultCenter };
        }

        // Calculate center of unobscured region as a fraction of viewport width
        const float unobscuredCenterX = static_cast<float>(unobscuredLeft + unobscuredRight) / 2.0f;
        const float xFraction = (unobscuredCenterX - static_cast<float>(vpLeft)) / static_cast<float>(viewport->width);

        // Keep Y centered (could do similar calculation for vertical if needed)
        return { xFraction, kDefaultCenter };
    }

    void AIAgentFollowController::MoveCamera(const CoordsXYZ& target) const
    {
        auto* mainWindow = WindowGetMain();
        if (mainWindow == nullptr || mainWindow->viewport == nullptr)
        {
            return;
        }
        // Set flag so viewport clamping applies during AI agent camera transitions
        mainWindow->flags.set(WindowFlag::clampScrollToPark);

        // Temporarily clear noScrolling so agent-driven scrolling works
        // even when viewport lock is active (blocking user manual scroll)
        const bool wasNoScrolling = mainWindow->flags.has(WindowFlag::noScrolling);
        if (wasNoScrolling)
        {
            mainWindow->flags.unset(WindowFlag::noScrolling);
        }

        auto [xBias, yBias] = CalculateUnobscuredViewportCenter(mainWindow->viewport);
        WindowScrollToLocationWithBias(*mainWindow, target, xBias, yBias);

        // Restore noScrolling flag if it was set
        if (wasNoScrolling)
        {
            mainWindow->flags.set(WindowFlag::noScrolling);
        }
    }
} // namespace OpenRCT2::Ui::AIAgent
