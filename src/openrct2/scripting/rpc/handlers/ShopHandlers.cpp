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
#include "../../../GameState.h"
#include "../../../actions/RideDemolishAction.h"
#include "../../../actions/RideSetPriceAction.h"
#include "../../../actions/RideSetStatusAction.h"
#include "../../../actions/RideCreateAction.h"
#include "../../../actions/TrackPlaceAction.h"
#include "../../../actions/GameActionResult.h"
#include "../../../core/Money.hpp"
#include "../../../core/Numerics.hpp"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatter.h"
#include "../../../localisation/Formatting.h"
#include "../../../localisation/StringIds.h"
#include "../../../object/ObjectList.h"
#include "../../../object/ObjectManager.h"
#include "../../../object/RideObject.h"
#include "../../../ride/Ride.h"
#include "../../../ride/RideData.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../world/Location.hpp"
#include "../../../world/Map.h"
#include "../../../world/TileElementsView.h"
#include "../../../world/tile_element/PathElement.h"
#include "../../../world/tile_element/SurfaceElement.h"
#include "../../../world/tile_element/TrackElement.h"

#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For shared types and utilities

    namespace
    {
        struct ShopRideInfo
        {
            ObjectEntryIndex entryIndex{ kObjectEntryIndexNull };
            ride_type_t rideType{ kRideTypeNull };
            std::string legacyIdentifier;
            std::string descriptorIdentifier;
            std::string displayName;
            const RideObjectEntry* rideEntry{};
            const RideTypeDescriptor* descriptor{};
        };

        // String conversion helpers
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

        std::string TrimLegacyIdentifier(std::string_view identifier)
        {
            auto begin = identifier.find_first_not_of(' ');
            auto end = identifier.find_last_not_of(' ');
            if (begin == std::string_view::npos || end == std::string_view::npos)
            {
                return {};
            }
            return std::string(identifier.substr(begin, end - begin + 1));
        }

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
            value = ToLower(std::move(value));
            if (value == "closed" || value == "close")
                return RideStatus::closed;
            if (value == "open")
                return RideStatus::open;
            if (value == "testing" || value == "test")
                return RideStatus::testing;
            if (value == "simulating" || value == "simulate")
                return RideStatus::simulating;
            return std::nullopt;
        }

        std::string_view RideModeToString(RideMode mode)
        {
            // Simplified version for shops
            return "continuous";
        }

        std::string_view RideClassificationToString(RideClassification classification)
        {
            switch (classification)
            {
                case RideClassification::ride:
                    return "ride";
                case RideClassification::shopOrStall:
                    return "shopOrStall";
                case RideClassification::kioskOrFacility:
                    return "kioskOrFacility";
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
                    return "none";
            }
        }

        std::string_view DirectionToString(Direction dir)
        {
            switch (dir)
            {
                case 0:
                    return "north";
                case 1:
                    return "south";
                case 2:
                    return "east";
                case 3:
                    return "west";
                default:
                    return "unknown";
            }
        }

        std::string_view GetRideEntryName(ObjectEntryIndex index)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                return {};
            }
            auto& manager = context->GetObjectManager();
            auto* rideObject = manager.GetLoadedObject<RideObject>(index);
            if (rideObject == nullptr)
            {
                return {};
            }
            return rideObject->GetLegacyIdentifier();
        }

        bool HasAnyParam(const json_t& params, std::initializer_list<const char*> keys)
        {
            for (const char* key : keys)
            {
                if (params.contains(key))
                {
                    return true;
                }
            }
            return false;
        }

        // Shop info builders
        std::optional<ShopRideInfo> BuildShopInfoFromRideObject(RideObject& rideObject, ObjectEntryIndex entryIndex)
        {
            const auto& rideEntry = rideObject.GetEntry();
            auto rideType = rideEntry.GetFirstNonNullRideType();
            if (rideType == kRideTypeNull)
            {
                return std::nullopt;
            }

            const auto& descriptor = GetRideTypeDescriptor(rideType);
            if (!descriptor.HasFlag(RtdFlag::isShopOrFacility))
            {
                return std::nullopt;
            }

            ShopRideInfo info;
            info.entryIndex = entryIndex;
            info.rideType = rideType;
            info.rideEntry = &rideEntry;
            info.descriptor = &descriptor;
            info.displayName = ResolveStringId(rideEntry.naming.Name);
            info.legacyIdentifier = TrimLegacyIdentifier(rideObject.GetLegacyIdentifier());
            auto descriptorIdentifier = rideObject.GetIdentifier();
            if (!descriptorIdentifier.empty())
            {
                info.descriptorIdentifier = std::string(descriptorIdentifier);
            }
            return info;
        }

        template<typename Predicate>
        std::optional<ShopRideInfo> FindLoadedShop(OpenRCT2::IContext& context, Predicate&& predicate)
        {
            auto& manager = context.GetObjectManager();
            auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::ride));
            for (ObjectEntryIndex i = 0; i < maxEntries; ++i)
            {
                auto* rideObject = manager.GetLoadedObject<RideObject>(i);
                if (rideObject == nullptr)
                {
                    continue;
                }
                auto info = BuildShopInfoFromRideObject(*rideObject, i);
                if (!info)
                {
                    continue;
                }
                if (predicate(*info))
                {
                    return info;
                }
            }
            return std::nullopt;
        }

        // Friendly name aliases for common shops/facilities
        // Maps user-friendly names to descriptor identifiers (rct2.ride.xxx format)
        std::optional<std::string> ResolveShopAlias(std::string_view input)
        {
            static const std::unordered_map<std::string, std::string> aliases = {
                // Facilities
                { "atm", "rct2.ride.atm1" },
                { "cash machine", "rct2.ride.atm1" },
                { "cashmachine", "rct2.ride.atm1" },
                { "toilet", "rct2.ride.tlt1" },
                { "toilets", "rct2.ride.tlt1" },
                { "restroom", "rct2.ride.tlt1" },
                { "restrooms", "rct2.ride.tlt1" },
                { "bathroom", "rct2.ride.tlt1" },
                { "bathrooms", "rct2.ride.tlt1" },
                { "first aid", "rct2.ride.faid1" },
                { "firstaid", "rct2.ride.faid1" },
                { "first aid room", "rct2.ride.faid1" },
                { "info", "rct2.ride.infok" },
                { "info kiosk", "rct2.ride.infok" },
                { "information", "rct2.ride.infok" },
                { "information kiosk", "rct2.ride.infok" },
                // Food stalls
                { "drinks", "rct2.ride.drnks" },
                { "drinks stall", "rct2.ride.drnks" },
                { "burger", "rct2.ride.burgb" },
                { "burgers", "rct2.ride.burgb" },
                { "burger bar", "rct2.ride.burgb" },
                { "candyfloss", "rct2.ride.cndyf" },
                { "candy floss", "rct2.ride.cndyf" },
                { "cotton candy", "rct2.ride.cndyf" },
                { "candyfloss stall", "rct2.ride.cndyf" },
                { "balloon", "rct2.ride.balln" },
                { "balloons", "rct2.ride.balln" },
                { "balloon stall", "rct2.ride.balln" },
                { "ice cream", "rct2.ride.icecr1" },
                { "icecream", "rct2.ride.icecr1" },
                { "ice cream stall", "rct2.ride.icecr1" },
                { "pizza", "rct2.ride.pizza1" },
                { "pizza stall", "rct2.ride.pizza1" },
                { "popcorn", "rct2.ride.popcs1" },
                { "popcorn stall", "rct2.ride.popcs1" },
                { "fries", "rct2.ride.chips1" },
                { "chips", "rct2.ride.chips1" },
                { "chip stall", "rct2.ride.chips1" },
                { "hot dog", "rct2.ride.hotds1" },
                { "hotdog", "rct2.ride.hotds1" },
                { "hot dog stall", "rct2.ride.hotds1" },
                { "coffee", "rct2.ride.coffee1" },
                { "coffee shop", "rct2.ride.coffee1" },
                { "chicken", "rct2.ride.chcks1" },
                { "fried chicken", "rct2.ride.chcks1" },
                { "lemonade", "rct2.ride.lemns1" },
                { "lemonade stall", "rct2.ride.lemns1" },
                // Merchandise
                { "hat", "rct2.ride.hatst1" },
                { "hats", "rct2.ride.hatst1" },
                { "hat stall", "rct2.ride.hatst1" },
                { "tshirt", "rct2.ride.tshrt1" },
                { "t-shirt", "rct2.ride.tshrt1" },
                { "t-shirt stall", "rct2.ride.tshrt1" },
                { "souvenir", "rct2.ride.souvs1" },
                { "souvenirs", "rct2.ride.souvs1" },
                { "souvenir stall", "rct2.ride.souvs1" },
            };

            auto lowered = ToLower(std::string(input));
            auto it = aliases.find(lowered);
            if (it != aliases.end())
            {
                return it->second;
            }
            return std::nullopt;
        }

        std::optional<ShopRideInfo> ResolveShopRideInfo(
            std::optional<std::string> identifier,
            std::optional<int32_t> entryIndex,
            std::optional<std::string> displayName,
            std::string& errorMessage)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                errorMessage = "Game context is not available";
                return std::nullopt;
            }

            auto& manager = context->GetObjectManager();
            auto tryEntryIndex = [&](int32_t value) -> std::optional<ShopRideInfo> {
                if (value < 0)
                {
                    return std::nullopt;
                }
                auto idx = static_cast<ObjectEntryIndex>(value);
                auto* rideObject = manager.GetLoadedObject<RideObject>(idx);
                if (rideObject == nullptr)
                {
                    return std::nullopt;
                }
                return BuildShopInfoFromRideObject(*rideObject, idx);
            };

            if (entryIndex)
            {
                if (auto info = tryEntryIndex(*entryIndex))
                {
                    return info;
                }
            }

            // Check for friendly name aliases (e.g., "ATM" -> "rct2.ride.atm1")
            // This allows users to use intuitive names instead of internal identifiers
            auto tryAlias = [&](const std::string& input) -> std::optional<ShopRideInfo> {
                if (auto aliasedId = ResolveShopAlias(input))
                {
                    auto loweredAlias = ToLower(*aliasedId);
                    return FindLoadedShop(*context, [&](const ShopRideInfo& candidate) {
                        return !candidate.descriptorIdentifier.empty()
                            && ToLower(candidate.descriptorIdentifier) == loweredAlias;
                    });
                }
                return std::nullopt;
            };

            // Try alias lookup for identifier (--type)
            if (identifier && !identifier->empty())
            {
                if (auto info = tryAlias(*identifier))
                {
                    return info;
                }
            }

            // Try alias lookup for displayName (--name)
            if (displayName && !displayName->empty())
            {
                if (auto info = tryAlias(*displayName))
                {
                    return info;
                }
            }

            if (identifier && !identifier->empty())
            {
                auto trimmed = TrimLegacyIdentifier(*identifier);
                if (!trimmed.empty())
                {
                    auto lowered = ToLower(trimmed);
                    if (auto info = FindLoadedShop(*context, [&](const ShopRideInfo& candidate) {
                            if (!candidate.legacyIdentifier.empty() && ToLower(candidate.legacyIdentifier) == lowered)
                            {
                                return true;
                            }
                            if (!candidate.descriptorIdentifier.empty() && ToLower(candidate.descriptorIdentifier) == lowered)
                            {
                                return true;
                            }
                            return false;
                        }))
                    {
                        return info;
                    }
                }
            }

            if (displayName && !displayName->empty())
            {
                auto lowered = ToLower(*displayName);
                if (auto info = FindLoadedShop(*context, [&](const ShopRideInfo& candidate) {
                        return !candidate.displayName.empty() && ToLower(candidate.displayName) == lowered;
                    }))
                {
                    return info;
                }
            }

            if (entryIndex)
            {
                errorMessage = "No shop is loaded at entry index " + std::to_string(*entryIndex);
            }
            else if (identifier && !identifier->empty())
            {
                errorMessage = "Shop identifier '" + *identifier + "' is not available";
            }
            else if (displayName && !displayName->empty())
            {
                errorMessage = "Shop \"" + *displayName + "\" is not available";
            }
            else
            {
                errorMessage = "Provide --type, --name, or --entry-index";
            }

            return std::nullopt;
        }

        json_t BuildShopItemList(const RideObjectEntry& rideEntry)
        {
            json_t items = json_t::array();
            for (const auto& shopItem : rideEntry.shop_item)
            {
                if (shopItem == ShopItem::none)
                {
                    continue;
                }
                json_t node = json_t::object();
                node["id"] = static_cast<int32_t>(shopItem);
                auto label = ShopItemToString(shopItem);
                if (!label.empty())
                {
                    node["label"] = std::move(label);
                }
                items.push_back(node);
            }
            return items;
        }

        json_t BuildShopCatalogPayload(OpenRCT2::IContext& context)
        {
            auto& manager = context.GetObjectManager();
            auto maxEntries = static_cast<ObjectEntryIndex>(getObjectEntryGroupCount(ObjectType::ride));
            json_t entries = json_t::array();
            for (ObjectEntryIndex i = 0; i < maxEntries; ++i)
            {
                auto* rideObject = manager.GetLoadedObject<RideObject>(i);
                if (rideObject == nullptr)
                {
                    continue;
                }

                auto shopInfo = BuildShopInfoFromRideObject(*rideObject, i);
                if (!shopInfo)
                {
                    continue;
                }

                json_t node = json_t::object();
                node["identifier"] = shopInfo->legacyIdentifier;
                node["legacyIdentifier"] = shopInfo->legacyIdentifier;
                if (!shopInfo->descriptorIdentifier.empty())
                {
                    node["descriptorIdentifier"] = shopInfo->descriptorIdentifier;
                }
                if (!shopInfo->displayName.empty())
                {
                    node["name"] = shopInfo->displayName;
                }
                node["entryIndex"] = shopInfo->entryIndex;
                node["rideType"] = shopInfo->descriptor->Name.empty() ? std::string("ride")
                                                                       : std::string(shopInfo->descriptor->Name);
                node["classification"]
                    = std::string(RideClassificationToString(shopInfo->descriptor->Classification));
                node["category"] = std::string(RideCategoryToString(shopInfo->descriptor->Category));
                node["startPiece"] = static_cast<int32_t>(shopInfo->descriptor->StartTrackPiece);
                node["defaultPrices"]
                    = json_t::array({ MoneyToDouble(shopInfo->descriptor->DefaultPrices[0]),
                        MoneyToDouble(shopInfo->descriptor->DefaultPrices[1]) });
                node["buildCost"] = MoneyToDouble(shopInfo->descriptor->BuildCosts.TrackPrice);
                node["items"] = BuildShopItemList(*shopInfo->rideEntry);

                entries.push_back(node);
            }

            json_t payload = json_t::object();
            payload["entries"] = entries;
            payload["count"] = entries.size();
            return payload;
        }

        std::optional<json_t> BuildRideTilePayload(const Ride& ride)
        {
            CoordsXYE coords{};
            if (!RideTryGetOriginElement(ride, &coords) || coords.element == nullptr)
            {
                return std::nullopt;
            }

            TileCoordsXY tile{ coords };
            json_t node = json_t::object();
            node["x"] = tile.x;
            node["y"] = tile.y;
            node["z"] = WorldZToTileZ(coords.element->GetBaseZ());

            auto direction = coords.element->GetDirection();
            node["directionIndex"] = direction;
            node["direction"] = std::string(DirectionToString(direction));
            return node;
        }

        json_t BuildShopInstancePayload(const Ride& ride)
        {
            json_t node = json_t::object();
            node["id"] = ride.id.ToUnderlying();
            node["name"] = ride.getName();
            node["status"] = RideStatusToString(ride.status);
            node["mode"] = RideModeToString(ride.mode);
            node["price"] = MoneyToDouble(RideGetPrice(ride));
            node["numPrices"] = ride.getNumPrices();
            if (ride.getNumPrices() > 1)
            {
                node["secondaryPrice"] = MoneyToDouble(ride.price[1]);
            }
            node["queueLength"] = ride.getTotalQueueLength();
            node["queueTime"] = ride.getMaxQueueTime();
            node["customersInterval"] = ride.curNumCustomers;
            node["totalCustomers"] = ride.totalCustomers;
            node["incomePerHour"] = MoneyToDouble(ride.incomePerHour);
            node["profitThisMonth"] = MoneyToDouble(ride.profit);
            node["totalProfit"] = MoneyToDouble(ride.totalProfit);
            node["numPrimaryItemsSold"] = ride.numPrimaryItemsSold;
            node["numSecondaryItemsSold"] = ride.numSecondaryItemsSold;

            const auto& descriptor = ride.getRideTypeDescriptor();
            node["rideType"] = descriptor.Name.empty() ? std::string("ride") : std::string(descriptor.Name);
            node["classification"] = std::string(RideClassificationToString(descriptor.Classification));
            node["category"] = std::string(RideCategoryToString(descriptor.Category));

            if (auto tile = BuildRideTilePayload(ride))
            {
                node["tile"] = *tile;
            }

            if (const auto* rideEntry = ride.getRideEntry())
            {
                json_t objectNode = json_t::object();
                objectNode["entryIndex"] = static_cast<int32_t>(ride.subtype);
                auto identifier = GetRideEntryName(ride.subtype);
                if (!identifier.empty())
                {
                    objectNode["identifier"] = std::string(identifier);
                }
                node["object"] = objectNode;
                node["items"] = BuildShopItemList(*rideEntry);
            }
            else
            {
                node["items"] = json_t::array();
            }

            return node;
        }

        std::string BuildShopPlacementDiagnostics(const TileCoordsXY& tile)
        {
            std::vector<std::string> reasons;

            auto* surface = MapGetSurfaceElementAt(tile);
            if (surface == nullptr)
            {
                reasons.push_back("No terrain data found at tile; try revealing and flattening the area first.");
            }
            else
            {
                const auto ownership = surface->GetOwnership();
                if ((ownership & (OWNERSHIP_OWNED | OWNERSHIP_CONSTRUCTION_RIGHTS_OWNED)) == 0)
                {
                    reasons.push_back("The park does not own tile (" + std::to_string(tile.x) + ", "
                        + std::to_string(tile.y)
                        + "); purchase the land or construction rights, then retry shops place.");
                }
            }

            bool hasPath = false;
            for (auto* element : TileElementsView<TileElement>(tile))
            {
                if (element == nullptr)
                {
                    break;
                }
                if (element->GetType() == TileElementType::Path)
                {
                    hasPath = true;
                    break;
                }
                if (element->IsLastForTile())
                {
                    break;
                }
            }
            if (!hasPath)
            {
                reasons.push_back("No path detected adjacent to tile (" + std::to_string(tile.x) + ", "
                    + std::to_string(tile.y) + "). Shops/stalls must be placed next to a path tile; lay a path first.");
            }

            if (reasons.empty())
            {
                return std::string();
            }

            std::ostringstream oss;
            for (size_t i = 0; i < reasons.size(); ++i)
            {
                if (i > 0)
                {
                    oss << " ";
                }
                oss << reasons[i];
            }
            return oss.str();
        }

        std::optional<int32_t> ResolvePlacementHeight(const json_t& params, const TileCoordsXY& tile, std::string& errorMessage,
            std::optional<int32_t> adjacentPathHeight = std::nullopt)
        {
            if (auto explicitZ = GetIntParam(params, "z"))
            {
                // User input is in tile units; convert to world units and align
                return OpenRCT2::Numerics::floor2(TileZToWorldZ(*explicitZ), kCoordsZStep);
            }

            // If we detected an adjacent path, use its height for proper alignment
            // This ensures shops are placed at the same level as the path they face
            if (adjacentPathHeight)
            {
                return OpenRCT2::Numerics::floor2(*adjacentPathHeight, kCoordsZStep);
            }

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

        std::optional<RideLookupResult> FindRideAtCoords(const CoordsXY& coords, std::optional<int32_t> zFilter)
        {
            for (auto* trackElement : TileElementsView<TrackElement>(coords))
            {
                if (trackElement == nullptr)
                {
                    break;
                }
                if (zFilter && trackElement->GetBaseZ() != *zFilter)
                {
                    continue;
                }
                RideId rideIndex = trackElement->GetRideIndex();
                auto* ride = GetRide(rideIndex);
                if (ride != nullptr && !ride->id.IsNull())
                {
                    return RideLookupResult{ rideIndex, ride };
                }
            }
            return std::nullopt;
        }

        std::optional<RideLookupResult> ResolveRideFromParams(const json_t& params, std::string& errorMessage)
        {
            auto idParam = GetIntParam(params, "rideId");
            if (!idParam)
            {
                idParam = GetIntParam(params, "id");
            }
            if (idParam)
            {
                RideId rideId = RideId::FromUnderlying(*idParam);
                auto* ride = GetRide(rideId);
                if (ride == nullptr || ride->id.IsNull())
                {
                    errorMessage = "Ride " + std::to_string(*idParam) + " not found";
                    return std::nullopt;
                }
                return RideLookupResult{ rideId, ride };
            }

            auto nameParam = GetStringParam(params, "rideName");
            if (!nameParam)
            {
                nameParam = GetStringParam(params, "ride");
            }
            if (!nameParam)
            {
                nameParam = GetStringParam(params, "name");
            }
            if (nameParam)
            {
                auto lowered = ToLower(*nameParam);
                auto& gameState = getGameState();
                for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
                {
                    auto& ride = gameState.rides[i];
                    if (ride.id.IsNull())
                    {
                        continue;
                    }
                    auto rideName = ToLower(ride.getName());
                    if (rideName == lowered)
                    {
                        return RideLookupResult{ ride.id, &ride };
                    }
                }
                errorMessage = "Ride '" + *nameParam + "' not found";
                return std::nullopt;
            }

            errorMessage = "rideId or rideName is required";
            return std::nullopt;
        }

        std::optional<RideLookupResult> ResolveShopRideInstance(const json_t& params, std::string& errorMessage)
        {
            if (HasAnyParam(params, { "rideId", "id", "rideName", "ride", "name" }))
            {
                auto rideLookup = ResolveRideFromParams(params, errorMessage);
                if (!rideLookup)
                {
                    return std::nullopt;
                }

                if (!rideLookup->ride->getRideTypeDescriptor().HasFlag(RtdFlag::isShopOrFacility))
                {
                    errorMessage = "Ride '" + rideLookup->ride->getName() + "' is not a shop or stall";
                    return std::nullopt;
                }

                return rideLookup;
            }

            auto xParam = GetIntParam(params, "x");
            auto yParam = GetIntParam(params, "y");
            if (xParam && yParam)
            {
                TileCoordsXY tile{ *xParam, *yParam };
                auto coords = tile.ToCoordsXY();
                if (!MapIsLocationValid(coords))
                {
                    errorMessage = "Tile is outside the current map bounds";
                    return std::nullopt;
                }

                auto zFilterParam = GetIntParam(params, "z");
                std::optional<int32_t> zFilter = zFilterParam ? std::make_optional(TileZToWorldZ(*zFilterParam)) : std::nullopt;
                auto rideLookup = FindRideAtCoords(coords, zFilter);
                if (!rideLookup)
                {
                    std::ostringstream oss;
                    oss << "No ride found at (" << tile.x << ", " << tile.y << ")";
                    if (zFilterParam)
                    {
                        oss << " z=" << *zFilterParam;
                    }
                    errorMessage = oss.str();
                    return std::nullopt;
                }

                if (!rideLookup->ride->getRideTypeDescriptor().HasFlag(RtdFlag::isShopOrFacility))
                {
                    std::ostringstream oss;
                    oss << "Ride at (" << tile.x << ", " << tile.y << ") is not a shop or stall";
                    if (zFilterParam)
                    {
                        oss << " at z=" << *zFilterParam;
                    }
                    errorMessage = oss.str();
                    return std::nullopt;
                }

                return rideLookup;
            }

            errorMessage = "rideId (--ride-id), ride name (--ride), or tile (--x/--y) is required";
            return std::nullopt;
        }

        json_t BuildRidePayload(const Ride& ride)
        {
            json_t rideJson = json_t::object();
            rideJson["id"] = ride.id.ToUnderlying();
            rideJson["name"] = ride.getName();
            rideJson["status"] = RideStatusToString(ride.status);
            return rideJson;
        }

        json_t BuildActionSuccessPayload(const GameActions::Result& result)
        {
            json_t payload = json_t::object();
            payload["status"] = GameActionStatusToString(result.Error);
            payload["cost"] = MoneyToDouble(result.Cost);
            return payload;
        }

        std::optional<CoordsXYZ> BuildTileCameraTarget(const TileCoordsXY& tile, int32_t width = 1, int32_t height = 1)
        {
            auto anchor = tile.ToCoordsXY();
            anchor.x += width * kCoordsXYHalfTile;
            anchor.y += height * kCoordsXYHalfTile;
            auto z = TileElementHeight(anchor);
            return CoordsXYZ{ anchor.x, anchor.y, z };
        }

        std::optional<CoordsXYZ> BuildRideCameraTarget(const Ride& ride)
        {
            CoordsXYE coords{};
            if (!RideTryGetOriginElement(ride, &coords) || coords.element == nullptr)
            {
                return std::nullopt;
            }
            return CoordsXYZ{ coords.x, coords.y, coords.element->GetBaseZ() };
        }

        Telemetry::AIAgentFollowHint MakeRideHint(
            std::string_view method, const Ride& ride, Telemetry::AIAgentRideWindowPage page, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            if (auto camera = BuildRideCameraTarget(ride))
            {
                hint.cameraTarget = camera;
            }
            Telemetry::RideWindowIntent intent;
            intent.rideId = ride.id;
            intent.page = page;
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

        Telemetry::AIAgentFollowHint MakeTileHint(
            std::string_view method, std::string contextLabel, const TileCoordsXY& tile, WindowClass windowClass,
            int32_t width = 1, int32_t height = 1)
        {
            auto camera = BuildTileCameraTarget(tile, width, height);
            return MakeGenericWindowHint(method, std::move(contextLabel), windowClass, camera);
        }

        Telemetry::AIAgentFollowHint MakeRideListHint(
            std::string_view method, std::string contextLabel, Telemetry::AIAgentRideListFilter filter,
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

        // Handler implementations
        RpcResult HandleShopsCatalog(const json_t& /*params*/)
        {
            auto* context = GetContext();
            if (context == nullptr)
            {
                return RpcResult::Error(-32603, "Game context is not available");
            }
            auto payload = BuildShopCatalogPayload(*context);
            auto hint = MakeConstructRideHint(
                "shops.catalog", "Browsed shop catalog", Telemetry::AIAgentConstructRideTab::Shop);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleShopsList(const json_t& params)
        {
            if (!params.is_object() && !params.is_null())
            {
                return RpcResult::Error(-32602, "Params must be a JSON object");
            }

            const auto& gameState = getGameState();
            json_t entries = json_t::array();
            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                {
                    continue;
                }

                const auto& descriptor = ride.getRideTypeDescriptor();
                if (!descriptor.HasFlag(RtdFlag::isShopOrFacility))
                {
                    continue;
                }

                entries.push_back(BuildShopInstancePayload(ride));
            }

            json_t payload = json_t::object();
            payload["entries"] = entries;
            payload["count"] = entries.size();
            auto hint = MakeRideListHint("shops.list", "Opened shop list", Telemetry::AIAgentRideListFilter::Shops);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleShopPlace(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(-32602, "Params must be a JSON object");
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
                return RpcResult::Error(-32602, "Provide --type, --name, or --entry-index");
            }

            auto xParam = GetIntParam(params, "x");
            auto yParam = GetIntParam(params, "y");
            if (!xParam || !yParam)
            {
                return RpcResult::Error(-32602, "x and y tile coordinates are required");
            }

            std::string errorMessage;
            auto shopInfo = ResolveShopRideInfo(typeParam, entryIndexParam, nameParam, errorMessage);
            if (!shopInfo)
            {
                return RpcResult::Error(-32602, errorMessage);
            }

            TileCoordsXY tile{ *xParam, *yParam };
            auto coords = tile.ToCoordsXY();
            if (!MapIsLocationValid(coords))
            {
                return RpcResult::Error(-32602, "Tile is outside the current map bounds");
            }

            // Always scan for adjacent paths - shops MUST be placed next to a path
            // This prevents placing inaccessible shops (e.g., underground or floating)
            struct AdjacentPath {
                Direction direction;
                TileCoordsXY tile;
                int priority;
                int32_t height; // Path element height
            };
            std::vector<AdjacentPath> adjacentPaths;

            const std::array<std::pair<TileCoordsXY, Direction>, 4> neighbors = {{
                {{tile.x, tile.y + 1}, Direction(1)}, // south
                {{tile.x + 1, tile.y}, Direction(2)}, // east
                {{tile.x - 1, tile.y}, Direction(0)}, // north
                {{tile.x, tile.y - 1}, Direction(3)}  // west
            }};

            int priority = 0;
            for (const auto& [neighborTile, dir] : neighbors)
            {
                auto neighborCoords = neighborTile.ToCoordsXY();
                if (MapIsLocationValid(neighborCoords))
                {
                    for (auto* element : TileElementsView<TileElement>(neighborTile))
                    {
                        if (element == nullptr)
                            break;
                        if (element->GetType() == TileElementType::Path)
                        {
                            adjacentPaths.push_back({dir, neighborTile, priority, element->GetBaseZ()});
                            // Don't break - collect all paths at different heights
                        }
                        if (element->IsLastForTile())
                            break;
                    }
                }
                priority++;
            }

            if (adjacentPaths.empty())
            {
                return RpcResult::Error(-32602,
                    "No adjacent path found. Shops must be placed next to a path tile to be accessible to guests.");
            }

            // Auto-select facing direction based on adjacent path
            // Shops must face toward a path to be accessible - no manual override allowed
            std::sort(adjacentPaths.begin(), adjacentPaths.end(),
                [](const AdjacentPath& a, const AdjacentPath& b) { return a.priority < b.priority; });
            Direction direction = adjacentPaths[0].direction;
            int32_t pathHeight = adjacentPaths[0].height;

            // Resolve placement height - explicit --z overrides, otherwise use path height
            auto placementHeight = ResolvePlacementHeight(params, tile, errorMessage, pathHeight);
            if (!placementHeight)
            {
                return RpcResult::Error(-32602, errorMessage);
            }

            // Validate that the placement height matches an adjacent path
            // This prevents placing shops at a height where no path exists
            bool hasPathAtHeight = std::any_of(adjacentPaths.begin(), adjacentPaths.end(),
                [&placementHeight](const AdjacentPath& p) {
                    // Allow small tolerance for height alignment (within one step)
                    return std::abs(p.height - *placementHeight) <= kCoordsZStep;
                });

            if (!hasPathAtHeight)
            {
                int32_t suggestedZ = WorldZToTileZ(pathHeight);
                return RpcResult::Error(-32602,
                    "No adjacent path at z=" + std::to_string(WorldZToTileZ(*placementHeight)) +
                    ". Adjacent paths found at z=" + std::to_string(suggestedZ) +
                    ". Use --z " + std::to_string(suggestedZ) + " or omit --z to auto-align with the path.");
            }

            auto& gameState = getGameState();

            // Handle dry-run mode: validate and estimate cost without actually placing
            auto dryRunParam = GetBoolParam(params, "dryRun");
            if (dryRunParam && *dryRunParam)
            {
                int32_t colour1 = RideGetRandomColourPresetIndex(shopInfo->rideType);
                int32_t colour2 = RideGetUnusedPresetVehicleColour(shopInfo->entryIndex);

                auto rideCreate = GameActions::RideCreateAction(
                    shopInfo->rideType, shopInfo->entryIndex, colour1, colour2, gameState.lastEntranceStyle);
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
                objectNode["identifier"] = shopInfo->legacyIdentifier;
                if (!shopInfo->descriptorIdentifier.empty())
                {
                    objectNode["descriptorIdentifier"] = shopInfo->descriptorIdentifier;
                }
                if (!shopInfo->displayName.empty())
                {
                    objectNode["displayName"] = shopInfo->displayName;
                }
                objectNode["entryIndex"] = shopInfo->entryIndex;
                objectNode["rideType"] = shopInfo->descriptor->Name.empty() ? std::string("shop")
                                                                             : std::string(shopInfo->descriptor->Name);
                payload["object"] = objectNode;

                std::string contextLabel = "Dry-run: would place " + shopInfo->displayName + " at (" + std::to_string(tile.x) + "," + std::to_string(tile.y) + ")";
                auto hint = MakeTileHint("shops.place", std::move(contextLabel), tile, WindowClass::constructRide);
                return RpcResult::Ok(payload, std::move(hint));
            }
            int32_t colour1 = RideGetRandomColourPresetIndex(shopInfo->rideType);
            int32_t colour2 = RideGetUnusedPresetVehicleColour(shopInfo->entryIndex);

            auto rideCreate = GameActions::RideCreateAction(
                shopInfo->rideType, shopInfo->entryIndex, colour1, colour2, gameState.lastEntranceStyle);
            auto createResult = GameActions::ExecuteNested(&rideCreate, gameState);
            if (createResult.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(-32000, BuildGameActionErrorMessage(createResult));
            }

            RideId rideId = createResult.GetData<RideId>();

            SelectedLiftAndInverted liftFlags{};
            CoordsXYZD origin{ coords.x, coords.y, *placementHeight, direction };
            auto trackAction = GameActions::TrackPlaceAction(
                rideId, shopInfo->descriptor->StartTrackPiece, shopInfo->rideType, origin, 0, 0, 0, liftFlags, false);
            auto placeResult = GameActions::ExecuteNested(&trackAction, gameState);
            if (placeResult.Error != GameActions::Status::Ok)
            {
                auto demolish = GameActions::RideDemolishAction(rideId, GameActions::RideModifyType::demolish);
                GameActions::Execute(&demolish, gameState);
                auto detailedMessage = BuildGameActionErrorMessage(placeResult);
                auto diagnostics = BuildShopPlacementDiagnostics(tile);
                if (!diagnostics.empty())
                {
                    detailedMessage += " — " + diagnostics;
                }
                return RpcResult::Error(-32000, detailedMessage);
            }

            money64 totalCost = createResult.Cost + placeResult.Cost;
            auto* ride = GetRide(rideId);

            // Automatically open the shop after placement
            auto openAction = GameActions::RideSetStatusAction(rideId, RideStatus::open);
            auto openResult = GameActions::ExecuteNested(&openAction, gameState);

            json_t payload = json_t::object();
            payload["status"] = GameActionStatusToString(placeResult.Error);
            payload["cost"] = MoneyToDouble(totalCost);

            json_t costBreakdown = json_t::object();
            costBreakdown["create"] = MoneyToDouble(createResult.Cost);
            costBreakdown["build"] = MoneyToDouble(placeResult.Cost);
            payload["costBreakdown"] = costBreakdown;

            json_t tileNode = json_t::object();
            tileNode["x"] = tile.x;
            tileNode["y"] = tile.y;
            tileNode["z"] = WorldZToTileZ(*placementHeight);
            payload["tile"] = tileNode;
            payload["direction"] = std::string(DirectionToString(direction));
            payload["directionIndex"] = direction;

            json_t rideNode = json_t::object();
            rideNode["id"] = rideId.ToUnderlying();
            rideNode["rideType"] = shopInfo->descriptor->Name.empty() ? std::string("ride")
                                                                       : std::string(shopInfo->descriptor->Name);
            rideNode["classification"] = std::string(RideClassificationToString(shopInfo->descriptor->Classification));
            rideNode["items"] = BuildShopItemList(*shopInfo->rideEntry);
            if (ride != nullptr)
            {
                rideNode["name"] = BuildRideDisplayName(*ride);
                rideNode["status"] = RideStatusToString(ride->status);
            }
            payload["ride"] = rideNode;

            json_t objectNode = json_t::object();
            objectNode["identifier"] = shopInfo->legacyIdentifier;
            if (!shopInfo->descriptorIdentifier.empty())
            {
                objectNode["descriptorIdentifier"] = shopInfo->descriptorIdentifier;
            }
            if (!shopInfo->displayName.empty())
            {
                objectNode["displayName"] = shopInfo->displayName;
            }
            objectNode["entryIndex"] = shopInfo->entryIndex;
            payload["object"] = objectNode;

            if (ride != nullptr)
            {
                auto rideLabel = BuildRideDisplayName(*ride);
                std::string contextLabel = "Placed " + rideLabel + " at (" + std::to_string(tile.x) + "," + std::to_string(tile.y) + ")";
                auto hint = MakeRideHint("shops.place", *ride, Telemetry::AIAgentRideWindowPage::Main, std::move(contextLabel));
                if (!hint.cameraTarget)
                {
                    hint.cameraTarget = BuildTileCameraTarget(tile);
                }
                return RpcResult::Ok(payload, std::move(hint));
            }
            std::string contextLabel = "Placed shop at (" + std::to_string(tile.x) + "," + std::to_string(tile.y) + ")";
            auto fallbackHint = MakeTileHint("shops.place", std::move(contextLabel), tile, WindowClass::constructRide);
            return RpcResult::Ok(payload, std::move(fallbackHint));
        }

        RpcResult HandleShopRemove(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(-32602, "Params must be a JSON object");
            }

            std::string errorMessage;
            auto rideLookup = ResolveShopRideInstance(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(-32602, errorMessage);
            }

            json_t rideSnapshot = BuildRidePayload(*rideLookup->ride);
            auto action = GameActions::RideDemolishAction(rideLookup->id, GameActions::RideModifyType::demolish);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(-32000, BuildGameActionErrorMessage(result));
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["ride"] = rideSnapshot;
            payload["demolished"] = true;

            auto xParam = GetIntParam(params, "x");
            auto yParam = GetIntParam(params, "y");
            if (xParam && yParam)
            {
                json_t tileNode = json_t::object();
                tileNode["x"] = *xParam;
                tileNode["y"] = *yParam;
                if (auto zParam = GetIntParam(params, "z"))
                {
                    tileNode["z"] = *zParam;
                }
                payload["tile"] = tileNode;
            }

            std::string rideLabel = rideSnapshot.contains("name") ? rideSnapshot["name"].get<std::string>() : std::string("shop");
            std::string contextLabel = "Removed " + rideLabel;
            std::optional<TileCoordsXY> removalTile;
            if (xParam && yParam)
            {
                contextLabel += " at (" + std::to_string(*xParam) + "," + std::to_string(*yParam) + ")";
                removalTile = TileCoordsXY{ *xParam, *yParam };
            }

            Telemetry::AIAgentFollowHint hint = removalTile.has_value()
                ? MakeTileHint("shops.remove", contextLabel, removalTile.value(), WindowClass::constructRide)
                : MakeGenericWindowHint("shops.remove", contextLabel, WindowClass::constructRide, std::nullopt);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleShopSetStatus(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(-32602, errorMessage);
            }

            // Verify this is actually a shop/stall, not a ride
            const auto& descriptor = GetRideTypeDescriptor(rideLookup->ride->type);
            if (descriptor.Classification != RideClassification::shopOrStall &&
                descriptor.Classification != RideClassification::kioskOrFacility)
            {
                auto rideName = rideLookup->ride->getName();
                auto rideId = rideLookup->id.ToUnderlying();
                return RpcResult::Error(-32602,
                    "ID " + std::to_string(rideId) + " is a ride (" + rideName + "), not a shop/stall.\n" +
                    "Use 'rctctl rides open --id " + std::to_string(rideId) + "' instead.");
            }

            auto statusParam = GetStringParam(params, "status");
            if (!statusParam)
            {
                statusParam = GetStringParam(params, "state");
            }
            if (!statusParam)
            {
                return RpcResult::Error(-32602, "status is required");
            }

            auto desiredStatus = RideStatusFromString(*statusParam);
            if (!desiredStatus)
            {
                return RpcResult::Error(-32602, "Unknown shop status: " + *statusParam);
            }

            const auto previousStatus = rideLookup->ride->status;
            auto action = GameActions::RideSetStatusAction(rideLookup->id, desiredStatus.value());
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(-32000, BuildGameActionErrorMessage(result));
            }

            auto* updatedRide = GetRide(rideLookup->id);
            if (updatedRide == nullptr || updatedRide->id.IsNull())
            {
                return RpcResult::Error(-32000, "Shop status action succeeded but shop could not be retrieved");
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["ride"] = BuildRidePayload(*updatedRide);
            payload["status"] = RideStatusToString(desiredStatus.value());
            payload["previousStatus"] = RideStatusToString(previousStatus);
            auto rideLabel = BuildRideDisplayName(*updatedRide);
            std::string contextLabel = "Set status of " + rideLabel + " to " + std::string(RideStatusToString(desiredStatus.value()));
            auto hint = MakeRideHint("shops.setStatus", *updatedRide, Telemetry::AIAgentRideWindowPage::Main, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleShopGetPrice(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(-32602, errorMessage);
            }

            // Verify this is actually a shop/stall, not a ride
            const auto& descriptor = GetRideTypeDescriptor(rideLookup->ride->type);
            if (descriptor.Classification != RideClassification::shopOrStall &&
                descriptor.Classification != RideClassification::kioskOrFacility)
            {
                auto rideName = rideLookup->ride->getName();
                auto rideId = rideLookup->id.ToUnderlying();
                return RpcResult::Error(-32602,
                    "ID " + std::to_string(rideId) + " is a ride (" + rideName + "), not a shop/stall.\n" +
                    "Use 'rctctl rides price --id " + std::to_string(rideId) + "' instead.");
            }

            const auto* ride = rideLookup->ride;
            const auto* rideEntry = ride->getRideEntry();

            json_t payload = json_t::object();
            payload["id"] = ride->id.ToUnderlying();
            payload["name"] = ride->getName();
            payload["numPrices"] = ride->getNumPrices();

            // Build items array with prices
            json_t items = json_t::array();
            if (rideEntry != nullptr)
            {
                int priceIndex = 0;
                for (const auto& shopItem : rideEntry->shop_item)
                {
                    if (shopItem == ShopItem::none)
                    {
                        continue;
                    }
                    json_t itemNode = json_t::object();
                    itemNode["id"] = static_cast<int32_t>(shopItem);
                    auto label = ShopItemToString(shopItem);
                    if (!label.empty())
                    {
                        itemNode["label"] = std::move(label);
                    }
                    itemNode["priceIndex"] = priceIndex;
                    itemNode["price"] = MoneyToDouble(ride->price[priceIndex]);

                    const auto& itemDescriptor = GetShopItemDescriptor(shopItem);
                    itemNode["defaultPrice"] = MoneyToDouble(itemDescriptor.DefaultPrice);

                    items.push_back(itemNode);
                    priceIndex++;
                }
            }
            payload["items"] = items;

            payload["price"] = MoneyToDouble(RideGetPrice(*ride));
            if (ride->getNumPrices() > 1)
            {
                payload["secondaryPrice"] = MoneyToDouble(ride->price[1]);
            }

            auto rideLabel = BuildRideDisplayName(*ride);
            std::string contextLabel = "Viewed prices for " + rideLabel;
            auto hint = MakeRideHint("shops.getPrice", *ride, Telemetry::AIAgentRideWindowPage::Income, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleShopSetPrice(const json_t& params)
        {
            std::string errorMessage;
            auto rideLookup = ResolveRideFromParams(params, errorMessage);
            if (!rideLookup)
            {
                return RpcResult::Error(-32602, errorMessage);
            }

            // Verify this is actually a shop/stall, not a ride
            const auto& descriptor = GetRideTypeDescriptor(rideLookup->ride->type);
            if (descriptor.Classification != RideClassification::shopOrStall &&
                descriptor.Classification != RideClassification::kioskOrFacility)
            {
                auto rideName = rideLookup->ride->getName();
                auto rideId = rideLookup->id.ToUnderlying();
                return RpcResult::Error(-32602,
                    "ID " + std::to_string(rideId) + " is a ride (" + rideName + "), not a shop/stall.\n" +
                    "Use 'rctctl rides price set --id " + std::to_string(rideId) + "' instead.");
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
                return RpcResult::Error(-32602, "price is required");
            }

            const bool secondary = GetBoolParam(params, "secondary").value_or(false);
            if (secondary && rideLookup->ride->getNumPrices() < 2)
            {
                return RpcResult::Error(-32602, "This shop does not have a secondary item");
            }

            money64 newPrice = ToMoney64FromGBP(*priceParam);
            const money64 previousPrice =
                secondary ? rideLookup->ride->price[1] : RideGetPrice(*rideLookup->ride);

            auto action = GameActions::RideSetPriceAction(rideLookup->id, newPrice, !secondary);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(-32000, BuildGameActionErrorMessage(result));
            }

            auto* updatedRide = GetRide(rideLookup->id);
            if (updatedRide == nullptr || updatedRide->id.IsNull())
            {
                return RpcResult::Error(-32000, "Shop price action succeeded but shop could not be retrieved");
            }

            // Get item label for the price we changed
            std::string itemLabel;
            const auto* rideEntry = updatedRide->getRideEntry();
            if (rideEntry != nullptr)
            {
                int targetIndex = secondary ? 1 : 0;
                if (targetIndex < static_cast<int>(std::size(rideEntry->shop_item)) &&
                    rideEntry->shop_item[targetIndex] != ShopItem::none)
                {
                    itemLabel = ShopItemToString(rideEntry->shop_item[targetIndex]);
                }
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["id"] = updatedRide->id.ToUnderlying();
            payload["name"] = updatedRide->getName();
            payload["price"] = MoneyToDouble(newPrice);
            payload["previousPrice"] = MoneyToDouble(previousPrice);
            payload["secondary"] = secondary;
            if (!itemLabel.empty())
            {
                payload["itemLabel"] = itemLabel;
            }

            auto rideLabel = BuildRideDisplayName(*updatedRide);
            std::string contextLabel = "Set ";
            if (!itemLabel.empty())
            {
                contextLabel += itemLabel + " ";
            }
            else if (secondary)
            {
                contextLabel += "secondary ";
            }
            contextLabel += "price for " + rideLabel + " to " + FormatMoneyString(newPrice);
            auto hint = MakeRideHint("shops.setPrice", *updatedRide, Telemetry::AIAgentRideWindowPage::Income, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        // ============================================================================
        // Shops Finances/Performance Handlers
        // ============================================================================

        enum class ShopFinanceOrderField
        {
            Profit,
            Income,
            RunningCost,
            TotalProfit,
        };

        enum class ShopPerformanceOrderField
        {
            Popularity,
            Satisfaction,
            TotalCustomers,
            Customers,
        };

        struct ShopFinanceQuery
        {
            ShopFinanceOrderField order = ShopFinanceOrderField::Profit;
            bool descending = true;
            std::optional<int32_t> limit;
        };

        struct ShopPerformanceQuery
        {
            ShopPerformanceOrderField order = ShopPerformanceOrderField::Popularity;
            bool descending = true;
            std::optional<int32_t> limit;
        };

        bool ParseShopFinanceOptions(const json_t& params, ShopFinanceQuery& query, std::string& errorMessage)
        {
            if (!params.is_object() && !params.is_null())
            {
                errorMessage = "Params must be a JSON object";
                return false;
            }
            if (auto orderStr = GetStringParam(params, "order"))
            {
                auto lower = ToLower(*orderStr);
                if (lower == "profit")
                    query.order = ShopFinanceOrderField::Profit;
                else if (lower == "income")
                    query.order = ShopFinanceOrderField::Income;
                else if (lower == "cost" || lower == "runningcost")
                    query.order = ShopFinanceOrderField::RunningCost;
                else if (lower == "totalprofit")
                    query.order = ShopFinanceOrderField::TotalProfit;
                else
                {
                    errorMessage = "Unknown order field: " + *orderStr;
                    return false;
                }
            }
            if (auto dir = GetStringParam(params, "direction"))
            {
                auto lower = ToLower(*dir);
                if (lower == "asc" || lower == "ascending")
                    query.descending = false;
                else if (lower == "desc" || lower == "descending")
                    query.descending = true;
                else
                {
                    errorMessage = "Unknown direction: " + *dir;
                    return false;
                }
            }
            query.limit = GetIntParam(params, "limit");
            return true;
        }

        bool ParseShopPerformanceOptions(const json_t& params, ShopPerformanceQuery& query, std::string& errorMessage)
        {
            if (!params.is_object() && !params.is_null())
            {
                errorMessage = "Params must be a JSON object";
                return false;
            }
            if (auto orderStr = GetStringParam(params, "order"))
            {
                auto lower = ToLower(*orderStr);
                if (lower == "popularity")
                    query.order = ShopPerformanceOrderField::Popularity;
                else if (lower == "satisfaction")
                    query.order = ShopPerformanceOrderField::Satisfaction;
                else if (lower == "totalcustomers")
                    query.order = ShopPerformanceOrderField::TotalCustomers;
                else if (lower == "customers")
                    query.order = ShopPerformanceOrderField::Customers;
                else
                {
                    errorMessage = "Unknown order field: " + *orderStr;
                    return false;
                }
            }
            if (auto dir = GetStringParam(params, "direction"))
            {
                auto lower = ToLower(*dir);
                if (lower == "asc" || lower == "ascending")
                    query.descending = false;
                else if (lower == "desc" || lower == "descending")
                    query.descending = true;
                else
                {
                    errorMessage = "Unknown direction: " + *dir;
                    return false;
                }
            }
            query.limit = GetIntParam(params, "limit");
            return true;
        }

        std::optional<Telemetry::AIAgentRideListColumn> MapFinanceOrderToColumn(ShopFinanceOrderField order)
        {
            switch (order)
            {
                case ShopFinanceOrderField::Profit:
                    return Telemetry::AIAgentRideListColumn::Profit;
                case ShopFinanceOrderField::Income:
                    return Telemetry::AIAgentRideListColumn::Income;
                case ShopFinanceOrderField::RunningCost:
                    return Telemetry::AIAgentRideListColumn::RunningCost;
                case ShopFinanceOrderField::TotalProfit:
                    return Telemetry::AIAgentRideListColumn::TotalProfit;
            }
            return std::nullopt;
        }

        std::optional<Telemetry::AIAgentRideListColumn> MapPerformanceOrderToColumn(ShopPerformanceOrderField order)
        {
            switch (order)
            {
                case ShopPerformanceOrderField::Popularity:
                    return Telemetry::AIAgentRideListColumn::Popularity;
                case ShopPerformanceOrderField::Satisfaction:
                    return Telemetry::AIAgentRideListColumn::Satisfaction;
                case ShopPerformanceOrderField::TotalCustomers:
                    return Telemetry::AIAgentRideListColumn::TotalCustomers;
                case ShopPerformanceOrderField::Customers:
                    return Telemetry::AIAgentRideListColumn::Customers;
            }
            return std::nullopt;
        }

        json_t BuildShopFinancePayload(const ShopFinanceQuery& query)
        {
            const auto& gameState = getGameState();
            std::vector<const Ride*> shops;
            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;
                const auto& descriptor = ride.getRideTypeDescriptor();
                if (descriptor.Classification != RideClassification::shopOrStall)
                    continue;
                shops.push_back(&ride);
            }

            auto getSortKey = [&](const Ride* r) -> int64_t {
                switch (query.order)
                {
                    case ShopFinanceOrderField::Profit:
                        return r->profit == kMoney64Undefined ? INT64_MIN : r->profit;
                    case ShopFinanceOrderField::Income:
                        return r->incomePerHour == kMoney64Undefined ? INT64_MIN : r->incomePerHour;
                    case ShopFinanceOrderField::RunningCost:
                        return r->upkeepCost == kMoney64Undefined ? INT64_MIN : r->upkeepCost;
                    case ShopFinanceOrderField::TotalProfit:
                        return r->totalProfit == kMoney64Undefined ? INT64_MIN : r->totalProfit;
                }
                return 0;
            };

            std::stable_sort(shops.begin(), shops.end(), [&](const Ride* a, const Ride* b) {
                auto keyA = getSortKey(a);
                auto keyB = getSortKey(b);
                return query.descending ? (keyA > keyB) : (keyA < keyB);
            });

            if (query.limit.has_value() && *query.limit > 0 && static_cast<size_t>(*query.limit) < shops.size())
            {
                shops.resize(*query.limit);
            }

            json_t entries = json_t::array();
            for (const auto* shop : shops)
            {
                json_t entry = json_t::object();
                entry["id"] = shop->id.ToUnderlying();
                entry["name"] = shop->getName();
                entry["status"] = RideStatusToString(shop->status);
                if (shop->profit == kMoney64Undefined)
                    entry["profit"] = nullptr;
                else
                    entry["profit"] = MoneyToDouble(shop->profit);
                if (shop->incomePerHour == kMoney64Undefined)
                    entry["income"] = nullptr;
                else
                    entry["income"] = MoneyToDouble(shop->incomePerHour);
                if (shop->upkeepCost == kMoney64Undefined)
                    entry["runningCost"] = nullptr;
                else
                    entry["runningCost"] = MoneyToDouble(shop->upkeepCost * 16);
                if (shop->totalProfit == kMoney64Undefined)
                    entry["totalProfit"] = nullptr;
                else
                    entry["totalProfit"] = MoneyToDouble(shop->totalProfit);
                entries.push_back(entry);
            }

            json_t payload = json_t::object();
            payload["shops"] = entries;
            payload["count"] = entries.size();
            return payload;
        }

        json_t BuildShopPerformancePayload(const ShopPerformanceQuery& query)
        {
            const auto& gameState = getGameState();
            std::vector<const Ride*> shops;
            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;
                const auto& descriptor = ride.getRideTypeDescriptor();
                if (descriptor.Classification != RideClassification::shopOrStall)
                    continue;
                shops.push_back(&ride);
            }

            auto getSortKey = [&](const Ride* r) -> int64_t {
                switch (query.order)
                {
                    case ShopPerformanceOrderField::Popularity:
                        return r->popularity == 255 ? -1 : r->popularity;
                    case ShopPerformanceOrderField::Satisfaction:
                        return r->satisfaction == 255 ? -1 : r->satisfaction;
                    case ShopPerformanceOrderField::TotalCustomers:
                        return r->totalCustomers;
                    case ShopPerformanceOrderField::Customers:
                        return RideCustomersPerHour(*r);
                }
                return 0;
            };

            std::stable_sort(shops.begin(), shops.end(), [&](const Ride* a, const Ride* b) {
                auto keyA = getSortKey(a);
                auto keyB = getSortKey(b);
                return query.descending ? (keyA > keyB) : (keyA < keyB);
            });

            if (query.limit.has_value() && *query.limit > 0 && static_cast<size_t>(*query.limit) < shops.size())
            {
                shops.resize(*query.limit);
            }

            json_t entries = json_t::array();
            for (const auto* shop : shops)
            {
                json_t entry = json_t::object();
                entry["id"] = shop->id.ToUnderlying();
                entry["name"] = shop->getName();
                entry["status"] = RideStatusToString(shop->status);
                if (shop->popularity == 255)
                    entry["popularity"] = nullptr;
                else
                    entry["popularity"] = static_cast<int>(shop->popularity * 4);
                if (shop->satisfaction == 255)
                    entry["satisfaction"] = nullptr;
                else
                    entry["satisfaction"] = static_cast<int>(shop->satisfaction * 5);
                entry["totalCustomers"] = shop->totalCustomers;
                entry["customersPerHour"] = RideCustomersPerHour(*shop);
                entries.push_back(entry);
            }

            json_t payload = json_t::object();
            payload["shops"] = entries;
            payload["count"] = entries.size();
            return payload;
        }

        RpcResult HandleShopFinances(const json_t& params)
        {
            ShopFinanceQuery query;
            std::string errorMessage;
            if (!ParseShopFinanceOptions(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildShopFinancePayload(query);
            auto column = MapFinanceOrderToColumn(query.order);
            auto hint = MakeRideListHint(
                "shops.finances",
                "Viewed shop financial summary",
                Telemetry::AIAgentRideListFilter::Shops,
                column,
                query.descending);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleShopPerformance(const json_t& params)
        {
            ShopPerformanceQuery query;
            std::string errorMessage;
            if (!ParseShopPerformanceOptions(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildShopPerformancePayload(query);
            auto column = MapPerformanceOrderToColumn(query.order);
            auto hint = MakeRideListHint(
                "shops.performance",
                "Viewed shop performance metrics",
                Telemetry::AIAgentRideListFilter::Shops,
                column,
                query.descending);
            return RpcResult::Ok(payload, std::move(hint));
        }

        // ============================================================================
        // Facilities Handlers (Kiosks, Toilets, ATMs, First Aid)
        // ============================================================================

        json_t BuildFacilityInstancePayload(const Ride& ride)
        {
            json_t node = json_t::object();
            node["id"] = ride.id.ToUnderlying();
            node["name"] = ride.getName();
            node["status"] = RideStatusToString(ride.status);
            node["price"] = MoneyToDouble(RideGetPrice(ride));
            node["queueLength"] = ride.getTotalQueueLength();
            node["customersInterval"] = ride.curNumCustomers;
            node["totalCustomers"] = ride.totalCustomers;
            node["incomePerHour"] = MoneyToDouble(ride.incomePerHour);
            node["profitThisMonth"] = MoneyToDouble(ride.profit);
            node["totalProfit"] = MoneyToDouble(ride.totalProfit);

            const auto& descriptor = ride.getRideTypeDescriptor();
            node["rideType"] = descriptor.Name.empty() ? std::string("facility") : std::string(descriptor.Name);
            node["classification"] = std::string(RideClassificationToString(descriptor.Classification));
            node["category"] = std::string(RideCategoryToString(descriptor.Category));

            if (auto tile = BuildRideTilePayload(ride))
            {
                node["tile"] = *tile;
            }

            return node;
        }

        RpcResult HandleFacilitiesList(const json_t& params)
        {
            if (!params.is_object() && !params.is_null())
            {
                return RpcResult::Error(-32602, "Params must be a JSON object");
            }

            const auto& gameState = getGameState();
            json_t entries = json_t::array();
            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;

                const auto& descriptor = ride.getRideTypeDescriptor();
                if (descriptor.Classification != RideClassification::kioskOrFacility)
                    continue;

                entries.push_back(BuildFacilityInstancePayload(ride));
            }

            json_t payload = json_t::object();
            payload["facilities"] = entries;
            payload["count"] = entries.size();
            auto hint = MakeRideListHint("facilities.list", "Opened facilities list", Telemetry::AIAgentRideListFilter::Facilities);
            return RpcResult::Ok(payload, std::move(hint));
        }

        // Facilities use the same finance/performance enums as shops (limited columns)
        using FacilityFinanceQuery = ShopFinanceQuery;
        using FacilityPerformanceQuery = ShopPerformanceQuery;

        json_t BuildFacilityFinancePayload(const FacilityFinanceQuery& query)
        {
            const auto& gameState = getGameState();
            std::vector<const Ride*> facilities;
            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;
                const auto& descriptor = ride.getRideTypeDescriptor();
                if (descriptor.Classification != RideClassification::kioskOrFacility)
                    continue;
                facilities.push_back(&ride);
            }

            auto getSortKey = [&](const Ride* r) -> int64_t {
                switch (query.order)
                {
                    case ShopFinanceOrderField::Profit:
                        return r->profit == kMoney64Undefined ? INT64_MIN : r->profit;
                    case ShopFinanceOrderField::Income:
                        return r->incomePerHour == kMoney64Undefined ? INT64_MIN : r->incomePerHour;
                    case ShopFinanceOrderField::RunningCost:
                        return r->upkeepCost == kMoney64Undefined ? INT64_MIN : r->upkeepCost;
                    case ShopFinanceOrderField::TotalProfit:
                        return r->totalProfit == kMoney64Undefined ? INT64_MIN : r->totalProfit;
                }
                return 0;
            };

            std::stable_sort(facilities.begin(), facilities.end(), [&](const Ride* a, const Ride* b) {
                auto keyA = getSortKey(a);
                auto keyB = getSortKey(b);
                return query.descending ? (keyA > keyB) : (keyA < keyB);
            });

            if (query.limit.has_value() && *query.limit > 0 && static_cast<size_t>(*query.limit) < facilities.size())
            {
                facilities.resize(*query.limit);
            }

            json_t entries = json_t::array();
            for (const auto* facility : facilities)
            {
                json_t entry = json_t::object();
                entry["id"] = facility->id.ToUnderlying();
                entry["name"] = facility->getName();
                entry["status"] = RideStatusToString(facility->status);
                if (facility->profit == kMoney64Undefined)
                    entry["profit"] = nullptr;
                else
                    entry["profit"] = MoneyToDouble(facility->profit);
                if (facility->incomePerHour == kMoney64Undefined)
                    entry["income"] = nullptr;
                else
                    entry["income"] = MoneyToDouble(facility->incomePerHour);
                if (facility->upkeepCost == kMoney64Undefined)
                    entry["runningCost"] = nullptr;
                else
                    entry["runningCost"] = MoneyToDouble(facility->upkeepCost * 16);
                if (facility->totalProfit == kMoney64Undefined)
                    entry["totalProfit"] = nullptr;
                else
                    entry["totalProfit"] = MoneyToDouble(facility->totalProfit);
                entries.push_back(entry);
            }

            json_t payload = json_t::object();
            payload["facilities"] = entries;
            payload["count"] = entries.size();
            return payload;
        }

        json_t BuildFacilityPerformancePayload(const FacilityPerformanceQuery& query)
        {
            const auto& gameState = getGameState();
            std::vector<const Ride*> facilities;
            for (size_t i = 0; i < gameState.ridesEndOfUsedRange; ++i)
            {
                const auto& ride = gameState.rides[i];
                if (ride.id.IsNull())
                    continue;
                const auto& descriptor = ride.getRideTypeDescriptor();
                if (descriptor.Classification != RideClassification::kioskOrFacility)
                    continue;
                facilities.push_back(&ride);
            }

            auto getSortKey = [&](const Ride* r) -> int64_t {
                switch (query.order)
                {
                    case ShopPerformanceOrderField::Popularity:
                        return r->popularity == 255 ? -1 : r->popularity;
                    case ShopPerformanceOrderField::Satisfaction:
                        return r->satisfaction == 255 ? -1 : r->satisfaction;
                    case ShopPerformanceOrderField::TotalCustomers:
                        return r->totalCustomers;
                    case ShopPerformanceOrderField::Customers:
                        return RideCustomersPerHour(*r);
                }
                return 0;
            };

            std::stable_sort(facilities.begin(), facilities.end(), [&](const Ride* a, const Ride* b) {
                auto keyA = getSortKey(a);
                auto keyB = getSortKey(b);
                return query.descending ? (keyA > keyB) : (keyA < keyB);
            });

            if (query.limit.has_value() && *query.limit > 0 && static_cast<size_t>(*query.limit) < facilities.size())
            {
                facilities.resize(*query.limit);
            }

            json_t entries = json_t::array();
            for (const auto* facility : facilities)
            {
                json_t entry = json_t::object();
                entry["id"] = facility->id.ToUnderlying();
                entry["name"] = facility->getName();
                entry["status"] = RideStatusToString(facility->status);
                if (facility->popularity == 255)
                    entry["popularity"] = nullptr;
                else
                    entry["popularity"] = static_cast<int>(facility->popularity * 4);
                if (facility->satisfaction == 255)
                    entry["satisfaction"] = nullptr;
                else
                    entry["satisfaction"] = static_cast<int>(facility->satisfaction * 5);
                entry["totalCustomers"] = facility->totalCustomers;
                entry["customersPerHour"] = RideCustomersPerHour(*facility);
                entries.push_back(entry);
            }

            json_t payload = json_t::object();
            payload["facilities"] = entries;
            payload["count"] = entries.size();
            return payload;
        }

        RpcResult HandleFacilityFinances(const json_t& params)
        {
            FacilityFinanceQuery query;
            std::string errorMessage;
            if (!ParseShopFinanceOptions(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildFacilityFinancePayload(query);
            auto column = MapFinanceOrderToColumn(query.order);
            auto hint = MakeRideListHint(
                "facilities.finances",
                "Viewed facility financial summary",
                Telemetry::AIAgentRideListFilter::Facilities,
                column,
                query.descending);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleFacilityPerformance(const json_t& params)
        {
            FacilityPerformanceQuery query;
            std::string errorMessage;
            if (!ParseShopPerformanceOptions(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            auto payload = BuildFacilityPerformancePayload(query);
            auto column = MapPerformanceOrderToColumn(query.order);
            auto hint = MakeRideListHint(
                "facilities.performance",
                "Viewed facility performance metrics",
                Telemetry::AIAgentRideListFilter::Facilities,
                column,
                query.descending);
            return RpcResult::Ok(payload, std::move(hint));
        }

        // Static registration
        struct ShopHandlerRegistrar
        {
            ShopHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("shops.catalog", HandleShopsCatalog);
                registry.Register("shops.list", HandleShopsList);
                registry.Register("shops.place", HandleShopPlace);
                registry.Register("shops.remove", HandleShopRemove);
                registry.Register("shops.setStatus", HandleShopSetStatus);
                registry.Register("shops.getPrice", HandleShopGetPrice);
                registry.Register("shops.setPrice", HandleShopSetPrice);
                registry.Register("shops.finances", HandleShopFinances);
                registry.Register("shops.performance", HandleShopPerformance);
                // Facilities (kiosks, toilets, ATMs)
                registry.Register("facilities.list", HandleFacilitiesList);
                registry.Register("facilities.finances", HandleFacilityFinances);
                registry.Register("facilities.performance", HandleFacilityPerformance);
            }
        } shopRegistrar;

    } // namespace

    void InitShopHandlers()
    {
        (void)shopRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
