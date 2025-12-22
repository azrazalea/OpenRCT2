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

#include "../../../Context.h"
#include "../../../Diagnostic.h"
#include "../../../GameState.h"
#include "../../../actions/GameActionResult.h"
#include "../../../actions/RideCreateAction.h"
#include "../../../actions/RideDemolishAction.h"
#include "../../../actions/RideEntranceExitPlaceAction.h"
#include "../../../actions/RideSetNameAction.h"
#include "../../../actions/RideSetPriceAction.h"
#include "../../../actions/RideSetSettingAction.h"
#include "../../../actions/RideSetAppearanceAction.h"
#include "../../../actions/RideSetStatusAction.h"
#include "../../../actions/TrackDesignAction.h"
#include "../../../actions/TrackPlaceAction.h"
#include "../../../core/Money.hpp"
#include "../../../core/Numerics.hpp"
#include "../../../entity/EntityList.h"
#include "../../../entity/Guest.h"
#include "../../../entity/Staff.h"
#include "../../../interface/Colour.h"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatting.h"
#include "../../../localisation/Language.h"
#include "../../../localisation/LocalisationService.h"
#include "../../../localisation/StringIdType.h"
#include "../../../object/ObjectList.h"
#include "../../../object/ObjectManager.h"
#include "../../../object/RideObject.h"
#include "../../../object/StationObject.h"
#include "../../../ride/Ride.h"
#include "../../../ride/RideColour.h"
#include "../../../ride/RideConstruction.h"
#include "../../../ride/RideData.h"
#include "../../../ride/RideManager.hpp"
#include "../../../ride/TrackData.h"
#include "../../../ride/TrackDesign.h"
#include "../../../ride/TrackDesignRepository.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../ui/WindowManager.h"
#include "../../../windows/Intent.h"
#include "../../../world/Location.hpp"
#include "../../../world/Map.h"
#include "../../../world/MapLimits.h"
#include "../../../world/Park.h"
#include "../../../world/tile_element/EntranceElement.h"
#include "../../../world/tile_element/SurfaceElement.h"
#include "../../../world/tile_element/TrackElement.h"
#include "../../../world/TileElementsView.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc; // For kError* constants and shared utilities
    using Rpc::ToLower;
    using Rpc::ResolveStringId;
    using Rpc::BuildGameActionErrorMessage;
    using Rpc::GameActionStatusToString;
    using Rpc::DirectionToString;
    using Rpc::DirectionFromString;
    using Rpc::MoneyToDouble;
    using Rpc::WorldZToTileZ;
    using Rpc::TileZToWorldZ;
    using Rpc::ExtractLimitParam;
    using Rpc::BuildPositionPayload;
    using Rpc::BuildActionSuccessPayload;
    using Rpc::FormatMoneyString;
    using Rpc::GetIntParam;
    using Rpc::GetStringParam;
    using Rpc::GetBoolParam;

    namespace
    {
        struct RideBlueprintInfo
        {
            ObjectEntryIndex entryIndex{ kObjectEntryIndexNull };
            ride_type_t rideType{ kRideTypeNull };
            std::string identifier;
            const RideObjectEntry* rideEntry{};
            const RideTypeDescriptor* descriptor{};
            RideObject* rideObject{};
        };

        struct RideFootprint
        {
            TileCoordsXY anchor;
            Direction direction{ 0 };
            OpenRCT2::TrackElemType trackType{ OpenRCT2::TrackElemType::None };
            std::vector<TileCoordsXY> tiles;
        };

        enum class RideFinancialStatusFilter
        {
            All,
            Open,
            Closed,
        };

        enum class RideFinancialOrderField
        {
            Profit,
            Income,
            Cost,
            Name,
        };

        struct RideFinancialQuery
        {
            RideFinancialStatusFilter status = RideFinancialStatusFilter::All;
            RideFinancialOrderField order = RideFinancialOrderField::Profit;
            bool descending = true;
            bool directionSpecified = false;
            size_t limit = 0;
            bool limitEnabled = false;
        };

        // Perception metrics: guest satisfaction and ride ratings
        enum class RidePerceptionOrderField
        {
            Popularity,
            Satisfaction,
            Excitement,
            Intensity,
            Nausea,
            Favorites,
        };

        struct RidePerceptionQuery
        {
            RideFinancialStatusFilter status = RideFinancialStatusFilter::All;
            RidePerceptionOrderField order = RidePerceptionOrderField::Popularity;
            bool descending = true;
            bool directionSpecified = false;
            size_t limit = 0;
            bool limitEnabled = false;
        };

        // Operations metrics: reliability, queues, downtime
        enum class RideOperationsOrderField
        {
            Reliability,
            Downtime,
            QueueTime,
            QueueLength,
            Customers,
            Age,
        };

        struct RideOperationsQuery
        {
            RideFinancialStatusFilter status = RideFinancialStatusFilter::All;
            RideOperationsOrderField order = RideOperationsOrderField::Reliability;
            bool descending = true;
            bool directionSpecified = false;
            size_t limit = 0;
            bool limitEnabled = false;
        };

        constexpr TileCoordsXY kFootprintDirectionOffsets[] = {
            TileCoordsXY{ -1, 0 },
            TileCoordsXY{ 0, 1 },
            TileCoordsXY{ 1, 0 },
            TileCoordsXY{ 0, -1 },
        };

        constexpr std::array<int32_t, 8> kRideInspectionIntervalMinutes = { 10, 20, 30, 45, 60, 90, 120, 0 };

        // ========== Helper function implementations ==========

        std::string_view RideStatusToString(RideStatus status)
        {
            switch (status)
            {
                case RideStatus::closed:
                    return "closed";
                case RideStatus::open:
                    return "open";
                case RideStatus::testing:
                    return "testing";
                case RideStatus::simulating:
                    return "simulating";
                default:
                    return "unknown";
            }
        }

        std::optional<RideStatus> RideStatusFromString(std::string value)
        {
            auto lowered = ToLower(std::move(value));
            if (lowered == "open" || lowered == "opened")
                return RideStatus::open;
            if (lowered == "closed" || lowered == "close" || lowered == "shut")
                return RideStatus::closed;
            if (lowered == "testing" || lowered == "test")
                return RideStatus::testing;
            return std::nullopt;
        }

        std::string_view RideModeToString(RideMode mode)
        {
            switch (mode)
            {
                case RideMode::normal:
                    return "normal";
                case RideMode::continuousCircuit:
                    return "continuousCircuit";
                case RideMode::reverseInclineLaunchedShuttle:
                    return "reverseInclineLaunchedShuttle";
                case RideMode::poweredLaunchPasstrough:
                    return "poweredLaunchPassthrough";
                case RideMode::shuttle:
                    return "shuttle";
                case RideMode::boatHire:
                    return "boatHire";
                case RideMode::upwardLaunch:
                    return "upwardLaunch";
                case RideMode::rotation:
                    return "rotation";
                case RideMode::forwardRotation:
                    return "forwardRotation";
                case RideMode::backwardRotation:
                    return "backwardRotation";
                case RideMode::filmAvengingAviators:
                    return "filmAvengingAviators";
                case RideMode::swing:
                    return "swing";
                case RideMode::shopStall:
                    return "shopStall";
                case RideMode::race:
                    return "race";
                case RideMode::dodgems:
                    return "dodgems";
                case RideMode::continuousCircuitBlockSectioned:
                    return "continuousCircuitBlockSectioned";
                case RideMode::poweredLaunch:
                    return "poweredLaunch";
                case RideMode::poweredLaunchBlockSectioned:
                    return "poweredLaunchBlockSectioned";
                default:
                    return "unknown";
            }
        }

        std::optional<RideMode> RideModeFromString(std::string value)
        {
            auto lowered = ToLower(std::move(value));
            if (lowered == "normal")
                return RideMode::normal;
            if (lowered == "continuouscircuit")
                return RideMode::continuousCircuit;
            if (lowered == "shuttle")
                return RideMode::shuttle;
            if (lowered == "race")
                return RideMode::race;
            if (lowered == "dodgems" || lowered == "bumpercars")
                return RideMode::dodgems;
            return std::nullopt;
        }

        std::string_view RideModifyTypeToString(GameActions::RideModifyType type)
        {
            switch (type)
            {
                case GameActions::RideModifyType::demolish:
                    return "demolish";
                case GameActions::RideModifyType::renew:
                    return "renew";
                default:
                    return "unknown";
            }
        }

        std::optional<GameActions::RideModifyType> RideModifyTypeFromString(std::string value)
        {
            auto token = Rpc::NormaliseToken(std::move(value));
            if (token.empty())
            {
                return std::nullopt;
            }
            if (token == "demolish" || token == "remove" || token == "delete")
            {
                return GameActions::RideModifyType::demolish;
            }
            if (token == "renew" || token == "refurbish" || token == "refurb")
            {
                return GameActions::RideModifyType::renew;
            }
            return std::nullopt;
        }

        std::optional<int32_t> TryParseInt(std::string_view value)
        {
            if (value.empty())
            {
                return std::nullopt;
            }
            int32_t parsed = 0;
            const char* begin = value.data();
            const char* end = value.data() + value.size();
            auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc() || ptr != end)
            {
                return std::nullopt;
            }
            return parsed;
        }

        std::string_view RideClassificationToString(RideClassification classification)
        {
            switch (classification)
            {
                case RideClassification::ride:
                    return "ride";
                case RideClassification::shopOrStall:
                    return "stall";
                case RideClassification::kioskOrFacility:
                    return "facility";
                default:
                    return "unknown";
            }
        }

        std::string_view RideCategoryToString(RideCategory category)
        {
            switch (category)
            {
                case RideCategory::transport:
                    return "transport";
                case RideCategory::gentle:
                    return "gentle";
                case RideCategory::rollerCoaster:
                    return "rollerCoaster";
                case RideCategory::thrill:
                    return "thrill";
                case RideCategory::water:
                    return "water";
                case RideCategory::shop:
                    return "shop";
                default:
                    return "ride";
            }
        }

        std::string_view PeepThoughtTypeToString(PeepThoughtType type)
        {
            switch (type)
            {
                case PeepThoughtType::CantAffordRide:
                    return "I can't afford";
                case PeepThoughtType::SpentMoney:
                    return "I've spent all my money";
                case PeepThoughtType::Sick:
                    return "I feel sick";
                case PeepThoughtType::VerySick:
                    return "I feel very sick";
                case PeepThoughtType::MoreThrilling:
                    return "I want something more thrilling than";
                case PeepThoughtType::Intense:
                    return "looks too intense for me";
                case PeepThoughtType::HaventFinished:
                    return "I haven't finished my";
                case PeepThoughtType::Sickening:
                    return "Just looking at it makes me feel sick";
                case PeepThoughtType::BadValue:
                    return "I'm not paying that much to go on";
                case PeepThoughtType::GoHome:
                    return "I want to go home";
                case PeepThoughtType::GoodValue:
                    return "is really good value";
                case PeepThoughtType::AlreadyGot:
                    return "I've already got";
                case PeepThoughtType::CantAffordItem:
                    return "I can't afford";
                case PeepThoughtType::NotHungry:
                    return "I'm not hungry";
                case PeepThoughtType::NotThirsty:
                    return "I'm not thirsty";
                case PeepThoughtType::Drowning:
                    return "Help! I'm drowning!";
                case PeepThoughtType::Lost:
                    return "I'm lost!";
                case PeepThoughtType::WasGreat:
                    return "was great";
                case PeepThoughtType::QueuingAges:
                    return "I've been queuing for ages";
                case PeepThoughtType::Tired:
                    return "I'm tired";
                case PeepThoughtType::Hungry:
                    return "I'm hungry";
                case PeepThoughtType::Thirsty:
                    return "I'm thirsty";
                case PeepThoughtType::Toilet:
                    return "I need the toilet";
                case PeepThoughtType::CantFind:
                    return "I can't find";
                case PeepThoughtType::NotPaying:
                    return "I'm not paying that much to use";
                case PeepThoughtType::NotWhileRaining:
                    return "I'm not going on it while it's raining";
                case PeepThoughtType::BadLitter:
                    return "The litter here is really bad";
                case PeepThoughtType::CantFindExit:
                    return "I can't find the exit";
                case PeepThoughtType::GetOff:
                    return "I want to get off";
                case PeepThoughtType::GetOut:
                    return "I want to get out of";
                case PeepThoughtType::NotSafe:
                    return "It isn't safe";
                case PeepThoughtType::PathDisgusting:
                    return "This path is disgusting";
                case PeepThoughtType::Crowded:
                    return "It's too crowded here";
                case PeepThoughtType::Vandalism:
                    return "The vandalism here is really bad";
                case PeepThoughtType::Scenery:
                    return "Great scenery!";
                case PeepThoughtType::VeryClean:
                    return "This park is very clean and tidy";
                case PeepThoughtType::Fountains:
                    return "The jumping fountains are great";
                case PeepThoughtType::Music:
                    return "The music is nice here";
                case PeepThoughtType::Balloon:
                case PeepThoughtType::Toy:
                case PeepThoughtType::Map:
                case PeepThoughtType::Photo:
                case PeepThoughtType::Umbrella:
                case PeepThoughtType::Drink:
                case PeepThoughtType::Burger:
                case PeepThoughtType::Chips:
                case PeepThoughtType::IceCream:
                case PeepThoughtType::Candyfloss:
                case PeepThoughtType::Pizza:
                case PeepThoughtType::Popcorn:
                case PeepThoughtType::HotDog:
                case PeepThoughtType::Tentacle:
                case PeepThoughtType::Hat:
                case PeepThoughtType::ToffeeApple:
                case PeepThoughtType::Tshirt:
                case PeepThoughtType::Doughnut:
                case PeepThoughtType::Coffee:
                case PeepThoughtType::Chicken:
                case PeepThoughtType::Lemonade:
                case PeepThoughtType::Pretzel:
                case PeepThoughtType::HotChocolate:
                case PeepThoughtType::IcedTea:
                case PeepThoughtType::FunnelCake:
                case PeepThoughtType::Sunglasses:
                case PeepThoughtType::BeefNoodles:
                case PeepThoughtType::FriedRiceNoodles:
                case PeepThoughtType::WontonSoup:
                case PeepThoughtType::MeatballSoup:
                case PeepThoughtType::FruitJuice:
                case PeepThoughtType::SoybeanMilk:
                case PeepThoughtType::Sujongkwa:
                case PeepThoughtType::SubSandwich:
                case PeepThoughtType::Cookie:
                case PeepThoughtType::RoastSausage:
                    return "is really good value";
                case PeepThoughtType::Help:
                    return "Help!";
                case PeepThoughtType::RunningOut:
                    return "I'm running out of cash";
                case PeepThoughtType::NewRide:
                    return "Wow! A new ride!";
                case PeepThoughtType::NiceRideDeprecated:
                    return "Nice ride";
                case PeepThoughtType::Wow:
                case PeepThoughtType::Wow2:
                    return "Wow!";
                case PeepThoughtType::Watched:
                    return "I have the strangest feeling someone is watching me";
                case PeepThoughtType::None:
                    return "";
                default:
                    return "thought";
            }
        }

        bool ThoughtNeedsShopItem(PeepThoughtType type)
        {
            switch (type)
            {
                case PeepThoughtType::HaventFinished:
                case PeepThoughtType::AlreadyGot:
                case PeepThoughtType::CantAffordItem:
                    return true;
                default:
                    return false;
            }
        }

        bool ThoughtUsesSingularItem(PeepThoughtType type)
        {
            return type == PeepThoughtType::HaventFinished;
        }

        std::string FormatThoughtText(const PeepThought& thought)
        {
            auto baseText = std::string(PeepThoughtTypeToString(thought.type));
            if (baseText.empty())
            {
                return baseText;
            }

            if (ThoughtNeedsShopItem(thought.type))
            {
                if (thought.shopItem != ShopItem::none)
                {
                    const auto& descriptor = GetShopItemDescriptor(thought.shopItem);
                    StringId nameId = ThoughtUsesSingularItem(thought.type)
                        ? descriptor.Naming.Singular
                        : descriptor.Naming.Indefinite;
                    if (nameId != kStringIdNone)
                    {
                        const utf8* itemName = LanguageGetString(nameId);
                        if (itemName != nullptr && itemName[0] != '\0')
                        {
                            baseText += ' ';
                            baseText += itemName;
                        }
                    }
                }
            }
            return baseText;
        }

        json_t BuildGuestSamplePayload(const Guest& guest)
        {
            json_t sample = json_t::object();
            sample["id"] = guest.Id.ToUnderlying();
            sample["name"] = guest.GetName();
            return sample;
        }

        std::optional<RideFootprint> BuildRideFootprint(const Ride& ride)
        {
            auto& gameState = getGameState();
            std::set<std::pair<int32_t, int32_t>> seen;
            RideFootprint footprint;
            footprint.trackType = TrackElemType::None;

            TileCoordsXY tilePos;
            for (tilePos.y = 0; tilePos.y < gameState.mapSize.y; ++tilePos.y)
            {
                for (tilePos.x = 0; tilePos.x < gameState.mapSize.x; ++tilePos.x)
                {
                    for (auto* trackElement : TileElementsView<TrackElement>(tilePos.ToCoordsXY()))
                    {
                        if (trackElement->GetRideIndex() != ride.id)
                        {
                            continue;
                        }
                        if (seen.insert({ tilePos.x, tilePos.y }).second)
                        {
                            footprint.tiles.push_back(tilePos);
                        }
                        if (footprint.trackType == TrackElemType::None)
                        {
                            footprint.anchor = tilePos;
                            footprint.direction = trackElement->GetDirection();
                            footprint.trackType = trackElement->GetTrackType();
                        }
                    }
                }
            }

            if (footprint.tiles.empty())
            {
                return std::nullopt;
            }

            std::sort(footprint.tiles.begin(), footprint.tiles.end(), [](const TileCoordsXY& lhs, const TileCoordsXY& rhs) {
                if (lhs.y == rhs.y)
                {
                    return lhs.x < rhs.x;
                }
                return lhs.y < rhs.y;
            });

            footprint.anchor = footprint.tiles.front();
            return footprint;
        }

        bool IsTileBlockedForEntrance(const TileCoordsXY& tile)
        {
            auto coords = tile.ToCoordsXY();
            if (!MapIsLocationValid(coords))
            {
                return true;
            }

            for (auto* element : TileElementsView<TileElement>(coords))
            {
                if (element == nullptr)
                {
                    break;
                }
                auto type = element->GetType();
                if (type == TileElementType::Path || type == TileElementType::Track
                    || type == TileElementType::Entrance || type == TileElementType::LargeScenery)
                {
                    return true;
                }
                if (element->IsLastForTile())
                {
                    break;
                }
            }
            return false;
        }

        std::optional<Direction> ValidateEntranceExitPlacement(
            const Ride& ride, const TileCoordsXY& entranceTile, StationIndex stationIndex)
        {
            auto& station = ride.getStation(stationIndex);
            int32_t stationBaseZ = station.GetBaseZ();

            for (Direction searchDir = 0; searchDir < 4; searchDir++)
            {
                auto delta = CoordsDirectionDelta[searchDir];
                TileCoordsXY adjacentTile{
                    entranceTile.x + (delta.x / kCoordsXYStep),
                    entranceTile.y + (delta.y / kCoordsXYStep)
                };

                auto coords = adjacentTile.ToCoordsXY();
                if (!MapIsLocationValid(coords))
                {
                    continue;
                }

                for (auto* trackElement : TileElementsView<TrackElement>(coords))
                {
                    if (trackElement->GetRideIndex() != ride.id)
                    {
                        continue;
                    }
                    if (trackElement->GetBaseZ() != stationBaseZ)
                    {
                        continue;
                    }

                    if (trackElement->GetTrackType() == TrackElemType::Maze)
                    {
                        return DirectionReverse(searchDir);
                    }

                    Direction relativeDir = (DirectionReverse(searchDir) - trackElement->GetDirection()) & 3;
                    const auto& ted = TrackMetaData::GetTrackElementDescriptor(trackElement->GetTrackType());
                    if (ted.sequences[trackElement->GetSequenceIndex()].flags & (1 << relativeDir))
                    {
                        return DirectionReverse(searchDir);
                    }
                }
            }
            return std::nullopt;
        }

        json_t BuildFootprintPayload(const RideFootprint& footprint, const Ride& ride)
        {
            json_t node = json_t::object();
            node["anchorMeaning"] = "north-west corner of the ride footprint";
            node["anchor"] = json_t::object({ { "x", footprint.anchor.x }, { "y", footprint.anchor.y } });
            node["directionIndex"] = footprint.direction;
            node["direction"] = std::string(DirectionToString(footprint.direction));
            node["trackType"] = static_cast<int32_t>(footprint.trackType);
            node["tileCount"] = static_cast<int32_t>(footprint.tiles.size());

            int32_t minX = std::numeric_limits<int32_t>::max();
            int32_t maxX = std::numeric_limits<int32_t>::min();
            int32_t minY = std::numeric_limits<int32_t>::max();
            int32_t maxY = std::numeric_limits<int32_t>::min();

            json_t tiles = json_t::array();
            for (const auto& tile : footprint.tiles)
            {
                tiles.push_back(json_t::object({ { "x", tile.x }, { "y", tile.y } }));
                minX = std::min(minX, tile.x);
                maxX = std::max(maxX, tile.x);
                minY = std::min(minY, tile.y);
                maxY = std::max(maxY, tile.y);
            }
            node["tiles"] = tiles;

            json_t bounds = json_t::object();
            bounds["xMin"] = minX;
            bounds["xMax"] = maxX;
            bounds["yMin"] = minY;
            bounds["yMax"] = maxY;
            bounds["width"] = (maxX - minX) + 1;
            bounds["height"] = (maxY - minY) + 1;
            node["bounds"] = bounds;

            // Generate entrance candidates using proper track sequence flag validation.
            json_t candidates = json_t::array();
            std::set<std::pair<int32_t, int32_t>> seenCandidates;
            for (const auto& tile : footprint.tiles)
            {
                for (Direction d = 0; d < kNumOrthogonalDirections; ++d)
                {
                    auto neighbor = tile + kFootprintDirectionOffsets[d];

                    if (!seenCandidates.insert({ neighbor.x, neighbor.y }).second)
                    {
                        continue;
                    }

                    auto isRideTile = std::find_if(
                        footprint.tiles.begin(), footprint.tiles.end(),
                        [&](const TileCoordsXY& rhs) { return rhs.x == neighbor.x && rhs.y == neighbor.y; })
                        != footprint.tiles.end();
                    if (isRideTile)
                    {
                        continue;
                    }

                    auto neighborCoords = neighbor.ToCoordsXY();
                    bool withinMap = MapIsLocationValid(neighborCoords);
                    bool owned = withinMap && MapIsLocationOwnedOrHasRights(neighborCoords);

                    if (!withinMap || !owned || IsTileBlockedForEntrance(neighbor))
                    {
                        continue;
                    }

                    auto validDirection = ValidateEntranceExitPlacement(ride, neighbor, StationIndex::FromUnderlying(0));
                    if (!validDirection)
                    {
                        continue;
                    }

                    json_t candidate = json_t::object();
                    candidate["x"] = neighbor.x;
                    candidate["y"] = neighbor.y;
                    candidate["directionIndex"] = static_cast<int32_t>(*validDirection);
                    candidate["direction"] = std::string(DirectionToString(*validDirection));
                    candidate["withinMap"] = withinMap;
                    candidate["owned"] = owned;
                    candidates.push_back(candidate);
                }
            }
            node["entranceCandidates"] = candidates;

            return node;
        }

        std::optional<money64> EstimateRideBlueprintPrice(const RideTypeDescriptor& descriptor)
        {
            if (descriptor.BuildCosts.TrackPrice == kMoney64Undefined)
                return std::nullopt;
            money64 price = descriptor.BuildCosts.TrackPrice;
            if (descriptor.StartTrackPiece != TrackElemType::None)
            {
                const auto& startPieceDescriptor = TrackMetaData::GetTrackElementDescriptor(descriptor.StartTrackPiece);
                price *= startPieceDescriptor.priceModifier;
                price >>= 16;
            }
            price *= descriptor.BuildCosts.PriceEstimateMultiplier;
            return price;
        }

        double RideRatingToDouble(RideRating_t value)
        {
            if (value == RideRating::kUndefined)
                return 0.0;
            return static_cast<double>(value) / 100.0;
        }

        int32_t GetInspectionIntervalMinutes(uint8_t index)
        {
            if (index >= kRideInspectionIntervalMinutes.size())
                return 0;
            return kRideInspectionIntervalMinutes[index];
        }

        std::string_view InspectionIntervalToString(uint8_t value)
        {
            switch (value)
            {
                case 0:
                    return "10min";
                case 1:
                    return "20min";
                case 2:
                    return "30min";
                case 3:
                    return "45min";
                case 4:
                    return "60min";
                case 5:
                    return "90min";
                case 6:
                    return "120min";
                case 7:
                    return "never";
                default:
                    return "unknown";
            }
        }

        std::optional<uint8_t> InspectionIntervalFromString(std::string value)
        {
            auto lowered = ToLower(std::move(value));
            if (lowered == "10" || lowered == "10min")
                return 0;
            if (lowered == "20" || lowered == "20min")
                return 1;
            if (lowered == "30" || lowered == "30min")
                return 2;
            if (lowered == "45" || lowered == "45min")
                return 3;
            if (lowered == "60" || lowered == "60min" || lowered == "1hr" || lowered == "1hour")
                return 4;
            if (lowered == "90" || lowered == "90min")
                return 5;
            if (lowered == "120" || lowered == "120min" || lowered == "2hr" || lowered == "2hours")
                return 6;
            if (lowered == "never" || lowered == "disabled" || lowered == "off")
                return 7;
            return std::nullopt;
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
                rideNameParam = GetStringParam(params, "ride");
            if (!rideNameParam)
                rideNameParam = GetStringParam(params, "name");
            if (rideNameParam)
            {
                const auto lowered = ToLower(*rideNameParam);
                auto& gameState = getGameState();
                for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
                {
                    auto& ride = gameState.rides[i];
                    if (ride.id.IsNull())
                        continue;
                    if (ToLower(ride.getName()) == lowered)
                        return RideLookupResult{ ride.id, &ride };
                }
                errorMessage = "Ride named '" + *rideNameParam + "' not found";
                return std::nullopt;
            }

            errorMessage = "rideId (--id) or ride name (--name) is required";
            return std::nullopt;
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
                return std::nullopt;
            auto coords = ride.overallView;
            auto z = TileElementHeight(coords);
            return CoordsXYZ{ coords.x, coords.y, z };
        }

        std::optional<CoordsXYZ> BuildTileCameraTarget(const TileCoordsXY& tile, int32_t width, int32_t height)
        {
            auto anchor = tile.ToCoordsXY();
            anchor.x += width * kCoordsXYHalfTile;
            anchor.y += height * kCoordsXYHalfTile;
            auto z = TileElementHeight(anchor);
            return CoordsXYZ{ anchor.x, anchor.y, z };
        }

        std::optional<CoordsXYZ> BuildTileCameraTarget(const TileCoordsXY& tile)
        {
            return BuildTileCameraTarget(tile, 1, 1);
        }

        Telemetry::AIAgentFollowHint MakeRideHint(
            std::string_view method, const Ride& ride, Telemetry::AIAgentRideWindowPage page, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            if (auto camera = BuildRideCameraTarget(ride))
                hint.cameraTarget = camera;
            Telemetry::RideWindowIntent intent;
            intent.rideId = ride.id;
            intent.page = page;
            hint.windowIntent = intent;
            return hint;
        }

        Telemetry::AIAgentFollowHint MakeRideListHint(
            std::string_view method,
            std::string contextLabel,
            Telemetry::AIAgentRideListFilter filter,
            std::optional<Telemetry::AIAgentRideListColumn> column = std::nullopt,
            std::optional<bool> sortDescending = std::nullopt)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.requestCameraFocus = false;
            Telemetry::RideListWindowIntent intent;
            intent.filter = filter;
            intent.column = column;
            intent.sortDescending = sortDescending;
            hint.windowIntent = intent;
            return hint;
        }

        // Maps RideFinancialOrderField to AIAgentRideListColumn for viewport hints
        std::optional<Telemetry::AIAgentRideListColumn> MapOrderFieldToColumn(RideFinancialOrderField order)
        {
            switch (order)
            {
                case RideFinancialOrderField::Profit:
                    return Telemetry::AIAgentRideListColumn::Profit;
                case RideFinancialOrderField::Income:
                    return Telemetry::AIAgentRideListColumn::Income;
                case RideFinancialOrderField::Cost:
                    return Telemetry::AIAgentRideListColumn::RunningCost;
                case RideFinancialOrderField::Name:
                    return std::nullopt; // Name sorting uses status column with name sort
            }
            return std::nullopt;
        }

        // Maps RidePerceptionOrderField to AIAgentRideListColumn for viewport hints
        std::optional<Telemetry::AIAgentRideListColumn> MapPerceptionOrderToColumn(RidePerceptionOrderField order)
        {
            switch (order)
            {
                case RidePerceptionOrderField::Popularity:
                    return Telemetry::AIAgentRideListColumn::Popularity;
                case RidePerceptionOrderField::Satisfaction:
                    return Telemetry::AIAgentRideListColumn::Satisfaction;
                case RidePerceptionOrderField::Excitement:
                    return Telemetry::AIAgentRideListColumn::Excitement;
                case RidePerceptionOrderField::Intensity:
                    return Telemetry::AIAgentRideListColumn::Intensity;
                case RidePerceptionOrderField::Nausea:
                    return Telemetry::AIAgentRideListColumn::Nausea;
                case RidePerceptionOrderField::Favorites:
                    return Telemetry::AIAgentRideListColumn::GuestsFavourite;
            }
            return std::nullopt;
        }

        // Maps RideOperationsOrderField to AIAgentRideListColumn for viewport hints
        std::optional<Telemetry::AIAgentRideListColumn> MapOperationsOrderToColumn(RideOperationsOrderField order)
        {
            switch (order)
            {
                case RideOperationsOrderField::Reliability:
                    return Telemetry::AIAgentRideListColumn::Reliability;
                case RideOperationsOrderField::Downtime:
                    return Telemetry::AIAgentRideListColumn::DownTime;
                case RideOperationsOrderField::QueueTime:
                    return Telemetry::AIAgentRideListColumn::QueueTime;
                case RideOperationsOrderField::QueueLength:
                    return Telemetry::AIAgentRideListColumn::QueueLength;
                case RideOperationsOrderField::Customers:
                    return Telemetry::AIAgentRideListColumn::Customers;
                case RideOperationsOrderField::Age:
                    return Telemetry::AIAgentRideListColumn::Age;
            }
            return std::nullopt;
        }

        Telemetry::AIAgentFollowHint MakeConstructRideHint(
            std::string_view method, std::string contextLabel, Telemetry::AIAgentConstructRideTab tab)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.requestCameraFocus = false;
            Telemetry::ConstructRideIntent intent;
            intent.tab = tab;
            hint.windowIntent = intent;
            return hint;
        }

        Telemetry::AIAgentFollowHint MakeGenericWindowHint(
            std::string_view method, std::string contextLabel, WindowClass windowClass, std::optional<CoordsXYZ> camera)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.cameraTarget = camera;
            Telemetry::GenericWindowIntent intent;
            intent.windowClass = windowClass;
            hint.windowIntent = intent;
            return hint;
        }

        json_t BuildRideStationAccessPayload(const Ride& ride)
        {
            json_t stations = json_t::array();
            for (StationIndex::UnderlyingType i = 0; i < ride.numStations; ++i)
            {
                const auto& station = ride.getStation(StationIndex::FromUnderlying(i));
                json_t stationJson = json_t::object();
                stationJson["index"] = i;
                stationJson["queueLength"] = station.QueueLength;
                stationJson["queueTime"] = station.QueueTime;
                stations.push_back(stationJson);
            }
            return stations;
        }

        json_t BuildRidePayload(const Ride& ride)
        {
            json_t rideJson = json_t::object();
            rideJson["id"] = ride.id.ToUnderlying();
            rideJson["name"] = ride.getName();
            rideJson["type"] = ride.type;
            rideJson["typeName"] = ResolveStringId(ride.getTypeNaming().Name);
            rideJson["status"] = RideStatusToString(ride.status);
            rideJson["mode"] = RideModeToString(ride.mode);
            rideJson["price"] = MoneyToDouble(RideGetPrice(ride));
            rideJson["queueLength"] = ride.getTotalQueueLength();
            rideJson["queueTime"] = ride.getMaxQueueTime();
            rideJson["trains"] = ride.numTrains;
            rideJson["carsPerTrain"] = ride.numCarsPerTrain;

            if (ride.overallView.x != kCoordsNull && ride.overallView.y != kCoordsNull)
            {
                json_t origin = json_t::object();
                origin["x"] = ride.overallView.x / kCoordsXYStep;
                origin["y"] = ride.overallView.y / kCoordsXYStep;
                rideJson["origin"] = origin;
            }

            json_t ratings = json_t::object();
            ratings["excitement"] = RideRatingToDouble(ride.ratings.excitement);
            ratings["intensity"] = RideRatingToDouble(ride.ratings.intensity);
            ratings["nausea"] = RideRatingToDouble(ride.ratings.nausea);
            rideJson["ratings"] = ratings;

            json_t stationAccess = BuildRideStationAccessPayload(ride);
            if (!stationAccess.empty())
                rideJson["stations"] = std::move(stationAccess);

            return rideJson;
        }

        int32_t CountGuestsOnRide(const Ride& ride)
        {
            int32_t totalGuests = 0;
            for (auto guest : EntityList<Guest>())
            {
                if (guest == nullptr)
                    continue;
                if (guest->CurrentRide == ride.id && guest->State == PeepState::onRide)
                    totalGuests++;
            }
            return totalGuests;
        }

        json_t BuildRideDetailPayload(const Ride& ride)
        {
            json_t rideJson = BuildRidePayload(ride);
            const int32_t inspectionIntervalMinutes = GetInspectionIntervalMinutes(ride.inspectionInterval);

            rideJson["numStations"] = ride.numStations;
            rideJson["numTrainsMax"] = ride.maxTrains;
            rideJson["numCarsPerTrainMax"] = ride.maxCarsPerTrain;
            rideJson["numRidersThisMonth"] = ride.numRiders;
            rideJson["currentCustomersInterval"] = ride.curNumCustomers;
            rideJson["totalCustomers"] = ride.totalCustomers;
            rideJson["favouriteGuests"] = ride.guestsFavourite;
            rideJson["guestsOnRide"] = CountGuestsOnRide(ride);
            rideJson["value"] = MoneyToDouble(ride.value);
            rideJson["profitThisMonth"] = MoneyToDouble(ride.profit);
            rideJson["totalProfit"] = MoneyToDouble(ride.totalProfit);
            rideJson["incomePerHour"] = MoneyToDouble(ride.incomePerHour);
            if (ride.upkeepCost != kMoney64Undefined)
                rideJson["runningCost"] = MoneyToDouble(ride.upkeepCost * 16);
            rideJson["reliabilityPercent"] = ride.reliabilityPercentage;
            rideJson["downtimePercent"] = ride.downtime;
            rideJson["minutesSinceInspection"] = ride.lastInspection;
            rideJson["inspectionIntervalMinutes"] = inspectionIntervalMinutes;
            rideJson["inspectionIntervalIndex"] = ride.inspectionInterval;
            rideJson["inspectionIntervalLabel"] = InspectionIntervalToString(ride.inspectionInterval);
            rideJson["dueInspection"] = (ride.lifecycleFlags & RIDE_LIFECYCLE_DUE_INSPECTION) != 0;
            rideJson["isBrokenDown"] =
                (ride.lifecycleFlags & (RIDE_LIFECYCLE_BREAKDOWN_PENDING | RIDE_LIFECYCLE_BROKEN_DOWN)) != 0;
            rideJson["mechanicDispatched"] = ride.mechanicStatus != 0;
            rideJson["ageMonths"] = ride.getAge();
            rideJson["numInversions"] = ride.numInversions;
            rideJson["lifecycleFlags"] = ride.lifecycleFlags;
            rideJson["numPrices"] = ride.getNumPrices();
            if (ride.getNumPrices() > 1)
                rideJson["secondaryPrice"] = MoneyToDouble(ride.price[1]);

            // Departure flags
            rideJson["departureFlags"] = ride.departFlags;
            rideJson["waitForLoad"] = (ride.departFlags & RIDE_DEPART_WAIT_FOR_LOAD) != 0;
            rideJson["waitForLoadLevel"] = ride.departFlags & RIDE_DEPART_WAIT_FOR_LOAD_MASK;
            rideJson["leaveWhenAnotherArrives"] = (ride.departFlags & RIDE_DEPART_LEAVE_WHEN_ANOTHER_ARRIVES) != 0;
            rideJson["syncWithAdjacentStations"] = (ride.departFlags & RIDE_DEPART_SYNCHRONISE_WITH_ADJACENT_STATIONS) != 0;

            return rideJson;
        }

        Staff* FindStaffById(int32_t id)
        {
            if (id < 0)
                return nullptr;
            for (auto staff : EntityList<Staff>())
            {
                if (staff != nullptr && staff->Id.ToUnderlying() == static_cast<uint16_t>(id))
                    return staff;
            }
            return nullptr;
        }

        std::string BreakdownReasonKey(uint8_t reason)
        {
            switch (reason)
            {
                case BREAKDOWN_SAFETY_CUT_OUT:
                    return "safetyCutOut";
                case BREAKDOWN_RESTRAINTS_STUCK_CLOSED:
                    return "restraintsStuckClosed";
                case BREAKDOWN_RESTRAINTS_STUCK_OPEN:
                    return "restraintsStuckOpen";
                case BREAKDOWN_DOORS_STUCK_CLOSED:
                    return "doorsStuckClosed";
                case BREAKDOWN_DOORS_STUCK_OPEN:
                    return "doorsStuckOpen";
                case BREAKDOWN_VEHICLE_MALFUNCTION:
                    return "vehicleMalfunction";
                case BREAKDOWN_BRAKES_FAILURE:
                    return "brakesFailure";
                case BREAKDOWN_CONTROL_FAILURE:
                    return "controlSystemFailure";
                case BREAKDOWN_NONE:
                default:
                    return "none";
            }
        }

        std::string BreakdownReasonLabel(uint8_t reason)
        {
            switch (reason)
            {
                case BREAKDOWN_SAFETY_CUT_OUT:
                    return "Safety cut-out triggered";
                case BREAKDOWN_RESTRAINTS_STUCK_CLOSED:
                    return "Restraints stuck closed";
                case BREAKDOWN_RESTRAINTS_STUCK_OPEN:
                    return "Restraints stuck open";
                case BREAKDOWN_DOORS_STUCK_CLOSED:
                    return "Doors stuck closed";
                case BREAKDOWN_DOORS_STUCK_OPEN:
                    return "Doors stuck open";
                case BREAKDOWN_VEHICLE_MALFUNCTION:
                    return "Vehicle malfunction";
                case BREAKDOWN_BRAKES_FAILURE:
                    return "Brakes failure";
                case BREAKDOWN_CONTROL_FAILURE:
                    return "Control system failure";
                case BREAKDOWN_NONE:
                default:
                    return "None";
            }
        }

        json_t BuildBreakdownReasonPayload(uint8_t reason)
        {
            json_t node = json_t::object();
            node["key"] = BreakdownReasonKey(reason);
            node["label"] = BreakdownReasonLabel(reason);
            node["code"] = reason;
            return node;
        }

        std::string MechanicStatusKey(uint8_t status)
        {
            switch (status)
            {
                case RIDE_MECHANIC_STATUS_CALLING:
                    return "calling";
                case RIDE_MECHANIC_STATUS_HEADING:
                    return "heading";
                case RIDE_MECHANIC_STATUS_FIXING:
                    return "fixing";
                case RIDE_MECHANIC_STATUS_HAS_FIXED_STATION_BRAKES:
                    return "resetting";
                case RIDE_MECHANIC_STATUS_UNDEFINED:
                default:
                    return "idle";
            }
        }

        json_t BuildMechanicStatusPayload(const Ride& ride)
        {
            json_t node = json_t::object();
            node["key"] = MechanicStatusKey(ride.mechanicStatus);
            node["mechanicAssigned"] = !ride.mechanic.IsNull();
            if (!ride.mechanic.IsNull())
            {
                const auto mechanicId = ride.mechanic.ToUnderlying();
                node["mechanicId"] = mechanicId;
                if (auto* mechanic = FindStaffById(mechanicId))
                    node["mechanicName"] = mechanic->GetName();
            }
            return node;
        }

        bool ParseRideFinancialOptions(const json_t& params, RideFinancialQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
                return true;

            if (auto it = params.find("status"); it != params.end())
            {
                if (!it->is_string())
                {
                    errorMessage = "status must be a string";
                    return false;
                }
                auto value = ToLower(it->get<std::string>());
                if (value == "all")
                    query.status = RideFinancialStatusFilter::All;
                else if (value == "open")
                    query.status = RideFinancialStatusFilter::Open;
                else if (value == "closed")
                    query.status = RideFinancialStatusFilter::Closed;
            }

            if (auto limit = GetIntParam(params, "limit"))
            {
                query.limit = static_cast<size_t>(std::max(0, *limit));
                query.limitEnabled = query.limit > 0;
            }

            return true;
        }

        json_t BuildRideFinancialsPayload(const RideFinancialQuery& query)
        {
            auto& gameState = getGameState();
            std::vector<std::pair<RideId, money64>> rideProfits;

            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;

                bool include = true;
                if (query.status == RideFinancialStatusFilter::Open && ride.status != RideStatus::open)
                    include = false;
                if (query.status == RideFinancialStatusFilter::Closed && ride.status == RideStatus::open)
                    include = false;

                if (include)
                    rideProfits.emplace_back(ride.id, ride.profit);
            }

            std::sort(rideProfits.begin(), rideProfits.end(), [&](const auto& a, const auto& b) {
                return query.descending ? (a.second > b.second) : (a.second < b.second);
            });

            json_t rides = json_t::array();
            size_t count = 0;
            for (const auto& [rideId, profit] : rideProfits)
            {
                if (query.limitEnabled && count >= query.limit)
                    break;
                auto* ride = GetRide(rideId);
                if (ride == nullptr)
                    continue;

                json_t rideJson = json_t::object();
                rideJson["id"] = rideId.ToUnderlying();
                rideJson["name"] = ride->getName();
                rideJson["status"] = RideStatusToString(ride->status);
                rideJson["profit"] = MoneyToDouble(profit);
                rideJson["income"] = MoneyToDouble(ride->incomePerHour);
                rideJson["runningCost"] = MoneyToDouble(ride->upkeepCost * 16);
                rides.push_back(rideJson);
                count++;
            }

            json_t payload = json_t::object();
            payload["rides"] = rides;
            payload["count"] = count;
            return payload;
        }

        bool ParseRidePerceptionOptions(const json_t& params, RidePerceptionQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
                return true;

            if (auto it = params.find("status"); it != params.end())
            {
                if (!it->is_string())
                {
                    errorMessage = "status must be a string";
                    return false;
                }
                auto value = ToLower(it->get<std::string>());
                if (value == "all")
                    query.status = RideFinancialStatusFilter::All;
                else if (value == "open")
                    query.status = RideFinancialStatusFilter::Open;
                else if (value == "closed")
                    query.status = RideFinancialStatusFilter::Closed;
            }

            if (auto it = params.find("order"); it != params.end())
            {
                if (!it->is_string())
                {
                    errorMessage = "order must be a string";
                    return false;
                }
                auto value = ToLower(it->get<std::string>());
                if (value == "popularity")
                    query.order = RidePerceptionOrderField::Popularity;
                else if (value == "satisfaction")
                    query.order = RidePerceptionOrderField::Satisfaction;
                else if (value == "excitement")
                    query.order = RidePerceptionOrderField::Excitement;
                else if (value == "intensity")
                    query.order = RidePerceptionOrderField::Intensity;
                else if (value == "nausea")
                    query.order = RidePerceptionOrderField::Nausea;
                else if (value == "favorites")
                    query.order = RidePerceptionOrderField::Favorites;
                else
                {
                    errorMessage = "order must be: popularity, satisfaction, excitement, intensity, nausea, or favorites";
                    return false;
                }
            }

            if (auto it = params.find("direction"); it != params.end())
            {
                if (!it->is_string())
                {
                    errorMessage = "direction must be a string";
                    return false;
                }
                auto value = ToLower(it->get<std::string>());
                query.directionSpecified = true;
                if (value == "asc")
                    query.descending = false;
                else if (value == "desc")
                    query.descending = true;
                else
                {
                    errorMessage = "direction must be: asc or desc";
                    return false;
                }
            }

            if (auto limit = GetIntParam(params, "limit"))
            {
                query.limit = static_cast<size_t>(std::max(0, *limit));
                query.limitEnabled = query.limit > 0;
            }

            return true;
        }

        // Helper to get a comparable value for perception ordering
        int64_t GetPerceptionSortValue(const Ride& ride, RidePerceptionOrderField order)
        {
            switch (order)
            {
                case RidePerceptionOrderField::Popularity:
                    return ride.popularity;
                case RidePerceptionOrderField::Satisfaction:
                    return ride.satisfaction;
                case RidePerceptionOrderField::Excitement:
                    return ride.ratings.isNull() ? -1 : static_cast<int64_t>(ride.ratings.excitement);
                case RidePerceptionOrderField::Intensity:
                    return ride.ratings.isNull() ? -1 : static_cast<int64_t>(ride.ratings.intensity);
                case RidePerceptionOrderField::Nausea:
                    return ride.ratings.isNull() ? -1 : static_cast<int64_t>(ride.ratings.nausea);
                case RidePerceptionOrderField::Favorites:
                    return ride.guestsFavourite;
            }
            return 0;
        }

        json_t BuildRidePerceptionPayload(const RidePerceptionQuery& query)
        {
            auto& gameState = getGameState();
            std::vector<std::pair<RideId, int64_t>> rideValues;

            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;

                bool include = true;
                if (query.status == RideFinancialStatusFilter::Open && ride.status != RideStatus::open)
                    include = false;
                if (query.status == RideFinancialStatusFilter::Closed && ride.status == RideStatus::open)
                    include = false;

                if (include)
                    rideValues.emplace_back(ride.id, GetPerceptionSortValue(ride, query.order));
            }

            std::sort(rideValues.begin(), rideValues.end(), [&](const auto& a, const auto& b) {
                return query.descending ? (a.second > b.second) : (a.second < b.second);
            });

            json_t rides = json_t::array();
            size_t count = 0;
            for (const auto& [rideId, sortValue] : rideValues)
            {
                if (query.limitEnabled && count >= query.limit)
                    break;
                auto* ride = GetRide(rideId);
                if (ride == nullptr)
                    continue;

                json_t rideJson = json_t::object();
                rideJson["id"] = rideId.ToUnderlying();
                rideJson["name"] = ride->getName();
                rideJson["status"] = RideStatusToString(ride->status);
                rideJson["popularity"] = ride->popularity;
                rideJson["satisfaction"] = ride->satisfaction;
                rideJson["guestsFavourite"] = ride->guestsFavourite;
                if (!ride->ratings.isNull())
                {
                    rideJson["excitement"] = RideRatingToDouble(ride->ratings.excitement);
                    rideJson["intensity"] = RideRatingToDouble(ride->ratings.intensity);
                    rideJson["nausea"] = RideRatingToDouble(ride->ratings.nausea);
                }
                else
                {
                    rideJson["excitement"] = nullptr;
                    rideJson["intensity"] = nullptr;
                    rideJson["nausea"] = nullptr;
                }
                rides.push_back(rideJson);
                count++;
            }

            json_t payload = json_t::object();
            payload["rides"] = rides;
            payload["count"] = count;
            return payload;
        }

        bool ParseRideOperationsOptions(const json_t& params, RideOperationsQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
                return true;

            if (auto it = params.find("status"); it != params.end())
            {
                if (!it->is_string())
                {
                    errorMessage = "status must be a string";
                    return false;
                }
                auto value = ToLower(it->get<std::string>());
                if (value == "all")
                    query.status = RideFinancialStatusFilter::All;
                else if (value == "open")
                    query.status = RideFinancialStatusFilter::Open;
                else if (value == "closed")
                    query.status = RideFinancialStatusFilter::Closed;
            }

            if (auto it = params.find("order"); it != params.end())
            {
                if (!it->is_string())
                {
                    errorMessage = "order must be a string";
                    return false;
                }
                auto value = ToLower(it->get<std::string>());
                if (value == "reliability")
                    query.order = RideOperationsOrderField::Reliability;
                else if (value == "downtime")
                    query.order = RideOperationsOrderField::Downtime;
                else if (value == "queuetime")
                    query.order = RideOperationsOrderField::QueueTime;
                else if (value == "queuelength")
                    query.order = RideOperationsOrderField::QueueLength;
                else if (value == "customers")
                    query.order = RideOperationsOrderField::Customers;
                else if (value == "age")
                    query.order = RideOperationsOrderField::Age;
                else
                {
                    errorMessage = "order must be: reliability, downtime, queueTime, queueLength, customers, or age";
                    return false;
                }
            }

            if (auto it = params.find("direction"); it != params.end())
            {
                if (!it->is_string())
                {
                    errorMessage = "direction must be a string";
                    return false;
                }
                auto value = ToLower(it->get<std::string>());
                query.directionSpecified = true;
                if (value == "asc")
                    query.descending = false;
                else if (value == "desc")
                    query.descending = true;
                else
                {
                    errorMessage = "direction must be: asc or desc";
                    return false;
                }
            }

            if (auto limit = GetIntParam(params, "limit"))
            {
                query.limit = static_cast<size_t>(std::max(0, *limit));
                query.limitEnabled = query.limit > 0;
            }

            return true;
        }

        // Helper to get a comparable value for operations ordering
        int64_t GetOperationsSortValue(const Ride& ride, RideOperationsOrderField order)
        {
            switch (order)
            {
                case RideOperationsOrderField::Reliability:
                    return ride.reliabilityPercentage;
                case RideOperationsOrderField::Downtime:
                    return ride.downtime;
                case RideOperationsOrderField::QueueTime:
                    return ride.getMaxQueueTime();
                case RideOperationsOrderField::QueueLength:
                    return ride.getTotalQueueLength();
                case RideOperationsOrderField::Customers:
                    return RideCustomersPerHour(ride);
                case RideOperationsOrderField::Age:
                    return ride.buildDate;
            }
            return 0;
        }

        json_t BuildRideOperationsPayload(const RideOperationsQuery& query)
        {
            auto& gameState = getGameState();
            std::vector<std::pair<RideId, int64_t>> rideValues;

            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;

                bool include = true;
                if (query.status == RideFinancialStatusFilter::Open && ride.status != RideStatus::open)
                    include = false;
                if (query.status == RideFinancialStatusFilter::Closed && ride.status == RideStatus::open)
                    include = false;

                if (include)
                    rideValues.emplace_back(ride.id, GetOperationsSortValue(ride, query.order));
            }

            // For most operations metrics, descending is "worse" (higher downtime = bad)
            // But reliability is inverted (higher = better), and age sort is special
            std::sort(rideValues.begin(), rideValues.end(), [&](const auto& a, const auto& b) {
                return query.descending ? (a.second > b.second) : (a.second < b.second);
            });

            json_t rides = json_t::array();
            size_t count = 0;
            for (const auto& [rideId, sortValue] : rideValues)
            {
                if (query.limitEnabled && count >= query.limit)
                    break;
                auto* ride = GetRide(rideId);
                if (ride == nullptr)
                    continue;

                json_t rideJson = json_t::object();
                rideJson["id"] = rideId.ToUnderlying();
                rideJson["name"] = ride->getName();
                rideJson["status"] = RideStatusToString(ride->status);
                rideJson["reliability"] = ride->reliabilityPercentage;
                rideJson["downtime"] = ride->downtime;
                rideJson["queueTime"] = ride->getMaxQueueTime();
                rideJson["queueLength"] = ride->getTotalQueueLength();
                rideJson["customersPerHour"] = RideCustomersPerHour(*ride);
                rideJson["totalCustomers"] = ride->totalCustomers;

                // Calculate age in months
                auto currentDate = getGameState().date.GetMonthsElapsed();
                auto ageMonths = currentDate - ride->buildDate;
                rideJson["ageMonths"] = ageMonths;

                rides.push_back(rideJson);
                count++;
            }

            json_t payload = json_t::object();
            payload["rides"] = rides;
            payload["count"] = count;
            return payload;
        }

        std::optional<int32_t> ResolvePlacementHeight(const json_t& params, const TileCoordsXY& tile, std::string& errorMessage)
        {
            if (auto explicitZ = GetIntParam(params, "z"))
                return OpenRCT2::Numerics::floor2(TileZToWorldZ(*explicitZ), kCoordsZStep);

            auto coords = tile.ToCoordsXY();
            if (!MapIsLocationValid(coords))
            {
                errorMessage = "Coordinates outside map bounds";
                return std::nullopt;
            }

            auto* surface = MapGetSurfaceElementAt(coords);
            if (surface == nullptr)
            {
                errorMessage = "Surface data not found at tile";
                return std::nullopt;
            }
            return OpenRCT2::Numerics::floor2(surface->GetBaseZ(), kCoordsZStep);
        }

        std::string TrimLegacyIdentifier(std::string_view identifier)
        {
            while (!identifier.empty() && identifier.back() == ' ')
                identifier.remove_suffix(1);
            return std::string(identifier);
        }

        std::optional<RideBlueprintInfo> BuildBlueprintFromRideObject(RideObject& rideObject, ObjectEntryIndex entryIndex)
        {
            const auto& rideEntry = rideObject.GetEntry();
            auto rideType = rideEntry.GetFirstNonNullRideType();
            if (rideType == kRideTypeNull)
                return std::nullopt;

            RideBlueprintInfo info;
            info.entryIndex = entryIndex;
            info.rideType = rideType;
            info.rideEntry = &rideEntry;
            info.descriptor = &GetRideTypeDescriptor(rideType);
            info.rideObject = &rideObject;
            auto descriptorIdentifier = rideObject.GetIdentifier();
            if (!descriptorIdentifier.empty())
                info.identifier = std::string(descriptorIdentifier);
            else
                info.identifier = TrimLegacyIdentifier(rideObject.GetLegacyIdentifier());
            return info;
        }

        std::optional<RideBlueprintInfo> ResolveRideBlueprint(std::string identifier, std::string& errorMessage)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                errorMessage = "Game context is not available";
                return std::nullopt;
            }

            auto& manager = context->GetObjectManager();
            const auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::ride));

            auto normalised = ToLower(identifier);
            for (ObjectEntryIndex entryIndex = 0; entryIndex < maxEntries; ++entryIndex)
            {
                auto* rideObject = manager.GetLoadedObject<RideObject>(entryIndex);
                if (rideObject == nullptr)
                    continue;

                auto legacyId = TrimLegacyIdentifier(rideObject->GetLegacyIdentifier());
                auto descriptorId = std::string(rideObject->GetIdentifier());

                if (ToLower(legacyId) == normalised || ToLower(descriptorId) == normalised)
                {
                    if (auto info = BuildBlueprintFromRideObject(*rideObject, entryIndex))
                        return info;
                }
            }

            errorMessage = "Ride blueprint '" + identifier + "' not found";
            return std::nullopt;
        }

        // Track Design Helpers
        static std::string RideCategoryDisplayName(RideCategory category)
        {
            switch (category)
            {
                case RideCategory::transport:
                    return "Transport Rides";
                case RideCategory::gentle:
                    return "Gentle Rides";
                case RideCategory::rollerCoaster:
                    return "Roller Coasters";
                case RideCategory::thrill:
                    return "Thrill Rides";
                case RideCategory::water:
                    return "Water Rides";
                case RideCategory::shop:
                    return "Shops & Stalls";
                default:
                    return "Unknown";
            }
        }

        static std::optional<RideCategory> RideCategoryFromString(const std::string& str)
        {
            if (str == "transport")
                return RideCategory::transport;
            if (str == "gentle")
                return RideCategory::gentle;
            if (str == "rollerCoaster" || str == "roller_coaster" || str == "coaster")
                return RideCategory::rollerCoaster;
            if (str == "thrill")
                return RideCategory::thrill;
            if (str == "water")
                return RideCategory::water;
            if (str == "shop")
                return RideCategory::shop;
            return std::nullopt;
        }

        // Helper to find a track design by name
        std::optional<std::pair<std::string, TrackDesign>> FindTrackDesignByName(
            ITrackDesignRepository* trackRepo, const std::string& designName)
        {
            trackRepo->Scan(LocalisationService_GetCurrentLanguage());

            // Search through all ride types for designs
            for (ride_type_t rideType = 0; rideType < RIDE_TYPE_COUNT; rideType++)
            {
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                if (descriptor.Category == RideCategory::none || descriptor.Category == RideCategory::shop)
                {
                    continue;
                }

                auto items = trackRepo->GetItemsForObjectEntry(rideType, "");
                for (const auto& item : items)
                {
                    if (item.name == designName)
                    {
                        auto td = TrackDesignImport(item.path.c_str());
                        if (td != nullptr)
                        {
                            return std::make_pair(item.path, std::move(*td));
                        }
                    }
                }
            }
            return std::nullopt;
        }

        // ========== Theme/Color Helper Functions ==========

        std::optional<colour_t> ResolveColorValue(const json_t& param, std::string& errorOut)
        {
            // Only accept string color names
            if (!param.is_string())
            {
                errorOut = "Color must be a name (e.g., 'bright_red'). Use 'rides theme colors' to see valid names.";
                return std::nullopt;
            }

            std::string input = param.get<std::string>();

            // Normalize: lowercase, replace hyphens with underscores
            auto normalised = ToLower(input);
            std::replace(normalised.begin(), normalised.end(), '-', '_');

            auto colorVal = OpenRCT2::Colour::FromString(normalised, COLOUR_NULL);
            if (colorVal == COLOUR_NULL)
            {
                errorOut = "Unknown color '" + input + "'. Use 'rides theme colors' to see valid names.";
                return std::nullopt;
            }
            return colorVal;
        }

        json_t BuildColorPayload(colour_t color)
        {
            return OpenRCT2::Colour::ToString(color);
        }

        std::string_view VehicleColourSettingsToString(VehicleColourSettings settings)
        {
            switch (settings)
            {
                case VehicleColourSettings::same:
                    return "same";
                case VehicleColourSettings::perTrain:
                    return "per-train";
                case VehicleColourSettings::perCar:
                    return "per-car";
                default:
                    return "unknown";
            }
        }

        std::optional<VehicleColourSettings> VehicleColourSettingsFromString(std::string value)
        {
            auto normalised = ToLower(value);
            std::replace(normalised.begin(), normalised.end(), '-', '_');
            std::replace(normalised.begin(), normalised.end(), ' ', '_');

            if (normalised == "same" || normalised == "all")
                return VehicleColourSettings::same;
            if (normalised == "per_train" || normalised == "pertrain")
                return VehicleColourSettings::perTrain;
            if (normalised == "per_car" || normalised == "percar")
                return VehicleColourSettings::perCar;

            return std::nullopt;
        }

        std::optional<ObjectEntryIndex> ResolveEntranceStyle(
            const json_t& styleParam,
            std::string& errorOut)
        {
            // Only accept string names or identifiers
            if (!styleParam.is_string())
            {
                errorOut = "Station style must be a name (e.g., 'Plain') or identifier (e.g., 'rct2.station.plain'). "
                           "Use 'rides theme entrance list' to see available styles.";
                return std::nullopt;
            }

            auto* context = GetContext();
            auto& manager = context->GetObjectManager();
            auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::station));

            auto searchTerm = ToLower(styleParam.get<std::string>());

            for (ObjectEntryIndex idx = 0; idx < maxEntries; ++idx)
            {
                auto* stationObj = manager.GetLoadedObject<StationObject>(idx);
                if (stationObj == nullptr)
                    continue;

                // Try matching identifier
                auto identifier = ToLower(std::string(stationObj->GetIdentifier()));
                if (identifier == searchTerm)
                    return idx;

                // Try matching display name
                auto name = ToLower(std::string(LanguageGetString(stationObj->NameStringId)));
                if (name == searchTerm)
                    return idx;
            }

            errorOut = "Unknown station style '" + styleParam.get<std::string>()
                     + "'. Use 'rides theme entrance list' to see available styles.";
            return std::nullopt;
        }

        json_t BuildTrackColourPayload(const TrackColour& colours, int schemeIndex)
        {
            return {
                { "scheme", schemeIndex },
                { "main", BuildColorPayload(colours.main) },
                { "additional", BuildColorPayload(colours.additional) },
                { "supports", BuildColorPayload(colours.supports) }
            };
        }

        json_t BuildVehicleColourPayload(const VehicleColour& colours, int vehicleIndex)
        {
            return {
                { "index", vehicleIndex },
                { "body", BuildColorPayload(colours.Body) },
                { "trim", BuildColorPayload(colours.Trim) },
                { "tertiary", BuildColorPayload(colours.Tertiary) }
            };
        }

        json_t BuildEntranceStylePayload(ObjectEntryIndex styleIndex)
        {
            auto* context = GetContext();
            auto& manager = context->GetObjectManager();

            json_t result = json_t::object();

            if (styleIndex != kObjectEntryIndexNull)
            {
                auto* stationObj = manager.GetLoadedObject<StationObject>(styleIndex);
                if (stationObj != nullptr)
                {
                    result["identifier"] = stationObj->GetIdentifier();
                    result["name"] = LanguageGetString(stationObj->NameStringId);
                }
            }

            return result;
        }

        // RPC Handlers

        json_t BuildRideListPayload()
        {
            const auto& gameState = getGameState();
            json_t rides = json_t::array();
            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                {
                    continue;
                }

                rides.push_back(BuildRidePayload(ride));
            }
            return rides;
        }

        RpcResult HandleRideAvailability(const json_t& params)
        {
            if (!params.is_object() && !params.is_null())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto* context = GetContext();
            if (context == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Game context is not available");
            }

            const bool includeLocked = params.is_object() ? GetBoolParam(params, "includeLocked").value_or(false) : false;
            auto& manager = context->GetObjectManager();
            const auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::ride));

            json_t rides = json_t::array();
            size_t loadedEntries = 0;
            for (ObjectEntryIndex entryIndex = 0; entryIndex < maxEntries; ++entryIndex)
            {
                auto* rideObject = manager.GetLoadedObject<RideObject>(entryIndex);
                if (rideObject == nullptr)
                {
                    continue;
                }
                loadedEntries++;

                const auto& rideEntry = rideObject->GetEntry();
                const auto primaryRideType = rideEntry.GetFirstNonNullRideType();
                if (primaryRideType == kRideTypeNull)
                {
                    continue;
                }

                const bool invented = ResearchIsInvented(ObjectType::ride, entryIndex) || RideEntryIsInvented(entryIndex);
                if (!includeLocked && !invented)
                {
                    continue;
                }

                const auto& descriptor = GetRideTypeDescriptor(primaryRideType);
                json_t node = json_t::object();
                node["entryIndex"] = entryIndex;
                node["identifier"] = std::string(rideObject->GetLegacyIdentifier());
                node["name"] = ResolveStringId(rideEntry.naming.Name);
                node["invented"] = invented;
                node["category"] = std::string(RideCategoryToString(descriptor.Category));
                node["classification"] = std::string(RideClassificationToString(descriptor.Classification));
                node["primaryType"] = primaryRideType;
                node["primaryTypeName"] = descriptor.Name.empty() ? std::string("ride") : std::string(descriptor.Name);

                json_t rideTypes = json_t::array();
                for (const auto rideType : rideEntry.ride_type)
                {
                    if (rideType == kRideTypeNull)
                    {
                        continue;
                    }
                    const auto& typeDescriptor = GetRideTypeDescriptor(rideType);
                    rideTypes.push_back(
                        typeDescriptor.Name.empty() ? std::string("ride") : std::string(typeDescriptor.Name));
                }
                node["rideTypes"] = rideTypes;

                json_t costNode = json_t::object();
                costNode["track"] = MoneyToDouble(descriptor.BuildCosts.TrackPrice);
                costNode["supports"] = MoneyToDouble(descriptor.BuildCosts.SupportPrice);
                costNode["estimateMultiplier"] = descriptor.BuildCosts.PriceEstimateMultiplier;
                node["buildCost"] = costNode;

                if (auto estimatedPrice = EstimateRideBlueprintPrice(descriptor))
                {
                    node["priceEstimate"] = MoneyToDouble(*estimatedPrice);
                }

                rides.push_back(node);
            }

            json_t payload = json_t::object();
            payload["rides"] = rides;
            payload["count"] = rides.size();
            payload["includeLocked"] = includeLocked;
            payload["loadedEntries"] = loadedEntries;
            std::string contextLabel = includeLocked ? "Listed all ride blueprints" : "Listed invented ride blueprints";
            auto hint = MakeConstructRideHint(
                "rides.available", std::move(contextLabel), Telemetry::AIAgentConstructRideTab::Transport);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideFinancials(const json_t& params)
        {
            RideFinancialQuery query;
            std::string errorMessage;
            if (!ParseRideFinancialOptions(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildRideFinancialsPayload(query);

            // Configure hint to show the same column and sort direction as the query
            auto column = MapOrderFieldToColumn(query.order);
            auto hint = MakeRideListHint(
                "rides.financials",
                "Viewed ride financial summary",
                Telemetry::AIAgentRideListFilter::Rides,
                column,
                query.descending);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRidePerception(const json_t& params)
        {
            RidePerceptionQuery query;
            std::string errorMessage;
            if (!ParseRidePerceptionOptions(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildRidePerceptionPayload(query);

            // Configure hint to show the same column and sort direction as the query
            auto column = MapPerceptionOrderToColumn(query.order);
            auto hint = MakeRideListHint(
                "rides.perception",
                "Viewed ride perception metrics",
                Telemetry::AIAgentRideListFilter::Rides,
                column,
                query.descending);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideOperations(const json_t& params)
        {
            RideOperationsQuery query;
            std::string errorMessage;
            if (!ParseRideOperationsOptions(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildRideOperationsPayload(query);

            // Configure hint to show the same column and sort direction as the query
            auto column = MapOperationsOrderToColumn(query.order);
            auto hint = MakeRideListHint(
                "rides.operations",
                "Viewed ride operations metrics",
                Telemetry::AIAgentRideListFilter::Rides,
                column,
                query.descending);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideStatus(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildRideDetailPayload(*rideLookup->ride);
            auto rideLabel = BuildRideDisplayName(*rideLookup->ride);
            std::string contextLabel = "Inspect status for " + rideLabel;
            auto hint = MakeRideHint("rides.status", *rideLookup->ride, Telemetry::AIAgentRideWindowPage::Main, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRidePrice(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            json_t payload = json_t::object();
            payload["ride"] = BuildRidePayload(*rideLookup->ride);
            payload["price"] = MoneyToDouble(RideGetPrice(*rideLookup->ride));
            payload["numPrices"] = rideLookup->ride->getNumPrices();
            if (rideLookup->ride->getNumPrices() > 1)
            {
                payload["secondaryPrice"] = MoneyToDouble(rideLookup->ride->price[1]);
            }
            auto rideLabel = BuildRideDisplayName(*rideLookup->ride);
            std::string contextLabel = "Inspect price for " + rideLabel;
            auto hint = MakeRideHint("rides.price", *rideLookup->ride, Telemetry::AIAgentRideWindowPage::Income, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideSetStatus(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            // Check if this is actually a shop/stall, not a ride
            const auto& descriptor = GetRideTypeDescriptor(rideLookup->ride->type);
            if (descriptor.Classification == RideClassification::shopOrStall ||
                descriptor.Classification == RideClassification::kioskOrFacility)
            {
                auto rideName = rideLookup->ride->getName();
                auto rideId = rideLookup->id.ToUnderlying();
                auto classification = RideClassificationToString(descriptor.Classification);
                return RpcResult::Error(kErrorInvalidParams,
                    "ID " + std::to_string(rideId) + " is a " + std::string(classification) +
                    " (" + rideName + "), not a ride.\n" +
                    "Use 'rctctl shops open --id " + std::to_string(rideId) + "' instead.");
            }

            auto statusParam = GetStringParam(params, "status");
            if (!statusParam)
            {
                statusParam = GetStringParam(params, "state");
            }
            if (!statusParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "status is required");
            }

            auto desiredStatus = RideStatusFromString(*statusParam);
            if (!desiredStatus)
            {
                return RpcResult::Error(kErrorInvalidParams, "Unknown ride status: " + *statusParam);
            }

            const auto previousStatus = rideLookup->ride->status;
            auto action = GameActions::RideSetStatusAction(rideLookup->id, desiredStatus.value());
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto* updatedRide = GetRide(rideLookup->id);
            if (updatedRide == nullptr || updatedRide->id.IsNull())
            {
                return RpcResult::Error(kErrorActionFailed, "Ride status action succeeded but ride could not be retrieved");
            }

            // Handle evictGuests flag - immediately remove guests from ride (like "close for repairs")
            const bool evictGuests = GetBoolParam(params, "evictGuests").value_or(false);
            if (evictGuests && desiredStatus.value() == RideStatus::closed)
            {
                RideClearForConstruction(*updatedRide);
                updatedRide->removePeeps();
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["ride"] = BuildRidePayload(*updatedRide);
            // Use the requested status since the action succeeded - reading back from ride may have timing issues
            payload["status"] = RideStatusToString(desiredStatus.value());
            payload["previousStatus"] = RideStatusToString(previousStatus);
            auto rideLabel = BuildRideDisplayName(*updatedRide);
            std::string contextLabel = "Set status of " + rideLabel + " to " + std::string(RideStatusToString(desiredStatus.value()));
            auto hint = MakeRideHint("rides.setStatus", *updatedRide, Telemetry::AIAgentRideWindowPage::Main, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideSetPrice(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            auto priceParam = GetDoubleParam(params, "price");
            if (!priceParam)
            {
                priceParam = GetDoubleParam(params, "value");
            }
            if (!priceParam)
            {
                priceParam = GetDoubleParam(params, "amount");
            }
            if (!priceParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "price is required");
            }

            const bool secondary = GetBoolParam(params, "secondary").value_or(false);
            if (secondary && rideLookup->ride->getNumPrices() < 2)
            {
                return RpcResult::Error(kErrorInvalidParams, "Ride does not have a secondary price");
            }

            // Check if price can actually be modified for this ride (matching UI behavior)
            // In pay-for-entry parks, only shops/stalls/toilets can have prices set
            if (!secondary)
            {
                const auto* rideEntry = GetRideEntryByIndex(rideLookup->ride->subtype);
                const auto& rtd = rideLookup->ride->getRideTypeDescriptor();
                bool canModifyPrice = Park::RidePricesUnlocked() || rtd.specialType == RtdSpecialType::toilet
                    || (rideEntry != nullptr && rideEntry->shop_item[0] != ShopItem::none);

                if (!canModifyPrice)
                {
                    return RpcResult::Error(
                        kErrorInvalidParams,
                        "Cannot set ride price: ride prices are locked in pay-for-entry parks. "
                        "Only shops, stalls, and toilets can have prices set.");
                }
            }

            money64 newPrice = ToMoney64FromGBP(*priceParam);
            const money64 previousPrice =
                secondary ? rideLookup->ride->price[1] : RideGetPrice(*rideLookup->ride);

            auto action = GameActions::RideSetPriceAction(rideLookup->id, newPrice, !secondary);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto* updatedRide = GetRide(rideLookup->id);
            if (updatedRide == nullptr || updatedRide->id.IsNull())
            {
                return RpcResult::Error(kErrorActionFailed, "Ride price action succeeded but ride could not be retrieved");
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["ride"] = BuildRidePayload(*updatedRide);
            // Use the price we're setting directly rather than re-reading from ride
            // (which may not be updated yet if action is async)
            payload["price"] = MoneyToDouble(newPrice);
            payload["previousPrice"] = MoneyToDouble(previousPrice);
            payload["secondary"] = secondary;
            auto rideLabel = BuildRideDisplayName(*updatedRide);
            std::string contextLabel = "Set ";
            if (secondary)
            {
                contextLabel += "secondary ";
            }
            contextLabel += "price for " + rideLabel + " to " + FormatMoneyString(newPrice);
            auto hint = MakeRideHint("rides.setPrice", *updatedRide, Telemetry::AIAgentRideWindowPage::Income, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideDemolish(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            auto modeParam = GetStringParam(params, "mode").value_or("demolish");
            auto modifyType = RideModifyTypeFromString(modeParam);
            if (!modifyType)
            {
                return RpcResult::Error(kErrorInvalidParams, "Unknown modify mode: " + modeParam);
            }

            json_t rideSnapshot = BuildRidePayload(*rideLookup->ride);
            auto rideLabelForHint = BuildRideDisplayName(*rideLookup->ride);
            auto hintContext = std::string(RideModifyTypeToString(*modifyType)) + " " + rideLabelForHint;
            auto hint = MakeRideHint("rides.demolish", *rideLookup->ride, Telemetry::AIAgentRideWindowPage::Main, std::move(hintContext));
            hint.requestWindowFocus = false;
            auto action = GameActions::RideDemolishAction(rideLookup->id, *modifyType);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["ride"] = rideSnapshot;
            payload["modifyType"] = std::string(RideModifyTypeToString(*modifyType));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideRename(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            auto newNameParam = GetStringParam(params, "newName");
            if (!newNameParam)
            {
                newNameParam = GetStringParam(params, "label");
            }
            if (!newNameParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "newName is required");
            }
            if (newNameParam->empty())
            {
                return RpcResult::Error(kErrorInvalidParams, "newName cannot be empty");
            }

            const std::string previousName = rideLookup->ride->getName();
            auto action = GameActions::RideSetNameAction(rideLookup->id, *newNameParam);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto* updatedRide = GetRide(rideLookup->id);
            if (updatedRide == nullptr || updatedRide->id.IsNull())
            {
                return RpcResult::Error(kErrorActionFailed, "Ride rename action succeeded but ride could not be retrieved");
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["ride"] = BuildRidePayload(*updatedRide);
            payload["previousName"] = previousName;
            // Use the name we're setting directly rather than re-reading from ride
            // (which may not be updated yet if action is async)
            payload["name"] = *newNameParam;
            std::string contextLabel = "Renamed " + previousName + " to " + *newNameParam;
            auto hint = MakeRideHint("rides.rename", *updatedRide, Telemetry::AIAgentRideWindowPage::Main, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideConfigure(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            using GameActions::RideSetSetting;
            json_t applied = json_t::object();
            bool mutated = false;
            GameActions::Result lastResult{};

            auto execSetting = [&](RideSetSetting setting, uint8_t value) -> bool {
                auto action = GameActions::RideSetSettingAction(rideLookup->id, setting, value);
                lastResult = GameActions::Execute(&action, getGameState());
                if (lastResult.Error != GameActions::Status::Ok)
                {
                    return false;
                }
                mutated = true;
                return true;
            };

            if (auto modeParam = GetStringParam(params, "mode"))
            {
                auto parsed = RideModeFromString(*modeParam);
                if (!parsed)
                {
                    return RpcResult::Error(kErrorInvalidParams, "Unknown ride mode: " + *modeParam);
                }
                if (!execSetting(RideSetSetting::Mode, static_cast<uint8_t>(parsed.value())))
                {
                    return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(lastResult));
                }
                applied["mode"] = RideModeToString(parsed.value());
            }

            auto minWaitParam = GetIntParam(params, "minWait");
            if (!minWaitParam)
            {
                minWaitParam = GetIntParam(params, "minWaitingTime");
            }
            if (minWaitParam)
            {
                if (*minWaitParam < 0 || *minWaitParam > 255)
                {
                    return RpcResult::Error(kErrorInvalidParams, "minWait must be between 0 and 255");
                }
                if (!execSetting(RideSetSetting::MinWaitingTime, static_cast<uint8_t>(*minWaitParam)))
                {
                    return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(lastResult));
                }
                applied["minWait"] = *minWaitParam;
            }

            auto maxWaitParam = GetIntParam(params, "maxWait");
            if (!maxWaitParam)
            {
                maxWaitParam = GetIntParam(params, "maxWaitingTime");
            }
            if (maxWaitParam)
            {
                if (*maxWaitParam < 0 || *maxWaitParam > 255)
                {
                    return RpcResult::Error(kErrorInvalidParams, "maxWait must be between 0 and 255");
                }
                if (!execSetting(RideSetSetting::MaxWaitingTime, static_cast<uint8_t>(*maxWaitParam)))
                {
                    return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(lastResult));
                }
                applied["maxWait"] = *maxWaitParam;
            }

            if (auto circuitsParam = GetIntParam(params, "numCircuits"))
            {
                if (*circuitsParam < 1 || *circuitsParam > 255)
                {
                    return RpcResult::Error(kErrorInvalidParams, "numCircuits must be between 1 and 255");
                }
                if (!execSetting(RideSetSetting::NumCircuits, static_cast<uint8_t>(*circuitsParam)))
                {
                    return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(lastResult));
                }
                applied["numCircuits"] = *circuitsParam;
            }

            if (auto liftSpeedParam = GetIntParam(params, "liftHillSpeed"))
            {
                if (*liftSpeedParam < 0 || *liftSpeedParam > 255)
                {
                    return RpcResult::Error(kErrorInvalidParams, "liftHillSpeed must be between 0 and 255");
                }
                if (!execSetting(RideSetSetting::LiftHillSpeed, static_cast<uint8_t>(*liftSpeedParam)))
                {
                    return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(lastResult));
                }
                applied["liftHillSpeed"] = *liftSpeedParam;
            }

            std::optional<uint8_t> inspectionValue;
            if (auto inspectionIndex = GetIntParam(params, "inspectionIndex"))
            {
                if (*inspectionIndex < 0 || *inspectionIndex > RIDE_INSPECTION_NEVER)
                {
                    return RpcResult::Error(kErrorInvalidParams, "inspectionIndex out of range");
                }
                inspectionValue = static_cast<uint8_t>(*inspectionIndex);
            }
            if (!inspectionValue)
            {
                auto inspectionLabel = GetStringParam(params, "inspectionInterval");
                if (!inspectionLabel)
                {
                    inspectionLabel = GetStringParam(params, "inspection");
                }
                if (inspectionLabel)
                {
                    inspectionValue = InspectionIntervalFromString(*inspectionLabel);
                    if (!inspectionValue)
                    {
                        return RpcResult::Error(kErrorInvalidParams, "Unknown inspection interval: " + *inspectionLabel);
                    }
                }
            }
            if (inspectionValue)
            {
                if (!execSetting(RideSetSetting::InspectionInterval, inspectionValue.value()))
                {
                    return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(lastResult));
                }
                applied["inspectionIntervalIndex"] = inspectionValue.value();
                applied["inspectionIntervalLabel"] = InspectionIntervalToString(inspectionValue.value());
            }

            // Operating option (laps, launch speed, rotations, time limit - depends on ride type)
            if (auto operationParam = GetIntParam(params, "operationOption"))
            {
                if (*operationParam < 0 || *operationParam > 255)
                {
                    return RpcResult::Error(kErrorInvalidParams, "operationOption must be between 0 and 255");
                }
                if (!execSetting(RideSetSetting::Operation, static_cast<uint8_t>(*operationParam)))
                {
                    // Include valid range in error message for better guidance
                    const auto& opSettings = rideLookup->ride->getRideTypeDescriptor().OperatingSettings;
                    std::string errorDetail = BuildGameActionErrorMessage(lastResult);
                    errorDetail += " (valid range for this ride: " + std::to_string(opSettings.MinValue) + "-" + std::to_string(opSettings.MaxValue) + ")";
                    return RpcResult::Error(kErrorActionFailed, errorDetail);
                }
                applied["operationOption"] = *operationParam;
            }

            // Departure flags
            if (auto departParam = GetIntParam(params, "departureFlags"))
            {
                if (*departParam < 0 || *departParam > 255)
                {
                    return RpcResult::Error(kErrorInvalidParams, "departureFlags must be between 0 and 255");
                }
                if (!execSetting(RideSetSetting::Departure, static_cast<uint8_t>(*departParam)))
                {
                    return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(lastResult));
                }
                applied["departureFlags"] = *departParam;
            }

            if (!mutated)
            {
                return RpcResult::Error(kErrorInvalidParams, "No configuration fields provided");
            }

            auto* updatedRide = GetRide(rideLookup->id);
            if (updatedRide == nullptr || updatedRide->id.IsNull())
            {
                return RpcResult::Error(kErrorActionFailed, "Ride configuration action succeeded but ride could not be retrieved");
            }

            json_t payload = BuildActionSuccessPayload(lastResult);
            payload["ride"] = BuildRideDetailPayload(*updatedRide);
            payload["applied"] = applied;
            payload["updatedFields"] = applied.size();
            auto rideLabel = BuildRideDisplayName(*updatedRide);
            std::string contextLabel = "Updated settings for " + rideLabel + " (" + std::to_string(applied.size()) + " fields)";
            auto hint = MakeRideHint("rides.configure", *updatedRide, Telemetry::AIAgentRideWindowPage::Operating, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideBreakdowns(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            const auto& ride = *rideLookup->ride;
            json_t payload = json_t::object();
            payload["ride"] = BuildRidePayload(ride);
            payload["isBrokenDown"] = (ride.lifecycleFlags & RIDE_LIFECYCLE_BROKEN_DOWN) != 0;
            payload["currentReason"] = BuildBreakdownReasonPayload(ride.breakdownReason);
            payload["pendingReason"] = BuildBreakdownReasonPayload(ride.breakdownReasonPending);
            payload["mechanic"] = BuildMechanicStatusPayload(ride);
            payload["reliabilityPercent"] = ride.reliabilityPercentage;
            payload["downtimePercent"] = ride.downtime;

            const int32_t inspectionMinutes = GetInspectionIntervalMinutes(ride.inspectionInterval);
            payload["minutesSinceInspection"] = ride.lastInspection;
            payload["minutesUntilInspection"] = std::max(0, inspectionMinutes - static_cast<int32_t>(ride.lastInspection));

            constexpr double kTickSeconds = 30.0 / 960.0; // 960 ticks ≈ 30s
            constexpr double kBucketTicks = 8192.0;
            const double bucketMinutes = (kBucketTicks * kTickSeconds) / 60.0;
            json_t history = json_t::array();
            for (size_t i = 0; i < std::size(ride.downtimeHistory); ++i)
            {
                const auto ticksBroken = ride.downtimeHistory[i];
                if (ticksBroken == 0 && history.empty())
                {
                    continue;
                }
                json_t bucket = json_t::object();
                bucket["windowStartMinutesAgo"] = static_cast<double>(i) * bucketMinutes;
                bucket["percentIntervalBroken"] = std::min(100.0, (ticksBroken / kBucketTicks) * 100.0);
                bucket["ticksBroken"] = ticksBroken;
                history.push_back(bucket);
            }
            payload["downtimeBucketMinutes"] = bucketMinutes;
            payload["downtimeHistory"] = history;
            payload["downtimeBuckets"] = static_cast<int32_t>(history.size());
            auto rideLabel = BuildRideDisplayName(ride);
            std::string contextLabel = "Inspected breakdowns for " + rideLabel;
            auto hint = MakeRideHint("rides.breakdowns", ride, Telemetry::AIAgentRideWindowPage::Maintenance, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideThroughput(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            const auto& ride = *rideLookup->ride;
            json_t payload = json_t::object();
            payload["ride"] = BuildRidePayload(ride);
            payload["customersPerHour"] = RideCustomersPerHour(ride);
            payload["currentIntervalCustomers"] = ride.curNumCustomers;

            constexpr size_t kBucketsPerFiveMinutes = 10;
            uint32_t recentCustomers = 0;
            for (size_t i = 0; i < std::min(kBucketsPerFiveMinutes, std::size(ride.numCustomers)); ++i)
            {
                recentCustomers += ride.numCustomers[i];
            }
            payload["customersLast5Minutes"] = recentCustomers;
            payload["queueTimeMinutes"] = ride.getMaxQueueTime();
            payload["numRiders"] = ride.numRiders;
            payload["popularityPercent"] = ride.popularity;
            payload["satisfactionPercent"] = ride.satisfaction;
            payload["totalCustomers"] = ride.totalCustomers;
            payload["guestsFavourite"] = ride.guestsFavourite;

            json_t itemsSold = json_t::array();
            if (ride.numPrimaryItemsSold > 0)
            {
                itemsSold.push_back(json_t::object({ { "slot", "primary" }, { "item", "primary" },
                    { "sold", ride.numPrimaryItemsSold } }));
            }
            if (ride.numSecondaryItemsSold > 0)
            {
                itemsSold.push_back(json_t::object({ { "slot", "secondary" }, { "item", "secondary" },
                    { "sold", ride.numSecondaryItemsSold } }));
            }
            payload["itemsSold"] = itemsSold;

            json_t history = json_t::array();
            for (size_t i = 0; i < std::size(ride.numCustomers); ++i)
            {
                if (ride.numCustomers[i] == 0 && history.empty())
                {
                    continue;
                }
                history.push_back(json_t::object({ { "index", static_cast<int32_t>(i) },
                    { "customers", ride.numCustomers[i] } }));
            }
            payload["customerHistory"] = history;
            auto rideLabel = BuildRideDisplayName(ride);
            std::string contextLabel = "Reviewed throughput for " + rideLabel;
            auto hint = MakeRideHint("rides.throughput", ride, Telemetry::AIAgentRideWindowPage::Customer, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideFeedback(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            const auto limit = params.is_object() ? ExtractLimitParam(params) : 6;
            const int32_t guestLimit = params.is_object() ? GetIntParam(params, "guestLimit").value_or(5) : 5;

            struct FeedbackGroup
            {
                int32_t count = 0;
                std::string text;
                std::vector<json_t> samples;
                std::unordered_set<EntityId::UnderlyingType> seenGuests;
            };

            std::unordered_map<int32_t, FeedbackGroup> groups;
            size_t totalMatches = 0;
            for (auto guest : EntityList<Guest>())
            {
                if (guest == nullptr || guest->OutsideOfPark)
                {
                    continue;
                }
                for (const auto& thought : guest->Thoughts)
                {
                    if (thought.freshness == 0 || thought.rideId.IsNull()
                        || thought.rideId.ToUnderlying() != rideLookup->id.ToUnderlying())
                    {
                        continue;
                    }
                    // Skip thoughts with empty text (e.g., PeepThoughtType::None)
                    auto thoughtText = FormatThoughtText(thought);
                    if (thoughtText.empty())
                    {
                        continue;
                    }
                    const auto key = static_cast<int32_t>(thought.type);
                    auto& group = groups[key];
                    if (group.text.empty())
                    {
                        group.text = thoughtText;
                    }
                    group.count++;
                    // Only add guest to samples if not already seen for this group
                    auto guestId = guest->Id.ToUnderlying();
                    if (static_cast<int32_t>(group.samples.size()) < guestLimit
                        && group.seenGuests.find(guestId) == group.seenGuests.end())
                    {
                        group.seenGuests.insert(guestId);
                        group.samples.push_back(BuildGuestSamplePayload(*guest));
                    }
                    totalMatches++;
                }
            }

            std::vector<std::pair<int32_t, FeedbackGroup>> ordered(groups.begin(), groups.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.second.count > rhs.second.count;
            });

            json_t jsonGroups = json_t::array();
            size_t emitted = 0;
            for (const auto& entry : ordered)
            {
                if (limit != 0 && emitted >= limit)
                {
                    break;
                }
                // Skip entries with empty thought text (e.g., PeepThoughtType::None)
                if (entry.second.text.empty())
                {
                    continue;
                }
                json_t group = json_t::object();
                group["key"] = entry.first;
                group["text"] = entry.second.text;
                group["count"] = entry.second.count;
                group["guestSamples"] = entry.second.samples;
                jsonGroups.push_back(group);
                emitted++;
            }

            json_t payload = json_t::object();
            payload["ride"] = BuildRidePayload(*rideLookup->ride);
            payload["groups"] = jsonGroups;
            payload["totalMatches"] = totalMatches;
            auto rideLabel = BuildRideDisplayName(*rideLookup->ride);
            std::string contextLabel = "Reviewed feedback for " + rideLabel;
            auto hint = MakeRideHint("rides.feedback", *rideLookup->ride, Telemetry::AIAgentRideWindowPage::Customer, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRidePlace(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto typeParam = GetStringParam(params, "type");
            auto nameParam = GetStringParam(params, "name");
            auto entryIndexParam = GetIntParam(params, "entryIndex");
            if (!entryIndexParam)
            {
                entryIndexParam = GetIntParam(params, "entry-index");
            }
            if (!typeParam && !nameParam && !entryIndexParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "Provide --type, --name, or --entry-index");
            }

            auto xParam = GetIntParam(params, "x");
            auto yParam = GetIntParam(params, "y");
            if (!xParam || !yParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "x and y tile coordinates are required");
            }

            TileCoordsXY tile{ *xParam, *yParam };
            auto coords = tile.ToCoordsXY();
            if (!MapIsLocationValid(coords))
            {
                return RpcResult::Error(kErrorInvalidParams, "Tile is outside the current map bounds");
            }

            std::string errorMessage;
            std::optional<RideBlueprintInfo> blueprint;

            // Try to resolve by type identifier first
            if (typeParam && !typeParam->empty())
            {
                blueprint = ResolveRideBlueprint(*typeParam, errorMessage);

                // Fallback: iterate loaded objects and match by identifier (case-insensitive)
                if (!blueprint)
                {
                    auto* context = GetContext();
                    if (context != nullptr)
                    {
                        auto& manager = context->GetObjectManager();
                        const auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::ride));
                        auto typeLower = ToLower(TrimLegacyIdentifier(*typeParam));
                        for (ObjectEntryIndex idx = 0; idx < maxEntries; ++idx)
                        {
                            auto* rideObject = manager.GetLoadedObject<RideObject>(idx);
                            if (rideObject == nullptr)
                            {
                                continue;
                            }
                            auto legacyId = ToLower(TrimLegacyIdentifier(rideObject->GetLegacyIdentifier()));
                            auto descriptorId = ToLower(std::string(rideObject->GetIdentifier()));
                            if (legacyId == typeLower || descriptorId == typeLower)
                            {
                                blueprint = BuildBlueprintFromRideObject(*rideObject, idx);
                                break;
                            }
                        }
                    }
                }
            }

            // Try by entry index if type didn't match
            if (!blueprint && entryIndexParam)
            {
                auto* context = GetContext();
                if (context != nullptr)
                {
                    auto& manager = context->GetObjectManager();
                    auto idx = static_cast<ObjectEntryIndex>(*entryIndexParam);
                    auto* rideObject = manager.GetLoadedObject<RideObject>(idx);
                    if (rideObject != nullptr)
                    {
                        blueprint = BuildBlueprintFromRideObject(*rideObject, idx);
                        if (!blueprint)
                        {
                            errorMessage = "Ride object at entry index " + std::to_string(*entryIndexParam)
                                + " has no valid ride type";
                        }
                    }
                    else
                    {
                        errorMessage = "No ride object at entry index " + std::to_string(*entryIndexParam);
                    }
                }
            }

            // Try by display name last
            if (!blueprint && nameParam && !nameParam->empty())
            {
                auto* context = GetContext();
                if (context != nullptr)
                {
                    auto& manager = context->GetObjectManager();
                    const auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::ride));
                    std::string nameLower = ToLower(*nameParam);
                    for (ObjectEntryIndex idx = 0; idx < maxEntries; ++idx)
                    {
                        auto* rideObject = manager.GetLoadedObject<RideObject>(idx);
                        if (rideObject == nullptr)
                        {
                            continue;
                        }
                        const auto& rideEntry = rideObject->GetEntry();
                        std::string entryName = ResolveStringId(rideEntry.naming.Name);
                        if (ToLower(entryName) == nameLower)
                        {
                            blueprint = BuildBlueprintFromRideObject(*rideObject, idx);
                            break;
                        }
                    }
                    if (!blueprint)
                    {
                        errorMessage = "No ride found with name '" + *nameParam + "'";
                    }
                }
            }

            if (!blueprint)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            if (blueprint->descriptor->HasFlag(RtdFlag::isShopOrFacility))
            {
                return RpcResult::Error(kErrorInvalidParams, "Use shops.place for shops and stalls");
            }
            if (!blueprint->descriptor->HasFlag(RtdFlag::isFlatRide))
            {
                return RpcResult::Error(kErrorInvalidParams, "rides.place currently supports flat rides only");
            }
            if (blueprint->descriptor->StartTrackPiece == OpenRCT2::TrackElemType::None)
            {
                return RpcResult::Error(kErrorInvalidParams, "Ride does not define a buildable start piece");
            }

            auto placementHeight = ResolvePlacementHeight(params, tile, errorMessage);
            if (!placementHeight)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            Direction direction = 1;
            if (auto facingParam = GetStringParam(params, "facing"))
            {
                auto parsedDirection = DirectionFromString(*facingParam);
                if (!parsedDirection)
                {
                    return RpcResult::Error(kErrorInvalidParams, "Unknown facing (use north|south|east|west)");
                }
                direction = *parsedDirection;
            }

            auto& gameState = getGameState();

            // Handle dry-run mode: validate and estimate cost without actually placing
            auto dryRunParam = GetBoolParam(params, "dryRun");
            if (dryRunParam && *dryRunParam)
            {
                int32_t colour1 = RideGetRandomColourPresetIndex(blueprint->rideType);
                int32_t colour2 = RideGetUnusedPresetVehicleColour(blueprint->entryIndex);

                auto rideCreate = GameActions::RideCreateAction(
                    blueprint->rideType, blueprint->entryIndex, colour1, colour2, gameState.lastEntranceStyle);
                auto createQueryResult = GameActions::Query(&rideCreate, gameState);

                json_t payload = json_t::object();
                payload["dryRun"] = true;
                payload["feasible"] = (createQueryResult.Error == GameActions::Status::Ok);

                if (createQueryResult.Error == GameActions::Status::Ok)
                {
                    payload["status"] = "ok";
                    payload["estimatedCost"] = MoneyToDouble(createQueryResult.Cost);
                    payload["message"] = "Placement would succeed. Cost is an estimate; actual cost may vary slightly.";
                }
                else
                {
                    payload["status"] = "error";
                    payload["error"] = BuildGameActionErrorMessage(createQueryResult);
                }

                json_t tileNode = json_t::object();
                tileNode["x"] = tile.x;
                tileNode["y"] = tile.y;
                tileNode["z"] = WorldZToTileZ(*placementHeight);
                payload["tile"] = tileNode;
                payload["direction"] = std::string(DirectionToString(direction));
                payload["directionIndex"] = direction;

                json_t objectNode = json_t::object();
                objectNode["identifier"] = blueprint->identifier;
                objectNode["entryIndex"] = blueprint->entryIndex;
                objectNode["rideType"] = blueprint->descriptor->Name.empty() ? std::string("ride")
                                                                              : std::string(blueprint->descriptor->Name);
                // Get display name from ride entry
                std::string displayName;
                if (blueprint->rideEntry != nullptr)
                {
                    displayName = ResolveStringId(blueprint->rideEntry->naming.Name);
                    objectNode["displayName"] = displayName;
                }
                payload["object"] = objectNode;

                std::string rideLabel = displayName.empty() ? std::string("ride") : displayName;
                std::string contextLabel = "Dry-run: would place " + rideLabel + " at (" + std::to_string(tile.x) + "," + std::to_string(tile.y) + ")";
                auto hint = MakeTileHint("rides.place", std::move(contextLabel), tile, WindowClass::constructRide);
                return RpcResult::Ok(payload, std::move(hint));
            }

            int32_t colour1 = RideGetRandomColourPresetIndex(blueprint->rideType);
            int32_t colour2 = RideGetUnusedPresetVehicleColour(blueprint->entryIndex);

            auto rideCreate = GameActions::RideCreateAction(
                blueprint->rideType, blueprint->entryIndex, colour1, colour2, gameState.lastEntranceStyle);
            auto createResult = GameActions::ExecuteNested(&rideCreate, gameState);
            if (createResult.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(createResult));
            }

            RideId rideId = createResult.GetData<RideId>();
            SelectedLiftAndInverted liftFlags{};
            CoordsXYZD origin{ coords.x, coords.y, *placementHeight, direction };
            auto trackAction = GameActions::TrackPlaceAction(
                rideId, blueprint->descriptor->StartTrackPiece, blueprint->rideType, origin, 0, 0, 0, liftFlags, false);
            auto placeResult = GameActions::ExecuteNested(&trackAction, gameState);
            if (placeResult.Error != GameActions::Status::Ok)
            {
                auto demolish = GameActions::RideDemolishAction(rideId, GameActions::RideModifyType::demolish);
                GameActions::Execute(&demolish, gameState);
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(placeResult));
            }

            auto* ride = GetRide(rideId);
            if (ride == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Ride data unavailable after placement");
            }

            money64 totalCost = createResult.Cost + placeResult.Cost;

            json_t payload = json_t::object();
            payload["status"] = GameActionStatusToString(placeResult.Error);
            payload["cost"] = MoneyToDouble(totalCost);

            json_t costBreakdown = json_t::object();
            costBreakdown["create"] = MoneyToDouble(createResult.Cost);
            costBreakdown["build"] = MoneyToDouble(placeResult.Cost);
            payload["costBreakdown"] = costBreakdown;
            payload["ride"] = BuildRidePayload(*ride);

            json_t tileNode = json_t::object();
            tileNode["x"] = tile.x;
            tileNode["y"] = tile.y;
            tileNode["z"] = WorldZToTileZ(*placementHeight);
            tileNode["meaning"] = "north-west anchor tile";
            payload["tile"] = tileNode;
            payload["direction"] = std::string(DirectionToString(direction));
            payload["directionIndex"] = direction;

            json_t objectNode = json_t::object();
            objectNode["identifier"] = blueprint->identifier;
            objectNode["entryIndex"] = blueprint->entryIndex;
            if (blueprint->rideEntry != nullptr)
            {
                auto rideNameId = blueprint->rideEntry->naming.Name;
                if (rideNameId != kStringIdNone)
                {
                    objectNode["name"] = ResolveStringId(rideNameId);
                }
            }
            payload["object"] = objectNode;

            if (auto footprint = BuildRideFootprint(*ride))
            {
                payload["footprint"] = BuildFootprintPayload(*footprint, *ride);
            }

            std::string contextLabel = "Placed " + ride->getName() + " at (" + std::to_string(tile.x) + ","
                + std::to_string(tile.y) + ")";
            auto hint = MakeRideHint("rides.place", *ride, Telemetry::AIAgentRideWindowPage::Main, std::move(contextLabel));
            if (!hint.cameraTarget)
            {
                hint.cameraTarget = BuildTileCameraTarget(tile);
            }
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideEntrancePlace(const json_t& params, bool isExit)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            auto xParam = GetIntParam(params, "x");
            auto yParam = GetIntParam(params, "y");
            if (!xParam || !yParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "x and y tile coordinates are required");
            }

            TileCoordsXY tile{ *xParam, *yParam };
            auto coords = tile.ToCoordsXY();
            if (!MapIsLocationValid(coords))
            {
                return RpcResult::Error(kErrorInvalidParams, "Tile is outside the current map bounds");
            }

            StationIndex stationIndex = StationIndex::FromUnderlying(0);
            if (auto stationParam = GetIntParam(params, "station"))
            {
                if (*stationParam < 0 || *stationParam >= Limits::kMaxStationsPerRide)
                {
                    return RpcResult::Error(kErrorInvalidParams, "station index out of range");
                }
                stationIndex = StationIndex::FromUnderlying(static_cast<uint8_t>(*stationParam));
            }

            // Validate placement using the same logic as the game UI.
            // This checks track sequence flags to ensure entrances are only placed adjacent to
            // station platforms (for coasters) or valid entrance points (for flat rides).
            auto validDirection = ValidateEntranceExitPlacement(*rideLookup->ride, tile, stationIndex);
            if (!validDirection)
            {
                std::string typeStr = isExit ? "Exit" : "Entrance";
                return RpcResult::Error(
                    kErrorInvalidParams,
                    typeStr + " placement at (" + std::to_string(tile.x) + "," + std::to_string(tile.y)
                        + ") is invalid. " + typeStr
                        + "s must be placed on a tile adjacent to the station platform, facing toward the ride.");
            }

            // Use the validated direction, or check explicit direction matches
            bool directionExplicit = false;
            Direction direction = *validDirection;
            if (auto directionParam = GetStringParam(params, "direction"))
            {
                auto parsedDirection = DirectionFromString(*directionParam);
                if (!parsedDirection)
                {
                    return RpcResult::Error(kErrorInvalidParams, "Unknown direction (use north|south|east|west)");
                }
                if (*parsedDirection != *validDirection)
                {
                    std::string msg = std::string(isExit ? "Exit" : "Entrance")
                        + " must face toward the station. Required direction: "
                        + std::string(DirectionToString(*validDirection));
                    return RpcResult::Error(kErrorInvalidParams, msg);
                }
                direction = *parsedDirection;
                directionExplicit = true;
            }

            auto& station = rideLookup->ride->getStation(stationIndex);

            // The RideEntranceExitPlaceAction expects the direction pointing TOWARD the track,
            // but ValidateEntranceExitPlacement returns the direction pointing AWAY from the track
            // (matching what guest paths approach from). The game UI applies DirectionReverse
            // in ToolDownEntranceExit before passing to the action, so we must do the same.
            Direction actionDirection = DirectionReverse(direction);

            LOG_VERBOSE(
                "[rctctl] Placing %s for ride %u at (%d,%d) validatedDir=%u actionDir=%u stationBaseZ=%d",
                isExit ? "exit" : "entrance", rideLookup->id.ToUnderlying(), tile.x, tile.y, direction, actionDirection,
                station.GetBaseZ());

            auto action = GameActions::RideEntranceExitPlaceAction(
                coords, actionDirection, rideLookup->id, stationIndex, isExit);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Verify the entrance was placed correctly
            auto* entranceElement = MapGetFirstTileElementWithBaseHeightBetween<EntranceElement>(
                { tile, station.GetBaseZ() / kCoordsZStep, (station.GetBaseZ() / kCoordsZStep) + 1 });
            if (entranceElement != nullptr)
            {
                LOG_VERBOSE(
                    "[rctctl] %s element created: baseZ=%d direction=%u rideIndex=%u stationIndex=%u",
                    isExit ? "Exit" : "Entrance", entranceElement->GetBaseZ(), entranceElement->GetDirection(),
                    entranceElement->GetRideIndex().ToUnderlying(), entranceElement->GetStationIndex().ToUnderlying());
            }
            else
            {
                LOG_WARNING("[rctctl] Could not find entrance element after placement at (%d,%d)", tile.x, tile.y);
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["ride"] = BuildRidePayload(*rideLookup->ride);
            payload["entranceType"] = isExit ? "exit" : "entrance";
            payload["stationIndex"] = stationIndex.ToUnderlying();

            json_t tileNode = json_t::object();
            tileNode["x"] = tile.x;
            tileNode["y"] = tile.y;
            tileNode["z"] = WorldZToTileZ(rideLookup->ride->getStation(stationIndex).GetBaseZ());
            tileNode["directionIndex"] = direction;
            tileNode["direction"] = std::string(DirectionToString(direction));
            tileNode["directionInferred"] = !directionExplicit;
            payload["tile"] = tileNode;

            // Calculate the adjacent station tile from the entrance direction.
            // The entrance direction (from ValidateEntranceExitPlacement) points away from the station,
            // so we use actionDirection (the reversed direction) to find the station tile.
            auto stationDelta = CoordsDirectionDelta[actionDirection];
            TileCoordsXY adjacentStationTile{
                tile.x + (stationDelta.x / kCoordsXYStep),
                tile.y + (stationDelta.y / kCoordsXYStep)
            };
            payload["adjacentRideTile"] = json_t::object({ { "x", adjacentStationTile.x }, { "y", adjacentStationTile.y } });

            std::string contextLabel = std::string(isExit ? "Placed exit for " : "Placed entrance for ")
                + rideLookup->ride->getName();
            auto hint = MakeRideHint(
                isExit ? "rides.exitPlace" : "rides.entrancePlace", *rideLookup->ride,
                Telemetry::AIAgentRideWindowPage::Main, std::move(contextLabel));
            if (!hint.cameraTarget)
            {
                hint.cameraTarget = BuildTileCameraTarget(tile);
            }
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideCoastersCategories(const json_t& /* params */)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Game context unavailable");
            }

            auto* trackRepo = context->GetTrackDesignRepository();
            if (trackRepo == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Track design repository unavailable");
            }

            // Scan repository to ensure it's up to date
            trackRepo->Scan(LocalisationService_GetCurrentLanguage());

            // Count designs per category by iterating all ride types
            std::map<RideCategory, std::pair<int32_t, std::set<ride_type_t>>> categoryData;

            for (ride_type_t rideType = 0; rideType < RIDE_TYPE_COUNT; rideType++)
            {
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                if (descriptor.Category == RideCategory::none || descriptor.Category == RideCategory::shop)
                {
                    continue; // Skip shops/invalid
                }

                auto designCount = trackRepo->GetCountForObjectEntry(rideType, "");
                if (designCount > 0)
                {
                    auto& data = categoryData[descriptor.Category];
                    data.first += static_cast<int32_t>(designCount);
                    data.second.insert(rideType);
                }
            }

            json_t categories = json_t::array();
            for (const auto& [category, data] : categoryData)
            {
                json_t catNode = json_t::object();
                catNode["id"] = RideCategoryToString(category);
                catNode["name"] = RideCategoryDisplayName(category);
                catNode["designCount"] = data.first;
                catNode["typeCount"] = static_cast<int32_t>(data.second.size());
                categories.push_back(catNode);
            }

            json_t payload = json_t::object();
            payload["categories"] = categories;
            payload["totalDesigns"] = static_cast<int32_t>(trackRepo->GetCount());

            auto hint = MakeConstructRideHint(
                "rides.coasters.categories", "Browsed coaster categories", Telemetry::AIAgentConstructRideTab::RollerCoaster);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideCoastersTypes(const json_t& params)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Game context unavailable");
            }

            auto* trackRepo = context->GetTrackDesignRepository();
            if (trackRepo == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Track design repository unavailable");
            }

            trackRepo->Scan(LocalisationService_GetCurrentLanguage());

            // Optional category filter
            std::optional<RideCategory> categoryFilter;
            if (params.is_object())
            {
                if (auto catParam = GetStringParam(params, "category"))
                {
                    categoryFilter = RideCategoryFromString(*catParam);
                    if (!categoryFilter)
                    {
                        return RpcResult::Error(
                            kErrorInvalidParams,
                            "Unknown category '" + *catParam
                                + "'. Valid: transport, gentle, rollerCoaster, thrill, water");
                    }
                }
            }

            auto& gameState = getGameState();
            json_t types = json_t::array();

            for (ride_type_t rideType = 0; rideType < RIDE_TYPE_COUNT; rideType++)
            {
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                if (descriptor.Category == RideCategory::none || descriptor.Category == RideCategory::shop)
                {
                    continue;
                }

                if (categoryFilter && descriptor.Category != *categoryFilter)
                {
                    continue;
                }

                auto designCount = trackRepo->GetCountForObjectEntry(rideType, "");
                if (designCount == 0)
                {
                    continue;
                }

                bool invented = RideTypeIsInvented(rideType) || gameState.cheats.ignoreResearchStatus;

                json_t typeNode = json_t::object();
                typeNode["rideType"] = rideType;
                typeNode["identifier"] = std::to_string(rideType);
                typeNode["name"] = LanguageGetString(descriptor.Naming.Name);
                typeNode["category"] = RideCategoryToString(descriptor.Category);
                typeNode["categoryName"] = RideCategoryDisplayName(descriptor.Category);
                typeNode["designCount"] = static_cast<int32_t>(designCount);
                typeNode["invented"] = invented;
                types.push_back(typeNode);
            }

            json_t payload = json_t::object();
            payload["types"] = types;
            if (categoryFilter)
            {
                payload["filteredCategory"] = RideCategoryToString(*categoryFilter);
            }

            // Map category to construction tab
            Telemetry::AIAgentConstructRideTab tab = Telemetry::AIAgentConstructRideTab::RollerCoaster;
            uint8_t tabIndex = 2; // Default to roller coaster tab
            if (categoryFilter)
            {
                switch (*categoryFilter)
                {
                    case RideCategory::transport:
                        tab = Telemetry::AIAgentConstructRideTab::Transport;
                        tabIndex = 0;
                        break;
                    case RideCategory::gentle:
                        tab = Telemetry::AIAgentConstructRideTab::Gentle;
                        tabIndex = 1;
                        break;
                    case RideCategory::rollerCoaster:
                        tab = Telemetry::AIAgentConstructRideTab::RollerCoaster;
                        tabIndex = 2;
                        break;
                    case RideCategory::thrill:
                        tab = Telemetry::AIAgentConstructRideTab::Thrill;
                        tabIndex = 3;
                        break;
                    case RideCategory::water:
                        tab = Telemetry::AIAgentConstructRideTab::Water;
                        tabIndex = 4;
                        break;
                    default:
                        tab = Telemetry::AIAgentConstructRideTab::RollerCoaster;
                        tabIndex = 2;
                        break;
                }
            }

            // Open ride construction window to the appropriate tab for onlooker experience
            if (categoryFilter)
            {
                auto* windowMgr = Ui::GetWindowManager();
                if (windowMgr != nullptr)
                {
                    Intent intent(INTENT_ACTION_NEW_RIDE_OF_CATEGORY);
                    intent.PutExtra(INTENT_EXTRA_PAGE, static_cast<uint32_t>(tabIndex));
                    windowMgr->OpenIntent(&intent);
                }
            }

            auto hint = MakeConstructRideHint(
                "rides.coasters.types", "Browsed coaster ride types", tab);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideCoastersList(const json_t& params)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Game context unavailable");
            }

            auto* trackRepo = context->GetTrackDesignRepository();
            if (trackRepo == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Track design repository unavailable");
            }

            trackRepo->Scan(LocalisationService_GetCurrentLanguage());

            // Optional ride type filter
            std::optional<ride_type_t> typeFilter;
            std::string typeFilterName;
            if (params.is_object())
            {
                if (auto typeParam = GetStringParam(params, "type"))
                {
                    // Try to find ride type by numeric identifier
                    auto parsedId = TryParseInt(*typeParam);
                    if (parsedId && *parsedId >= 0 && static_cast<ride_type_t>(*parsedId) < RIDE_TYPE_COUNT)
                    {
                        typeFilter = static_cast<ride_type_t>(*parsedId);
                        typeFilterName = std::to_string(*parsedId);
                    }
                    if (!typeFilter)
                    {
                        return RpcResult::Error(
                            kErrorInvalidParams,
                            "Unknown ride type '" + *typeParam
                                + "'. Use 'rides designs types' to see valid identifiers (numeric).");
                    }
                }
            }

            auto& gameState = getGameState();
            json_t designs = json_t::array();

            auto processDesignsForType = [&](ride_type_t rideType) {
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                auto items = trackRepo->GetItemsForObjectEntry(rideType, "");

                bool invented = RideTypeIsInvented(rideType) || gameState.cheats.ignoreResearchStatus;

                for (const auto& item : items)
                {
                    json_t designNode = json_t::object();
                    designNode["name"] = item.name;
                    designNode["path"] = item.path;
                    designNode["rideType"] = rideType;
                    designNode["rideTypeIdentifier"] = std::to_string(rideType);
                    designNode["rideTypeName"] = LanguageGetString(descriptor.Naming.Name);
                    designNode["category"] = RideCategoryToString(descriptor.Category);
                    designNode["invented"] = invented;

                    // Try to load the design to get statistics
                    auto td = TrackDesignImport(item.path.c_str());
                    if (td != nullptr)
                    {
                        json_t stats = json_t::object();
                        stats["excitement"] = static_cast<double>(td->statistics.ratings.excitement) / 100.0;
                        stats["intensity"] = static_cast<double>(td->statistics.ratings.intensity) / 100.0;
                        stats["nausea"] = static_cast<double>(td->statistics.ratings.nausea) / 100.0;

                        json_t space = json_t::object();
                        space["x"] = td->statistics.spaceRequired.x;
                        space["y"] = td->statistics.spaceRequired.y;
                        stats["spaceRequired"] = space;

                        designNode["statistics"] = stats;
                    }

                    designs.push_back(designNode);
                }
            };

            if (typeFilter)
            {
                processDesignsForType(*typeFilter);
            }
            else
            {
                for (ride_type_t rt = 0; rt < RIDE_TYPE_COUNT; rt++)
                {
                    const auto& desc = GetRideTypeDescriptor(rt);
                    if (desc.Category == RideCategory::none || desc.Category == RideCategory::shop)
                    {
                        continue;
                    }
                    processDesignsForType(rt);
                }
            }

            json_t payload = json_t::object();
            payload["designs"] = designs;
            payload["totalCount"] = static_cast<int32_t>(designs.size());
            if (typeFilter)
            {
                payload["filteredType"] = typeFilterName;
            }

            // Open ride construction window to the appropriate tab for onlooker experience
            Telemetry::AIAgentConstructRideTab tab = Telemetry::AIAgentConstructRideTab::RollerCoaster;
            if (typeFilter)
            {
                const auto& descriptor = GetRideTypeDescriptor(*typeFilter);
                uint8_t tabIndex = 2; // Default to roller coaster

                switch (descriptor.Category)
                {
                    case RideCategory::transport:
                        tab = Telemetry::AIAgentConstructRideTab::Transport;
                        tabIndex = 0;
                        break;
                    case RideCategory::gentle:
                        tab = Telemetry::AIAgentConstructRideTab::Gentle;
                        tabIndex = 1;
                        break;
                    case RideCategory::rollerCoaster:
                        tab = Telemetry::AIAgentConstructRideTab::RollerCoaster;
                        tabIndex = 2;
                        break;
                    case RideCategory::thrill:
                        tab = Telemetry::AIAgentConstructRideTab::Thrill;
                        tabIndex = 3;
                        break;
                    case RideCategory::water:
                        tab = Telemetry::AIAgentConstructRideTab::Water;
                        tabIndex = 4;
                        break;
                    default:
                        break;
                }

                auto* windowMgr = Ui::GetWindowManager();
                if (windowMgr != nullptr)
                {
                    Intent intent(INTENT_ACTION_NEW_RIDE_OF_CATEGORY);
                    intent.PutExtra(INTENT_EXTRA_PAGE, static_cast<uint32_t>(tabIndex));
                    windowMgr->OpenIntent(&intent);
                }
            }

            auto hint = MakeConstructRideHint(
                "rides.coasters.list", "Listed available coasters", tab);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideCoastersPreview(const json_t& params)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Game context unavailable");
            }

            auto* trackRepo = context->GetTrackDesignRepository();
            if (trackRepo == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Track design repository unavailable");
            }

            // Required parameters
            auto nameParam = GetStringParam(params, "name");
            if (!nameParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "Missing required parameter: name");
            }

            auto xParam = params.value("x", -1);
            auto yParam = params.value("y", -1);
            if (xParam < 0 || yParam < 0)
            {
                return RpcResult::Error(kErrorInvalidParams, "Missing or invalid coordinates: x, y required");
            }

            // Optional parameters
            auto directionParam = params.value("direction", 0);

            // Find the track design
            auto designResult = FindTrackDesignByName(trackRepo, *nameParam);
            if (!designResult)
            {
                return RpcResult::Error(
                    kErrorNotFound, "Coaster '" + *nameParam + "' not found. Use 'rides coasters list' to see available coasters.");
            }

            auto& [designPath, trackDesign] = *designResult;

            // Ensure the vehicle object is loaded (same pattern as ResolveRideBlueprint)
            auto& objManager = context->GetObjectManager();
            auto vehicleEntryIndex = objManager.GetLoadedObjectEntryIndex(trackDesign.trackAndVehicle.vehicleObject);
            if (vehicleEntryIndex == kObjectEntryIndexNull)
            {
                // Try to load the object
                if (objManager.LoadObject(trackDesign.trackAndVehicle.vehicleObject) == nullptr)
                {
                    return RpcResult::Error(
                        kErrorNotFound,
                        "Vehicle object for coaster '" + *nameParam + "' could not be loaded. "
                        "The required ride object may not be available in this scenario.");
                }
            }

            // Check if ride type is invented
            auto& gameState = getGameState();
            auto rideType = trackDesign.trackAndVehicle.rtdIndex;
            if (!RideTypeIsInvented(rideType) && !gameState.cheats.ignoreResearchStatus)
            {
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                return RpcResult::Error(
                    kErrorInvalidParams,
                    "Ride type '" + std::string(LanguageGetString(descriptor.Naming.Name))
                        + "' is not yet invented. Check 'research status'.");
            }

            // Resolve placement height - auto-detect if not specified
            int32_t zParam = 0;
            bool zExplicit = params.contains("z") && params["z"].is_number();
            if (zExplicit)
            {
                zParam = params["z"].get<int>();
            }
            else
            {
                // Use TrackDesignGetZPlacement to properly calculate Z for the entire track design
                // This scans all tiles the coaster will occupy and finds the minimum Z that clears terrain
                CoordsXYZD queryCoords;
                queryCoords.x = xParam * kCoordsXYStep;
                queryCoords.y = yParam * kCoordsXYStep;
                queryCoords.z = kMinimumLandZ; // Start at minimum, function will calculate offset
                queryCoords.direction = directionParam & 3;

                int32_t zOffset = TrackDesignGetZPlacement(trackDesign, RideGetTemporaryForPreview(), queryCoords);
                int32_t worldZ = kMinimumLandZ + zOffset;

                // Convert to tile Z and ensure valid track alignment (16-byte aligned)
                zParam = worldZ / kCoordsZStep;
                if (zParam & 1)
                {
                    zParam++; // Round up to even for valid track placement
                }
                // Ensure minimum height for track placement
                if (zParam < 2)
                {
                    zParam = 2;
                }
            }

            // Convert tile coordinates to world coordinates
            CoordsXYZD location;
            location.x = xParam * kCoordsXYStep;
            location.y = yParam * kCoordsXYStep;
            location.z = zParam * kCoordsZStep;
            location.direction = directionParam & 3;

            // Execute the action in query mode
            auto action = GameActions::TrackDesignAction(location, trackDesign, false);
            action.SetFlags(GAME_COMMAND_FLAG_NO_SPEND); // Query only
            auto result = GameActions::Query(&action, gameState);

            json_t payload = json_t::object();

            // Design info
            json_t designInfo = json_t::object();
            designInfo["name"] = *nameParam;
            designInfo["path"] = designPath;
            const auto& descriptor = GetRideTypeDescriptor(rideType);
            designInfo["rideType"] = std::to_string(rideType);
            designInfo["rideTypeName"] = LanguageGetString(descriptor.Naming.Name);
            payload["design"] = designInfo;

            // Footprint info - estimate based on track design space requirements
            json_t footprint = json_t::object();
            int32_t spaceX = trackDesign.statistics.spaceRequired.x;
            int32_t spaceY = trackDesign.statistics.spaceRequired.y;
            footprint["spaceRequired"] = json_t::object({ { "x", spaceX }, { "y", spaceY } });
            footprint["anchorMeaning"] = "The x,y coordinate is typically near the northwest corner. "
                "The coaster extends approximately spaceRequired tiles from the anchor in directions "
                "determined by the --direction parameter.";

            // Compute estimated bounds based on direction
            // Direction 0=N, 1=E, 2=S, 3=W - the coaster extends in that rotated orientation
            int32_t boundsMinX = xParam;
            int32_t boundsMinY = yParam;
            int32_t boundsMaxX = xParam + spaceX - 1;
            int32_t boundsMaxY = yParam + spaceY - 1;
            // Rotate bounds estimate based on direction
            if ((directionParam & 3) == 1 || (directionParam & 3) == 3)
            {
                // Rotated 90 or 270 degrees - swap x/y space
                boundsMaxX = xParam + spaceY - 1;
                boundsMaxY = yParam + spaceX - 1;
            }
            json_t bounds = json_t::object();
            bounds["xMin"] = boundsMinX;
            bounds["xMax"] = boundsMaxX;
            bounds["yMin"] = boundsMinY;
            bounds["yMax"] = boundsMaxY;
            bounds["note"] = "Estimated bounds - actual footprint may vary slightly based on track layout";
            footprint["estimatedBounds"] = bounds;
            payload["footprint"] = footprint;

            // Placement info
            json_t placement = json_t::object();
            placement["x"] = xParam;
            placement["y"] = yParam;
            placement["z"] = zParam;
            placement["zAutoDetected"] = !zExplicit;
            placement["direction"] = directionParam;
            placement["cost"] = MoneyToDouble(result.Cost);
            placement["canPlace"] = result.Error == GameActions::Status::Ok;

            if (result.Error != GameActions::Status::Ok)
            {
                placement["errorMessage"] = result.GetErrorMessage();
                std::string errMsg = result.GetErrorMessage();

                // Provide targeted height suggestions based on error type
                if (errMsg.find("Too high") != std::string::npos || errMsg.find("too high") != std::string::npos)
                {
                    placement["heightHint"] = "The placement is too high. Try a lower --z value (current: "
                        + std::to_string(zParam) + "). Suggested: z=" + std::to_string(zParam - 1) + " or lower.";
                }
                else if (errMsg.find("Too low") != std::string::npos || errMsg.find("too low") != std::string::npos)
                {
                    placement["heightHint"] = "The placement is too low. Try a higher --z value (current: "
                        + std::to_string(zParam) + "). Suggested: z=" + std::to_string(zParam + 1) + " or higher.";
                }
                else if (errMsg.find("Raise") != std::string::npos || errMsg.find("lower land") != std::string::npos)
                {
                    placement["heightHint"] = "The terrain needs modification. Use 'construction land raise' or "
                        "'construction land lower' to adjust terrain at the placement area, or try a different location.";
                }
                else if (errMsg.find("support") != std::string::npos)
                {
                    placement["heightHint"] = "Support structure issue at z=" + std::to_string(zParam)
                        + ". This coaster may require specific terrain conditions. Try adjusting --z or terrain.";
                }
                else if (errMsg.find("edge") != std::string::npos || errMsg.find("Edge") != std::string::npos)
                {
                    placement["heightHint"] = "The coaster extends beyond map bounds. Move the placement "
                        "further from the map edge. The design requires approximately " + std::to_string(spaceX)
                        + "x" + std::to_string(spaceY) + " tiles.";
                }
                else if (errMsg.find("owned") != std::string::npos || errMsg.find("Owned") != std::string::npos)
                {
                    placement["heightHint"] = "Land ownership issue. Ensure all tiles within the estimated "
                        "footprint (" + std::to_string(boundsMinX) + "," + std::to_string(boundsMinY) + ") to ("
                        + std::to_string(boundsMaxX) + "," + std::to_string(boundsMaxY) + ") are owned by the park.";
                }
            }

            payload["placement"] = placement;

            auto hint = MakeGenericWindowHint(
                "rides.coasters.preview", "Previewed coaster placement", WindowClass::trackDesignPlace,
                CoordsXYZ{ location.x, location.y, location.z });
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideCoastersPlace(const json_t& params)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Game context unavailable");
            }

            auto* trackRepo = context->GetTrackDesignRepository();
            if (trackRepo == nullptr)
            {
                return RpcResult::Error(kErrorServerError, "Track design repository unavailable");
            }

            // Required parameters
            auto nameParam = GetStringParam(params, "name");
            if (!nameParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "Missing required parameter: name");
            }

            auto xParam = params.value("x", -1);
            auto yParam = params.value("y", -1);
            if (xParam < 0 || yParam < 0)
            {
                return RpcResult::Error(kErrorInvalidParams, "Missing or invalid coordinates: x, y required");
            }

            // Optional parameters
            auto directionParam = params.value("direction", 0);
            auto placeScenery = params.value("scenery", false);

            // Find the track design
            auto designResult = FindTrackDesignByName(trackRepo, *nameParam);
            if (!designResult)
            {
                return RpcResult::Error(
                    kErrorNotFound, "Coaster '" + *nameParam + "' not found. Use 'rides coasters list' to see available coasters.");
            }

            auto& [designPath, trackDesign] = *designResult;

            // Ensure the vehicle object is loaded (same pattern as ResolveRideBlueprint)
            auto& objManager = context->GetObjectManager();
            auto vehicleEntryIndex = objManager.GetLoadedObjectEntryIndex(trackDesign.trackAndVehicle.vehicleObject);
            if (vehicleEntryIndex == kObjectEntryIndexNull)
            {
                // Try to load the object
                if (objManager.LoadObject(trackDesign.trackAndVehicle.vehicleObject) == nullptr)
                {
                    return RpcResult::Error(
                        kErrorNotFound,
                        "Vehicle object for coaster '" + *nameParam + "' could not be loaded. "
                        "The required ride object may not be available in this scenario.");
                }
            }

            // Check if ride type is invented
            auto& gameState = getGameState();
            auto rideType = trackDesign.trackAndVehicle.rtdIndex;
            if (!RideTypeIsInvented(rideType) && !gameState.cheats.ignoreResearchStatus)
            {
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                return RpcResult::Error(
                    kErrorInvalidParams,
                    "Ride type '" + std::string(LanguageGetString(descriptor.Naming.Name))
                        + "' is not yet invented. Check 'research status'.");
            }

            // Resolve placement height - auto-detect if not specified
            int32_t zParam = 0;
            bool zExplicit = params.contains("z") && params["z"].is_number();
            if (zExplicit)
            {
                zParam = params["z"].get<int>();
            }
            else
            {
                // Use TrackDesignGetZPlacement to properly calculate Z for the entire track design
                // This scans all tiles the coaster will occupy and finds the minimum Z that clears terrain
                CoordsXYZD queryCoords;
                queryCoords.x = xParam * kCoordsXYStep;
                queryCoords.y = yParam * kCoordsXYStep;
                queryCoords.z = kMinimumLandZ; // Start at minimum, function will calculate offset
                queryCoords.direction = directionParam & 3;

                int32_t zOffset = TrackDesignGetZPlacement(trackDesign, RideGetTemporaryForPreview(), queryCoords);
                int32_t worldZ = kMinimumLandZ + zOffset;

                // Convert to tile Z and ensure valid track alignment (16-byte aligned)
                zParam = worldZ / kCoordsZStep;
                if (zParam & 1)
                {
                    zParam++; // Round up to even for valid track placement
                }
                // Ensure minimum height for track placement
                if (zParam < 2)
                {
                    zParam = 2;
                }
            }

            // Convert tile coordinates to world coordinates
            CoordsXYZD location;
            location.x = xParam * kCoordsXYStep;
            location.y = yParam * kCoordsXYStep;
            location.z = zParam * kCoordsZStep;
            location.direction = directionParam & 3;

            // Execute the action
            auto action = GameActions::TrackDesignAction(location, trackDesign, placeScenery);
            auto result = GameActions::Execute(&action, gameState);

            if (result.Error != GameActions::Status::Ok)
            {
                std::string errorMsg = result.GetErrorMessage();
                // Add targeted hint based on error type
                std::string hint;
                if (errorMsg.find("Too high") != std::string::npos || errorMsg.find("too high") != std::string::npos)
                {
                    hint = " [Hint: Try a lower --z value, e.g., z=" + std::to_string(zParam - 1) + "]";
                }
                else if (errorMsg.find("Too low") != std::string::npos || errorMsg.find("too low") != std::string::npos)
                {
                    hint = " [Hint: Try a higher --z value, e.g., z=" + std::to_string(zParam + 1) + "]";
                }
                else if (errorMsg.find("Raise") != std::string::npos || errorMsg.find("lower land") != std::string::npos)
                {
                    hint = " [Hint: Use 'construction land raise/lower' to modify terrain, or try a different location]";
                }
                else if (errorMsg.find("support") != std::string::npos)
                {
                    hint = " [Hint: Support issue - try adjusting --z or terrain. Use 'rides coasters preview' first]";
                }
                else if (errorMsg.find("edge") != std::string::npos || errorMsg.find("Edge") != std::string::npos)
                {
                    hint = " [Hint: Coaster extends beyond map. Move placement further from map edge]";
                }
                else if (errorMsg.find("owned") != std::string::npos || errorMsg.find("Owned") != std::string::npos)
                {
                    hint = " [Hint: Land not owned. Check 'map area ownership' at placement location]";
                }
                return RpcResult::Error(kErrorActionFailed, "Failed to place coaster: " + errorMsg + hint);
            }

            // Get the created ride
            auto rideId = result.GetData<RideId>();
            auto* ride = GetRide(rideId);
            if (ride == nullptr)
            {
                // Fallback: search for the ride by the track design name
                // This handles edge cases where the ride ID wasn't properly propagated
                const auto& expectedName = trackDesign.gameStateData.name;
                for (const auto& r : RideManager(gameState))
                {
                    if (r.getName() == expectedName || r.getName().find(expectedName) == 0)
                    {
                        ride = const_cast<Ride*>(&r);
                        break;
                    }
                }
            }

            json_t payload = json_t::object();
            payload["status"] = "ok";
            payload["cost"] = MoneyToDouble(result.Cost);

            // Ride info - even if ride lookup failed, the coaster was placed successfully
            json_t rideInfo = json_t::object();
            if (ride != nullptr)
            {
                rideInfo["id"] = static_cast<int32_t>(ride->id.ToUnderlying());
                rideInfo["name"] = ride->getName();
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                rideInfo["type"] = LanguageGetString(descriptor.Naming.Name);
            }
            else
            {
                // Placement succeeded but we couldn't look up the ride data
                // Return partial info so the agent knows it worked
                // Don't return invalid ride IDs (65535 is the null sentinel value)
                if (rideId.IsNull())
                {
                    rideInfo["note"] = "Ride was placed but ID not available. Use 'rides list' to find it.";
                }
                else
                {
                    rideInfo["id"] = static_cast<int32_t>(rideId.ToUnderlying());
                }
                rideInfo["name"] = trackDesign.gameStateData.name;
                const auto& descriptor = GetRideTypeDescriptor(rideType);
                rideInfo["type"] = LanguageGetString(descriptor.Naming.Name);
                payload["note"] = "Ride lookup returned stale data. Use 'rides list' to confirm placement.";
            }
            payload["ride"] = rideInfo;

            // Position info
            json_t position = json_t::object();
            position["x"] = xParam;
            position["y"] = yParam;
            position["z"] = zParam;
            position["direction"] = directionParam;
            payload["position"] = position;

            if (ride != nullptr)
            {
                auto hint = MakeRideHint(
                    "rides.coasters.place", *ride, Telemetry::AIAgentRideWindowPage::Main,
                    "Placed coaster: " + *nameParam);
                return RpcResult::Ok(payload, std::move(hint));
            }
            else
            {
                // No ride pointer available, return without window hint
                return RpcResult::Ok(payload);
            }
        }

        // ========== Theme Handlers ==========

        RpcResult HandleRideThemeColorsList(const json_t& /*params*/)
        {
            json_t colors = json_t::array();
            for (colour_t i = 0; i < COLOUR_COUNT; ++i)
            {
                json_t color = {
                    { "name", OpenRCT2::Colour::ToString(i) }
                };

                // Add notes for classic vs extended colors
                if (i < kColourNumOriginal)
                {
                    color["category"] = "classic";
                }
                else if (i < kColourNumNormal)
                {
                    color["category"] = "extended";
                }
                else
                {
                    color["category"] = "special";
                }

                colors.push_back(color);
            }

            return RpcResult::Ok(json_t{ { "colors", colors } });
        }

        RpcResult HandleRideThemeEntranceList(const json_t& /*params*/)
        {
            auto* context = GetContext();
            auto& manager = context->GetObjectManager();
            auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::station));

            json_t styles = json_t::array();
            for (ObjectEntryIndex idx = 0; idx < maxEntries; ++idx)
            {
                auto* stationObj = manager.GetLoadedObject<StationObject>(idx);
                if (stationObj == nullptr)
                    continue;

                json_t style = {
                    { "identifier", stationObj->GetIdentifier() },
                    { "name", LanguageGetString(stationObj->NameStringId) }
                };
                styles.push_back(style);
            }

            return RpcResult::Ok(json_t{ { "styles", styles } });
        }

        RpcResult HandleRideThemeGet(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            const auto* ride = rideLookup->ride;

            // Build track colors payload (4 schemes)
            json_t trackColours = json_t::array();
            for (int i = 0; i < kNumRideColourSchemes; ++i)
            {
                trackColours.push_back(BuildTrackColourPayload(ride->trackColours[i], i));
            }

            // Build vehicle colors payload
            // Use numTrains as a reasonable count for per-train mode; for same/per-car we show fewer
            json_t vehicleColours = json_t::array();
            int numVehicleColors = std::min(static_cast<int>(ride->numTrains), static_cast<int>(Limits::kMaxVehicleColours));
            if (numVehicleColors < 1)
                numVehicleColors = 1; // Always show at least one
            for (int i = 0; i < numVehicleColors; ++i)
            {
                vehicleColours.push_back(BuildVehicleColourPayload(ride->vehicleColours[i], i));
            }

            json_t payload = {
                { "ride", BuildRidePayload(*ride) },
                { "trackColours", trackColours },
                { "vehicleColourSettings", {
                    { "mode", VehicleColourSettingsToString(ride->vehicleColourSettings) },
                    { "value", static_cast<int>(ride->vehicleColourSettings) }
                }},
                { "vehicleColours", vehicleColours },
                { "entranceStyle", BuildEntranceStylePayload(ride->entranceStyle) }
            };

            auto hint = MakeRideHint(
                "rides.theme.get", *ride, Telemetry::AIAgentRideWindowPage::Colour,
                "Theme info: " + ride->getName());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideThemeTrackSet(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            // Get scheme index (default 0)
            int schemeIndex = GetIntParam(params, "scheme").value_or(0);
            if (schemeIndex < 0 || schemeIndex >= kNumRideColourSchemes)
            {
                return RpcResult::Error(kErrorInvalidParams, "scheme must be 0-" + std::to_string(kNumRideColourSchemes - 1));
            }

            // Track what changes we're making
            json_t applied = json_t::object();
            json_t previous = json_t::object();
            bool anyChange = false;

            // Store previous values
            const auto& prevTrackColour = rideLookup->ride->trackColours[schemeIndex];
            previous["main"] = BuildColorPayload(prevTrackColour.main);
            previous["additional"] = BuildColorPayload(prevTrackColour.additional);
            previous["supports"] = BuildColorPayload(prevTrackColour.supports);

            // Helper to execute appearance action
            auto execAppearance = [&](GameActions::RideSetAppearanceType type, colour_t value) -> bool {
                auto action = GameActions::RideSetAppearanceAction(
                    rideLookup->id, type, static_cast<uint16_t>(value), static_cast<uint32_t>(schemeIndex));
                auto result = GameActions::Execute(&action, getGameState());
                return result.Error == GameActions::Status::Ok;
            };

            // Process main color
            if (params.contains("main"))
            {
                auto colorVal = ResolveColorValue(params["main"], errorMessage);
                if (!colorVal)
                    return RpcResult::Error(kErrorInvalidParams, errorMessage);
                if (!execAppearance(GameActions::RideSetAppearanceType::TrackColourMain, *colorVal))
                    return RpcResult::Error(kErrorActionFailed, "Failed to set main track color");
                applied["main"] = BuildColorPayload(*colorVal);
                anyChange = true;
            }

            // Process additional color
            if (params.contains("additional"))
            {
                auto colorVal = ResolveColorValue(params["additional"], errorMessage);
                if (!colorVal)
                    return RpcResult::Error(kErrorInvalidParams, errorMessage);
                if (!execAppearance(GameActions::RideSetAppearanceType::TrackColourAdditional, *colorVal))
                    return RpcResult::Error(kErrorActionFailed, "Failed to set additional track color");
                applied["additional"] = BuildColorPayload(*colorVal);
                anyChange = true;
            }

            // Process supports color
            if (params.contains("supports"))
            {
                auto colorVal = ResolveColorValue(params["supports"], errorMessage);
                if (!colorVal)
                    return RpcResult::Error(kErrorInvalidParams, errorMessage);
                if (!execAppearance(GameActions::RideSetAppearanceType::TrackColourSupports, *colorVal))
                    return RpcResult::Error(kErrorActionFailed, "Failed to set supports color");
                applied["supports"] = BuildColorPayload(*colorVal);
                anyChange = true;
            }

            if (!anyChange)
            {
                return RpcResult::Error(kErrorInvalidParams, "No color changes specified. Use --main, --additional, or --supports.");
            }

            auto* updatedRide = GetRide(rideLookup->id);
            json_t payload = {
                { "ride", BuildRidePayload(*updatedRide) },
                { "scheme", schemeIndex },
                { "applied", applied },
                { "previous", previous }
            };

            auto hint = MakeRideHint(
                "rides.theme.track.set", *updatedRide, Telemetry::AIAgentRideWindowPage::Colour,
                "Set track colors: " + updatedRide->getName());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideThemeVehicleSet(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            // Get train/vehicle index (default 0)
            int trainIndex = GetIntParam(params, "train").value_or(0);
            if (trainIndex < 0 || trainIndex >= static_cast<int>(Limits::kMaxVehicleColours))
            {
                return RpcResult::Error(kErrorInvalidParams, "train index out of range");
            }

            // Track what changes we're making
            json_t applied = json_t::object();
            json_t previous = json_t::object();
            bool anyChange = false;

            // Store previous values
            const auto& prevVehicleColour = rideLookup->ride->vehicleColours[trainIndex];
            previous["body"] = BuildColorPayload(prevVehicleColour.Body);
            previous["trim"] = BuildColorPayload(prevVehicleColour.Trim);
            previous["tertiary"] = BuildColorPayload(prevVehicleColour.Tertiary);

            // Helper to execute appearance action
            auto execAppearance = [&](GameActions::RideSetAppearanceType type, colour_t value) -> bool {
                auto action = GameActions::RideSetAppearanceAction(
                    rideLookup->id, type, static_cast<uint16_t>(value), static_cast<uint32_t>(trainIndex));
                auto result = GameActions::Execute(&action, getGameState());
                return result.Error == GameActions::Status::Ok;
            };

            // Process body color
            if (params.contains("body"))
            {
                auto colorVal = ResolveColorValue(params["body"], errorMessage);
                if (!colorVal)
                    return RpcResult::Error(kErrorInvalidParams, errorMessage);
                if (!execAppearance(GameActions::RideSetAppearanceType::VehicleColourBody, *colorVal))
                    return RpcResult::Error(kErrorActionFailed, "Failed to set vehicle body color");
                applied["body"] = BuildColorPayload(*colorVal);
                anyChange = true;
            }

            // Process trim color
            if (params.contains("trim"))
            {
                auto colorVal = ResolveColorValue(params["trim"], errorMessage);
                if (!colorVal)
                    return RpcResult::Error(kErrorInvalidParams, errorMessage);
                if (!execAppearance(GameActions::RideSetAppearanceType::VehicleColourTrim, *colorVal))
                    return RpcResult::Error(kErrorActionFailed, "Failed to set vehicle trim color");
                applied["trim"] = BuildColorPayload(*colorVal);
                anyChange = true;
            }

            // Process tertiary color
            if (params.contains("tertiary"))
            {
                auto colorVal = ResolveColorValue(params["tertiary"], errorMessage);
                if (!colorVal)
                    return RpcResult::Error(kErrorInvalidParams, errorMessage);
                if (!execAppearance(GameActions::RideSetAppearanceType::VehicleColourTertiary, *colorVal))
                    return RpcResult::Error(kErrorActionFailed, "Failed to set vehicle tertiary color");
                applied["tertiary"] = BuildColorPayload(*colorVal);
                anyChange = true;
            }

            if (!anyChange)
            {
                return RpcResult::Error(kErrorInvalidParams, "No color changes specified. Use --body, --trim, or --tertiary.");
            }

            auto* updatedRide = GetRide(rideLookup->id);
            json_t payload = {
                { "ride", BuildRidePayload(*updatedRide) },
                { "train", trainIndex },
                { "applied", applied },
                { "previous", previous }
            };

            auto hint = MakeRideHint(
                "rides.theme.vehicle.set", *updatedRide, Telemetry::AIAgentRideWindowPage::Colour,
                "Set vehicle colors: " + updatedRide->getName());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideThemeVehicleMode(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            auto modeParam = GetStringParam(params, "mode");
            if (!modeParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "mode is required (same, per-train, per-car)");
            }

            auto modeVal = VehicleColourSettingsFromString(*modeParam);
            if (!modeVal)
            {
                return RpcResult::Error(kErrorInvalidParams, "Invalid mode '" + *modeParam + "'. Use: same, per-train, or per-car");
            }

            auto previousMode = rideLookup->ride->vehicleColourSettings;

            auto action = GameActions::RideSetAppearanceAction(
                rideLookup->id, GameActions::RideSetAppearanceType::VehicleColourScheme,
                static_cast<uint16_t>(*modeVal), 0);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto* updatedRide = GetRide(rideLookup->id);
            json_t payload = {
                { "ride", BuildRidePayload(*updatedRide) },
                { "mode", VehicleColourSettingsToString(*modeVal) },
                { "previousMode", VehicleColourSettingsToString(previousMode) }
            };

            auto hint = MakeRideHint(
                "rides.theme.vehicle.mode", *updatedRide, Telemetry::AIAgentRideWindowPage::Colour,
                "Set vehicle color mode: " + updatedRide->getName());
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleRideThemeEntranceSet(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            // Check if ride supports entrance/exit styling
            const auto& rtd = rideLookup->ride->getRideTypeDescriptor();
            if (!rtd.HasFlag(RtdFlag::hasEntranceAndExit))
            {
                return RpcResult::Error(kErrorInvalidParams, "This ride type does not have entrance/exit styling");
            }

            if (!params.contains("style"))
            {
                return RpcResult::Error(kErrorInvalidParams, "style is required (name or identifier)");
            }

            auto styleVal = ResolveEntranceStyle(params["style"], errorMessage);
            if (!styleVal)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            auto previousStyle = rideLookup->ride->entranceStyle;

            auto action = GameActions::RideSetAppearanceAction(
                rideLookup->id, GameActions::RideSetAppearanceType::EntranceStyle,
                static_cast<uint16_t>(*styleVal), 0);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto* updatedRide = GetRide(rideLookup->id);
            json_t payload = {
                { "ride", BuildRidePayload(*updatedRide) },
                { "entranceStyle", BuildEntranceStylePayload(*styleVal) },
                { "previousStyle", BuildEntranceStylePayload(previousStyle) }
            };

            auto hint = MakeRideHint(
                "rides.theme.entrance.set", *updatedRide, Telemetry::AIAgentRideWindowPage::Colour,
                "Set entrance style: " + updatedRide->getName());
            return RpcResult::Ok(payload, std::move(hint));
        }

        // Static registration
        struct RideHandlerRegistrar
        {
            RideHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("rides.available", HandleRideAvailability);
                registry.Register("rides.list", [](const json_t& /*params*/) {
                    auto payload = BuildRideListPayload();
                    auto hint = MakeRideListHint("rides.list", "Listed rides", Telemetry::AIAgentRideListFilter::Rides);
                    return RpcResult::Ok(payload, std::move(hint));
                });
                registry.Register("rides.financials", HandleRideFinancials);
                registry.Register("rides.perception", HandleRidePerception);
                registry.Register("rides.operations", HandleRideOperations);
                registry.Register("rides.status", HandleRideStatus);
                registry.Register("rides.price", HandleRidePrice);
                registry.Register("rides.setStatus", HandleRideSetStatus);
                registry.Register("rides.setPrice", HandleRideSetPrice);
                registry.Register("rides.demolish", HandleRideDemolish);
                registry.Register("rides.rename", HandleRideRename);
                registry.Register("rides.configure", HandleRideConfigure);
                registry.Register("rides.breakdowns", HandleRideBreakdowns);
                registry.Register("rides.throughput", HandleRideThroughput);
                registry.Register("rides.feedback", HandleRideFeedback);
                registry.Register("rides.place", HandleRidePlace);
                registry.Register("rides.entrancePlace", [](const json_t& params) {
                    return HandleRideEntrancePlace(params, false);
                });
                registry.Register("rides.exitPlace", [](const json_t& params) {
                    return HandleRideEntrancePlace(params, true);
                });
                registry.Register("rides.coasters.categories", HandleRideCoastersCategories);
                registry.Register("rides.coasters.types", HandleRideCoastersTypes);
                registry.Register("rides.coasters.list", HandleRideCoastersList);
                registry.Register("rides.coasters.preview", HandleRideCoastersPreview);
                registry.Register("rides.coasters.place", HandleRideCoastersPlace);

                // Theme handlers
                registry.Register("rides.theme.colors.list", HandleRideThemeColorsList);
                registry.Register("rides.theme.entrance.list", HandleRideThemeEntranceList);
                registry.Register("rides.theme.get", HandleRideThemeGet);
                registry.Register("rides.theme.track.set", HandleRideThemeTrackSet);
                registry.Register("rides.theme.vehicle.set", HandleRideThemeVehicleSet);
                registry.Register("rides.theme.vehicle.mode", HandleRideThemeVehicleMode);
                registry.Register("rides.theme.entrance.set", HandleRideThemeEntranceSet);
            }
        } static rideRegistrar;

    } // namespace

    void InitRideHandlers()
    {
        (void)rideRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
