/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_SCRIPTING

#include "../../actions/GameActionResult.h"
#include "../../core/Json.hpp"
#include "../../core/Money.hpp"
#include "../../interface/WindowBase.h"
#include "../../localisation/StringIdType.h"
#include "../../telemetry/AIAgentActivityFeed.h"
#include "../../world/Location.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace OpenRCT2::Scripting::Rpc
{
    // Parameter extraction from JSON-RPC params
    std::optional<int32_t> GetIntParam(const json_t& params, const char* key);
    std::optional<std::string> GetStringParam(const json_t& params, const char* key);
    std::optional<bool> GetBoolParam(const json_t& params, const char* key);
    std::optional<double> GetDoubleParam(const json_t& params, const char* key);
    size_t ExtractLimitParam(const json_t& params);

    // String utilities
    std::string ToLower(std::string value);
    std::string NormaliseToken(std::string value);
    std::string NormaliseClassKey(std::string value);

    // Direction utilities
    std::string_view DirectionToString(Direction dir);
    std::optional<Direction> DirectionFromString(std::string value);

    // Error message building
    std::string BuildGameActionErrorMessage(const GameActions::Result& result);
    std::string_view GameActionStatusToString(GameActions::Status status);

    // Payload building
    json_t BuildPositionPayload(const CoordsXYZ& coords);
    json_t BuildActionSuccessPayload(const GameActions::Result& result);

    // String ID resolution
    std::string ResolveStringId(StringId id);

    // Money conversions
    double MoneyToDouble(money64 value);
    money64 DoubleToMoney(double value);

    // Coordinate conversions
    int32_t WorldZToTileZ(int32_t worldZ);
    int32_t TileZToWorldZ(int32_t tileZ);
    double HeightToMeters(int32_t height);

    // Money formatting
    std::string FormatMoneyString(money64 amount);

    // Camera target building
    std::optional<CoordsXYZ> BuildTileCameraTarget(const TileCoordsXY& tile, int32_t width = 1, int32_t height = 1);

    // Hint builders
    Telemetry::AIAgentFollowHint MakeGenericWindowHint(
        std::string_view method, std::string contextLabel, WindowClass windowClass, std::optional<CoordsXYZ> camera);
    Telemetry::AIAgentFollowHint MakeTileHint(
        std::string_view method, std::string contextLabel, const TileCoordsXY& tile, WindowClass windowClass,
        int32_t width = 1, int32_t height = 1);

} // namespace OpenRCT2::Scripting::Rpc

#endif // ENABLE_SCRIPTING
