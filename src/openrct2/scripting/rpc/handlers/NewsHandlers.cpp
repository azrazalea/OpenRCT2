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

#include "../../../Date.h"
#include "../../../GameState.h"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatting.h"
#include "../../../management/NewsItem.h"
#include "../../../telemetry/AIAgentActivityFeed.h"

#include <array>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For shared types and utilities

    namespace
    {
        constexpr std::array<const char*, MONTH_COUNT> kMonthNames = {
            "March",
            "April",
            "May",
            "June",
            "July",
            "August",
            "September",
            "October",
        };

        std::string_view GetMonthName(int32_t monthIndex)
        {
            if (monthIndex < 0)
            {
                return kMonthNames[0];
            }
            return kMonthNames[monthIndex % static_cast<int32_t>(kMonthNames.size())];
        }

        std::string_view NewsItemTypeToString(News::ItemType type)
        {
            using News::ItemType;
            switch (type)
            {
                case ItemType::ride:
                    return "ride";
                case ItemType::peepOnRide:
                    return "peepOnRide";
                case ItemType::peep:
                    return "peep";
                case ItemType::money:
                    return "money";
                case ItemType::blank:
                    return "blank";
                case ItemType::research:
                    return "research";
                case ItemType::peeps:
                    return "peeps";
                case ItemType::award:
                    return "award";
                case ItemType::graph:
                    return "graph";
                case ItemType::campaign:
                    return "campaign";
                case ItemType::null:
                case ItemType::count:
                default:
                    return "unknown";
            }
        }

        json_t BuildNewsItemPayload(const News::Item& item, std::string_view source)
        {
            json_t payload = json_t::object();
            payload["type"] = NewsItemTypeToString(item.type);
            // Strip internal format codes like {YELLOW}, {RED}, {NEWLINE} for clean CLI output
            payload["text"] = FmtString(item.text).WithoutFormatTokens();
            // day is already 1-indexed when stored in NewsItem
            payload["day"] = item.day;
            // Convert month to 1-indexed to match what the game UI displays
            payload["month"] = DateGetMonth(item.monthYear) + 1;
            payload["monthName"] = GetMonthName(DateGetMonth(item.monthYear));
            payload["year"] = DateGetYear(item.monthYear) + 1;
            payload["ticks"] = item.ticks;
            payload["assoc"] = item.assoc;
            payload["hasButton"] = item.hasButton();
            payload["hasSubject"] = item.typeHasSubject();
            payload["hasLocation"] = item.typeHasLocation();
            payload["source"] = source;
            return payload;
        }

        json_t BuildNewsFeedPayload(bool includeArchived, size_t limit)
        {
            auto& newsQueues = getGameState().newsItems;
            json_t items = json_t::array();
            size_t emitted = 0;

            auto appendItem = [&](const News::Item& newsItem, std::string_view source) {
                if (newsItem.isEmpty())
                {
                    return;
                }
                if (limit != 0 && emitted >= limit)
                {
                    return;
                }
                items.push_back(BuildNewsItemPayload(newsItem, source));
                emitted++;
            };

            newsQueues.ForeachRecentNews([&](const News::Item& item) { appendItem(item, "recent"); });
            if (includeArchived && (limit == 0 || emitted < limit))
            {
                newsQueues.ForeachArchivedNews([&](const News::Item& item) { appendItem(item, "archived"); });
            }

            json_t payload = json_t::object();
            payload["items"] = items;
            payload["includesArchived"] = includeArchived;
            payload["requestedLimit"] = limit;
            return payload;
        }

        std::optional<int32_t> GetIntParam(const json_t& params, const char* key)
        {
            const auto it = params.find(key);
            if (it == params.end() || !it->is_number_integer())
            {
                return std::nullopt;
            }
            return it->get<int32_t>();
        }

        std::optional<bool> GetBoolParam(const json_t& params, const char* key)
        {
            const auto it = params.find(key);
            if (it == params.end())
            {
                return std::nullopt;
            }
            if (it->is_boolean())
            {
                return it->get<bool>();
            }
            if (it->is_number_integer())
            {
                return it->get<int64_t>() != 0;
            }
            return std::nullopt;
        }

        size_t ExtractLimitParam(const json_t& params)
        {
            if (!params.is_object())
            {
                return 0;
            }
            if (auto limitParam = GetIntParam(params, "limit"))
            {
                return static_cast<size_t>(std::max(0, *limitParam));
            }
            return 0;
        }

        Telemetry::AIAgentFollowHint MakeNewsHint(std::string_view method, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.requestCameraFocus = false;
            Telemetry::GenericWindowIntent intent;
            intent.windowClass = WindowClass::recentNews;
            hint.windowIntent = intent;
            return hint;
        }

        RpcResult HandleNewsRecent(const json_t& params)
        {
            size_t limit = ExtractLimitParam(params);
            bool includeArchived = false;
            if (params.is_object())
            {
                if (auto archivedParam = GetBoolParam(params, "includeArchived"))
                {
                    includeArchived = *archivedParam;
                }
            }
            auto payload = BuildNewsFeedPayload(includeArchived, limit);
            auto hint = MakeNewsHint("news.recent", "Reviewed recent news");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        RpcResult HandleNewsArchive(const json_t& params)
        {
            size_t limit = ExtractLimitParam(params);
            auto payload = BuildNewsFeedPayload(true, limit);
            auto hint = MakeNewsHint("news.archive", "Opened news archive");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        json_t BuildAwardsHistoryPayload(size_t limit)
        {
            auto& newsQueues = getGameState().newsItems;
            json_t items = json_t::array();
            size_t emitted = 0;

            auto appendAward = [&](const News::Item& item, std::string_view source) {
                if (item.isEmpty() || item.type != News::ItemType::award)
                {
                    return;
                }
                if (limit != 0 && emitted >= limit)
                {
                    return;
                }
                items.push_back(BuildNewsItemPayload(item, source));
                emitted++;
            };

            newsQueues.ForeachRecentNews([&](const News::Item& item) { appendAward(item, "recent"); });
            newsQueues.ForeachArchivedNews([&](const News::Item& item) { appendAward(item, "archived"); });

            json_t payload = json_t::object();
            payload["history"] = items;
            payload["limit"] = limit;
            return payload;
        }

        RpcResult HandleAwardsHistory(const json_t& params)
        {
            size_t limit = ExtractLimitParam(params);
            auto payload = BuildAwardsHistoryPayload(limit);
            auto hint = MakeNewsHint("awards.history", "Reviewed awards history");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        RpcResult HandleNewsOpenHistory(const json_t& /*params*/)
        {
            auto& newsQueues = getGameState().newsItems;
            size_t recentCount = 0;
            size_t archivedCount = 0;

            newsQueues.ForeachRecentNews([&](const News::Item& item) {
                if (!item.isEmpty())
                    recentCount++;
            });
            newsQueues.ForeachArchivedNews([&](const News::Item& item) {
                if (!item.isEmpty())
                    archivedCount++;
            });

            json_t payload = json_t::object();
            payload["recentCount"] = recentCount;
            payload["archivedCount"] = archivedCount;
            payload["totalCount"] = recentCount + archivedCount;

            auto hint = MakeNewsHint("news.openHistory", "Opened message history");
            hint.requestWindowFocus = true;
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        // Static registration
        struct NewsHandlerRegistrar
        {
            NewsHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("news.recent", HandleNewsRecent);
                registry.Register("news.archive", HandleNewsArchive);
                registry.Register("news.openHistory", HandleNewsOpenHistory);
                registry.Register("awards.history", HandleAwardsHistory);
            }
        } newsRegistrar;

    } // namespace

    void InitNewsHandlers()
    {
        (void)newsRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
