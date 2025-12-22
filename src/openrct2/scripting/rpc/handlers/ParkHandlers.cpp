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

#include "../../../Cheats.h"
#include "../../../Date.h"
#include "../../../GameState.h"
#include "../../../actions/CheatSetAction.h"
#include "../../../actions/GameActionResult.h"
#include "../../../actions/ParkSetEntranceFeeAction.h"
#include "../../../actions/ParkSetParameterAction.h"
#include "../../../core/EnumUtils.hpp"
#include "../../../core/Money.hpp"
#include "../../../entity/EntityList.h"
#include "../../../entity/Guest.h"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatting.h"
#include "../../../localisation/StringIdType.h"
#include "../../../management/Award.h"
#include "../../../management/NewsItem.h"
#include "../../../ride/Ride.h"
#include "../../../ride/RideData.h"
#include "../../../scenario/ScenarioObjective.h"
#include "../../../scenario/ScenarioOptions.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../world/Location.hpp"
#include "../../../world/Map.h"
#include "../../../world/Park.h"
#include "../../../world/ParkData.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For kError* constants

    namespace
    {
        struct ParkWarningSnapshot
        {
            int32_t hunger = 0;
            int32_t thirst = 0;
            int32_t toilet = 0;
            int32_t litter = 0;
            int32_t disgust = 0;
            int32_t vandalism = 0;
            int32_t lost = 0;
            int32_t noExit = 0;
            int32_t queueComplaints = 0;
            int32_t ridesBroken = 0;
            int32_t inQueueGuests = 0;
            int32_t queueWorstCount = 0;
            RideId queueWorstRide = RideId::GetNull();
            uint32_t guestsInPark = 0;
            uint32_t guestsHeading = 0;
        };

        struct CheatToggleDescriptor
        {
            const char* key;
            CheatType type;
        };

        constexpr std::array<CheatToggleDescriptor, 18> kSandboxCheatToggles = {
            CheatToggleDescriptor{ "sandboxMode", CheatType::sandboxMode },
            CheatToggleDescriptor{ "unlockOperatingLimits", CheatType::fastLiftHill },
            CheatToggleDescriptor{ "ignorePrice", CheatType::ignorePrice },
            CheatToggleDescriptor{ "ignoreRideIntensity", CheatType::ignoreRideIntensity },
            CheatToggleDescriptor{ "disableVandalism", CheatType::disableVandalism },
            CheatToggleDescriptor{ "disableLittering", CheatType::disableLittering },
            CheatToggleDescriptor{ "disableAllBreakdowns", CheatType::disableAllBreakdowns },
            CheatToggleDescriptor{ "disableBrakesFailure", CheatType::disableBrakesFailure },
            CheatToggleDescriptor{ "neverendingMarketing", CheatType::neverendingMarketing },
            CheatToggleDescriptor{ "freezeWeather", CheatType::freezeWeather },
            CheatToggleDescriptor{ "unlockAllPrices", CheatType::unlockAllPrices },
            CheatToggleDescriptor{ "noMoney", CheatType::noMoney },
            CheatToggleDescriptor{ "buildInPauseMode", CheatType::buildInPauseMode },
            CheatToggleDescriptor{ "allowArbitraryRideTypeChanges", CheatType::allowArbitraryRideTypeChanges },
            CheatToggleDescriptor{ "allowTrackPlaceInvalidHeights", CheatType::allowTrackPlaceInvalidHeights },
            CheatToggleDescriptor{ "allowRegularPathAsQueue", CheatType::allowRegularPathAsQueue },
            CheatToggleDescriptor{ "allowSpecialColourSchemes", CheatType::allowSpecialColourSchemes },
            CheatToggleDescriptor{ "ignoreResearchStatus", CheatType::ignoreResearchStatus },
        };

        bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(lhs[i]))
                        != std::tolower(static_cast<unsigned char>(rhs[i])))
                {
                    return false;
                }
            }
            return true;
        }

        const CheatToggleDescriptor* FindCheatToggleDescriptor(std::string_view key)
        {
            for (const auto& descriptor : kSandboxCheatToggles)
            {
                if (EqualsIgnoreCase(descriptor.key, key))
                {
                    return &descriptor;
                }
            }
            return nullptr;
        }

        std::string_view AwardTypeToString(AwardType type)
        {
            switch (type)
            {
                case AwardType::mostUntidy:
                    return "mostUntidy";
                case AwardType::mostTidy:
                    return "mostTidy";
                case AwardType::bestRollerCoasters:
                    return "bestRollerCoasters";
                case AwardType::bestValue:
                    return "bestValue";
                case AwardType::mostBeautiful:
                    return "mostBeautiful";
                case AwardType::worstValue:
                    return "worstValue";
                case AwardType::safest:
                    return "safest";
                case AwardType::bestStaff:
                    return "bestStaff";
                case AwardType::bestFood:
                    return "bestFood";
                case AwardType::worstFood:
                    return "worstFood";
                case AwardType::bestToilets:
                    return "bestToilets";
                case AwardType::mostDisappointing:
                    return "mostDisappointing";
                case AwardType::bestWaterRides:
                    return "bestWaterRides";
                case AwardType::bestCustomDesignedRides:
                    return "bestCustomDesignedRides";
                case AwardType::mostDazzlingRideColours:
                    return "mostDazzlingRideColours";
                case AwardType::mostConfusingLayout:
                    return "mostConfusingLayout";
                case AwardType::bestGentleRides:
                    return "bestGentleRides";
                default:
                    return "unknown";
            }
        }

        std::string_view DirectionToString(Direction dir)
        {
            switch (dir)
            {
                case 0: // NORTH
                    return "north";
                case 1: // EAST
                    return "east";
                case 2: // SOUTH
                    return "south";
                case 3: // WEST
                    return "west";
                default:
                    return "north";
            }
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

        constexpr std::array<const char*, MONTH_COUNT> kMonthNames = {
            "March", "April", "May", "June", "July", "August", "September", "October",
        };

        std::string_view GetMonthName(int32_t monthIndex)
        {
            if (monthIndex < 0)
            {
                return kMonthNames[0];
            }
            return kMonthNames[monthIndex % static_cast<int32_t>(kMonthNames.size())];
        }

        std::string_view ObjectiveTypeToString(Scenario::ObjectiveType type)
        {
            switch (type)
            {
                case Scenario::ObjectiveType::none:
                    return "none";
                case Scenario::ObjectiveType::guestsBy:
                    return "guestsBy";
                case Scenario::ObjectiveType::parkValueBy:
                    return "parkValueBy";
                case Scenario::ObjectiveType::haveFun:
                    return "haveFun";
                case Scenario::ObjectiveType::buildTheBest:
                    return "buildTheBest";
                case Scenario::ObjectiveType::tenRollercoasters:
                    return "tenRollercoasters";
                case Scenario::ObjectiveType::guestsAndRating:
                    return "guestsAndRating";
                case Scenario::ObjectiveType::monthlyRideIncome:
                    return "monthlyRideIncome";
                case Scenario::ObjectiveType::tenRollercoastersLength:
                    return "tenRollercoastersLength";
                case Scenario::ObjectiveType::finishFiveRollercoasters:
                    return "finishFiveRollercoasters";
                case Scenario::ObjectiveType::repayLoanAndParkValue:
                    return "repayLoanAndParkValue";
                case Scenario::ObjectiveType::monthlyFoodIncome:
                    return "monthlyFoodIncome";
                default:
                    return "unknown";
            }
        }

        double RideRatingToDouble(uint16_t rating)
        {
            return static_cast<double>(rating) / 100.0;
        }

        std::string BuildObjectiveSummary(const Scenario::Objective& objective)
        {
            using Scenario::ObjectiveType;
            std::ostringstream oss;
            switch (objective.Type)
            {
                case ObjectiveType::none:
                    oss << "No active objective.";
                    break;
                case ObjectiveType::guestsBy:
                    oss << "Reach at least " << objective.NumGuests << " guests by year " << static_cast<int>(objective.Year) << '.';
                    break;
                case ObjectiveType::parkValueBy:
                    oss << "Reach park value of " << MoneyToDouble(objective.Currency) << " by year " << static_cast<int>(objective.Year)
                        << '.';
                    break;
                case ObjectiveType::guestsAndRating:
                    oss << "Maintain at least " << objective.NumGuests << " guests with park rating >= 600.";
                    break;
                case ObjectiveType::monthlyRideIncome:
                    oss << "Achieve monthly ride income of " << MoneyToDouble(objective.Currency) << '.';
                    break;
                case ObjectiveType::tenRollercoasters:
                    oss << "Build at least 10 operating roller coasters.";
                    break;
                case ObjectiveType::tenRollercoastersLength:
                    oss << "Build 10 roller coasters with minimum length " << objective.MinimumLength << "m.";
                    break;
                case ObjectiveType::finishFiveRollercoasters:
                    oss << "Operate 5 roller coasters with excitement >= "
                        << RideRatingToDouble(objective.MinimumExcitement) << '.';
                    break;
                case ObjectiveType::repayLoanAndParkValue:
                    oss << "Repay all loans and reach park value of " << MoneyToDouble(objective.Currency) << '.';
                    break;
                case ObjectiveType::monthlyFoodIncome:
                    oss << "Achieve monthly food income of " << MoneyToDouble(objective.Currency) << '.';
                    break;
                case ObjectiveType::buildTheBest:
                    oss << "Build the best ride (ID " << objective.RideId << ").";
                    break;
                case ObjectiveType::haveFun:
                    oss << "Play freely—have fun!";
                    break;
                default:
                    oss << "Objective type " << EnumValue(objective.Type) << '.';
                    break;
            }
            return oss.str();
        }

        json_t BuildObjectivePayload(const Scenario::Objective& objective)
        {
            json_t result = json_t::object();
            result["type"] = ObjectiveTypeToString(objective.Type);
            result["year"] = objective.Year;
            result["summary"] = BuildObjectiveSummary(objective);

            switch (objective.Type)
            {
                case Scenario::ObjectiveType::guestsBy:
                    result["guestTarget"] = objective.NumGuests;
                    break;
                case Scenario::ObjectiveType::parkValueBy:
                    result["parkValueTarget"] = MoneyToDouble(objective.Currency);
                    break;
                case Scenario::ObjectiveType::guestsAndRating:
                    result["guestTarget"] = objective.NumGuests;
                    result["ratingTarget"] = 600;
                    break;
                case Scenario::ObjectiveType::monthlyRideIncome:
                    result["rideIncomeTarget"] = MoneyToDouble(objective.Currency);
                    break;
                case Scenario::ObjectiveType::tenRollercoastersLength:
                    result["lengthTarget"] = objective.MinimumLength;
                    break;
                case Scenario::ObjectiveType::finishFiveRollercoasters:
                    result["excitementTarget"] = RideRatingToDouble(objective.MinimumExcitement);
                    break;
                case Scenario::ObjectiveType::repayLoanAndParkValue:
                    result["parkValueTarget"] = MoneyToDouble(objective.Currency);
                    break;
                case Scenario::ObjectiveType::monthlyFoodIncome:
                    result["foodIncomeTarget"] = MoneyToDouble(objective.Currency);
                    break;
                case Scenario::ObjectiveType::buildTheBest:
                    result["rideTypeId"] = objective.RideId;
                    break;
                default:
                    break;
            }

            return result;
        }

        std::optional<CoordsXYZ> BuildEntranceCameraTarget()
        {
            const auto& park = getGameState().park;
            if (park.entrances.empty())
            {
                return std::nullopt;
            }
            const auto& entrance = park.entrances.front();
            return CoordsXYZ{ entrance.x, entrance.y, entrance.z };
        }

        Telemetry::AIAgentFollowHint MakeParkHint(
            std::string_view method, std::string contextLabel, Telemetry::AIAgentParkWindowPage page,
            std::optional<CoordsXYZ> camera = std::nullopt)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            if (camera)
            {
                hint.cameraTarget = camera;
            }
            Telemetry::ParkWindowIntent intent;
            intent.page = page;
            hint.windowIntent = intent;
            return hint;
        }

        Telemetry::AIAgentFollowHint MakeGenericWindowHint(
            std::string_view method, std::string contextLabel, WindowClass windowClass,
            std::optional<CoordsXYZ> camera = std::nullopt)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            if (camera)
            {
                hint.cameraTarget = camera;
            }
            Telemetry::GenericWindowIntent intent;
            intent.windowClass = windowClass;
            hint.windowIntent = intent;
            return hint;
        }

        json_t BuildParkStatusPayload()
        {
            const auto& gameState = getGameState();
            const auto& park = gameState.park;

            json_t result = json_t::object();
            result["name"] = park.name;
            result["isOpen"] = Park::IsOpen(park);
            result["guests"] = park.numGuestsInPark;
            result["guestsHeading"] = park.numGuestsHeadingForPark;
            result["parkRating"] = park.rating;
            result["cash"] = MoneyToDouble(park.cash);
            result["loan"] = MoneyToDouble(park.bankLoan);
            result["loanMax"] = MoneyToDouble(park.maxBankLoan);
            result["parkValue"] = MoneyToDouble(park.value);
            result["companyValue"] = MoneyToDouble(park.companyValue);
            result["entranceFee"] = MoneyToDouble(park.entranceFee);

            json_t dateJson = json_t::object();
            // Convert from 0-indexed internal representation to 1-indexed user-facing values
            // to match what the game UI displays
            dateJson["day"] = gameState.date.GetDay() + 1;
            dateJson["month"] = gameState.date.GetMonth();
            dateJson["monthName"] = GetMonthName(gameState.date.GetMonth());
            dateJson["year"] = gameState.date.GetYear() + 1;
            result["date"] = dateJson;

            json_t scenarioJson = json_t::object();
            scenarioJson["name"] = gameState.scenarioOptions.name;
            scenarioJson["details"] = gameState.scenarioOptions.details;
            scenarioJson["goalSummary"] = BuildObjectiveSummary(gameState.scenarioOptions.objective);
            result["scenario"] = scenarioJson;

            result["objective"] = BuildObjectivePayload(gameState.scenarioOptions.objective);

            // Add spatial layout information
            json_t spatial = json_t::object();

            // Calculate park boundary bounding box
            int32_t minX = std::numeric_limits<int32_t>::max();
            int32_t minY = std::numeric_limits<int32_t>::max();
            int32_t maxX = std::numeric_limits<int32_t>::min();
            int32_t maxY = std::numeric_limits<int32_t>::min();
            int32_t ownedTileCount = 0;

            auto mapSize = GetMapSizeUnits();
            for (int32_t y = 0; y < mapSize.y; y += kCoordsXYStep)
            {
                for (int32_t x = 0; x < mapSize.x; x += kCoordsXYStep)
                {
                    if (MapIsLocationOwned({ x, y, 0 }))
                    {
                        ownedTileCount++;
                        if (x < minX) minX = x;
                        if (y < minY) minY = y;
                        if (x > maxX) maxX = x;
                        if (y > maxY) maxY = y;
                    }
                }
            }

            // Only add bounding box if we have owned tiles
            if (ownedTileCount > 0)
            {
                json_t boundary = json_t::object();
                // Convert to tile coordinates for consistency with entrance positions
                boundary["minX"] = minX / kCoordsXYStep;
                boundary["minY"] = minY / kCoordsXYStep;
                boundary["maxX"] = maxX / kCoordsXYStep;
                boundary["maxY"] = maxY / kCoordsXYStep;
                spatial["boundary"] = boundary;
                spatial["areaInTiles"] = ownedTileCount;
            }

            // Add entrance coordinates
            json_t entrances = json_t::array();
            int32_t index = 0;
            for (const auto& entrance : park.entrances)
            {
                json_t node = json_t::object();
                node["index"] = index++;
                node["x"] = entrance.x / kCoordsXYStep;
                node["y"] = entrance.y / kCoordsXYStep;
                node["z"] = WorldZToTileZ(entrance.z);
                node["facing"] = std::string(DirectionToString(entrance.direction));
                entrances.push_back(node);
            }
            spatial["entrances"] = entrances;

            result["spatial"] = spatial;

            // Add recent news (last 5 items)
            json_t recentNews = json_t::array();
            size_t newsCount = 0;
            constexpr size_t kMaxRecentNews = 5;

            // Need non-const reference for ForeachRecentNews
            auto& newsQueues = getGameState().newsItems;
            newsQueues.ForeachRecentNews([&](const News::Item& item) {
                if (item.isEmpty() || newsCount >= kMaxRecentNews)
                {
                    return;
                }
                json_t newsEntry = json_t::object();
                newsEntry["type"] = NewsItemTypeToString(item.type);
                // Strip internal format codes like {YELLOW}, {RED}, {NEWLINE} for clean CLI output
                newsEntry["text"] = FmtString(item.text).WithoutFormatTokens();
                // day is already 1-indexed when stored in NewsItem
                newsEntry["day"] = item.day;
                newsEntry["month"] = DateGetMonth(item.monthYear);
                newsEntry["monthName"] = GetMonthName(DateGetMonth(item.monthYear));
                newsEntry["year"] = DateGetYear(item.monthYear) + 1;
                recentNews.push_back(newsEntry);
                newsCount++;
            });

            result["recentNews"] = recentNews;

            return result;
        }

        ParkWarningSnapshot CaptureParkWarnings()
        {
            ParkWarningSnapshot snapshot;
            auto& gameState = getGameState();
            snapshot.guestsInPark = gameState.park.numGuestsInPark;
            snapshot.guestsHeading = gameState.park.numGuestsHeadingForPark;

            std::unordered_map<uint16_t, int32_t> queueComplaintsByRide;

            for (auto guest : EntityList<Guest>())
            {
                if (guest == nullptr || guest->OutsideOfPark)
                {
                    continue;
                }

                if (guest->State == PeepState::queuing || guest->State == PeepState::queuingFront)
                {
                    snapshot.inQueueGuests++;
                }

                const auto& primaryThought = guest->Thoughts[0];
                if (primaryThought.freshness > 5)
                {
                    continue;
                }

                Ride* headingRide = guest->GuestHeadingToRideId.IsNull() ? nullptr : GetRide(guest->GuestHeadingToRideId);
                const bool rideSellsFood = headingRide != nullptr
                    && headingRide->getRideTypeDescriptor().HasFlag(RtdFlag::sellsFood);
                const bool rideSellsDrinks = headingRide != nullptr
                    && headingRide->getRideTypeDescriptor().HasFlag(RtdFlag::sellsDrinks);
                const bool rideIsToilet = headingRide != nullptr
                    && headingRide->getRideTypeDescriptor().specialType == RtdSpecialType::toilet;

                switch (primaryThought.type)
                {
                    case PeepThoughtType::Hungry:
                        if (guest->GuestHeadingToRideId.IsNull() || !rideSellsFood)
                        {
                            snapshot.hunger++;
                        }
                        break;
                    case PeepThoughtType::Thirsty:
                        if (guest->GuestHeadingToRideId.IsNull() || !rideSellsDrinks)
                        {
                            snapshot.thirst++;
                        }
                        break;
                    case PeepThoughtType::Toilet:
                        if (guest->GuestHeadingToRideId.IsNull() || !rideIsToilet)
                        {
                            snapshot.toilet++;
                        }
                        break;
                    case PeepThoughtType::BadLitter:
                        snapshot.litter++;
                        break;
                    case PeepThoughtType::PathDisgusting:
                        snapshot.disgust++;
                        break;
                    case PeepThoughtType::Vandalism:
                        snapshot.vandalism++;
                        break;
                    case PeepThoughtType::Lost:
                        snapshot.lost++;
                        break;
                    case PeepThoughtType::CantFindExit:
                        snapshot.noExit++;
                        break;
                    case PeepThoughtType::QueuingAges:
                        snapshot.queueComplaints++;
                        if (!primaryThought.rideId.IsNull())
                        {
                            queueComplaintsByRide[primaryThought.rideId.ToUnderlying()]++;
                        }
                        break;
                    default:
                        break;
                }
            }

            for (const auto& entry : queueComplaintsByRide)
            {
                if (entry.second > snapshot.queueWorstCount)
                {
                    snapshot.queueWorstCount = entry.second;
                    snapshot.queueWorstRide = RideId::FromUnderlying(static_cast<uint16_t>(entry.first));
                }
            }

            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                {
                    continue;
                }
                if (ride.lifecycleFlags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN))
                {
                    snapshot.ridesBroken++;
                }
            }

            return snapshot;
        }

        json_t BuildParkPricePayload()
        {
            const auto& park = getGameState().park;
            json_t payload = json_t::object();
            payload["entranceFee"] = MoneyToDouble(park.entranceFee);
            payload["isFreeEntry"] = park.entranceFee <= 0;
            payload["parkOpen"] = Park::IsOpen(park);
            return payload;
        }

        json_t BuildParkRatingHistoryPayload()
        {
            const auto& park = getGameState().park;
            json_t records = json_t::array();
            constexpr auto historySize = std::extent_v<decltype(Park::ParkData::ratingHistory)>;
            for (size_t i = 0; i < historySize; i++)
            {
                auto rating = park.ratingHistory[i];
                if (rating == kParkRatingHistoryUndefined)
                {
                    continue;
                }
                json_t record = json_t::object();
                record["monthOffset"] = static_cast<int>(i);
                record["rating"] = rating;
                records.push_back(record);
            }

            json_t payload = json_t::object();
            payload["records"] = records;
            payload["monthsTracked"] = historySize;
            return payload;
        }

        json_t BuildParkRewardsPayload()
        {
            const auto& park = getGameState().park;
            json_t awards = json_t::array();
            for (const auto& award : park.currentAwards)
            {
                json_t entry = json_t::object();
                entry["type"] = AwardTypeToString(award.Type);
                entry["label"] = ResolveStringId(AwardGetText(award.Type));
                entry["isPositive"] = AwardIsPositive(award.Type);
                entry["expiresInMonths"] = award.Time;
                awards.push_back(entry);
            }

            json_t payload = json_t::object();
            payload["awards"] = awards;
            return payload;
        }

        json_t BuildWarningEntry(
            std::string_view key, int32_t count, int32_t staticThreshold, int32_t scaledThreshold, bool strictGreater)
        {
            json_t entry = json_t::object();
            entry["key"] = key;
            entry["count"] = count;
            entry["threshold"] = staticThreshold;
            entry["scaledThreshold"] = scaledThreshold;
            const int32_t target = std::max(staticThreshold, scaledThreshold);
            bool triggered = false;
            if (target > 0)
            {
                triggered = strictGreater ? (count > target) : (count >= target);
            }
            else if (staticThreshold > 0)
            {
                triggered = strictGreater ? (count > staticThreshold) : (count >= staticThreshold);
            }
            entry["triggered"] = triggered;
            entry["headroom"] = target > 0 ? target - count : staticThreshold - count;
            return entry;
        }

        json_t BuildParkWarningsPayload()
        {
            const auto snapshot = CaptureParkWarnings();
            const auto& park = getGameState().park;
            const auto guestsDiv16 = static_cast<int32_t>(snapshot.guestsInPark / 16);
            const auto guestsDiv32 = static_cast<int32_t>(snapshot.guestsInPark / 32);
            const auto queueDivisor = snapshot.inQueueGuests > 0 ? std::max(1, snapshot.inQueueGuests / 20) : 0;

            json_t warnings = json_t::array();
            warnings.push_back(
                BuildWarningEntry("hunger", snapshot.hunger, kPeepHungerWarningThreshold, guestsDiv16, false));
            warnings.push_back(
                BuildWarningEntry("thirst", snapshot.thirst, kPeepThirstWarningThreshold, guestsDiv16, false));
            warnings.push_back(
                BuildWarningEntry("toilet", snapshot.toilet, kPeepToiletWarningThreshold, guestsDiv16, false));
            warnings.push_back(
                BuildWarningEntry("litter", snapshot.litter, kPeepLitterWarningThreshold, guestsDiv32, false));
            warnings.push_back(
                BuildWarningEntry("disgust", snapshot.disgust, kPeepDisgustWarningThreshold, guestsDiv32, false));
            warnings.push_back(
                BuildWarningEntry("vandalism", snapshot.vandalism, kPeepVandalismWarningThreshold, guestsDiv32, false));
            warnings.push_back(
                BuildWarningEntry("cantFindExit", snapshot.noExit, kPeepNoExitWarningThreshold, 0, false));
            warnings.push_back(
                BuildWarningEntry("lost", snapshot.lost, kPeepLostWarningThreshold, 0, false));
            warnings.push_back(
                BuildWarningEntry("queue", snapshot.queueComplaints, kPeepTooLongQueueThreshold, queueDivisor, true));

            json_t throttle = json_t::array();
            for (auto value : park.peepWarningThrottle)
            {
                throttle.push_back(value);
            }

            json_t payload = json_t::object();
            payload["warnings"] = warnings;
            payload["ridesBroken"] = snapshot.ridesBroken;
            payload["guestsInPark"] = snapshot.guestsInPark;
            payload["guestsHeading"] = snapshot.guestsHeading;
            payload["inQueueGuests"] = snapshot.inQueueGuests;
            payload["warningThrottle"] = throttle;

            if (!snapshot.queueWorstRide.IsNull())
            {
                json_t hotspot = json_t::object();
                hotspot["rideId"] = snapshot.queueWorstRide.ToUnderlying();
                if (auto* ride = GetRide(snapshot.queueWorstRide))
                {
                    hotspot["rideName"] = ride->getName();
                }
                hotspot["complaints"] = snapshot.queueWorstCount;
                payload["queueHotspot"] = hotspot;
            }

            return payload;
        }

        json_t BuildSandboxStatusPayload()
        {
            const auto& park = getGameState().park;
            const auto& cheats = getGameState().cheats;

            json_t payload = json_t::object();
            payload["noMoney"] = (park.flags & PARK_FLAGS_NO_MONEY) != 0;
            payload["parkFreeEntry"] = (park.flags & PARK_FLAGS_PARK_FREE_ENTRY) != 0;
            payload["unlockAllPrices"] = (park.flags & PARK_FLAGS_UNLOCK_ALL_PRICES) != 0;
            payload["forbidLandscapeChanges"] = (park.flags & PARK_FLAGS_FORBID_LANDSCAPE_CHANGES) != 0;
            payload["forbidTreeRemoval"] = (park.flags & PARK_FLAGS_FORBID_TREE_REMOVAL) != 0;
            payload["difficultGuestGeneration"] = (park.flags & PARK_FLAGS_DIFFICULT_GUEST_GENERATION) != 0;
            payload["difficultParkRating"] = (park.flags & PARK_FLAGS_DIFFICULT_PARK_RATING) != 0;

            json_t cheatsJson = json_t::object();
            cheatsJson["sandboxMode"] = cheats.sandboxMode;
            cheatsJson["buildInPauseMode"] = cheats.buildInPauseMode;
            cheatsJson["ignoreRideIntensity"] = cheats.ignoreRideIntensity;
            cheatsJson["ignorePrice"] = cheats.ignorePrice;
            cheatsJson["disableVandalism"] = cheats.disableVandalism;
            cheatsJson["disableLittering"] = cheats.disableLittering;
            cheatsJson["disableAllBreakdowns"] = cheats.disableAllBreakdowns;
            cheatsJson["disableBrakesFailure"] = cheats.disableBrakesFailure;
            cheatsJson["unlockOperatingLimits"] = cheats.unlockOperatingLimits;
            cheatsJson["neverendingMarketing"] = cheats.neverendingMarketing;
            cheatsJson["freezeWeather"] = cheats.freezeWeather;
            cheatsJson["allowArbitraryRideTypeChanges"] = cheats.allowArbitraryRideTypeChanges;
            cheatsJson["allowTrackPlaceInvalidHeights"] = cheats.allowTrackPlaceInvalidHeights;
            cheatsJson["allowRegularPathAsQueue"] = cheats.allowRegularPathAsQueue;
            cheatsJson["allowSpecialColourSchemes"] = cheats.allowSpecialColourSchemes;
            cheatsJson["ignoreResearchStatus"] = cheats.ignoreResearchStatus;
            cheatsJson["makeAllDestructible"] = cheats.makeAllDestructible;
            cheatsJson["sandboxSpeed"] = static_cast<int32_t>(cheats.selectedStaffSpeed);
            payload["cheats"] = cheatsJson;

            return payload;
        }

        RpcResult HandleParkStatus(const json_t& /*params*/)
        {
            auto payload = BuildParkStatusPayload();
            auto hint = MakeParkHint("park.status", "Viewed park status", Telemetry::AIAgentParkWindowPage::Stats, BuildEntranceCameraTarget());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleParkGuests(const json_t& /*params*/)
        {
            const auto& park = getGameState().park;
            json_t payload = json_t::object();
            payload["count"] = park.numGuestsInPark;
            payload["headingToPark"] = park.numGuestsHeadingForPark;
            payload["isOpen"] = Park::IsOpen(park);
            payload["parkRating"] = park.rating;
            auto hint = MakeParkHint(
                "park.guests", "Viewed guest counts", Telemetry::AIAgentParkWindowPage::Guests, BuildEntranceCameraTarget());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleParkPrice(const json_t& /*params*/)
        {
            auto payload = BuildParkPricePayload();
            auto hint = MakeParkHint("park.price", "Viewed park pricing", Telemetry::AIAgentParkWindowPage::Price, BuildEntranceCameraTarget());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleParkSetOpen(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto openParam = GetBoolParam(params, "open");
            if (!openParam)
            {
                openParam = GetBoolParam(params, "state");
            }
            if (!openParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "Boolean field 'open' is required");
            }

            auto& gameState = getGameState();
            bool wasOpen = Park::IsOpen(gameState.park);

            GameActions::ParkParameter mode = openParam.value() ? GameActions::ParkParameter::Open
                                                                : GameActions::ParkParameter::Close;
            GameActions::ParkSetParameterAction action(mode);
            auto result = GameActions::Execute(&action, gameState);
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Use the requested state since the action succeeded - reading back may have timing issues
            bool isOpen = openParam.value();
            json_t payload = json_t::object();
            payload["isOpen"] = isOpen;
            payload["previousState"] = wasOpen ? "open" : "closed";

            auto entranceCamera = BuildEntranceCameraTarget();
            std::string contextLabel = isOpen ? "Opened park" : "Closed park";
            auto hint = MakeParkHint("park.setOpen", std::move(contextLabel), Telemetry::AIAgentParkWindowPage::Entrance, entranceCamera);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleParkSetEntranceFee(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto priceParam = GetDoubleParam(params, "price");
            if (!priceParam)
            {
                priceParam = GetDoubleParam(params, "fee");
            }
            if (!priceParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "Numeric field 'price' is required");
            }

            double clamped = std::clamp(priceParam.value(), 0.0, MoneyToDouble(kMaxEntranceFee));
            money64 fee = std::clamp<money64>(ToMoney64FromGBP(clamped), 0.00_GBP, kMaxEntranceFee);

            GameActions::ParkSetEntranceFeeAction action(fee);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }
            // Build payload directly using the clamped value we're setting, rather than
            // re-reading from game state (which may not be updated yet if action is async)
            json_t payload = json_t::object();
            payload["entranceFee"] = clamped;
            payload["isFreeEntry"] = (fee <= 0);
            payload["parkOpen"] = Park::IsOpen(getGameState().park);
            auto entranceCamera = BuildEntranceCameraTarget();
            std::string contextLabel = "Set park entrance fee to " + FormatMoneyString(fee);
            auto hint = MakeParkHint("park.setEntranceFee", std::move(contextLabel), Telemetry::AIAgentParkWindowPage::Price, entranceCamera);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleParkRatingHistory(const json_t& /*params*/)
        {
            auto payload = BuildParkRatingHistoryPayload();
            auto hint = MakeParkHint(
                "park.ratingHistory", "Reviewed park rating history", Telemetry::AIAgentParkWindowPage::Stats,
                BuildEntranceCameraTarget());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleParkRewards(const json_t& /*params*/)
        {
            auto payload = BuildParkRewardsPayload();
            auto hint = MakeParkHint(
                "park.rewards", "Viewed active park awards", Telemetry::AIAgentParkWindowPage::Awards, BuildEntranceCameraTarget());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleParkWarnings(const json_t& /*params*/)
        {
            auto payload = BuildParkWarningsPayload();
            auto hint = MakeParkHint(
                "park.warnings", "Reviewed park warnings", Telemetry::AIAgentParkWindowPage::Stats, BuildEntranceCameraTarget());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleSandboxStatus(const json_t& /*params*/)
        {
            auto payload = BuildSandboxStatusPayload();
            auto hint = MakeGenericWindowHint("park.sandboxStatus", "Viewed sandbox toggles", WindowClass::cheats, std::nullopt);
            hint.requestCameraFocus = false;
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleSandboxSet(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto keyParam = GetStringParam(params, "key");
            if (!keyParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "key is required");
            }
            auto valueParam = GetBoolParam(params, "value");
            if (!valueParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "value must be a boolean");
            }

            const auto* descriptor = FindCheatToggleDescriptor(*keyParam);
            if (descriptor == nullptr)
            {
                return RpcResult::Error(kErrorInvalidParams, "unknown sandbox toggle");
            }

            auto action = GameActions::CheatSetAction(descriptor->type, *valueParam ? 1 : 0);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto payload = BuildSandboxStatusPayload();
            std::string contextLabel = "Set sandbox toggle '" + *keyParam + "' to " + (*valueParam ? "on" : "off");
            auto hint = MakeGenericWindowHint("park.sandboxSet", std::move(contextLabel), WindowClass::cheats, std::nullopt);
            hint.requestCameraFocus = false;
            return RpcResult::Ok(payload, std::move(hint));
        }

        struct EntranceListResult
        {
            json_t payload;
            std::optional<CoordsXYZ> cameraTarget;
        };

        EntranceListResult BuildEntranceListPayload()
        {
            EntranceListResult result;
            result.payload = json_t::object();
            try
            {
                const auto& park = getGameState().park;

                // Snapshot entrances to avoid race conditions
                std::vector<CoordsXYZD> entranceSnapshot(park.entrances.begin(), park.entrances.end());

                json_t entries = json_t::array();
                int32_t index = 0;
                for (const auto& entrance : entranceSnapshot)
                {
                    json_t node = json_t::object();
                    node["index"] = index++;
                    node["x"] = entrance.x / kCoordsXYStep;
                    node["y"] = entrance.y / kCoordsXYStep;
                    node["z"] = WorldZToTileZ(entrance.z);
                    node["direction"] = std::string(DirectionToString(entrance.direction));
                    entries.push_back(node);
                }

                result.payload["entrances"] = entries;
                result.payload["count"] = entries.size();
                result.payload["parkOpen"] = Park::IsOpen(park);

                // Compute camera target from the same snapshot (fixes TOCTOU)
                if (!entranceSnapshot.empty())
                {
                    const auto& firstEntrance = entranceSnapshot.front();
                    result.cameraTarget = CoordsXYZ{ firstEntrance.x, firstEntrance.y, firstEntrance.z };
                }
            }
            catch (...)
            {
                result.payload["entrances"] = json_t::array();
                result.payload["count"] = 0;
                result.payload["parkOpen"] = false;
                result.payload["error"] = "Failed to enumerate entrances";
            }
            return result;
        }

        RpcResult HandleInfrastructureEntrances(const json_t& /*params*/)
        {
            auto entranceData = BuildEntranceListPayload();
            auto hint = MakeGenericWindowHint(
                "infrastructure.entrances", "Listed park entrances", WindowClass::map, entranceData.cameraTarget);
            return RpcResult::Ok(entranceData.payload, std::move(hint));
        }

        // Static registration
        struct ParkHandlerRegistrar
        {
            ParkHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("park.status", HandleParkStatus);
                registry.Register("park.guests", HandleParkGuests);
                registry.Register("park.price", HandleParkPrice);
                registry.Register("park.setOpen", HandleParkSetOpen);
                registry.Register("park.setEntranceFee", HandleParkSetEntranceFee);
                registry.Register("park.ratingHistory", HandleParkRatingHistory);
                registry.Register("park.rewards", HandleParkRewards);
                registry.Register("park.warnings", HandleParkWarnings);
                registry.Register("park.sandboxStatus", HandleSandboxStatus);
                registry.Register("park.sandboxSet", HandleSandboxSet);
                registry.Register("infrastructure.entrances", HandleInfrastructureEntrances);
            }
        } parkRegistrar;

    } // namespace

    void InitParkHandlers()
    {
        // Force static initialization of parkRegistrar
        (void)parkRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
