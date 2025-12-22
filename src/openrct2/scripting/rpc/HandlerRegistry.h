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

#include "RpcTypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace OpenRCT2::Scripting::Rpc
{
    using RpcHandler = std::function<RpcResult(const json_t& params)>;

    class HandlerRegistry
    {
    public:
        static HandlerRegistry& Instance();

        void Register(std::string_view method, RpcHandler handler);
        bool HasHandler(std::string_view method) const;
        RpcResult Dispatch(std::string_view method, const json_t& params) const;

        // Forces linking of all handler files
        static void InitializeAllHandlers();

    private:
        HandlerRegistry() = default;
        std::unordered_map<std::string, RpcHandler> _handlers;
    };

    // Helper for static registration via RAII
    struct HandlerRegistrar
    {
        HandlerRegistrar(std::string_view method, RpcHandler handler)
        {
            HandlerRegistry::Instance().Register(method, std::move(handler));
        }
    };

// Macro for convenient static handler registration
#define RPC_REGISTER_HANDLER(method, handler) \
    static ::OpenRCT2::Scripting::Rpc::HandlerRegistrar _registrar_##__LINE__(method, handler)

} // namespace OpenRCT2::Scripting::Rpc

#endif // ENABLE_SCRIPTING
