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

#include "../../core/Json.hpp"
#include "../../ride/Ride.h"
#include "../../telemetry/AIAgentActivityFeed.h"

#include <optional>
#include <string>

namespace OpenRCT2::Scripting::Rpc
{
    // Shared lookup result for ride operations
    struct RideLookupResult
    {
        RideId id;
        Ride* ride;
    };

    // JSON-RPC error codes
    constexpr int32_t kErrorInvalidParams = -32602;
    constexpr int32_t kErrorServerError = -32000;
    constexpr int32_t kErrorActionFailed = -32010;
    constexpr int32_t kErrorNotFound = -32020;
    constexpr int32_t kErrorInternalError = -32603;

    struct RpcResult
    {
        bool success;
        json_t payload;
        int32_t errorCode;
        std::string errorMessage;
        std::optional<Telemetry::AIAgentFollowHint> followHint;

        static RpcResult Ok(json_t value, std::optional<Telemetry::AIAgentFollowHint> hint = std::nullopt)
        {
            return RpcResult{ true, std::move(value), 0, {}, std::move(hint) };
        }

        static RpcResult Error(int32_t code, std::string message)
        {
            return RpcResult{ false, json_t(), code, std::move(message), std::nullopt };
        }
    };

} // namespace OpenRCT2::Scripting::Rpc

#endif // ENABLE_SCRIPTING
