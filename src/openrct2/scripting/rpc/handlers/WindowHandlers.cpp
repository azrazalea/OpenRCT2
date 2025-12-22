/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

#include "../HandlerRegistry.h"
#include "HandlerInit.h"
#include "../RpcTypes.h"
#include "../RpcUtils.h"

#include "../../../core/EnumUtils.hpp"
#include "../../../interface/Window.h"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatter.h"
#include "../../../localisation/Formatting.h"
#include "../../../localisation/StringIdType.h"
#include "../../../localisation/StringIds.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../ui/WindowManager.h"
#include "../../../windows/Intent.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For kError* constants

    namespace
    {
        struct WindowClassLabel
        {
            WindowClass cls;
            const char* name;
        };

        constexpr WindowClassLabel kWindowClassLabels[] = {
            { WindowClass::mainWindow, "mainWindow" },
            { WindowClass::topToolbar, "topToolbar" },
            { WindowClass::bottomToolbar, "bottomToolbar" },
            { WindowClass::tooltip, "tooltip" },
            { WindowClass::dropdown, "dropdown" },
            { WindowClass::about, "about" },
            { WindowClass::error, "error" },
            { WindowClass::ride, "ride" },
            { WindowClass::rideConstruction, "rideConstruction" },
            { WindowClass::savePrompt, "savePrompt" },
            { WindowClass::rideList, "rideList" },
            { WindowClass::constructRide, "constructRide" },
            { WindowClass::demolishRidePrompt, "demolishRidePrompt" },
            { WindowClass::scenery, "scenery" },
            { WindowClass::options, "options" },
            { WindowClass::footpath, "footpath" },
            { WindowClass::land, "land" },
            { WindowClass::water, "water" },
            { WindowClass::peep, "peep" },
            { WindowClass::guestList, "guestList" },
            { WindowClass::staffList, "staffList" },
            { WindowClass::firePrompt, "firePrompt" },
            { WindowClass::parkInformation, "parkInformation" },
            { WindowClass::finances, "finances" },
            { WindowClass::titleMenu, "titleMenu" },
            { WindowClass::titleExit, "titleExit" },
            { WindowClass::recentNews, "recentNews" },
            { WindowClass::scenarioSelect, "scenarioSelect" },
            { WindowClass::trackDesignList, "trackDesignList" },
            { WindowClass::trackDesignPlace, "trackDesignPlace" },
            { WindowClass::newCampaign, "newCampaign" },
            { WindowClass::keyboardShortcutList, "keyboardShortcutList" },
            { WindowClass::changeKeyboardShortcut, "changeKeyboardShortcut" },
            { WindowClass::map, "map" },
            { WindowClass::titleLogo, "titleLogo" },
            { WindowClass::banner, "banner" },
            { WindowClass::mapTooltip, "mapTooltip" },
            { WindowClass::editorObjectSelection, "editorObjectSelection" },
            { WindowClass::editorInventionList, "editorInventionList" },
            { WindowClass::editorInventionListDrag, "editorInventionListDrag" },
            { WindowClass::editorScenarioOptions, "editorScenarioOptions" },
            { WindowClass::manageTrackDesign, "manageTrackDesign" },
            { WindowClass::trackDeletePrompt, "trackDeletePrompt" },
            { WindowClass::installTrack, "installTrack" },
            { WindowClass::clearScenery, "clearScenery" },
            { WindowClass::sceneryScatter, "sceneryScatter" },
            { WindowClass::cheats, "cheats" },
            { WindowClass::research, "research" },
            { WindowClass::viewport, "viewport" },
            { WindowClass::textinput, "textinput" },
            { WindowClass::mapgen, "mapgen" },
            { WindowClass::loadsave, "loadsave" },
            { WindowClass::loadsaveOverwritePrompt, "loadsaveOverwritePrompt" },
            { WindowClass::titleOptions, "titleOptions" },
            { WindowClass::landRights, "landRights" },
            { WindowClass::themes, "themes" },
            { WindowClass::tileInspector, "tileInspector" },
            { WindowClass::changelog, "changelog" },
            { WindowClass::multiplayer, "multiplayer" },
            { WindowClass::player, "player" },
            { WindowClass::networkStatus, "networkStatus" },
            { WindowClass::serverList, "serverList" },
            { WindowClass::serverStart, "serverStart" },
            { WindowClass::customCurrencyConfig, "customCurrencyConfig" },
            { WindowClass::debugPaint, "debugPaint" },
            { WindowClass::viewClipping, "viewClipping" },
            { WindowClass::objectLoadError, "objectLoadError" },
            { WindowClass::patrolArea, "patrolArea" },
            { WindowClass::transparency, "transparency" },
            { WindowClass::assetPacks, "assetPacks" },
            { WindowClass::progressWindow, "progressWindow" },
            { WindowClass::titleVersion, "titleVersion" },
            { WindowClass::editorParkEntrance, "editorParkEntrance" },
            { WindowClass::aiAgentTerminal, "aiAgentTerminal" },
        };

        std::string_view WindowClassToString(WindowClass cls)
        {
            for (const auto& entry : kWindowClassLabels)
            {
                if (entry.cls == cls)
                {
                    return entry.name;
                }
            }
            return "unknown";
        }

        std::optional<WindowClass> WindowClassFromString(std::string value)
        {
            auto normalised = NormaliseClassKey(std::move(value));
            for (const auto& entry : kWindowClassLabels)
            {
                auto entryKey = NormaliseClassKey(entry.name);
                if (entryKey == normalised)
                {
                    return entry.cls;
                }
            }
            return std::nullopt;
        }

        bool IsWindowClassProtected(WindowClass cls)
        {
            switch (cls)
            {
                case WindowClass::mainWindow:
                case WindowClass::viewport:
                case WindowClass::topToolbar:
                case WindowClass::bottomToolbar:
                case WindowClass::titleMenu:
                case WindowClass::titleLogo:
                case WindowClass::titleOptions:
                case WindowClass::titleExit:
                case WindowClass::titleVersion:
                case WindowClass::progressWindow:
                case WindowClass::mapTooltip:
                case WindowClass::dropdown:
                case WindowClass::tooltip:
                case WindowClass::textinput:
                case WindowClass::loadsave:
                case WindowClass::loadsaveOverwritePrompt:
                case WindowClass::aiAgentTerminal:
                    return true;
                default:
                    return false;
            }
        }

        std::optional<WindowClass> ResolveWindowClassParam(const json_t& params)
        {
            if (auto classId = GetIntParam(params, "classId"))
            {
                auto raw = *classId;
                if (raw >= 0 && raw <= 255)
                {
                    return static_cast<WindowClass>(raw);
                }
            }
            if (auto className = GetStringParam(params, "class"))
            {
                return WindowClassFromString(*className);
            }
            return std::nullopt;
        }

        std::string GetWindowTitle(const WindowBase& window)
        {
            if (window.widgets.size() < 2 || window.flags.has(WindowFlag::noTitleBar))
            {
                return {};
            }

            const auto& titleWidget = window.widgets[1];
            if (titleWidget.type != WidgetType::caption)
            {
                return {};
            }

            // Pre-formatted string - safe to use directly
            if (titleWidget.flags.has(WidgetFlag::textIsString) && titleWidget.string != nullptr)
            {
                return std::string(reinterpret_cast<const char*>(titleWidget.string));
            }

            // String ID that needs formatting - use common format args
            // Note: This may not produce correct titles for all windows since
            // windows typically set up their format args in OnPrepareDraw
            if (titleWidget.text != kStringIdNone)
            {
                try
                {
                    char buffer[256]{};
                    FormatStringLegacy(buffer, sizeof(buffer), titleWidget.text, Formatter::Common().Data());
                    return std::string(buffer);
                }
                catch (...)
                {
                    // Formatting failed - return empty string
                    return {};
                }
            }

            return {};
        }

        json_t BuildWindowListPayload()
        {
            // Snapshot window pointers to avoid race conditions with UI thread
            std::vector<WindowBase*> windowSnapshot;
            for (const auto& entry : OpenRCT2::gWindowList)
            {
                if (entry == nullptr)
                    continue;
                auto* window = entry.get();
                if (window == nullptr)
                    continue;
                if (window->flags.has(WindowFlag::dead))
                    continue;
                windowSnapshot.push_back(window);
            }

            json_t windows = json_t::array();
            size_t zIndex = 0;
            for (auto* window : windowSnapshot)
            {
                // Re-check validity in case window was closed between snapshot and now
                if (window == nullptr)
                    continue;

                try
                {
                    json_t node = json_t::object();
                    node["id"] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window));
                    node["classId"] = EnumValue(window->classification);
                    node["class"] = std::string(WindowClassToString(window->classification));
                    node["number"] = window->number;
                    node["x"] = window->windowPos.x;
                    node["y"] = window->windowPos.y;
                    node["width"] = window->width;
                    node["height"] = window->height;
                    node["visible"] = window->isVisible;
                    node["flashing"] = window->flashTimer > 0;
                    node["hasViewport"] = window->viewport != nullptr;
                    node["flags"] = window->flags.holder;
                    node["zIndex"] = zIndex++;
                    node["protected"] = IsWindowClassProtected(window->classification);

                    // canClose() is a virtual function - wrap in try-catch for safety
                    bool canClose = false;
                    try
                    {
                        canClose = window->canClose() && !IsWindowClassProtected(window->classification);
                    }
                    catch (...)
                    {
                        canClose = false;
                    }
                    node["canClose"] = canClose;

                    auto title = GetWindowTitle(*window);
                    if (!title.empty())
                    {
                        node["title"] = title;
                    }

                    windows.push_back(node);
                }
                catch (...)
                {
                    // Skip this window if any exception occurs
                    continue;
                }
            }

            json_t payload = json_t::object();
            payload["windows"] = windows;
            payload["count"] = windows.size();
            return payload;
        }

        template<typename TPredicate>
        size_t CountWindowsMatching(TPredicate&& predicate)
        {
            size_t count = 0;
            for (const auto& entry : OpenRCT2::gWindowList)
            {
                if (entry == nullptr)
                    continue;
                const auto* window = entry.get();
                if (window == nullptr)
                    continue;
                if (window->flags.has(WindowFlag::dead))
                    continue;
                if (predicate(*window))
                {
                    count++;
                }
            }
            return count;
        }

        Telemetry::AIAgentFollowHint MakeWindowHint(std::string_view method, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.requestCameraFocus = false;
            hint.requestWindowFocus = false;
            return hint;
        }

        RpcResult HandleWindowsList(const json_t& /*params*/)
        {
            auto payload = BuildWindowListPayload();
            auto hint = MakeWindowHint("windows.list", "Listed open windows");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        std::optional<uint8_t> ParseRideCategoryForTab(const std::string& str)
        {
            // Maps category strings to NewRideTabId values (0-5)
            if (str == "transport")
                return 0; // TRANSPORT_TAB
            if (str == "gentle")
                return 1; // GENTLE_TAB
            if (str == "rollerCoaster" || str == "roller_coaster" || str == "coaster")
                return 2; // ROLLER_COASTER_TAB
            if (str == "thrill")
                return 3; // THRILL_TAB
            if (str == "water")
                return 4; // WATER_TAB
            if (str == "shop")
                return 5; // SHOP_TAB
            return std::nullopt;
        }

        RpcResult HandleWindowsOpenRideConstruction(const json_t& params)
        {
            auto* windowMgr = Ui::GetWindowManager();
            if (windowMgr == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Window manager unavailable");
            }

            std::optional<uint8_t> categoryTab;
            if (params.is_object())
            {
                if (auto catParam = GetStringParam(params, "category"))
                {
                    categoryTab = ParseRideCategoryForTab(*catParam);
                    if (!categoryTab)
                    {
                        return RpcResult::Error(
                            kErrorInvalidParams,
                            "Unknown category '" + *catParam
                                + "'. Valid: transport, gentle, rollerCoaster, thrill, water, shop");
                    }
                }
            }

            Intent intent(INTENT_ACTION_NEW_RIDE_OF_CATEGORY);
            intent.PutExtra(INTENT_EXTRA_PAGE, static_cast<uint32_t>(categoryTab.value_or(0)));
            windowMgr->OpenIntent(&intent);

            json_t payload = json_t::object();
            payload["opened"] = true;
            if (categoryTab)
            {
                payload["category"] = *categoryTab;
            }

            std::string contextLabel = "Opened ride construction window";
            auto hint = MakeWindowHint("windows.openRideConstruction", std::move(contextLabel));
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        RpcResult HandleWindowsClose(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto* windowMgr = Ui::GetWindowManager();
            if (windowMgr == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Window manager unavailable");
            }

            size_t closed = 0;
            bool handled = false;

            if (auto it = params.find("id"); it != params.end())
            {
                uint64_t handle = 0;
                if (it->is_string())
                {
                    try
                    {
                        auto text = it->get<std::string>();
                        size_t processed = 0;
                        handle = std::stoull(text, &processed, 0);
                        if (processed != text.size())
                        {
                            throw std::runtime_error("invalid");
                        }
                    }
                    catch (const std::exception&)
                    {
                        return RpcResult::Error(
                            kErrorInvalidParams, "id must be an integer (decimal or 0x-prefixed hex)");
                    }
                }
                else if (it->is_number_unsigned())
                {
                    handle = it->get<uint64_t>();
                }
                else if (it->is_number_integer())
                {
                    auto raw = it->get<int64_t>();
                    if (raw < 0)
                    {
                        return RpcResult::Error(kErrorInvalidParams, "id must be positive");
                    }
                    handle = static_cast<uint64_t>(raw);
                }
                else
                {
                    return RpcResult::Error(kErrorInvalidParams, "id must be numeric");
                }

                auto* targetPtr = reinterpret_cast<WindowBase*>(static_cast<uintptr_t>(handle));
                for (const auto& entry : OpenRCT2::gWindowList)
                {
                    if (entry == nullptr)
                        continue;
                    auto* window = entry.get();
                    if (window->flags.has(WindowFlag::dead))
                        continue;
                    if (window == targetPtr)
                    {
                        if (IsWindowClassProtected(window->classification))
                        {
                            return RpcResult::Error(kErrorActionFailed, "Closing this window is not allowed");
                        }
                        windowMgr->Close(*window);
                        closed = 1;
                        handled = true;
                        break;
                    }
                }
                if (!handled)
                {
                    return RpcResult::Error(kErrorNotFound, "Window not found");
                }
            }
            else
            {
                auto windowClass = ResolveWindowClassParam(params);
                if (!windowClass)
                {
                    return RpcResult::Error(kErrorInvalidParams, "id or class is required");
                }
                if (IsWindowClassProtected(*windowClass))
                {
                    return RpcResult::Error(kErrorActionFailed, "Closing this window class is not allowed");
                }

                if (auto numberParam = GetIntParam(params, "number"))
                {
                    const auto number = static_cast<WindowNumber>(*numberParam);
                    closed = CountWindowsMatching(
                        [&](const WindowBase& w) { return w.classification == *windowClass && w.number == number; });
                    windowMgr->CloseByNumber(*windowClass, number);
                }
                else
                {
                    closed =
                        CountWindowsMatching([&](const WindowBase& w) { return w.classification == *windowClass; });
                    windowMgr->CloseByClass(*windowClass);
                }
                handled = true;
                if (closed == 0)
                {
                    return RpcResult::Error(kErrorNotFound, "No windows matched the criteria");
                }
            }

            auto payload = BuildWindowListPayload();
            payload["closed"] = closed;
            payload["handled"] = handled;
            std::string contextLabel;
            if (closed == 0)
            {
                contextLabel = "No windows closed";
            }
            else if (closed == 1)
            {
                contextLabel = "Closed one window";
            }
            else
            {
                contextLabel = "Closed " + std::to_string(closed) + " windows";
            }
            auto hint = MakeWindowHint("windows.close", std::move(contextLabel));
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        // Static registration
        struct WindowHandlerRegistrar
        {
            WindowHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("windows.list", HandleWindowsList);
                registry.Register("windows.close", HandleWindowsClose);
                registry.Register("windows.openRideConstruction", HandleWindowsOpenRideConstruction);
            }
        } windowRegistrar;

    } // namespace

    void InitWindowHandlers()
    {
        (void)windowRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
