#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace rctctl::rpc {

class JsonRpcClient
{
public:
    JsonRpcClient(std::string host, uint16_t port);
    ~JsonRpcClient();

    nlohmann::json Call(const std::string& method, const nlohmann::json& params);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace rctctl::rpc
