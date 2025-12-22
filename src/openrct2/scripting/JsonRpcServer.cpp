/****
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 ****/

#ifdef ENABLE_SCRIPTING

#include "JsonRpcServer.h"
#include "rpc/HandlerRegistry.h"
#include "rpc/RpcTypes.h"

#include "../Diagnostic.h"
#include "../OpenRCT2.h"
#include "../interface/WindowBase.h"
#include "../network/Network.h"
#include "../network/Socket.h"
#include "../telemetry/AIAgentActivityFeed.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace OpenRCT2::Scripting
{
    using OpenRCT2::Network::ITcpSocket;
    using OpenRCT2::Network::ReadPacket;
    using Rpc::RpcResult;
    using Rpc::kErrorServerError;

    namespace
    {
        constexpr int32_t kDefaultPort = 9876;
        constexpr size_t kMaxBufferedBytes = 256 * 1024;
        constexpr size_t kReadChunk = 2048;

        struct WindowClassLabel
        {
            WindowClass cls;
            const char* name;
        };

        constexpr WindowClassLabel kWindowClassLabels[] = {
            { WindowClass::mainWindow, "mainWindow" },
            { WindowClass::topToolbar, "topToolbar" },
            { WindowClass::bottomToolbar, "bottomToolbar" },
            { WindowClass::tooltip, "tooltip" },
            { WindowClass::dropdown, "dropdown" },
            { WindowClass::about, "about" },
            { WindowClass::error, "error" },
            { WindowClass::ride, "ride" },
            { WindowClass::rideConstruction, "rideConstruction" },
            { WindowClass::savePrompt, "savePrompt" },
            { WindowClass::rideList, "rideList" },
            { WindowClass::constructRide, "constructRide" },
            { WindowClass::demolishRidePrompt, "demolishRidePrompt" },
            { WindowClass::scenery, "scenery" },
            { WindowClass::options, "options" },
            { WindowClass::footpath, "footpath" },
            { WindowClass::land, "land" },
            { WindowClass::water, "water" },
            { WindowClass::peep, "peep" },
            { WindowClass::guestList, "guestList" },
            { WindowClass::staffList, "staffList" },
            { WindowClass::firePrompt, "firePrompt" },
            { WindowClass::parkInformation, "parkInformation" },
            { WindowClass::finances, "finances" },
            { WindowClass::titleMenu, "titleMenu" },
            { WindowClass::titleExit, "titleExit" },
            { WindowClass::recentNews, "recentNews" },
            { WindowClass::scenarioSelect, "scenarioSelect" },
            { WindowClass::trackDesignList, "trackDesignList" },
            { WindowClass::trackDesignPlace, "trackDesignPlace" },
            { WindowClass::newCampaign, "newCampaign" },
            { WindowClass::keyboardShortcutList, "keyboardShortcutList" },
            { WindowClass::changeKeyboardShortcut, "changeKeyboardShortcut" },
            { WindowClass::map, "map" },
            { WindowClass::titleLogo, "titleLogo" },
            { WindowClass::banner, "banner" },
            { WindowClass::mapTooltip, "mapTooltip" },
            { WindowClass::editorObjectSelection, "editorObjectSelection" },
            { WindowClass::editorInventionList, "editorInventionList" },
            { WindowClass::editorInventionListDrag, "editorInventionListDrag" },
            { WindowClass::editorScenarioOptions, "editorScenarioOptions" },
            { WindowClass::manageTrackDesign, "manageTrackDesign" },
            { WindowClass::trackDeletePrompt, "trackDeletePrompt" },
            { WindowClass::installTrack, "installTrack" },
            { WindowClass::clearScenery, "clearScenery" },
            { WindowClass::sceneryScatter, "sceneryScatter" },
            { WindowClass::cheats, "cheats" },
            { WindowClass::research, "research" },
            { WindowClass::viewport, "viewport" },
            { WindowClass::textinput, "textinput" },
            { WindowClass::mapgen, "mapgen" },
            { WindowClass::loadsave, "loadsave" },
            { WindowClass::loadsaveOverwritePrompt, "loadsaveOverwritePrompt" },
            { WindowClass::titleOptions, "titleOptions" },
            { WindowClass::landRights, "landRights" },
            { WindowClass::themes, "themes" },
            { WindowClass::tileInspector, "tileInspector" },
            { WindowClass::changelog, "changelog" },
            { WindowClass::multiplayer, "multiplayer" },
            { WindowClass::player, "player" },
            { WindowClass::networkStatus, "networkStatus" },
            { WindowClass::serverList, "serverList" },
            { WindowClass::serverStart, "serverStart" },
            { WindowClass::customCurrencyConfig, "customCurrencyConfig" },
            { WindowClass::debugPaint, "debugPaint" },
            { WindowClass::viewClipping, "viewClipping" },
            { WindowClass::objectLoadError, "objectLoadError" },
            { WindowClass::patrolArea, "patrolArea" },
            { WindowClass::transparency, "transparency" },
            { WindowClass::assetPacks, "assetPacks" },
            { WindowClass::progressWindow, "progressWindow" },
            { WindowClass::titleVersion, "titleVersion" },
            { WindowClass::editorParkEntrance, "editorParkEntrance" },
            { WindowClass::aiAgentTerminal, "aiAgentTerminal" },
        };

        std::string_view WindowClassToString(WindowClass cls)
        {
            for (const auto& entry : kWindowClassLabels)
            {
                if (entry.cls == cls)
                {
                    return entry.name;
                }
            }
            return "unknown";
        }

        // Helper functions for serialising follow hints
        const char* RidePageToString(Telemetry::AIAgentRideWindowPage page)
        {
            switch (page)
            {
                case Telemetry::AIAgentRideWindowPage::Main:
                    return "main";
                case Telemetry::AIAgentRideWindowPage::Vehicle:
                    return "vehicle";
                case Telemetry::AIAgentRideWindowPage::Operating:
                    return "operating";
                case Telemetry::AIAgentRideWindowPage::Maintenance:
                    return "maintenance";
                case Telemetry::AIAgentRideWindowPage::Colour:
                    return "colour";
                case Telemetry::AIAgentRideWindowPage::Music:
                    return "music";
                case Telemetry::AIAgentRideWindowPage::Measurements:
                    return "measurements";
                case Telemetry::AIAgentRideWindowPage::Graphs:
                    return "graphs";
                case Telemetry::AIAgentRideWindowPage::Income:
                    return "income";
                case Telemetry::AIAgentRideWindowPage::Customer:
                    return "customer";
            }
            return "main";
        }

        const char* ParkPageToString(Telemetry::AIAgentParkWindowPage page)
        {
            switch (page)
            {
                case Telemetry::AIAgentParkWindowPage::Entrance:
                    return "entrance";
                case Telemetry::AIAgentParkWindowPage::Rating:
                    return "rating";
                case Telemetry::AIAgentParkWindowPage::Guests:
                    return "guests";
                case Telemetry::AIAgentParkWindowPage::Price:
                    return "price";
                case Telemetry::AIAgentParkWindowPage::Stats:
                    return "stats";
                case Telemetry::AIAgentParkWindowPage::Objective:
                    return "objective";
                case Telemetry::AIAgentParkWindowPage::Awards:
                    return "awards";
            }
            return "entrance";
        }

        const char* FinancesPageToString(Telemetry::AIAgentFinancesWindowPage page)
        {
            switch (page)
            {
                case Telemetry::AIAgentFinancesWindowPage::Marketing:
                    return "marketing";
                case Telemetry::AIAgentFinancesWindowPage::Research:
                    return "research";
                case Telemetry::AIAgentFinancesWindowPage::Summary:
                default:
                    return "summary";
            }
        }

        const char* ConstructRideTabToString(Telemetry::AIAgentConstructRideTab tab)
        {
            switch (tab)
            {
                case Telemetry::AIAgentConstructRideTab::Transport:
                    return "transport";
                case Telemetry::AIAgentConstructRideTab::Gentle:
                    return "gentle";
                case Telemetry::AIAgentConstructRideTab::RollerCoaster:
                    return "rollerCoaster";
                case Telemetry::AIAgentConstructRideTab::Thrill:
                    return "thrill";
                case Telemetry::AIAgentConstructRideTab::Water:
                    return "water";
                case Telemetry::AIAgentConstructRideTab::Shop:
                    return "shop";
                case Telemetry::AIAgentConstructRideTab::Research:
                    return "research";
            }
            return "transport";
        }

        const char* RideListColumnToString(Telemetry::AIAgentRideListColumn column)
        {
            switch (column)
            {
                case Telemetry::AIAgentRideListColumn::Status:
                    return "status";
                case Telemetry::AIAgentRideListColumn::RideType:
                    return "rideType";
                case Telemetry::AIAgentRideListColumn::Popularity:
                    return "popularity";
                case Telemetry::AIAgentRideListColumn::Satisfaction:
                    return "satisfaction";
                case Telemetry::AIAgentRideListColumn::Profit:
                    return "profit";
                case Telemetry::AIAgentRideListColumn::TotalCustomers:
                    return "totalCustomers";
                case Telemetry::AIAgentRideListColumn::TotalProfit:
                    return "totalProfit";
                case Telemetry::AIAgentRideListColumn::Customers:
                    return "customers";
                case Telemetry::AIAgentRideListColumn::Age:
                    return "age";
                case Telemetry::AIAgentRideListColumn::Income:
                    return "income";
                case Telemetry::AIAgentRideListColumn::RunningCost:
                    return "runningCost";
                case Telemetry::AIAgentRideListColumn::QueueLength:
                    return "queueLength";
                case Telemetry::AIAgentRideListColumn::QueueTime:
                    return "queueTime";
                case Telemetry::AIAgentRideListColumn::Reliability:
                    return "reliability";
                case Telemetry::AIAgentRideListColumn::DownTime:
                    return "downTime";
                case Telemetry::AIAgentRideListColumn::LastInspection:
                    return "lastInspection";
                case Telemetry::AIAgentRideListColumn::GuestsFavourite:
                    return "guestsFavourite";
                case Telemetry::AIAgentRideListColumn::Excitement:
                    return "excitement";
                case Telemetry::AIAgentRideListColumn::Intensity:
                    return "intensity";
                case Telemetry::AIAgentRideListColumn::Nausea:
                    return "nausea";
            }
            return "status";
        }

        json_t SerialiseFollowHint(const Telemetry::AIAgentFollowHint& hint)
        {
            json_t node = json_t::object();
            node["contextLabel"] = hint.contextLabel;
            node["sourceMethod"] = hint.sourceMethod;
            node["requestCameraFocus"] = hint.requestCameraFocus;
            node["requestWindowFocus"] = hint.requestWindowFocus;
            if (hint.cameraTarget)
            {
                json_t camera = json_t::object();
                camera["x"] = hint.cameraTarget->x;
                camera["y"] = hint.cameraTarget->y;
                camera["z"] = hint.cameraTarget->z;
                node["camera"] = camera;
            }

            std::visit(
                [&](const auto& intent) {
                    using T = std::decay_t<decltype(intent)>;
                    if constexpr (std::is_same_v<T, std::monostate>)
                    {
                        return;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::RideWindowIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "ride";
                        window["rideId"] = intent.rideId.ToUnderlying();
                        window["page"] = RidePageToString(intent.page);
                        node["window"] = window;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::StaffWindowIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "staff";
                        window["id"] = intent.staffId.ToUnderlying();
                        node["window"] = window;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::GuestWindowIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "guest";
                        window["id"] = intent.guestId.ToUnderlying();
                        node["window"] = window;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::ParkWindowIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "park";
                        window["page"] = ParkPageToString(intent.page);
                        node["window"] = window;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::ConstructRideIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "constructRide";
                        window["tab"] = ConstructRideTabToString(intent.tab);
                        node["window"] = window;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::RideListWindowIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "rideList";
                        switch (intent.filter)
                        {
                            case Telemetry::AIAgentRideListFilter::Rides:
                                window["filter"] = "rides";
                                break;
                            case Telemetry::AIAgentRideListFilter::Shops:
                                window["filter"] = "shops";
                                break;
                            case Telemetry::AIAgentRideListFilter::Facilities:
                                window["filter"] = "facilities";
                                break;
                        }
                        if (intent.column.has_value())
                        {
                            window["column"] = RideListColumnToString(*intent.column);
                        }
                        if (intent.sortDescending.has_value())
                        {
                            window["sortDescending"] = *intent.sortDescending;
                        }
                        node["window"] = window;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::FinancesWindowIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "finances";
                        window["page"] = FinancesPageToString(intent.page);
                        node["window"] = window;
                    }
                    else if constexpr (std::is_same_v<T, Telemetry::GenericWindowIntent>)
                    {
                        json_t window = json_t::object();
                        window["type"] = "window";
                        window["class"] = WindowClassToString(intent.windowClass);
                        node["window"] = window;
                    }
                },
                hint.windowIntent);

            return node;
        }

        RpcResult Dispatch(std::string_view method, const json_t& params)
        {
            auto& registry = Rpc::HandlerRegistry::Instance();
            return registry.Dispatch(method, params);
        }
    } // namespace

    struct JsonRpcServer::Client
    {
        explicit Client(std::unique_ptr<ITcpSocket> socket)
            : connection(std::move(socket))
        {
        }

        std::unique_ptr<ITcpSocket> connection;
        std::string buffer;
    };

    JsonRpcServer::JsonRpcServer(ScriptEngine& engine)
        : _engine(engine)
    {
    }

    JsonRpcServer::~JsonRpcServer()
    {
        Stop();
    }

    void JsonRpcServer::Start()
    {
        // Initialize all handler registrations (forces linker to include handler object files)
        Rpc::HandlerRegistry::InitializeAllHandlers();

        if (_running)
        {
            return;
        }

        int32_t port = gJsonRpcServerPort > 0 ? gJsonRpcServerPort : kDefaultPort;
        try
        {
            _listener = Network::CreateTcpSocket();
            _listener->Listen("127.0.0.1", port);
            LOG_INFO("JsonRpcServer listening on 127.0.0.1:%d", port);
            _running = true;
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR("JsonRpcServer failed to start: %s", ex.what());
            _listener.reset();
            _running = false;
        }
    }

    void JsonRpcServer::Stop()
    {
        if (_listener)
        {
            _listener->Close();
            _listener.reset();
        }
        for (auto& client : _clients)
        {
            if (client && client->connection)
            {
                client->connection->Close();
            }
        }
        _clients.clear();
        _running = false;
    }

    void JsonRpcServer::Tick()
    {
        if (!_running)
        {
            return;
        }

        AcceptClients();

        for (auto it = _clients.begin(); it != _clients.end();)
        {
            auto& client = **it;
            if (!ServiceClient(client))
            {
                it = _clients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void JsonRpcServer::AcceptClients()
    {
        if (!_listener)
        {
            return;
        }

        while (true)
        {
            auto socket = _listener->Accept();
            if (!socket)
            {
                break;
            }
            socket->SetNoDelay(true);
            _clients.emplace_back(std::make_unique<Client>(std::move(socket)));
        }
    }

    bool JsonRpcServer::ServiceClient(Client& client)
    {
        std::array<std::byte, kReadChunk> buffer{};
        bool keepOpen = true;
        while (true)
        {
            size_t bytesRead = 0;
            auto status = client.connection->ReceiveData(buffer.data(), buffer.size(), &bytesRead);
            if (status == ReadPacket::success && bytesRead > 0)
            {
                client.buffer.append(reinterpret_cast<const char*>(buffer.data()), bytesRead);
                if (client.buffer.size() > kMaxBufferedBytes)
                {
                    return false;
                }
                ProcessClientBuffer(client);
            }
            else if (status == ReadPacket::noData)
            {
                break;
            }
            else
            {
                keepOpen = false;
                break;
            }
        }
        return keepOpen;
    }

    void JsonRpcServer::ProcessClientBuffer(Client& client)
    {
        size_t newlinePos = std::string::npos;
        while ((newlinePos = client.buffer.find('\n')) != std::string::npos)
        {
            std::string line = client.buffer.substr(0, newlinePos);
            client.buffer.erase(0, newlinePos + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty())
            {
                continue;
            }
            HandleLine(client, line);
        }
    }

    void JsonRpcServer::HandleLine(Client& client, std::string_view line)
    {
        json_t message;
        try
        {
            message = json_t::parse(line);
        }
        catch (const std::exception&)
        {
            SendError(client, json_t(), -32700, "Parse error");
            return;
        }

        if (!message.is_object())
        {
            SendError(client, json_t(), -32600, "Invalid request");
            return;
        }

        HandleMessage(client, message);
    }

    void JsonRpcServer::HandleMessage(Client& client, const json_t& message)
    {
        auto idIt = message.find("id");
        const bool hasId = idIt != message.end();
        const json_t id = hasId ? *idIt : json_t();

        const auto jsonrpcIt = message.find("jsonrpc");
        if (jsonrpcIt == message.end() || !jsonrpcIt->is_string() || jsonrpcIt->get<std::string>() != "2.0")
        {
            SendError(client, hasId ? id : json_t(), -32600, "Invalid request");
            return;
        }

        const auto methodIt = message.find("method");
        if (methodIt == message.end() || !methodIt->is_string())
        {
            SendError(client, hasId ? id : json_t(), -32600, "Invalid request");
            return;
        }

        const std::string method = methodIt->get<std::string>();
        json_t params = json_t();
        if (auto paramsIt = message.find("params"); paramsIt != message.end())
        {
            params = *paramsIt;
        }

        Telemetry::AIAgentActivityEvent startedEvent;
        startedEvent.phase = Telemetry::AIAgentActivityPhase::Started;
        startedEvent.method = method;
        startedEvent.label = method;
        startedEvent.success = true;
        Telemetry::AIAgentActivityFeed::Instance().Publish(startedEvent);

        RpcResult result;
        try
        {
            result = Dispatch(method, params);
        }
        catch (const std::exception& ex)
        {
            result = RpcResult::Error(kErrorServerError, std::string("Internal error: ") + ex.what());
        }
        catch (...)
        {
            result = RpcResult::Error(kErrorServerError, "Unknown internal error");
        }
        std::optional<json_t> followHintPayload;
        if (result.success && result.followHint)
        {
            followHintPayload = SerialiseFollowHint(*result.followHint);
        }

        Telemetry::AIAgentActivityEvent completedEvent;
        completedEvent.phase = result.success ? Telemetry::AIAgentActivityPhase::Completed : Telemetry::AIAgentActivityPhase::Failed;
        completedEvent.method = method;
        completedEvent.success = result.success;
        if (result.success && result.followHint)
        {
            completedEvent.label = result.followHint->contextLabel;
            completedEvent.followHint = result.followHint;
        }
        else
        {
            completedEvent.label = result.success ? method : (result.errorMessage.empty() ? method : result.errorMessage);
        }
        Telemetry::AIAgentActivityFeed::Instance().Publish(completedEvent);

        if (!hasId)
        {
            return;
        }

        if (result.success)
        {
            SendResponse(client, id, result.payload, followHintPayload ? &*followHintPayload : nullptr);
        }
        else
        {
            SendError(client, id, result.errorCode, result.errorMessage);
        }
    }

    void JsonRpcServer::SendResponse(Client& client, const json_t& id, const json_t& result, const json_t* followHint)
    {
        json_t response = json_t::object();
        response["jsonrpc"] = "2.0";
        response["id"] = id;
        response["result"] = result;
        if (followHint != nullptr)
        {
            response["followHint"] = *followHint;
        }

        auto payload = response.dump();
        payload.push_back('\n');
        client.connection->SendData(payload.data(), payload.size());
    }

    void JsonRpcServer::SendError(Client& client, const json_t& id, int32_t code, std::string_view message)
    {
        json_t response = json_t::object();
        response["jsonrpc"] = "2.0";
        response["id"] = id.is_null() ? nullptr : id;
        json_t error = json_t::object();
        error["code"] = code;
        error["message"] = message;
        response["error"] = error;

        auto payload = response.dump();
        payload.push_back('\n');
        if (client.connection)
        {
            client.connection->SendData(payload.data(), payload.size());
        }
    }
} // namespace OpenRCT2::Scripting

#endif // ENABLE_SCRIPTING
