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

#include "../../../aiagent/AIAgentFollowApi.h"
#include "../../../aiagent/AIAgentPromptBridge.h"

#include <optional>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For kError* constants and utilities

    namespace
    {
        RpcResult HandleAgentFollowGetMode(const json_t& /*params*/)
        {
            json_t payload = json_t::object();
            payload["enabled"] = AIAgent::IsFollowEnabled();
            return RpcResult::Ok(payload);
        }

        RpcResult HandleAgentFollowSetMode(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }
            auto enabledParam = GetBoolParam(params, "enabled");
            if (!enabledParam)
            {
                enabledParam = GetBoolParam(params, "value");
            }
            if (!enabledParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "Boolean field 'enabled' is required");
            }

            AIAgent::SetFollowEnabled(*enabledParam, true);

            json_t payload = json_t::object();
            payload["enabled"] = AIAgent::IsFollowEnabled();
            return RpcResult::Ok(payload);
        }

        RpcResult HandleAgentStatus(const json_t& /*params*/)
        {
            auto statusInfo = AIAgent::GetStatus();

            json_t payload = json_t::object();
            switch (statusInfo.status)
            {
                case AIAgent::AgentStatus::NotRunning:
                    payload["status"] = "not_running";
                    break;
                case AIAgent::AgentStatus::Running:
                    payload["status"] = "running";
                    break;
                case AIAgent::AgentStatus::Exited:
                    payload["status"] = "exited";
                    break;
            }
            payload["exitCode"] = statusInfo.exitCode;
            payload["lastOutputTimestamp"] = statusInfo.lastOutputTimestamp;
            payload["turnComplete"] = statusInfo.turnComplete;
            payload["lastTurnCompleteTimestamp"] = statusInfo.lastTurnCompleteTimestamp;

            return RpcResult::Ok(payload);
        }

        RpcResult HandleAgentSendPrompt(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }

            auto textParam = GetStringParam(params, "text");
            if (!textParam)
            {
                return RpcResult::Error(kErrorInvalidParams, "String field 'text' is required");
            }

            bool success = AIAgent::SendPrompt(*textParam);

            json_t payload = json_t::object();
            payload["success"] = success;
            if (!success)
            {
                payload["error"] = "Agent terminal not running or not accepting input";
            }
            return RpcResult::Ok(payload);
        }

        RpcResult HandleAgentRestart(const json_t& /*params*/)
        {
            bool success = AIAgent::Restart();

            json_t payload = json_t::object();
            payload["success"] = success;
            if (!success)
            {
                payload["error"] = "Agent terminal not available for restart";
            }
            return RpcResult::Ok(payload);
        }

        // Static registration
        struct AgentHandlerRegistrar
        {
            AgentHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("agent.follow.getMode", HandleAgentFollowGetMode);
                registry.Register("agent.follow.setMode", HandleAgentFollowSetMode);
                registry.Register("agent.status", HandleAgentStatus);
                registry.Register("agent.sendPrompt", HandleAgentSendPrompt);
                registry.Register("agent.restart", HandleAgentRestart);
            }
        } agentRegistrar;

    } // namespace

    void InitAgentHandlers()
    {
        (void)agentRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
