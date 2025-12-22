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

#include "../../../GameState.h"
#include "../../../actions/GameActionResult.h"
#include "../../../actions/ParkMarketingAction.h"
#include "../../../actions/ParkSetResearchFundingAction.h"
#include "../../../core/EnumUtils.hpp"
#include "../../../entity/Guest.h"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatter.h"
#include "../../../localisation/Formatting.h"
#include "../../../localisation/StringIds.h"
#include "../../../management/Marketing.h"
#include "../../../management/Research.h"
#include "../../../object/ObjectManager.h"
#include "../../../ride/Ride.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../world/Location.hpp"
#include "../../../world/Map.h"

#include <algorithm>
#include <vector>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For kError* constants

    namespace
    {
        // Utility functions
        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        int CompareCaseInsensitive(std::string_view lhs, std::string_view rhs)
        {
            auto left = ToLower(std::string(lhs));
            auto right = ToLower(std::string(rhs));
            if (left == right)
            {
                return 0;
            }
            return left < right ? -1 : 1;
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

        std::string ShopItemToString(ShopItem item)
        {
            if (item == ShopItem::none)
            {
                return {};
            }
            const auto& descriptor = GetShopItemDescriptor(item);
            auto nameId = descriptor.Naming.Plural;
            if (nameId == kStringIdNone)
            {
                nameId = descriptor.Naming.Singular;
            }
            return ResolveStringId(nameId);
        }

        std::string BuildRideDisplayName(const Ride& ride)
        {
            Formatter ft;
            ride.formatNameTo(ft);
            char buffer[256]{};
            FormatStringLegacy(buffer, sizeof(buffer), STR_STRINGID, ft.Data());
            return std::string(buffer);
        }

        std::optional<CoordsXYZ> BuildRideCameraTarget(const Ride& ride)
        {
            if (ride.overallView.IsNull())
            {
                return std::nullopt;
            }
            auto coords = ride.overallView;
            auto z = TileElementHeight(coords);
            return CoordsXYZ{ coords.x, coords.y, z };
        }

        std::optional<RideLookupResult> ResolveRideFromParams(const json_t& params, std::string& errorMessage)
        {
            std::optional<RideId> rideId;
            if (auto idParam = GetIntParam(params, "rideId"))
            {
                if (*idParam < 0 || *idParam >= std::numeric_limits<uint16_t>::max())
                {
                    errorMessage = "rideId must be between 0 and 65534";
                    return std::nullopt;
                }
                rideId = RideId::FromUnderlying(static_cast<uint16_t>(*idParam));
            }
            else if (auto altIdParam = GetIntParam(params, "id"))
            {
                if (*altIdParam < 0 || *altIdParam >= std::numeric_limits<uint16_t>::max())
                {
                    errorMessage = "id must be between 0 and 65534";
                    return std::nullopt;
                }
                rideId = RideId::FromUnderlying(static_cast<uint16_t>(*altIdParam));
            }

            if (rideId)
            {
                auto* ride = GetRide(*rideId);
                if (ride == nullptr || ride->id.IsNull())
                {
                    errorMessage = "Ride with id " + std::to_string(rideId->ToUnderlying()) + " not found";
                    return std::nullopt;
                }
                return RideLookupResult{ rideId.value(), ride };
            }

            auto rideNameParam = GetStringParam(params, "rideName");
            if (!rideNameParam)
            {
                rideNameParam = GetStringParam(params, "ride");
            }
            if (!rideNameParam)
            {
                rideNameParam = GetStringParam(params, "name");
            }
            if (rideNameParam)
            {
                const auto lowered = ToLower(*rideNameParam);
                auto& gameState = getGameState();
                for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
                {
                    auto& ride = gameState.rides[i];
                    if (ride.id.IsNull())
                    {
                        continue;
                    }
                    if (ToLower(ride.getName()) == lowered)
                    {
                        return RideLookupResult{ ride.id, &ride };
                    }
                }
                errorMessage = "Ride named '" + *rideNameParam + "' not found";
                return std::nullopt;
            }

            errorMessage = "rideId or rideName must be specified";
            return std::nullopt;
        }

        // Research-specific functions
        std::optional<ResearchCategory> ResearchCategoryFromString(std::string value)
        {
            auto lowered = ToLower(std::move(value));
            if (lowered == "transport")
            {
                return ResearchCategory::transport;
            }
            if (lowered == "gentle")
            {
                return ResearchCategory::gentle;
            }
            if (lowered == "rollercoaster" || lowered == "coaster")
            {
                return ResearchCategory::rollercoaster;
            }
            if (lowered == "thrill")
            {
                return ResearchCategory::thrill;
            }
            if (lowered == "water")
            {
                return ResearchCategory::water;
            }
            if (lowered == "shop" || lowered == "shops")
            {
                return ResearchCategory::shop;
            }
            if (lowered == "scenery" || lowered == "scenerygroup")
            {
                return ResearchCategory::sceneryGroup;
            }
            return std::nullopt;
        }

        std::string_view ResearchCategoryToKey(ResearchCategory category)
        {
            switch (category)
            {
                case ResearchCategory::transport:
                    return "transport";
                case ResearchCategory::gentle:
                    return "gentle";
                case ResearchCategory::rollercoaster:
                    return "rollercoaster";
                case ResearchCategory::thrill:
                    return "thrill";
                case ResearchCategory::water:
                    return "water";
                case ResearchCategory::shop:
                    return "shop";
                case ResearchCategory::sceneryGroup:
                    return "scenery";
                default:
                    return "unknown";
            }
        }

        std::string_view ResearchFundingLevelToString(uint8_t level)
        {
            switch (level)
            {
                case RESEARCH_FUNDING_NONE:
                    return "none";
                case RESEARCH_FUNDING_MINIMUM:
                    return "minimum";
                case RESEARCH_FUNDING_NORMAL:
                    return "normal";
                case RESEARCH_FUNDING_MAXIMUM:
                    return "maximum";
                default:
                    return "unknown";
            }
        }

        std::optional<uint8_t> ResearchFundingLevelFromString(std::string value)
        {
            auto lowered = ToLower(std::move(value));
            if (lowered == "none")
            {
                return RESEARCH_FUNDING_NONE;
            }
            if (lowered == "minimum" || lowered == "min")
            {
                return RESEARCH_FUNDING_MINIMUM;
            }
            if (lowered == "normal")
            {
                return RESEARCH_FUNDING_NORMAL;
            }
            if (lowered == "maximum" || lowered == "max")
            {
                return RESEARCH_FUNDING_MAXIMUM;
            }
            return std::nullopt;
        }

        json_t BuildResearchItemPayload(const ResearchItem& item)
        {
            json_t node = json_t::object();
            node["category"] = ResolveStringId(item.GetCategoryName());
            node["categoryKey"] = ResearchCategoryToKey(item.category);
            node["entryIndex"] = item.entryIndex;
            node["baseRideType"] = item.baseRideType;
            node["flags"] = item.flags;
            node["type"] = item.type == OpenRCT2::Research::EntryType::ride ? "ride" : "scenery";
            node["name"] = ResolveStringId(item.GetName());
            return node;
        }

        enum class ResearchQueueOrder
        {
            Scenario,
            Name,
            Category,
        };

        struct ResearchStatusQuery
        {
            ResearchQueueOrder queueOrder = ResearchQueueOrder::Scenario;
            bool descending = false;
            bool directionSpecified = false;
            std::vector<ResearchCategory> categories;
            size_t queueLimit = 0;
            bool limitEnabled = false;
        };

        double CalculateResearchProgressPercent(uint16_t progress, uint8_t stage)
        {
            constexpr double kResearchProgressUnits = 65535.0;
            if (stage == RESEARCH_STAGE_FINISHED_ALL)
            {
                return 100.0;
            }
            if (stage == RESEARCH_STAGE_INITIAL_RESEARCH)
            {
                return 0.0;
            }
            auto percent = (static_cast<double>(progress) / kResearchProgressUnits) * 100.0;
            return std::clamp(percent, 0.0, 100.0);
        }

        bool ParseResearchStatusQuery(const json_t& params, ResearchStatusQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
            {
                return true;
            }
            if (auto limit = GetIntParam(params, "queueLimit"))
            {
                if (*limit <= 0)
                {
                    errorMessage = "queueLimit must be positive";
                    return false;
                }
                query.queueLimit = static_cast<size_t>(*limit);
                query.limitEnabled = true;
            }
            if (auto categories = params.find("queueCategories"); categories != params.end())
            {
                if (!categories->is_array())
                {
                    errorMessage = "queueCategories must be an array";
                    return false;
                }
                for (const auto& entry : *categories)
                {
                    if (!entry.is_string())
                    {
                        continue;
                    }
                    auto category = ResearchCategoryFromString(entry.get<std::string>());
                    if (!category)
                    {
                        errorMessage = "Unknown research category in queue filter";
                        return false;
                    }
                    query.categories.push_back(*category);
                }
            }
            if (auto orderParam = GetStringParam(params, "queueOrder"))
            {
                auto lowered = ToLower(*orderParam);
                if (lowered == "scenario" || lowered == "default")
                {
                    query.queueOrder = ResearchQueueOrder::Scenario;
                }
                else if (lowered == "name")
                {
                    query.queueOrder = ResearchQueueOrder::Name;
                }
                else if (lowered == "category")
                {
                    query.queueOrder = ResearchQueueOrder::Category;
                }
                else
                {
                    errorMessage = "Unknown queue order (use scenario, name, or category)";
                    return false;
                }
            }
            if (auto directionParam = GetStringParam(params, "queueDirection"))
            {
                auto lowered = ToLower(*directionParam);
                if (lowered == "asc" || lowered == "ascending" || lowered == "up")
                {
                    query.descending = false;
                }
                else if (lowered == "desc" || lowered == "descending" || lowered == "down")
                {
                    query.descending = true;
                }
                else
                {
                    errorMessage = "Unknown queue direction (use asc or desc)";
                    return false;
                }
                query.directionSpecified = true;
            }
            return true;
        }

        json_t BuildResearchStatusPayload(const ResearchStatusQuery& query)
        {
            const auto& gameState = getGameState();
            json_t payload = json_t::object();
            payload["fundingLevel"] = ResearchFundingLevelToString(gameState.researchFundingLevel);
            payload["fundingIndex"] = gameState.researchFundingLevel;
            payload["progress"] = gameState.researchProgress;
            payload["progressStage"] = gameState.researchProgressStage;
            payload["progressPercent"] = CalculateResearchProgressPercent(gameState.researchProgress, gameState.researchProgressStage);
            payload["expectedMonth"] = gameState.researchExpectedMonth;
            payload["expectedDay"] = gameState.researchExpectedDay;

            json_t priorities = json_t::object();
            auto addPriority = [&](ResearchCategory category, std::string_view name) {
                bool active = (gameState.researchPriorities & EnumToFlag(category)) != 0;
                priorities[std::string(name)] = active;
            };
            addPriority(ResearchCategory::transport, "transport");
            addPriority(ResearchCategory::gentle, "gentle");
            addPriority(ResearchCategory::rollercoaster, "rollercoaster");
            addPriority(ResearchCategory::thrill, "thrill");
            addPriority(ResearchCategory::water, "water");
            addPriority(ResearchCategory::shop, "shop");
            addPriority(ResearchCategory::sceneryGroup, "scenery");
            payload["priorities"] = priorities;

            if (gameState.researchNextItem)
            {
                payload["next"] = BuildResearchItemPayload(gameState.researchNextItem.value());
            }
            if (gameState.researchLastItem)
            {
                payload["last"] = BuildResearchItemPayload(gameState.researchLastItem.value());
            }

            std::vector<json_t> queueEntries;
            size_t scenarioIndex = 0;
            for (const auto& item : gameState.researchItemsUninvented)
            {
                if (!query.categories.empty())
                {
                    bool match = false;
                    for (auto category : query.categories)
                    {
                        if (category == item.category)
                        {
                            match = true;
                            break;
                        }
                    }
                    if (!match)
                    {
                        continue;
                    }
                }
                json_t entry = BuildResearchItemPayload(item);
                entry["queueIndex"] = scenarioIndex++;
                queueEntries.push_back(entry);
            }

            if (query.queueOrder != ResearchQueueOrder::Scenario)
            {
                std::sort(queueEntries.begin(), queueEntries.end(), [&](const json_t& lhs, const json_t& rhs) {
                    switch (query.queueOrder)
                    {
                        case ResearchQueueOrder::Name:
                        {
                            int cmp = CompareCaseInsensitive(lhs.value("name", std::string("")), rhs.value("name", std::string("")));
                            if (cmp != 0)
                            {
                                return query.descending ? cmp > 0 : cmp < 0;
                            }
                            break;
                        }
                        case ResearchQueueOrder::Category:
                        {
                            int cmp = CompareCaseInsensitive(lhs.value("categoryKey", std::string("")), rhs.value("categoryKey", std::string("")));
                            if (cmp != 0)
                            {
                                return query.descending ? cmp > 0 : cmp < 0;
                            }
                            break;
                        }
                        case ResearchQueueOrder::Scenario:
                            break;
                    }
                    auto leftIndex = lhs.value("queueIndex", 0);
                    auto rightIndex = rhs.value("queueIndex", 0);
                    return leftIndex < rightIndex;
                });
            }

            json_t queue = json_t::array();
            size_t emitted = 0;
            for (const auto& entry : queueEntries)
            {
                queue.push_back(entry);
                emitted++;
                if (query.limitEnabled && emitted >= query.queueLimit)
                {
                    break;
                }
            }
            payload["queue"] = queue;
            payload["queueCount"] = queueEntries.size();
            if (query.limitEnabled)
            {
                payload["queueLimit"] = query.queueLimit;
            }
            payload["allComplete"] = gameState.researchProgressStage == RESEARCH_STAGE_FINISHED_ALL;
            return payload;
        }

        // Marketing-specific functions
        std::string_view MarketingCampaignTypeToString(int32_t type)
        {
            switch (type)
            {
                case ADVERTISING_CAMPAIGN_PARK_ENTRY_FREE:
                    return "freeEntry";
                case ADVERTISING_CAMPAIGN_RIDE_FREE:
                    return "freeRide";
                case ADVERTISING_CAMPAIGN_PARK_ENTRY_HALF_PRICE:
                    return "halfPriceEntry";
                case ADVERTISING_CAMPAIGN_FOOD_OR_DRINK_FREE:
                    return "freeFood";
                case ADVERTISING_CAMPAIGN_PARK:
                    return "park";
                case ADVERTISING_CAMPAIGN_RIDE:
                    return "ride";
                default:
                    return "campaign";
            }
        }

        std::optional<int32_t> MarketingCampaignTypeFromString(std::string value)
        {
            auto lowered = ToLower(std::move(value));
            if (lowered == "freeentry" || lowered == "free_entry")
            {
                return ADVERTISING_CAMPAIGN_PARK_ENTRY_FREE;
            }
            if (lowered == "freeride" || lowered == "free_ride")
            {
                return ADVERTISING_CAMPAIGN_RIDE_FREE;
            }
            if (lowered == "halfpriceentry" || lowered == "halfentry" || lowered == "half_price_entry")
            {
                return ADVERTISING_CAMPAIGN_PARK_ENTRY_HALF_PRICE;
            }
            if (lowered == "freefood" || lowered == "fooddrink" || lowered == "food" || lowered == "drink")
            {
                return ADVERTISING_CAMPAIGN_FOOD_OR_DRINK_FREE;
            }
            if (lowered == "park")
            {
                return ADVERTISING_CAMPAIGN_PARK;
            }
            if (lowered == "ride")
            {
                return ADVERTISING_CAMPAIGN_RIDE;
            }
            return std::nullopt;
        }

        enum class MarketingOrderField
        {
            Weeks,
            Type,
            Target,
        };

        struct MarketingStatusQuery
        {
            MarketingOrderField order = MarketingOrderField::Weeks;
            bool descending = true;
            bool directionSpecified = false;
            std::optional<int32_t> typeFilter;
            size_t limit = 0;
            bool limitEnabled = false;
        };

        bool ParseMarketingStatusQuery(const json_t& params, MarketingStatusQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
            {
                return true;
            }
            query.limit = ExtractLimitParam(params);
            query.limitEnabled = query.limit != 0;
            if (auto orderParam = GetStringParam(params, "order"))
            {
                auto lowered = ToLower(*orderParam);
                if (lowered == "weeks" || lowered == "time")
                {
                    query.order = MarketingOrderField::Weeks;
                }
                else if (lowered == "type")
                {
                    query.order = MarketingOrderField::Type;
                }
                else if (lowered == "target")
                {
                    query.order = MarketingOrderField::Target;
                }
                else
                {
                    errorMessage = "Unknown order (use weeks, type, or target)";
                    return false;
                }
            }
            if (auto directionParam = GetStringParam(params, "direction"))
            {
                auto lowered = ToLower(*directionParam);
                if (lowered == "asc" || lowered == "ascending" || lowered == "up")
                {
                    query.descending = false;
                }
                else if (lowered == "desc" || lowered == "descending" || lowered == "down")
                {
                    query.descending = true;
                }
                else
                {
                    errorMessage = "Unknown direction (use asc or desc)";
                    return false;
                }
                query.directionSpecified = true;
            }
            if (auto typeParam = GetStringParam(params, "type"))
            {
                auto type = MarketingCampaignTypeFromString(*typeParam);
                if (!type)
                {
                    errorMessage = "Unknown campaign type";
                    return false;
                }
                query.typeFilter = type;
            }
            return true;
        }

        json_t BuildMarketingStatusPayload(const MarketingStatusQuery& query)
        {
            const auto& park = getGameState().park;
            std::vector<json_t> campaigns;
            campaigns.reserve(std::size(park.marketingCampaigns));
            for (const auto& campaign : park.marketingCampaigns)
            {
                if (query.typeFilter && campaign.Type != *query.typeFilter)
                {
                    continue;
                }

                json_t entry = json_t::object();
                entry["type"] = MarketingCampaignTypeToString(campaign.Type);
                entry["weeksLeft"] = campaign.WeeksLeft;
                entry["target"] = std::string("park");
                if (campaign.Type == ADVERTISING_CAMPAIGN_RIDE || campaign.Type == ADVERTISING_CAMPAIGN_RIDE_FREE)
                {
                    entry["rideId"] = campaign.RideId.IsNull() ? -1 : campaign.RideId.ToUnderlying();
                    if (!campaign.RideId.IsNull())
                    {
                        if (auto* ride = GetRide(campaign.RideId))
                        {
                            entry["rideName"] = ride->getName();
                            entry["target"] = ride->getName();
                        }
                    }
                }
                if (campaign.Type == ADVERTISING_CAMPAIGN_FOOD_OR_DRINK_FREE)
                {
                    auto item = ShopItemToString(static_cast<ShopItem>(campaign.ShopItemType));
                    entry["shopItem"] = item;
                    entry["target"] = item;
                }
                campaigns.push_back(entry);
            }

            std::sort(campaigns.begin(), campaigns.end(), [&](const json_t& lhs, const json_t& rhs) {
                switch (query.order)
                {
                    case MarketingOrderField::Weeks:
                    {
                        auto left = lhs.value("weeksLeft", 0);
                        auto right = rhs.value("weeksLeft", 0);
                        if (left != right)
                        {
                            return query.descending ? left > right : left < right;
                        }
                        break;
                    }
                    case MarketingOrderField::Type:
                    {
                        int cmp = CompareCaseInsensitive(lhs.value("type", std::string("")), rhs.value("type", std::string("")));
                        if (cmp != 0)
                        {
                            return query.descending ? cmp > 0 : cmp < 0;
                        }
                        break;
                    }
                    case MarketingOrderField::Target:
                    {
                        int cmp = CompareCaseInsensitive(lhs.value("target", std::string("")), rhs.value("target", std::string("")));
                        if (cmp != 0)
                        {
                            return query.descending ? cmp > 0 : cmp < 0;
                        }
                        break;
                    }
                }
                auto leftWeeks = lhs.value("weeksLeft", 0);
                auto rightWeeks = rhs.value("weeksLeft", 0);
                if (leftWeeks != rightWeeks)
                {
                    return leftWeeks > rightWeeks;
                }
                return CompareCaseInsensitive(lhs.value("type", std::string("")), rhs.value("type", std::string(""))) < 0;
            });

            json_t active = json_t::array();
            size_t emitted = 0;
            for (const auto& entry : campaigns)
            {
                active.push_back(entry);
                emitted++;
                if (query.limitEnabled && emitted >= query.limit)
                {
                    break;
                }
            }

            json_t payload = json_t::object();
            payload["active"] = active;
            payload["totalActive"] = campaigns.size();
            if (query.limitEnabled)
            {
                payload["limit"] = query.limit;
            }
            if (query.typeFilter)
            {
                payload["typeFilter"] = MarketingCampaignTypeToString(*query.typeFilter);
            }
            return payload;
        }

        // Hint makers
        Telemetry::AIAgentFollowHint MakeFinancesHint(
            std::string_view method, std::string contextLabel, Telemetry::AIAgentFinancesWindowPage page,
            std::optional<CoordsXYZ> camera = std::nullopt)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            if (camera)
            {
                hint.cameraTarget = camera;
            }
            Telemetry::FinancesWindowIntent intent;
            intent.page = page;
            hint.windowIntent = intent;
            return hint;
        }

        Telemetry::AIAgentFollowHint MakeResearchHint(
            std::string_view method, std::string contextLabel, Telemetry::AIAgentResearchWindowPage page)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            Telemetry::ResearchWindowIntent intent;
            intent.page = page;
            hint.windowIntent = intent;
            return hint;
        }

        // Handler functions
        RpcResult HandleResearchStatus(const json_t& params)
        {
            ResearchStatusQuery query;
            std::string errorMessage;
            if (!ParseResearchStatusQuery(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildResearchStatusPayload(query);
            auto hint = MakeResearchHint(
                "research.status", "Reviewed research queue", Telemetry::AIAgentResearchWindowPage::Development);
            hint.requestCameraFocus = false;
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleResearchConfigure(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto& gameState = getGameState();
            uint32_t priorities = gameState.researchPriorities;
            uint8_t funding = gameState.researchFundingLevel;

            if (auto fundingParam = GetStringParam(params, "funding"))
            {
                auto parsedFunding = ResearchFundingLevelFromString(*fundingParam);
                if (!parsedFunding)
                {
                    return RpcResult::Error(kErrorInvalidParams, "Unknown funding level");
                }
                funding = *parsedFunding;
            }
            else if (auto fundingIndex = GetIntParam(params, "fundingIndex"))
            {
                if (*fundingIndex < 0 || *fundingIndex >= RESEARCH_FUNDING_COUNT)
                {
                    return RpcResult::Error(kErrorInvalidParams, "fundingIndex out of range");
                }
                funding = static_cast<uint8_t>(*fundingIndex);
            }

            if (auto prioritiesParamIt = params.find("priorities"); prioritiesParamIt != params.end())
            {
                if (!prioritiesParamIt->is_array())
                {
                    return RpcResult::Error(kErrorInvalidParams, "priorities must be an array");
                }
                uint32_t mask = 0;
                for (const auto& entry : *prioritiesParamIt)
                {
                    if (!entry.is_string())
                    {
                        continue;
                    }
                    auto categoryOpt = ResearchCategoryFromString(entry.get<std::string>());
                    if (categoryOpt)
                    {
                        mask |= EnumToFlag(*categoryOpt);
                    }
                }
                if (mask != 0)
                {
                    priorities = mask;
                }
            }

            auto action = GameActions::ParkSetResearchFundingAction(priorities, funding);
            auto result = GameActions::Execute(&action, gameState);
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }
            json_t payload = BuildResearchStatusPayload(ResearchStatusQuery{});
            // Override funding level with the value we're setting (game state may not be updated yet)
            payload["fundingLevel"] = ResearchFundingLevelToString(funding);
            payload["fundingIndex"] = funding;
            std::string contextLabel = "Set research funding to " + std::string(ResearchFundingLevelToString(funding));
            auto hint = MakeResearchHint(
                "research.set", std::move(contextLabel), Telemetry::AIAgentResearchWindowPage::Funding);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleMarketingStatus(const json_t& params)
        {
            MarketingStatusQuery query;
            std::string errorMessage;
            if (!ParseMarketingStatusQuery(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildMarketingStatusPayload(query);
            std::string contextLabel = "Reviewed marketing campaigns";
            if (query.typeFilter)
            {
                contextLabel += " (" + std::string(MarketingCampaignTypeToString(*query.typeFilter)) + ")";
            }
            auto hint = MakeFinancesHint(
                "marketing.status", std::move(contextLabel), Telemetry::AIAgentFinancesWindowPage::Marketing);
            hint.requestCameraFocus = false;
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleMarketingLaunch(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto typeParam = GetStringParam(params, "type");
            if (!typeParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "type is required");
            }
            auto typeValue = MarketingCampaignTypeFromString(*typeParam);
            if (!typeValue)
            {
                return RpcResult::Error(kErrorInvalidParams,
                    "Unknown marketing type: '" + *typeParam + "'. "
                    "Valid types: freeEntry, halfPriceEntry, freeRide, ride, freeFood, park");
            }

            int32_t item = 0;
            std::optional<RideLookupResult> rideContext;
            std::string targetLabel = getGameState().park.name;
            if (*typeValue == ADVERTISING_CAMPAIGN_RIDE || *typeValue == ADVERTISING_CAMPAIGN_RIDE_FREE)
            {
                std::string errorMessage;
                auto rideLookup = ResolveRideFromParams(params, errorMessage);
                if (!rideLookup)
                {
                    return RpcResult::Error(kErrorInvalidParams, errorMessage);
                }
                item = rideLookup->id.ToUnderlying();
                rideContext = rideLookup;
                if (rideLookup->ride != nullptr)
                {
                    targetLabel = BuildRideDisplayName(*rideLookup->ride);
                }
                else
                {
                    targetLabel = "ride";
                }
            }
            else if (*typeValue == ADVERTISING_CAMPAIGN_FOOD_OR_DRINK_FREE)
            {
                auto itemParam = GetStringParam(params, "item");
                if (!itemParam)
                {
                    return RpcResult::Error(kErrorInvalidParams, "item is required for this campaign");
                }
                auto lowered = ToLower(*itemParam);
                bool found = false;
                for (int i = 0; i < static_cast<int>(ShopItem::count); ++i)
                {
                    auto label = ToLower(ShopItemToString(static_cast<ShopItem>(i)));
                    if (!label.empty() && lowered == label)
                    {
                        item = i;
                        found = true;
                        targetLabel = ShopItemToString(static_cast<ShopItem>(i));
                        break;
                    }
                }
                if (!found)
                {
                    return RpcResult::Error(kErrorInvalidParams, "Unknown shop item");
                }
            }

            int32_t weeks = GetIntParam(params, "weeks").value_or(4);
            weeks = std::clamp(weeks, 1, 6);

            auto action = GameActions::ParkMarketingAction(*typeValue, item, weeks);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Build response directly from what we launched (don't read back state - timing issues)
            json_t launchedCampaign = json_t::object();
            launchedCampaign["type"] = MarketingCampaignTypeToString(*typeValue);
            launchedCampaign["weeksLeft"] = weeks;
            launchedCampaign["target"] = targetLabel;
            if (*typeValue == ADVERTISING_CAMPAIGN_RIDE || *typeValue == ADVERTISING_CAMPAIGN_RIDE_FREE)
            {
                launchedCampaign["rideId"] = item;
                launchedCampaign["rideName"] = targetLabel;
            }
            if (*typeValue == ADVERTISING_CAMPAIGN_FOOD_OR_DRINK_FREE)
            {
                launchedCampaign["shopItem"] = ShopItemToString(static_cast<ShopItem>(item));
            }

            json_t payload = json_t::object();
            payload["launched"] = launchedCampaign;
            // Put the launched campaign in "active" so the renderer displays it
            json_t activeArray = json_t::array();
            activeArray.push_back(launchedCampaign);
            payload["active"] = activeArray;
            payload["totalActive"] = 1;
            std::string typeLabel = std::string(MarketingCampaignTypeToString(*typeValue));
            std::string contextLabel = "Launched " + typeLabel + " campaign";
            if (!targetLabel.empty())
            {
                contextLabel += " for " + targetLabel;
            }
            contextLabel += " (" + std::to_string(weeks) + (weeks == 1 ? " week" : " weeks") + ")";
            std::optional<CoordsXYZ> camera;
            if (rideContext && rideContext->ride != nullptr)
            {
                camera = BuildRideCameraTarget(*rideContext->ride);
            }
            auto hint = MakeFinancesHint(
                "marketing.launch", std::move(contextLabel), Telemetry::AIAgentFinancesWindowPage::Marketing, camera);
            if (!camera)
            {
                hint.requestCameraFocus = false;
            }
            return RpcResult::Ok(payload, std::move(hint));
        }

        // Static registration
        struct ResearchHandlerRegistrar
        {
            ResearchHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("research.status", HandleResearchStatus);
                registry.Register("research.set", HandleResearchConfigure);
                registry.Register("marketing.status", HandleMarketingStatus);
                registry.Register("marketing.launch", HandleMarketingLaunch);
            }
        } researchRegistrar;

    } // namespace

    void InitResearchHandlers()
    {
        (void)researchRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
