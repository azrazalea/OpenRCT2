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
#include "../../../actions/PeepPickupAction.h"
#include "../../../entity/EntityList.h"
#include "../../../entity/Guest.h"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatting.h"
#include "../../../localisation/StringIdType.h"
#include "../../../network/Network.h"
#include "../../../object/ObjectManager.h"
#include "../../../ride/Ride.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../world/Location.hpp"
#include "../../../world/Map.h"
#include "../../../world/MapLimits.h"
#include "../../../world/tile_element/SurfaceElement.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For kError* constants

    namespace
    {
        std::string_view PeepStateToString(PeepState state)
        {
            switch (state)
            {
                case PeepState::falling:
                    return "falling";
                case PeepState::one:
                    return "walking";
                case PeepState::queuingFront:
                    return "queuing";
                case PeepState::onRide:
                    return "onRide";
                case PeepState::leavingRide:
                    return "leavingRide";
                case PeepState::walking:
                    return "walking";
                case PeepState::queuing:
                    return "queuing";
                case PeepState::enteringRide:
                    return "enteringRide";
                case PeepState::sitting:
                    return "sitting";
                case PeepState::picked:
                    return "picked";
                case PeepState::patrolling:
                    return "patrolling";
                case PeepState::mowing:
                    return "mowing";
                case PeepState::sweeping:
                    return "sweeping";
                case PeepState::enteringPark:
                    return "enteringPark";
                case PeepState::leavingPark:
                    return "leavingPark";
                case PeepState::answering:
                    return "answering";
                case PeepState::fixing:
                    return "fixing";
                case PeepState::buying:
                    return "buying";
                case PeepState::watching:
                    return "watching";
                case PeepState::emptyingBin:
                    return "emptyingBin";
                case PeepState::usingBin:
                    return "usingBin";
                case PeepState::watering:
                    return "watering";
                case PeepState::headingToInspection:
                    return "headingToInspection";
                case PeepState::inspecting:
                    return "inspecting";
                default:
                    return "unknown";
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
                case PeepThoughtType::Photo2:
                case PeepThoughtType::Photo3:
                case PeepThoughtType::Photo4:
                case PeepThoughtType::Umbrella:
                    return "is really good value";
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
                case PeepThoughtType::BalloonMuch:
                case PeepThoughtType::ToyMuch:
                case PeepThoughtType::MapMuch:
                case PeepThoughtType::PhotoMuch:
                case PeepThoughtType::Photo2Much:
                case PeepThoughtType::Photo3Much:
                case PeepThoughtType::Photo4Much:
                case PeepThoughtType::UmbrellaMuch:
                case PeepThoughtType::DrinkMuch:
                case PeepThoughtType::BurgerMuch:
                case PeepThoughtType::ChipsMuch:
                case PeepThoughtType::IceCreamMuch:
                case PeepThoughtType::CandyflossMuch:
                case PeepThoughtType::PizzaMuch:
                case PeepThoughtType::PopcornMuch:
                case PeepThoughtType::HotDogMuch:
                case PeepThoughtType::TentacleMuch:
                case PeepThoughtType::HatMuch:
                case PeepThoughtType::ToffeeAppleMuch:
                case PeepThoughtType::TshirtMuch:
                case PeepThoughtType::DoughnutMuch:
                case PeepThoughtType::CoffeeMuch:
                case PeepThoughtType::ChickenMuch:
                case PeepThoughtType::LemonadeMuch:
                case PeepThoughtType::PretzelMuch:
                case PeepThoughtType::HotChocolateMuch:
                case PeepThoughtType::IcedTeaMuch:
                case PeepThoughtType::FunnelCakeMuch:
                case PeepThoughtType::SunglassesMuch:
                case PeepThoughtType::BeefNoodlesMuch:
                case PeepThoughtType::FriedRiceNoodlesMuch:
                case PeepThoughtType::WontonSoupMuch:
                case PeepThoughtType::MeatballSoupMuch:
                case PeepThoughtType::FruitJuiceMuch:
                case PeepThoughtType::SoybeanMilkMuch:
                case PeepThoughtType::SujongkwaMuch:
                case PeepThoughtType::SubSandwichMuch:
                case PeepThoughtType::CookieMuch:
                case PeepThoughtType::RoastSausageMuch:
                    return "I'm not paying that much for";
                case PeepThoughtType::HereWeAre:
                    return "...and here we are on";
                case PeepThoughtType::Help:
                    return "Help!";
                case PeepThoughtType::RunningOut:
                    return "I'm running out of cash";
                case PeepThoughtType::NewRide:
                    return "Wow! A new ride!";
                case PeepThoughtType::NiceRideDeprecated:
                    return "Nice ride";
                case PeepThoughtType::ExcitedDeprecated:
                    return "I'm so excited - It's an Intamin ride!";
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

        bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle)
        {
            if (needle.empty())
            {
                return true;
            }
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                [](char lhs, char rhs) {
                    return std::tolower(static_cast<unsigned char>(lhs))
                        == std::tolower(static_cast<unsigned char>(rhs));
                });
            return it != haystack.end();
        }

        bool EqualsCaseInsensitive(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            return std::equal(a.begin(), a.end(), b.begin(),
                [](char lhs, char rhs) {
                    return std::tolower(static_cast<unsigned char>(lhs))
                        == std::tolower(static_cast<unsigned char>(rhs));
                });
        }

        Guest* FindGuestByName(const std::string& name)
        {
            for (auto guest : EntityList<Guest>())
            {
                if (guest != nullptr && EqualsCaseInsensitive(guest->GetName(), name))
                {
                    return guest;
                }
            }
            return nullptr;
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

        Guest* FindGuestById(int32_t id)
        {
            if (id < 0)
            {
                return nullptr;
            }
            for (auto guest : EntityList<Guest>())
            {
                if (guest != nullptr && guest->Id.ToUnderlying() == static_cast<uint16_t>(id))
                {
                    return guest;
                }
            }
            return nullptr;
        }

        std::string BuildRideDisplayName(const Ride& ride)
        {
            Formatter ft;
            ride.formatNameTo(ft);
            char buffer[256]{};
            FormatStringLegacy(buffer, sizeof(buffer), STR_STRINGID, ft.Data());
            return std::string(buffer);
        }

        std::optional<int32_t> ResolvePlacementHeight(const json_t& params, const TileCoordsXY& tile, std::string& errorMessage)
        {
            if (auto explicitZ = GetIntParam(params, "z"))
            {
                // User input is in tile units; convert to world units and align
                return OpenRCT2::Numerics::floor2(TileZToWorldZ(*explicitZ), kCoordsZStep);
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
            return surface->GetBaseZ();
        }

        std::optional<CoordsXYZ> BuildTileCameraTarget(const TileCoordsXY& tile, int32_t width = 1, int32_t height = 1)
        {
            auto anchor = tile.ToCoordsXY();
            anchor.x += width * kCoordsXYHalfTile;
            anchor.y += height * kCoordsXYHalfTile;
            auto z = TileElementHeight(anchor);
            return CoordsXYZ{ anchor.x, anchor.y, z };
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

        Telemetry::AIAgentFollowHint MakeGuestHint(std::string_view method, const Guest& guest, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            auto coords = guest.GetLocation();
            if (!coords.IsNull())
            {
                hint.cameraTarget = coords;
            }
            Telemetry::GuestWindowIntent intent;
            intent.guestId = guest.Id;
            hint.windowIntent = intent;
            return hint;
        }

        Telemetry::AIAgentFollowHint MakeGuestListHint(std::string_view method, std::string contextLabel)
        {
            auto hint = MakeGenericWindowHint(method, std::move(contextLabel), WindowClass::guestList, std::nullopt);
            hint.requestCameraFocus = false;
            return hint;
        }

        Telemetry::AIAgentFollowHint MakeTileHint(
            std::string_view method, std::string contextLabel, const TileCoordsXY& tile, WindowClass windowClass,
            int32_t width = 1, int32_t height = 1)
        {
            auto camera = BuildTileCameraTarget(tile, width, height);
            return MakeGenericWindowHint(method, std::move(contextLabel), windowClass, camera);
        }

        json_t BuildGuestThoughtsPayload(const Guest& guest, size_t limit)
        {
            json_t thoughts = json_t::array();
            for (size_t i = 0; i < kPeepMaxThoughts && i < limit; ++i)
            {
                const auto& thought = guest.Thoughts[i];
                if (thought.freshness == 0)
                {
                    continue;
                }
                json_t node = json_t::object();
                node["type"] = PeepThoughtTypeToString(thought.type);
                node["text"] = FormatThoughtText(thought);
                node["freshness"] = thought.freshness;
                node["arguments"] = thought.item;
                if (!thought.rideId.IsNull())
                {
                    node["rideId"] = thought.rideId.ToUnderlying();
                    if (auto* ride = GetRide(thought.rideId))
                    {
                        node["rideName"] = BuildRideDisplayName(*ride);
                    }
                }
                thoughts.push_back(node);
            }
            return thoughts;
        }

        json_t BuildGuestPayload(const Guest& guest, bool includeThoughts)
        {
            auto loc = guest.GetLocation();
            const int32_t tileX = loc.x / kCoordsXYStep;
            const int32_t tileY = loc.y / kCoordsXYStep;

            json_t node = json_t::object();
            node["id"] = guest.Id.ToUnderlying();
            node["name"] = guest.GetName();
            node["state"] = PeepStateToString(guest.State);
            node["coords"] = json_t::object({ { "x", tileX }, { "y", tileY }, { "z", WorldZToTileZ(loc.z) } });

            json_t needs = json_t::object();
            needs["happiness"] = guest.Happiness;
            needs["hunger"] = guest.Hunger;
            needs["thirst"] = guest.Thirst;
            needs["nausea"] = guest.Nausea;
            needs["energy"] = guest.Energy;
            needs["toilet"] = guest.Toilet;
            node["needs"] = needs;

            json_t wallet = json_t::object();
            wallet["cash"] = MoneyToDouble(guest.CashInPocket);
            wallet["spent"] = MoneyToDouble(guest.CashSpent);
            wallet["paidOnRides"] = MoneyToDouble(guest.PaidOnRides);
            wallet["paidOnFood"] = MoneyToDouble(guest.PaidOnFood);
            wallet["paidOnDrink"] = MoneyToDouble(guest.PaidOnDrink);
            wallet["paidOnSouvenirs"] = MoneyToDouble(guest.PaidOnSouvenirs);
            node["wallet"] = wallet;

            node["favoriteRide"] = guest.FavouriteRide.IsNull() ? -1 : guest.FavouriteRide.ToUnderlying();
            node["previousRide"] = guest.PreviousRide.IsNull() ? -1 : guest.PreviousRide.ToUnderlying();
            node["headingToRide"] = guest.GuestHeadingToRideId.IsNull() ? -1 : guest.GuestHeadingToRideId.ToUnderlying();
            node["timeInQueue"] = guest.TimeInQueue;
            node["outsidePark"] = guest.OutsideOfPark;

            if (includeThoughts)
            {
                node["thoughts"] = BuildGuestThoughtsPayload(guest, kPeepMaxThoughts);
            }

            return node;
        }

        json_t BuildGuestSamplePayload(const Guest& guest)
        {
            json_t sample = json_t::object();
            sample["id"] = guest.Id.ToUnderlying();
            sample["name"] = guest.GetName();
            return sample;
        }

        RpcResult HandleGuestsList(const json_t& params)
        {
            size_t limit = params.is_object() ? ExtractLimitParam(params) : 64;
            if (limit == 0)
            {
                limit = 64;
            }
            const int32_t afterCursor = params.is_object() ? GetIntParam(params, "after").value_or(-1) : -1;

            // Collect and sort all in-park guests by ID
            std::vector<Guest*> allGuests;
            for (auto guest : EntityList<Guest>())
            {
                if (guest == nullptr || guest->OutsideOfPark)
                {
                    continue;
                }
                allGuests.push_back(guest);
            }
            std::sort(allGuests.begin(), allGuests.end(), [](const Guest* lhs, const Guest* rhs) {
                return lhs->Id.ToUnderlying() < rhs->Id.ToUnderlying();
            });

            json_t guests = json_t::array();
            size_t emitted = 0;
            bool hasMore = false;
            int32_t nextCursor = -1;
            for (auto* guest : allGuests)
            {
                if (afterCursor >= 0 && guest->Id.ToUnderlying() <= static_cast<uint16_t>(afterCursor))
                {
                    continue;
                }
                if (emitted >= limit)
                {
                    hasMore = true;
                    break;
                }
                guests.push_back(BuildGuestPayload(*guest, false));
                nextCursor = guest->Id.ToUnderlying();
                emitted++;
            }

            json_t payload = json_t::object();
            payload["guests"] = guests;
            payload["returned"] = emitted;
            payload["limit"] = limit;
            payload["hasMore"] = hasMore;
            if (hasMore && nextCursor >= 0)
            {
                payload["nextCursor"] = nextCursor;
            }
            payload["totalGuests"] = getGameState().park.numGuestsInPark;
            auto hint = MakeGuestListHint("guests.list", "Listed guests");
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleGuestsGet(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }
            Guest* guest = nullptr;
            auto idParam = GetIntParam(params, "id");
            auto nameParam = GetStringParam(params, "name");
            if (idParam)
            {
                guest = FindGuestById(*idParam);
                if (guest == nullptr)
                {
                    return RpcResult::Error(kErrorNotFound, "Guest not found with id " + std::to_string(*idParam));
                }
            }
            else if (nameParam)
            {
                guest = FindGuestByName(*nameParam);
                if (guest == nullptr)
                {
                    return RpcResult::Error(kErrorNotFound, "Guest not found with name '" + *nameParam + "'");
                }
            }
            else
            {
                return RpcResult::Error(kErrorInvalidParams, "id or name is required");
            }
            auto payload = BuildGuestPayload(*guest, true);
            std::string guestLabel = guest->GetName();
            if (guestLabel.empty())
            {
                guestLabel = "Guest " + std::to_string(guest->Id.ToUnderlying());
            }
            else
            {
                guestLabel = "Guest " + guestLabel;
            }
            auto hint = MakeGuestHint("guests.get", *guest, std::move(guestLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleGuestsSearch(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            const std::string nameFilter = GetStringParam(params, "name").value_or(std::string());
            const bool includeOutside = GetBoolParam(params, "includeOutside").value_or(false);
            const int32_t afterCursor = GetIntParam(params, "after").value_or(-1);
            const size_t limit = std::max<size_t>(1, GetIntParam(params, "limit").value_or(50));

            std::optional<TileCoordsXY> center;
            if (auto xParam = GetIntParam(params, "x"))
            {
                auto yParam = GetIntParam(params, "y");
                if (!yParam)
                {
                    return RpcResult::Error(kErrorInvalidParams, "y is required when x is supplied");
                }
                center = TileCoordsXY{ *xParam, *yParam };
            }
            int32_t radius = GetIntParam(params, "radius").value_or(0);
            if (!center && radius != 0)
            {
                return RpcResult::Error(kErrorInvalidParams, "radius requires x and y");
            }

            std::vector<Guest*> matches;
            for (auto guest : EntityList<Guest>())
            {
                if (guest == nullptr)
                {
                    continue;
                }
                if (!includeOutside && guest->OutsideOfPark)
                {
                    continue;
                }
                if (!nameFilter.empty() && !ContainsCaseInsensitive(guest->GetName(), nameFilter))
                {
                    continue;
                }
                if (center)
                {
                    TileCoordsXY guestTile{ guest->x / kCoordsXYStep, guest->y / kCoordsXYStep };
                    const int32_t dx = guestTile.x - center->x;
                    const int32_t dy = guestTile.y - center->y;
                    if (std::max(std::abs(dx), std::abs(dy)) > radius)
                    {
                        continue;
                    }
                }
                matches.push_back(guest);
            }

            std::sort(matches.begin(), matches.end(), [](const Guest* lhs, const Guest* rhs) {
                return lhs->Id.ToUnderlying() < rhs->Id.ToUnderlying();
            });

            json_t guests = json_t::array();
            size_t emitted = 0;
            bool hasMore = false;
            int32_t nextCursor = -1;
            for (auto* guest : matches)
            {
                if (afterCursor >= 0 && guest->Id.ToUnderlying() <= static_cast<uint16_t>(afterCursor))
                {
                    continue;
                }
                if (emitted >= limit)
                {
                    hasMore = true;
                    break;
                }
                guests.push_back(BuildGuestPayload(*guest, false));
                nextCursor = guest->Id.ToUnderlying();
                emitted++;
            }

            json_t payload = json_t::object();
            payload["guests"] = guests;
            payload["matchedGuests"] = matches.size();
            payload["limit"] = limit;
            payload["hasMore"] = hasMore;
            if (hasMore && nextCursor >= 0)
            {
                payload["nextCursor"] = nextCursor;
            }
            payload["totalGuests"] = getGameState().park.numGuestsInPark;
            std::optional<Telemetry::AIAgentFollowHint> hint;
            if (!matches.empty())
            {
                auto* focusGuest = matches.front();
                std::string label = focusGuest->GetName();
                if (label.empty())
                {
                    label = "Guest " + std::to_string(focusGuest->Id.ToUnderlying());
                }
                else
                {
                    label = "Guest " + label;
                }
                hint = MakeGuestHint("guests.search", *focusGuest, std::move(label));
            }
            else if (center)
            {
                std::string label = "Searched guests near (" + std::to_string(center->x) + "," + std::to_string(center->y) + ")";
                hint = MakeTileHint("guests.search", std::move(label), *center, WindowClass::map);
            }
            else
            {
                hint = MakeGuestListHint("guests.search", "Searched guests");
            }
            return RpcResult::Ok(payload, std::move(*hint));
        }

        enum class GuestThoughtOrderField
        {
            Count,
            Text,
            Ride,
        };

        struct GuestThoughtsQuery
        {
            size_t limit = 0;
            bool limitEnabled = false;
            size_t offset = 0;
            int32_t guestLimit = 5;
            GuestThoughtOrderField order = GuestThoughtOrderField::Count;
            bool descending = true;
            bool directionSpecified = false;
            bool rideOnly = false;
        };

        bool ParseGuestThoughtsQuery(const json_t& params, GuestThoughtsQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
            {
                return true;
            }
            query.limit = ExtractLimitParam(params);
            query.limitEnabled = query.limit != 0;
            if (auto offset = GetIntParam(params, "offset"))
            {
                query.offset = static_cast<size_t>(std::max(0, *offset));
            }
            if (auto guestLimit = GetIntParam(params, "guestLimit"))
            {
                if (*guestLimit <= 0)
                {
                    errorMessage = "guestLimit must be positive";
                    return false;
                }
                query.guestLimit = *guestLimit;
            }
            if (auto orderParam = GetStringParam(params, "order"))
            {
                auto lowered = ToLower(*orderParam);
                if (lowered == "count")
                {
                    query.order = GuestThoughtOrderField::Count;
                }
                else if (lowered == "text" || lowered == "label")
                {
                    query.order = GuestThoughtOrderField::Text;
                }
                else if (lowered == "ride")
                {
                    query.order = GuestThoughtOrderField::Ride;
                }
                else
                {
                    errorMessage = "Unknown order (use count, text, or ride)";
                    return false;
                }
                if (!query.directionSpecified && query.order == GuestThoughtOrderField::Count)
                {
                    query.descending = true;
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
            if (auto rideOnly = GetBoolParam(params, "rideOnly"))
            {
                query.rideOnly = *rideOnly;
            }
            return true;
        }

        RpcResult HandleGuestsThoughtsSummary(const json_t& params)
        {
            GuestThoughtsQuery query;
            std::string errorMessage;
            if (!ParseGuestThoughtsQuery(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            const size_t limit = query.limitEnabled ? query.limit : 0;
            const int32_t guestLimit = query.guestLimit;

            struct ThoughtGroup
            {
                int32_t count = 0;
                std::string text;
                RideId rideId{ RideId::GetNull() };
                std::string rideName;
                std::vector<json_t> samples;
                std::unordered_set<EntityId::UnderlyingType> seenGuests;
            };

            std::unordered_map<std::string, ThoughtGroup> groups;
            size_t considered = 0;
            for (auto guest : EntityList<Guest>())
            {
                if (guest == nullptr || guest->OutsideOfPark)
                {
                    continue;
                }
                considered++;
                for (const auto& thought : guest->Thoughts)
                {
                    if (thought.freshness == 0)
                    {
                        continue;
                    }
                    // Skip thoughts with empty text (e.g., PeepThoughtType::None)
                    auto thoughtText = FormatThoughtText(thought);
                    if (thoughtText.empty())
                    {
                        continue;
                    }
                    std::string key = std::to_string(static_cast<int32_t>(thought.type));
                    if (!thought.rideId.IsNull())
                    {
                        key += '#' + std::to_string(thought.rideId.ToUnderlying());
                    }
                    else if (ThoughtNeedsShopItem(thought.type) && thought.shopItem != ShopItem::none)
                    {
                        // Include shop item in key to group by specific item
                        key += '@' + std::to_string(static_cast<int32_t>(thought.shopItem));
                    }

                    auto& group = groups[key];
                    if (group.text.empty())
                    {
                        group.text = thoughtText;
                    }
                    if (!thought.rideId.IsNull())
                    {
                        group.rideId = thought.rideId;
                        if (group.rideName.empty())
                        {
                            if (auto* ride = GetRide(thought.rideId))
                            {
                                group.rideName = BuildRideDisplayName(*ride);
                            }
                        }
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
                }
            }

            std::vector<std::pair<std::string, ThoughtGroup>> ordered;
            ordered.reserve(groups.size());
            for (auto& entry : groups)
            {
                if (query.rideOnly && entry.second.rideId.IsNull())
                {
                    continue;
                }
                ordered.push_back(entry);
            }

            auto comparator = [&](const auto& lhs, const auto& rhs) {
                const auto& left = lhs.second;
                const auto& right = rhs.second;
                switch (query.order)
                {
                    case GuestThoughtOrderField::Count:
                        if (left.count != right.count)
                        {
                            return query.descending ? left.count > right.count : left.count < right.count;
                        }
                        break;
                    case GuestThoughtOrderField::Text:
                    {
                        int cmp = CompareCaseInsensitive(left.text, right.text);
                        if (cmp != 0)
                        {
                            return query.descending ? cmp > 0 : cmp < 0;
                        }
                        break;
                    }
                    case GuestThoughtOrderField::Ride:
                    {
                        int cmp = CompareCaseInsensitive(left.rideName, right.rideName);
                        if (cmp != 0)
                        {
                            return query.descending ? cmp > 0 : cmp < 0;
                        }
                        break;
                    }
                }
                if (left.count != right.count)
                {
                    return left.count > right.count;
                }
                return CompareCaseInsensitive(lhs.first, rhs.first) < 0;
            };

            std::sort(ordered.begin(), ordered.end(), comparator);

            // Count total valid groups (excluding empty text entries)
            size_t totalGroups = 0;
            for (const auto& entry : ordered)
            {
                if (!entry.second.text.empty())
                {
                    totalGroups++;
                }
            }

            const size_t offset = query.offset;
            json_t jsonGroups = json_t::array();
            size_t skipped = 0;
            size_t emitted = 0;
            for (const auto& entry : ordered)
            {
                // Skip entries with empty thought text (e.g., PeepThoughtType::None)
                if (entry.second.text.empty())
                {
                    continue;
                }
                // Handle offset - skip entries before offset
                if (skipped < offset)
                {
                    skipped++;
                    continue;
                }
                // Stop if we've reached the limit
                if (limit != 0 && emitted >= limit)
                {
                    break;
                }
                json_t group = json_t::object();
                group["key"] = entry.first;
                group["text"] = entry.second.text;
                group["count"] = entry.second.count;
                group["guestSamples"] = entry.second.samples;
                if (!entry.second.rideId.IsNull())
                {
                    group["rideId"] = entry.second.rideId.ToUnderlying();
                    if (auto* ride = GetRide(entry.second.rideId))
                    {
                        group["rideName"] = BuildRideDisplayName(*ride);
                    }
                }
                jsonGroups.push_back(group);
                emitted++;
            }

            bool hasMore = (offset + emitted) < totalGroups;
            json_t payload = json_t::object();
            payload["groups"] = jsonGroups;
            payload["totalGroups"] = totalGroups;
            payload["offset"] = offset;
            payload["hasMore"] = hasMore;
            if (hasMore)
            {
                payload["nextOffset"] = offset + emitted;
            }
            payload["consideredGuests"] = considered;
            payload["totalGuests"] = getGameState().park.numGuestsInPark;
            auto hint = MakeGuestListHint("guests.thoughts", "Reviewed guest thoughts");
            return RpcResult::Ok(payload, std::move(hint));
        }

        enum class GuestMoodOrderField
        {
            Count,
            Average,
            Label,
        };

        struct GuestMoodQuery
        {
            size_t limit = 0;
            bool limitEnabled = false;
            int32_t guestLimit = 5;
            GuestMoodOrderField order = GuestMoodOrderField::Count;
            bool descending = true;
            bool directionSpecified = false;
            std::vector<std::string> bands;
        };

        bool ParseGuestMoodQuery(const json_t& params, GuestMoodQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
            {
                return true;
            }
            query.limit = ExtractLimitParam(params);
            query.limitEnabled = query.limit != 0;
            if (auto guestLimit = GetIntParam(params, "guestLimit"))
            {
                if (*guestLimit <= 0)
                {
                    errorMessage = "guestLimit must be positive";
                    return false;
                }
                query.guestLimit = *guestLimit;
            }
            if (auto orderParam = GetStringParam(params, "order"))
            {
                auto lowered = ToLower(*orderParam);
                if (lowered == "count")
                {
                    query.order = GuestMoodOrderField::Count;
                }
                else if (lowered == "avg" || lowered == "average")
                {
                    query.order = GuestMoodOrderField::Average;
                }
                else if (lowered == "label")
                {
                    query.order = GuestMoodOrderField::Label;
                }
                else
                {
                    errorMessage = "Unknown order (use count, avg, or label)";
                    return false;
                }
                if (!query.directionSpecified && query.order == GuestMoodOrderField::Count)
                {
                    query.descending = true;
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
            if (auto bandsParam = params.find("bands"); bandsParam != params.end())
            {
                if (!bandsParam->is_array())
                {
                    errorMessage = "bands must be an array";
                    return false;
                }
                for (const auto& entry : *bandsParam)
                {
                    if (!entry.is_string())
                    {
                        continue;
                    }
                    query.bands.push_back(ToLower(entry.get<std::string>()));
                }
            }
            return true;
        }

        RpcResult HandleGuestsMoodSummary(const json_t& params)
        {
            GuestMoodQuery query;
            std::string errorMessage;
            if (!ParseGuestMoodQuery(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }
            const int32_t guestLimit = query.guestLimit;

            struct MoodBand
            {
                int32_t min;
                int32_t max;
                const char* key;
                const char* label;
            };

            static constexpr MoodBand kMoodBands[] = {
                { 230, 255, "ecstatic", "Ecstatic" },
                { 190, 229, "happy", "Happy" },
                { 140, 189, "content", "Content" },
                { 90, 139, "meh", "Meh" },
                { 40, 89, "upset", "Upset" },
                { 0, 39, "furious", "Furious" },
            };

            struct MoodGroup
            {
                int32_t count = 0;
                int32_t total = 0;
                std::vector<json_t> samples;
            };

            std::array<MoodGroup, std::size(kMoodBands)> bands{};
            size_t considered = 0;

            for (auto guest : EntityList<Guest>())
            {
                if (guest == nullptr || guest->OutsideOfPark)
                {
                    continue;
                }
                considered++;
                const int32_t happiness = guest->Happiness;
                for (size_t i = 0; i < std::size(kMoodBands); ++i)
                {
                    if (happiness >= kMoodBands[i].min && happiness <= kMoodBands[i].max)
                    {
                        auto& band = bands[i];
                        band.count++;
                        band.total += happiness;
                        if (static_cast<int32_t>(band.samples.size()) < guestLimit)
                        {
                            band.samples.push_back(BuildGuestSamplePayload(*guest));
                        }
                        break;
                    }
                }
            }

            std::unordered_set<std::string> allowedBands;
            for (const auto& entry : query.bands)
            {
                allowedBands.insert(entry);
            }

            std::vector<json_t> groupList;
            for (size_t i = 0; i < std::size(kMoodBands); ++i)
            {
                const auto& band = bands[i];
                if (band.count == 0)
                {
                    continue;
                }
                std::string key = kMoodBands[i].key;
                if (!allowedBands.empty() && allowedBands.count(ToLower(key)) == 0)
                {
                    continue;
                }
                json_t node = json_t::object();
                node["key"] = key;
                node["label"] = kMoodBands[i].label;
                node["count"] = band.count;
                node["avgHappiness"] = band.count == 0 ? 0.0 : static_cast<double>(band.total) / band.count;
                node["range"] = json_t::object({ { "min", kMoodBands[i].min }, { "max", kMoodBands[i].max } });
                node["guestSamples"] = band.samples;
                groupList.push_back(node);
            }

            std::sort(groupList.begin(), groupList.end(), [&](const json_t& lhs, const json_t& rhs) {
                auto lhsCount = lhs.value("count", 0);
                auto rhsCount = rhs.value("count", 0);
                switch (query.order)
                {
                    case GuestMoodOrderField::Count:
                        if (lhsCount != rhsCount)
                        {
                            return query.descending ? lhsCount > rhsCount : lhsCount < rhsCount;
                        }
                        break;
                    case GuestMoodOrderField::Average:
                    {
                        auto lhsAvg = lhs.value("avgHappiness", 0.0);
                        auto rhsAvg = rhs.value("avgHappiness", 0.0);
                        if (lhsAvg != rhsAvg)
                        {
                            return query.descending ? lhsAvg > rhsAvg : lhsAvg < rhsAvg;
                        }
                        break;
                    }
                    case GuestMoodOrderField::Label:
                    {
                        auto lhsLabel = lhs.value("label", std::string(""));
                        auto rhsLabel = rhs.value("label", std::string(""));
                        int cmp = CompareCaseInsensitive(lhsLabel, rhsLabel);
                        if (cmp != 0)
                        {
                            return query.descending ? cmp > 0 : cmp < 0;
                        }
                        break;
                    }
                }
                if (lhsCount != rhsCount)
                {
                    return lhsCount > rhsCount;
                }
                return CompareCaseInsensitive(lhs.value("key", std::string("")), rhs.value("key", std::string("")))
                    < 0;
            });

            json_t jsonGroups = json_t::array();
            size_t emitted = 0;
            for (const auto& item : groupList)
            {
                jsonGroups.push_back(item);
                emitted++;
                if (query.limitEnabled && emitted >= query.limit)
                {
                    break;
                }
            }

            json_t payload = json_t::object();
            payload["groups"] = jsonGroups;
            payload["consideredGuests"] = considered;
            payload["totalGuests"] = getGameState().park.numGuestsInPark;
            auto hint = MakeGuestListHint("guests.moods", "Reviewed guest moods");
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleGuestPickup(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }
            auto idParam = GetIntParam(params, "id");
            if (!idParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "id is required");
            }
            auto* guest = FindGuestById(*idParam);
            if (guest == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Guest not found");
            }

            CoordsXYZ nullLoc{};
            nullLoc.SetNull();
            GameActions::PeepPickupAction pickupAction{
                GameActions::PeepPickupType::Pickup, guest->Id, nullLoc, Network::GetCurrentPlayerId() };
            auto result = GameActions::Execute(&pickupAction, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Re-fetch guest after pickup to get updated state
            guest = FindGuestById(*idParam);
            if (guest == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Guest could not be retrieved after pickup");
            }

            json_t payload = BuildGuestPayload(*guest, true);
            std::string guestLabel = guest->GetName();
            if (guestLabel.empty())
            {
                guestLabel = "Guest " + std::to_string(guest->Id.ToUnderlying());
            }
            std::string contextLabel = "Picked up " + guestLabel;
            auto hint = MakeGuestHint("guests.pickup", *guest, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleGuestPlace(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }
            auto idParam = GetIntParam(params, "id");
            auto xParam = GetIntParam(params, "x");
            auto yParam = GetIntParam(params, "y");
            if (!idParam || !xParam || !yParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "id, x, and y are required");
            }
            auto* guest = FindGuestById(*idParam);
            if (guest == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Guest not found");
            }

            TileCoordsXY tile{ *xParam, *yParam };
            std::string errorMessage;
            auto placementZ = ResolvePlacementHeight(params, tile, errorMessage);
            if (!placementZ)
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            CoordsXYZ coords{ tile.ToCoordsXY().x, tile.ToCoordsXY().y, *placementZ };
            GameActions::PeepPickupAction placeAction{
                GameActions::PeepPickupType::Place, guest->Id, coords, Network::GetCurrentPlayerId() };
            auto result = GameActions::Execute(&placeAction, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Re-fetch guest after place to get updated state
            guest = FindGuestById(*idParam);
            if (guest == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Guest could not be retrieved after place");
            }

            json_t payload = BuildGuestPayload(*guest, true);
            std::string guestLabel = guest->GetName();
            if (guestLabel.empty())
            {
                guestLabel = "Guest " + std::to_string(guest->Id.ToUnderlying());
            }
            std::string contextLabel = "Placed " + guestLabel + " at (" + std::to_string(tile.x) + "," + std::to_string(tile.y) + ")";
            auto hint = MakeGuestHint("guests.place", *guest, std::move(contextLabel));
            hint.cameraTarget = CoordsXYZ{ tile.ToCoordsXY().x, tile.ToCoordsXY().y, *placementZ };
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleGuestDrop(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }
            auto idParam = GetIntParam(params, "id");
            if (!idParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "id is required");
            }
            auto* guest = FindGuestById(*idParam);
            if (guest == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Guest not found");
            }

            // Check if this peep is actually picked up
            auto playerId = Network::GetCurrentPlayerId();
            if (Network::GetPickupPeep(playerId) != guest)
            {
                return RpcResult::Error(kErrorActionFailed, "Guest is not currently picked up");
            }

            // Get the original X coordinate to restore the peep's position
            int32_t oldX = Network::GetPickupPeepOldX(playerId);
            CoordsXYZ restoreLoc{ oldX, 0, 0 };
            GameActions::PeepPickupAction cancelAction{
                GameActions::PeepPickupType::Cancel, guest->Id, restoreLoc, playerId };
            auto result = GameActions::Execute(&cancelAction, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Re-fetch guest after drop to get updated state
            guest = FindGuestById(*idParam);
            if (guest == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Guest could not be retrieved after drop");
            }

            json_t payload = BuildGuestPayload(*guest, false);
            std::string guestLabel = guest->GetName();
            std::string contextLabel = "Dropped " + guestLabel;
            auto hint = MakeGuestHint("guests.drop", *guest, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        // Static registration
        struct GuestHandlerRegistrar
        {
            GuestHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("guests.list", HandleGuestsList);
                registry.Register("guests.get", HandleGuestsGet);
                registry.Register("guests.search", HandleGuestsSearch);
                registry.Register("guests.thoughts", HandleGuestsThoughtsSummary);
                registry.Register("guests.moods", HandleGuestsMoodSummary);
                registry.Register("guests.pickup", HandleGuestPickup);
                registry.Register("guests.place", HandleGuestPlace);
                registry.Register("guests.drop", HandleGuestDrop);
            }
        } guestRegistrar;

    } // namespace

    void InitGuestHandlers()
    {
        (void)guestRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
