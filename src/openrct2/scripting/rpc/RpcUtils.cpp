/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

#include "RpcUtils.h"

#include "../../localisation/Currency.h"
#include "../../localisation/Formatter.h"
#include "../../localisation/Formatting.h"
#include "../../management/Finance.h"
#include "../../world/Map.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace OpenRCT2::Scripting::Rpc
{
    std::optional<int32_t> GetIntParam(const json_t& params, const char* key)
    {
        const auto it = params.find(key);
        if (it == params.end() || !it->is_number_integer())
        {
            return std::nullopt;
        }
        return it->get<int32_t>();
    }

    std::optional<std::string> GetStringParam(const json_t& params, const char* key)
    {
        const auto it = params.find(key);
        if (it == params.end() || !it->is_string())
        {
            return std::nullopt;
        }
        return it->get<std::string>();
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
        if (it->is_string())
        {
            const auto value = ToLower(it->get<std::string>());
            if (value == "true" || value == "1" || value == "open" || value == "yes")
            {
                return true;
            }
            if (value == "false" || value == "0" || value == "closed" || value == "no")
            {
                return false;
            }
        }
        return std::nullopt;
    }

    std::optional<double> GetDoubleParam(const json_t& params, const char* key)
    {
        const auto it = params.find(key);
        if (it == params.end())
        {
            return std::nullopt;
        }
        if (it->is_number_float() || it->is_number_integer())
        {
            return it->get<double>();
        }
        if (it->is_string())
        {
            try
            {
                return std::stod(it->get<std::string>());
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
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

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::string NormaliseToken(std::string value)
    {
        value = ToLower(std::move(value));
        value.erase(
            std::remove_if(
                value.begin(), value.end(),
                [](unsigned char ch) { return std::isspace(ch) || ch == '_' || ch == '-' || ch == '/' || ch == '.'; }),
            value.end());
        return value;
    }

    std::string NormaliseClassKey(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        value.erase(
            std::remove_if(value.begin(), value.end(), [](unsigned char c) { return c == '_' || c == '-' || c == ' '; }),
            value.end());
        return value;
    }

    std::string_view DirectionToString(Direction dir)
    {
        switch (dir & 0x03)
        {
            case 0:
                return "west";
            case 1:
                return "south";
            case 2:
                return "east";
            case 3:
                return "north";
            default:
                return "unknown";
        }
    }

    std::optional<Direction> DirectionFromString(std::string value)
    {
        auto lowered = ToLower(std::move(value));
        if (lowered == "west" || lowered == "w" || lowered == "left")
        {
            return Direction{ 0 };
        }
        if (lowered == "south" || lowered == "s" || lowered == "down")
        {
            return Direction{ 1 };
        }
        if (lowered == "east" || lowered == "e" || lowered == "right")
        {
            return Direction{ 2 };
        }
        if (lowered == "north" || lowered == "n" || lowered == "up")
        {
            return Direction{ 3 };
        }
        return std::nullopt;
    }

    std::string_view GameActionStatusToString(GameActions::Status status)
    {
        using GameActions::Status;
        switch (status)
        {
            case Status::Ok:
                return "ok";
            case Status::InvalidParameters:
                return "invalidParameters";
            case Status::Disallowed:
                return "disallowed";
            case Status::GamePaused:
                return "gamePaused";
            case Status::InsufficientFunds:
                return "insufficientFunds";
            case Status::NotInEditorMode:
                return "notInEditor";
            case Status::NotOwned:
                return "notOwned";
            case Status::TooLow:
                return "tooLow";
            case Status::TooHigh:
                return "tooHigh";
            case Status::NoClearance:
                return "noClearance";
            case Status::ItemAlreadyPlaced:
                return "itemAlreadyPlaced";
            case Status::NotClosed:
                return "notClosed";
            case Status::Broken:
                return "broken";
            case Status::NoFreeElements:
                return "noFreeElements";
            default:
                return "unknown";
        }
    }

    std::string BuildGameActionErrorMessage(const GameActions::Result& result)
    {
        std::string message = result.GetErrorMessage();
        std::string title = result.GetErrorTitle();

        if (!title.empty())
        {
            if (message.empty())
            {
                message = title;
            }
            else
            {
                message = title + ": " + message;
            }
        }

        if (message.empty())
        {
            message = "Game action failed (" + std::string(GameActionStatusToString(result.Error)) + ")";
        }
        return message;
    }

    json_t BuildPositionPayload(const CoordsXYZ& coords)
    {
        json_t position = json_t::object();
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
            tile["z"] = coords.z / kCoordsZStep;
        }
        position["tile"] = tile;
        return position;
    }

    json_t BuildActionSuccessPayload(const GameActions::Result& result)
    {
        json_t payload = json_t::object();
        payload["status"] = GameActionStatusToString(result.Error);
        payload["cost"] = MoneyToDouble(result.Cost);
        payload["position"] = BuildPositionPayload(result.Position);
        if (result.Expenditure != ExpenditureType::count)
        {
            payload["expenditure"] = static_cast<int32_t>(result.Expenditure);
        }
        return payload;
    }

    std::string ResolveStringId(StringId id)
    {
        if (id == kStringIdNone)
        {
            return {};
        }
        try
        {
            return FormatStringIDLegacy(id, nullptr);
        }
        catch (...)
        {
            // Invalid string ID - return empty string
            return {};
        }
    }

    double MoneyToDouble(money64 value)
    {
        if (value == kMoney64Undefined)
        {
            return 0.0;
        }
        return static_cast<double>(value) / 10.0;
    }

    money64 DoubleToMoney(double value)
    {
        if (!std::isfinite(value))
        {
            return 0;
        }
        return static_cast<money64>(std::llround(value * 10.0));
    }

    int32_t WorldZToTileZ(int32_t worldZ)
    {
        return worldZ / kCoordsZStep;
    }

    int32_t TileZToWorldZ(int32_t tileZ)
    {
        return tileZ * kCoordsZStep;
    }

    double HeightToMeters(int32_t height)
    {
        return static_cast<double>(height) * 0.5;
    }

    std::string FormatMoneyString(money64 amount)
    {
        char buffer[64]{};
        MoneyToString(amount, buffer, sizeof(buffer), true);
        return std::string(buffer);
    }

    std::optional<CoordsXYZ> BuildTileCameraTarget(const TileCoordsXY& tile, int32_t width, int32_t height)
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

    Telemetry::AIAgentFollowHint MakeTileHint(
        std::string_view method, std::string contextLabel, const TileCoordsXY& tile, WindowClass windowClass,
        int32_t width, int32_t height)
    {
        auto camera = BuildTileCameraTarget(tile, width, height);
        return MakeGenericWindowHint(method, std::move(contextLabel), windowClass, camera);
    }

} // namespace OpenRCT2::Scripting::Rpc

#endif // ENABLE_SCRIPTING
