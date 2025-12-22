/****
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 ****/

#pragma once

#ifdef ENABLE_SCRIPTING

#include "../core/JsonFwd.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace OpenRCT2::Network
{
    struct ITcpSocket;
}

namespace OpenRCT2::Scripting
{
    class ScriptEngine;

    class JsonRpcServer
    {
    public:
        explicit JsonRpcServer(ScriptEngine& engine);
        ~JsonRpcServer();

        void Start();
        void Stop();
        void Tick();

    private:
        struct Client;

        void AcceptClients();
        bool ServiceClient(Client& client);
        void ProcessClientBuffer(Client& client);
        void HandleLine(Client& client, std::string_view line);
        void HandleMessage(Client& client, const json_t& message);
        void SendResponse(Client& client, const json_t& id, const json_t& result, const json_t* followHint = nullptr);
        void SendError(Client& client, const json_t& id, int32_t code, std::string_view message);

        [[maybe_unused]] ScriptEngine& _engine;
        std::unique_ptr<OpenRCT2::Network::ITcpSocket> _listener;
        std::vector<std::unique_ptr<Client>> _clients;
        bool _running = false;
    };
} // namespace OpenRCT2::Scripting

#endif // ENABLE_SCRIPTING
