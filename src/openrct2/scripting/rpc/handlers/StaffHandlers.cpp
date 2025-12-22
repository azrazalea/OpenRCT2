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
#include "../../../actions/StaffFireAction.h"
#include "../../../actions/StaffHireNewAction.h"
#include "../../../actions/StaffSetOrdersAction.h"
#include "../../../actions/StaffSetPatrolAreaAction.h"
#include "../../../core/EnumUtils.hpp"
#include "../../../entity/EntityList.h"
#include "../../../entity/PatrolArea.h"
#include "../../../entity/Peep.h"
#include "../../../entity/Staff.h"
#include "../../../peep/PeepAnimations.h"
#include "../../../interface/WindowBase.h"
#include "../../../localisation/Formatter.h"
#include "../../../localisation/Formatting.h"
#include "../../../localisation/StringIds.h"
#include "../../../management/Finance.h"
#include "../../../network/Network.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../world/Location.hpp"
#include "../../../world/Map.h"
#include "../../../world/TileElementsView.h"
#include "../../../world/tile_element/SurfaceElement.h"

#include <algorithm>
#include <vector>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For kError* constants

    namespace
    {
        std::string_view StaffTypeToString(StaffType type)
        {
            switch (type)
            {
                case StaffType::handyman:
                    return "handyman";
                case StaffType::mechanic:
                    return "mechanic";
                case StaffType::security:
                    return "security";
                case StaffType::entertainer:
                    return "entertainer";
                default:
                    return "staff";
            }
        }

        std::optional<StaffType> StaffTypeFromString(std::string value)
        {
            auto lowered = ToLower(std::move(value));
            if (lowered == "handyman" || lowered == "janitor")
            {
                return StaffType::handyman;
            }
            if (lowered == "mechanic")
            {
                return StaffType::mechanic;
            }
            if (lowered == "security" || lowered == "guard")
            {
                return StaffType::security;
            }
            if (lowered == "entertainer" || lowered == "entertainment")
            {
                return StaffType::entertainer;
            }
            return std::nullopt;
        }

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
                case PeepState::leavingPark:
                    return "leavingPark";
                case PeepState::watching:
                    return "watching";
                case PeepState::enteringPark:
                    return "enteringPark";
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
                case PeepState::watering:
                    return "watering";
                case PeepState::emptyingBin:
                    return "emptyingBin";
                case PeepState::answering:
                    return "answering";
                case PeepState::fixing:
                    return "fixing";
                case PeepState::inspecting:
                    return "inspecting";
                case PeepState::headingToInspection:
                    return "headingToInspection";
                default:
                    return "unknown";
            }
        }

        std::optional<GameActions::StaffSetPatrolAreaMode> StaffPatrolModeFromString(std::string value)
        {
            auto token = NormaliseToken(std::move(value));
            if (token.empty() || token == "set" || token == "assign")
            {
                return GameActions::StaffSetPatrolAreaMode::Set;
            }
            if (token == "unset" || token == "cleararea")
            {
                return GameActions::StaffSetPatrolAreaMode::Unset;
            }
            if (token == "clear" || token == "clearall" || token == "reset")
            {
                return GameActions::StaffSetPatrolAreaMode::ClearAll;
            }
            return std::nullopt;
        }

        std::string_view StaffPatrolModeToString(GameActions::StaffSetPatrolAreaMode mode)
        {
            switch (mode)
            {
                case GameActions::StaffSetPatrolAreaMode::Set:
                    return "set";
                case GameActions::StaffSetPatrolAreaMode::Unset:
                    return "unset";
                case GameActions::StaffSetPatrolAreaMode::ClearAll:
                    return "clear";
                default:
                    return "unknown";
            }
        }

        Staff* FindStaffById(int32_t id)
        {
            if (id < 0)
            {
                return nullptr;
            }
            for (auto staff : EntityList<Staff>())
            {
                if (staff != nullptr && staff->Id.ToUnderlying() == static_cast<uint16_t>(id))
                {
                    return staff;
                }
            }
            return nullptr;
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

        Staff* FindStaffByName(const std::string& name)
        {
            for (auto staff : EntityList<Staff>())
            {
                if (staff != nullptr && EqualsCaseInsensitive(staff->GetName(), name))
                {
                    return staff;
                }
            }
            return nullptr;
        }

        uint32_t DefaultStaffOrders(StaffType type)
        {
            switch (type)
            {
                case StaffType::handyman:
                    return STAFF_ORDERS_SWEEPING | STAFF_ORDERS_WATER_FLOWERS | STAFF_ORDERS_EMPTY_BINS;
                case StaffType::mechanic:
                    return STAFF_ORDERS_INSPECT_RIDES | STAFF_ORDERS_FIX_RIDES;
                default:
                    return 0;
            }
        }

        std::optional<CoordsXYZ> BuildStaffCameraTarget(const Staff& staff)
        {
            auto loc = staff.GetLocation();
            if (loc.IsNull())
            {
                return std::nullopt;
            }
            return loc;
        }

        std::optional<CoordsXYZ> BuildTileCameraTarget(const TileCoordsXY& tile, int32_t width = 1, int32_t height = 1)
        {
            auto anchor = tile.ToCoordsXY();
            anchor.x += width * kCoordsXYHalfTile;
            anchor.y += height * kCoordsXYHalfTile;
            auto z = TileElementHeight(anchor);
            return CoordsXYZ{ anchor.x, anchor.y, z };
        }

        Telemetry::AIAgentFollowHint MakeStaffHint(std::string_view method, const Staff& staff, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            if (auto camera = BuildStaffCameraTarget(staff))
            {
                hint.cameraTarget = camera;
            }
            Telemetry::StaffWindowIntent intent;
            intent.staffId = staff.Id;
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

        std::optional<Telemetry::AIAgentStaffListTab> StaffTypeToListTab(StaffType type)
        {
            switch (type)
            {
                case StaffType::handyman:
                    return Telemetry::AIAgentStaffListTab::Handymen;
                case StaffType::mechanic:
                    return Telemetry::AIAgentStaffListTab::Mechanics;
                case StaffType::security:
                    return Telemetry::AIAgentStaffListTab::Security;
                case StaffType::entertainer:
                    return Telemetry::AIAgentStaffListTab::Entertainers;
                default:
                    return std::nullopt;
            }
        }

        Telemetry::AIAgentFollowHint MakeStaffListWindowHint(
            std::string_view method, std::string contextLabel, std::optional<StaffType> staffType)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.requestCameraFocus = false;
            Telemetry::StaffListWindowIntent intent;
            if (staffType)
            {
                intent.tab = StaffTypeToListTab(*staffType);
            }
            hint.windowIntent = intent;
            return hint;
        }

        json_t BuildStaffOrdersPayload(const Staff& staff)
        {
            json_t orders = json_t::object();
            if (staff.AssignedStaffType == StaffType::handyman)
            {
                orders["sweeping"] = (staff.StaffOrders & STAFF_ORDERS_SWEEPING) != 0;
                orders["watering"] = (staff.StaffOrders & STAFF_ORDERS_WATER_FLOWERS) != 0;
                orders["emptyBins"] = (staff.StaffOrders & STAFF_ORDERS_EMPTY_BINS) != 0;
                orders["mowing"] = (staff.StaffOrders & STAFF_ORDERS_MOWING) != 0;
            }
            else if (staff.AssignedStaffType == StaffType::mechanic)
            {
                orders["inspectRides"] = (staff.StaffOrders & STAFF_ORDERS_INSPECT_RIDES) != 0;
                orders["fixRides"] = (staff.StaffOrders & STAFF_ORDERS_FIX_RIDES) != 0;
            }
            return orders;
        }

        json_t BuildStaffPayload(const Staff& staff, bool includePatrol)
        {
            auto loc = staff.GetLocation();

            json_t node = json_t::object();
            node["id"] = staff.Id.ToUnderlying();
            node["name"] = staff.GetName();
            node["type"] = StaffTypeToString(staff.AssignedStaffType);
            node["state"] = PeepStateToString(staff.State);
            // Only include coordinates if location is valid (staff not picked up)
            if (!loc.IsNull())
            {
                const int32_t tileX = loc.x / kCoordsXYStep;
                const int32_t tileY = loc.y / kCoordsXYStep;
                node["coords"] = json_t::object({ { "x", tileX }, { "y", tileY }, { "z", WorldZToTileZ(loc.z) } });
            }
            node["orders"] = BuildStaffOrdersPayload(staff);
            node["energy"] = staff.Energy;
            node["wage"] = MoneyToDouble(GetStaffWage(staff.AssignedStaffType));
            node["hireDateMonth"] = staff.GetHireDate();

            if (includePatrol && staff.HasPatrolArea())
            {
                json_t patrolTiles = json_t::array();
                auto tiles = staff.PatrolInfo->ToVector();
                size_t emitted = 0;
                for (const auto& tile : tiles)
                {
                    json_t entry = json_t::object();
                    entry["x"] = tile.x;
                    entry["y"] = tile.y;
                    patrolTiles.push_back(entry);
                    if (++emitted >= 256)
                    {
                        break;
                    }
                }
                json_t patrol = json_t::object();
                patrol["tileCount"] = tiles.size();
                patrol["sample"] = patrolTiles;
                node["patrol"] = patrol;
            }

            return node;
        }

        json_t BuildActionSuccessPayload(const GameActions::Result& result)
        {
            json_t payload = json_t::object();
            payload["status"] = GameActionStatusToString(result.Error);
            payload["cost"] = MoneyToDouble(result.Cost);
            json_t position = json_t::object();
            auto coords = result.Position;
            position["x"] = coords.x;
            position["y"] = coords.y;
            position["z"] = coords.z;
            json_t tile = json_t::object();
            if (coords.x != kCoordsNull)
            {
                tile["x"] = coords.x / kCoordsXYStep;
            }
            if (coords.y != kCoordsNull)
            {
                tile["y"] = coords.y / kCoordsXYStep;
            }
            // Include z in tile units if coords are valid (z=0 is valid, so check x)
            if (coords.x != kCoordsNull)
            {
                tile["z"] = WorldZToTileZ(coords.z);
            }
            position["tile"] = tile;
            payload["position"] = position;
            if (result.Expenditure != ExpenditureType::count)
            {
                payload["expenditure"] = static_cast<int32_t>(result.Expenditure);
            }
            return payload;
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
                errorMessage = "No surface element at coordinates";
                return std::nullopt;
            }

            return surface->GetBaseZ();
        }

        MapRange BuildTileBrushRange(const TileCoordsXY& anchorTile, int32_t width, int32_t height)
        {
            auto anchor = anchorTile.ToCoordsXY();
            TileCoordsXY corner = anchorTile;
            corner.x += width - 1;
            corner.y += height - 1;
            auto farCorner = corner.ToCoordsXY();
            return MapRange(anchor, farCorner).Normalise();
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

        enum class StaffOrderField
        {
            Id,
            Name,
            Role,
            Energy,
            Wage,
            Hire,
        };

        struct StaffListQuery
        {
            std::optional<StaffType> role;
            StaffOrderField order = StaffOrderField::Id;
            bool descending = false;
            bool directionSpecified = false;
            size_t limit = 0;
            bool limitEnabled = false;
        };

        int CompareCaseInsensitive(const std::string& a, const std::string& b)
        {
            return std::lexicographical_compare(
                a.begin(), a.end(), b.begin(), b.end(),
                [](char lhs, char rhs) { return std::tolower(lhs) < std::tolower(rhs); }) ? -1 : (a == b ? 0 : 1);
        }

        bool ParseStaffListQuery(const json_t& params, StaffListQuery& query, std::string& errorMessage)
        {
            if (!params.is_object())
            {
                return true;
            }
            query.limit = ExtractLimitParam(params);
            query.limitEnabled = query.limit != 0;
            if (auto roleParam = GetStringParam(params, "role"))
            {
                auto role = StaffTypeFromString(*roleParam);
                if (!role)
                {
                    errorMessage = "Unknown role (use handyman, mechanic, security, or entertainer)";
                    return false;
                }
                query.role = role;
            }
            if (auto orderParam = GetStringParam(params, "order"))
            {
                auto lowered = ToLower(*orderParam);
                if (lowered == "name")
                {
                    query.order = StaffOrderField::Name;
                }
                else if (lowered == "role")
                {
                    query.order = StaffOrderField::Role;
                }
                else if (lowered == "energy")
                {
                    query.order = StaffOrderField::Energy;
                    if (!query.directionSpecified)
                    {
                        query.descending = true;
                    }
                }
                else if (lowered == "wage" || lowered == "pay")
                {
                    query.order = StaffOrderField::Wage;
                    if (!query.directionSpecified)
                    {
                        query.descending = true;
                    }
                }
                else if (lowered == "hire" || lowered == "hiredate")
                {
                    query.order = StaffOrderField::Hire;
                }
                else if (lowered == "id")
                {
                    query.order = StaffOrderField::Id;
                }
                else
                {
                    errorMessage = "Unknown order (use id, name, role, energy, wage, or hire)";
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
            return true;
        }

        RpcResult HandleStaffList(const json_t& params)
        {
            StaffListQuery query;
            std::string errorMessage;
            if (!ParseStaffListQuery(params, query, errorMessage))
            {
                return RpcResult::Error(kErrorInvalidParams, errorMessage);
            }

            std::vector<json_t> staffEntries;
            for (auto staff : EntityList<Staff>())
            {
                if (staff == nullptr)
                {
                    continue;
                }
                if (query.role && staff->AssignedStaffType != *query.role)
                {
                    continue;
                }
                staffEntries.push_back(BuildStaffPayload(*staff, false));
            }

            auto fallbackCompare = [](const json_t& lhs, const json_t& rhs) {
                auto lhsName = lhs.value("name", std::string(""));
                auto rhsName = rhs.value("name", std::string(""));
                int cmp = CompareCaseInsensitive(lhsName, rhsName);
                if (cmp == 0)
                {
                    return lhs.value("id", -1) < rhs.value("id", -1);
                }
                return cmp < 0;
            };

            std::sort(staffEntries.begin(), staffEntries.end(), [&](const json_t& lhs, const json_t& rhs) {
                switch (query.order)
                {
                    case StaffOrderField::Id:
                    {
                        auto left = lhs.value("id", -1);
                        auto right = rhs.value("id", -1);
                        if (left != right)
                        {
                            return query.descending ? left > right : left < right;
                        }
                        break;
                    }
                    case StaffOrderField::Name:
                    {
                        int cmp = CompareCaseInsensitive(lhs.value("name", std::string("")), rhs.value("name", std::string("")));
                        if (cmp != 0)
                        {
                            return query.descending ? cmp > 0 : cmp < 0;
                        }
                        break;
                    }
                    case StaffOrderField::Role:
                    {
                        int cmp = CompareCaseInsensitive(lhs.value("type", std::string("")), rhs.value("type", std::string("")));
                        if (cmp != 0)
                        {
                            return query.descending ? cmp > 0 : cmp < 0;
                        }
                        break;
                    }
                    case StaffOrderField::Energy:
                    {
                        auto left = lhs.value("energy", 0);
                        auto right = rhs.value("energy", 0);
                        if (left != right)
                        {
                            return query.descending ? left > right : left < right;
                        }
                        break;
                    }
                    case StaffOrderField::Wage:
                    {
                        auto left = lhs.value("wage", 0.0);
                        auto right = rhs.value("wage", 0.0);
                        if (left != right)
                        {
                            return query.descending ? left > right : left < right;
                        }
                        break;
                    }
                    case StaffOrderField::Hire:
                    {
                        auto left = lhs.value("hireDateMonth", 0);
                        auto right = rhs.value("hireDateMonth", 0);
                        if (left != right)
                        {
                            return query.descending ? left > right : left < right;
                        }
                        break;
                    }
                }
                return fallbackCompare(lhs, rhs);
            });

            json_t staffArray = json_t::array();
            size_t emitted = 0;
            for (const auto& entry : staffEntries)
            {
                staffArray.push_back(entry);
                emitted++;
                if (query.limitEnabled && emitted >= query.limit)
                {
                    break;
                }
            }

            json_t payload = json_t::object();
            payload["staff"] = staffArray;
            payload["count"] = staffEntries.size();
            payload["limit"] = query.limitEnabled ? query.limit : 0;
            if (query.role)
            {
                payload["roleFilter"] = StaffTypeToString(*query.role);
            }
            std::string contextLabel = "Opened staff list";
            if (query.role)
            {
                contextLabel = "Opened " + std::string(StaffTypeToString(*query.role)) + " list";
            }
            auto hint = MakeStaffListWindowHint("staff.list", std::move(contextLabel), query.role);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffGet(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }
            Staff* staff = nullptr;
            auto idParam = GetIntParam(params, "id");
            auto nameParam = GetStringParam(params, "name");
            if (idParam)
            {
                staff = FindStaffById(*idParam);
                if (staff == nullptr)
                {
                    return RpcResult::Error(kErrorNotFound, "Staff member not found with id " + std::to_string(*idParam));
                }
            }
            else if (nameParam)
            {
                staff = FindStaffByName(*nameParam);
                if (staff == nullptr)
                {
                    return RpcResult::Error(kErrorNotFound, "Staff member not found with name '" + *nameParam + "'");
                }
            }
            else
            {
                return RpcResult::Error(kErrorInvalidParams, "id or name is required");
            }
            auto payload = BuildStaffPayload(*staff, true);
            std::string staffLabel = staff->GetName();
            if (staffLabel.empty())
            {
                staffLabel = StaffTypeToString(staff->AssignedStaffType);
            }
            std::string contextLabel = "Inspect staff " + staffLabel;
            auto hint = MakeStaffHint("staff.get", *staff, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffHire(const json_t& params)
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
            auto staffType = StaffTypeFromString(*typeParam);
            if (!staffType)
            {
                return RpcResult::Error(kErrorInvalidParams, "Unknown staff type");
            }

            const bool autoPlace = params.is_object() ? GetBoolParam(params, "autoPlace").value_or(true) : true;
            uint32_t orders = params.is_object() ? GetIntParam(params, "orders").value_or(DefaultStaffOrders(*staffType))
                                                 : DefaultStaffOrders(*staffType);
            ObjectEntryIndex costumeIndex = 0;
            if (auto costumeParam = GetIntParam(params, "costume"))
            {
                costumeIndex = static_cast<ObjectEntryIndex>(std::max(0, *costumeParam));
            }
            else if (*staffType == StaffType::entertainer)
            {
                // For entertainers without a specified costume, get a valid default
                auto validCostumes = findAllPeepAnimationsIndexesForType(AnimationPeepType::entertainer);
                if (!validCostumes.empty())
                {
                    costumeIndex = validCostumes[0];
                }
            }

            auto action = GameActions::StaffHireNewAction(autoPlace, *staffType, costumeIndex, orders);
            // Use ExecuteNested to bypass queueing and get immediate result with valid entity ID
            auto result = GameActions::ExecuteNested(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto hireResult = result.GetData<GameActions::StaffHireNewActionResult>();
            Staff* newestStaff = nullptr;
            if (!hireResult.StaffEntityId.IsNull())
            {
                // Use direct entity lookup from the action result
                newestStaff = getGameState().entities.GetEntity<Staff>(hireResult.StaffEntityId);
            }

            json_t payload = json_t::object();
            std::string contextLabel;
            std::optional<CoordsXYZ> camera;

            if (newestStaff != nullptr)
            {
                // Staff found - use entity data
                payload = BuildStaffPayload(*newestStaff, false);
                std::string staffLabel = newestStaff->GetName();
                if (staffLabel.empty())
                {
                    staffLabel = StaffTypeToString(newestStaff->AssignedStaffType);
                }
                contextLabel = "Hired " + staffLabel;
                auto loc = newestStaff->GetLocation();
                if (!loc.IsNull())
                {
                    camera = loc;
                }
            }
            else
            {
                // Staff entity not found (should be rare) - return requested data with defaults
                int32_t entityId = hireResult.StaffEntityId.IsNull() ? -1 : hireResult.StaffEntityId.ToUnderlying();
                payload["id"] = entityId;
                payload["type"] = StaffTypeToString(*staffType);
                // Provide placeholder values so output isn't empty
                payload["name"] = "(new " + std::string(StaffTypeToString(*staffType)) + ")";
                payload["state"] = "hired";
                payload["energy"] = 96; // Default energy level
                // Build orders from the requested orders value
                json_t ordersPayload = json_t::object();
                if (*staffType == StaffType::handyman)
                {
                    ordersPayload["sweeping"] = (orders & STAFF_ORDERS_SWEEPING) != 0;
                    ordersPayload["watering"] = (orders & STAFF_ORDERS_WATER_FLOWERS) != 0;
                    ordersPayload["emptyBins"] = (orders & STAFF_ORDERS_EMPTY_BINS) != 0;
                    ordersPayload["mowing"] = (orders & STAFF_ORDERS_MOWING) != 0;
                }
                else if (*staffType == StaffType::mechanic)
                {
                    ordersPayload["inspectRides"] = (orders & STAFF_ORDERS_INSPECT_RIDES) != 0;
                    ordersPayload["fixRides"] = (orders & STAFF_ORDERS_FIX_RIDES) != 0;
                }
                payload["orders"] = ordersPayload;
                contextLabel = "Hired " + std::string(StaffTypeToString(*staffType));
            }

            auto hint = MakeStaffListWindowHint("staff.hire", std::move(contextLabel), *staffType);
            hint.cameraTarget = camera;
            hint.requestCameraFocus = camera.has_value();
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffFire(const json_t& params)
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
            auto* staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Staff member not found");
            }

            std::string staffLabel = staff->GetName();
            if (staffLabel.empty())
            {
                staffLabel = StaffTypeToString(staff->AssignedStaffType);
            }
            auto location = staff->GetLocation();
            std::optional<CoordsXYZ> camera;
            if (!location.IsNull())
            {
                camera = location;
            }

            auto action = GameActions::StaffFireAction(staff->Id);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            json_t payload = json_t::object();
            payload["id"] = *idParam;
            payload["status"] = "fired";
            std::string contextLabel = "Fired " + staffLabel;
            auto hint = MakeGenericWindowHint("staff.fire", std::move(contextLabel), WindowClass::staffList, camera);
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffSetOrders(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto idParam = GetIntParam(params, "id");
            if (!idParam)
            {
                idParam = GetIntParam(params, "staffId");
            }
            if (!idParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "id is required");
            }

            auto* staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Staff member not found");
            }

            if (staff->AssignedStaffType != StaffType::handyman && staff->AssignedStaffType != StaffType::mechanic)
            {
                return RpcResult::Error(kErrorInvalidParams, "Only handymen and mechanics have configurable orders");
            }

            auto applyToggle = [&](const char* key, uint8_t flag, uint8_t& orders, bool& mutated) {
                if (auto value = GetBoolParam(params, key))
                {
                    mutated = true;
                    if (*value)
                    {
                        orders |= flag;
                    }
                    else
                    {
                        orders &= ~flag;
                    }
                }
            };

            bool mutated = false;
            uint8_t desiredOrders = staff->StaffOrders;

            if (staff->AssignedStaffType == StaffType::handyman)
            {
                applyToggle("sweeping", STAFF_ORDERS_SWEEPING, desiredOrders, mutated);
                applyToggle("watering", STAFF_ORDERS_WATER_FLOWERS, desiredOrders, mutated);
                applyToggle("emptyBins", STAFF_ORDERS_EMPTY_BINS, desiredOrders, mutated);
                applyToggle("mowing", STAFF_ORDERS_MOWING, desiredOrders, mutated);
            }
            else if (staff->AssignedStaffType == StaffType::mechanic)
            {
                applyToggle("inspectRides", STAFF_ORDERS_INSPECT_RIDES, desiredOrders, mutated);
                applyToggle("fixRides", STAFF_ORDERS_FIX_RIDES, desiredOrders, mutated);
            }

            if (!mutated)
            {
                return RpcResult::Error(kErrorInvalidParams, "Provide at least one order toggle to change");
            }

            auto action = GameActions::StaffSetOrdersAction(staff->Id, desiredOrders);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto* updatedStaff = FindStaffById(*idParam);
            if (updatedStaff == nullptr)
            {
                updatedStaff = staff;
            }

            json_t payload = BuildActionSuccessPayload(result);
            payload["staff"] = BuildStaffPayload(*updatedStaff, false);
            // Build orders directly from desiredOrders rather than re-reading from staff
            // (which may not be updated yet if action is async)
            json_t orders = json_t::object();
            if (staff->AssignedStaffType == StaffType::handyman)
            {
                orders["sweeping"] = (desiredOrders & STAFF_ORDERS_SWEEPING) != 0;
                orders["watering"] = (desiredOrders & STAFF_ORDERS_WATER_FLOWERS) != 0;
                orders["emptyBins"] = (desiredOrders & STAFF_ORDERS_EMPTY_BINS) != 0;
                orders["mowing"] = (desiredOrders & STAFF_ORDERS_MOWING) != 0;
            }
            else if (staff->AssignedStaffType == StaffType::mechanic)
            {
                orders["inspectRides"] = (desiredOrders & STAFF_ORDERS_INSPECT_RIDES) != 0;
                orders["fixRides"] = (desiredOrders & STAFF_ORDERS_FIX_RIDES) != 0;
            }
            payload["orders"] = orders;
            payload["staff"]["orders"] = orders;
            std::string staffLabel = updatedStaff->GetName();
            if (staffLabel.empty())
            {
                staffLabel = StaffTypeToString(updatedStaff->AssignedStaffType);
            }
            std::string contextLabel = "Updated orders for " + staffLabel;
            auto hint = MakeStaffHint("staff.setOrders", *updatedStaff, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffSetPatrol(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto idParam = GetIntParam(params, "id");
            if (!idParam)
            {
                idParam = GetIntParam(params, "staffId");
            }
            if (!idParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "id is required");
            }

            auto* staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Staff member not found");
            }

            auto modeParam = GetStringParam(params, "mode").value_or("set");
            auto mode = StaffPatrolModeFromString(modeParam);
            if (!mode)
            {
                return RpcResult::Error(kErrorInvalidParams, "Unknown patrol mode: " + modeParam);
            }

            MapRange range(0, 0, 0, 0);
            const bool requiresArea = *mode != GameActions::StaffSetPatrolAreaMode::ClearAll;
            if (requiresArea)
            {
                auto xParam = GetIntParam(params, "x");
                auto yParam = GetIntParam(params, "y");
                if (!xParam || !yParam)
                {
                    return RpcResult::Error(kErrorInvalidParams, "x and y tile coordinates are required for this mode");
                }
                int32_t width = GetIntParam(params, "width").value_or(1);
                int32_t height = GetIntParam(params, "height").value_or(1);
                if (width < 1 || height < 1)
                {
                    return RpcResult::Error(kErrorInvalidParams, "width and height must be >= 1 tile");
                }
                TileCoordsXY origin{ *xParam, *yParam };
                range = BuildTileBrushRange(origin, width, height);
            }

            auto action = GameActions::StaffSetPatrolAreaAction(staff->Id, range, *mode);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            auto* updatedStaff = FindStaffById(*idParam);
            if (updatedStaff == nullptr)
            {
                updatedStaff = staff;
            }

            json_t payload = BuildActionSuccessPayload(result);
            // Build staff payload without patrol (we'll add it ourselves with correct values)
            payload["staff"] = BuildStaffPayload(*updatedStaff, false);
            payload["mode"] = StaffPatrolModeToString(*mode);
            std::string staffLabel = updatedStaff->GetName();
            if (staffLabel.empty())
            {
                staffLabel = StaffTypeToString(updatedStaff->AssignedStaffType);
            }
            std::string contextLabel = "Updated patrol for " + staffLabel;
            std::optional<TileCoordsXY> patrolTile;
            int32_t patrolWidth = 1;
            int32_t patrolHeight = 1;
            if (requiresArea)
            {
                json_t area = json_t::object();
                area["x"] = range.GetX1() / kCoordsXYStep;
                area["y"] = range.GetY1() / kCoordsXYStep;
                area["width"] = (range.GetX2() - range.GetX1()) / kCoordsXYStep + 1;
                area["height"] = (range.GetY2() - range.GetY1()) / kCoordsXYStep + 1;
                payload["area"] = area;
                patrolTile = TileCoordsXY{ range.GetX1() / kCoordsXYStep, range.GetY1() / kCoordsXYStep };
                patrolWidth = area["width"].get<int32_t>();
                patrolHeight = area["height"].get<int32_t>();
                contextLabel += " near (" + std::to_string(patrolTile->x) + "," + std::to_string(patrolTile->y) + ")";

                // Build patrol tiles directly from range (not from entity which may be stale)
                json_t patrolTiles = json_t::array();
                const int32_t x1 = range.GetX1() / kCoordsXYStep;
                const int32_t y1 = range.GetY1() / kCoordsXYStep;
                for (int32_t dy = 0; dy < patrolHeight && patrolTiles.size() < 256; ++dy)
                {
                    for (int32_t dx = 0; dx < patrolWidth && patrolTiles.size() < 256; ++dx)
                    {
                        json_t entry = json_t::object();
                        entry["x"] = x1 + dx;
                        entry["y"] = y1 + dy;
                        patrolTiles.push_back(entry);
                    }
                }
                json_t patrol = json_t::object();
                patrol["tileCount"] = patrolWidth * patrolHeight;
                patrol["sample"] = patrolTiles;
                payload["staff"]["patrol"] = patrol;
            }
            auto hint = MakeStaffHint("staff.setPatrol", *updatedStaff, std::move(contextLabel));
            if (patrolTile)
            {
                hint.cameraTarget = BuildTileCameraTarget(*patrolTile, patrolWidth, patrolHeight);
            }
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffPickup(const json_t& params)
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
            auto* staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Staff member not found");
            }

            CoordsXYZ nullLoc{};
            nullLoc.SetNull();
            GameActions::PeepPickupAction pickupAction{
                GameActions::PeepPickupType::Pickup, staff->Id, nullLoc, Network::GetCurrentPlayerId() };
            auto result = GameActions::Execute(&pickupAction, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Re-fetch staff after pickup to get updated state
            staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Staff member could not be retrieved after pickup");
            }

            json_t payload = BuildStaffPayload(*staff, true);
            std::string staffLabel = staff->GetName();
            if (staffLabel.empty())
            {
                staffLabel = StaffTypeToString(staff->AssignedStaffType);
            }
            std::string contextLabel = "Picked up " + staffLabel;
            auto hint = MakeStaffHint("staff.pickup", *staff, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffDrop(const json_t& params)
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
            auto* staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Staff member not found");
            }

            // Check if this peep is actually picked up
            auto playerId = Network::GetCurrentPlayerId();
            if (Network::GetPickupPeep(playerId) != staff)
            {
                return RpcResult::Error(kErrorActionFailed, "Staff member is not currently picked up");
            }

            // Get the original X coordinate to restore the peep's position
            int32_t oldX = Network::GetPickupPeepOldX(playerId);
            CoordsXYZ restoreLoc{ oldX, 0, 0 };
            GameActions::PeepPickupAction cancelAction{
                GameActions::PeepPickupType::Cancel, staff->Id, restoreLoc, playerId };
            auto result = GameActions::Execute(&cancelAction, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Re-fetch staff after drop to get updated state
            staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Staff member could not be retrieved after drop");
            }

            json_t payload = BuildStaffPayload(*staff, true);
            std::string staffLabel = staff->GetName();
            if (staffLabel.empty())
            {
                staffLabel = StaffTypeToString(staff->AssignedStaffType);
            }
            std::string contextLabel = "Dropped " + staffLabel;
            auto hint = MakeStaffHint("staff.drop", *staff, std::move(contextLabel));
            return RpcResult::Ok(payload, std::move(hint));
        }

        RpcResult HandleStaffPlace(const json_t& params)
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
            auto* staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorNotFound, "Staff member not found");
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
                GameActions::PeepPickupType::Place, staff->Id, coords, Network::GetCurrentPlayerId() };
            auto result = GameActions::Execute(&placeAction, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }

            // Re-fetch staff after place to get updated state
            staff = FindStaffById(*idParam);
            if (staff == nullptr)
            {
                return RpcResult::Error(kErrorActionFailed, "Staff member could not be retrieved after place");
            }

            json_t payload = BuildStaffPayload(*staff, true);
            std::string staffLabel = staff->GetName();
            if (staffLabel.empty())
            {
                staffLabel = StaffTypeToString(staff->AssignedStaffType);
            }
            std::string contextLabel = "Placed " + staffLabel + " at (" + std::to_string(tile.x) + "," + std::to_string(tile.y) + ")";
            auto hint = MakeStaffHint("staff.place", *staff, std::move(contextLabel));
            hint.cameraTarget = CoordsXYZ{ tile.ToCoordsXY().x, tile.ToCoordsXY().y, *placementZ };
            return RpcResult::Ok(payload, std::move(hint));
        }

        // Static registration
        struct StaffHandlerRegistrar
        {
            StaffHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("staff.list", HandleStaffList);
                registry.Register("staff.get", HandleStaffGet);
                registry.Register("staff.hire", HandleStaffHire);
                registry.Register("staff.fire", HandleStaffFire);
                registry.Register("staff.setOrders", HandleStaffSetOrders);
                registry.Register("staff.setPatrol", HandleStaffSetPatrol);
                registry.Register("staff.pickup", HandleStaffPickup);
                registry.Register("staff.drop", HandleStaffDrop);
                registry.Register("staff.place", HandleStaffPlace);
            }
        } staffRegistrar;

    } // namespace

    void InitStaffHandlers()
    {
        (void)staffRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
