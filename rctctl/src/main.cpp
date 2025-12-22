#include "rctctl/cli/cli.hpp"
#include "rctctl/commands/registry.hpp"
#include "rctctl/renderers/context.hpp"
#include "rctctl/rpc/json_rpc_client.hpp"
#include "rctctl/util/string_utils.hpp"

#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

int main(int argc, char** argv)
{
    int exitCode = 0;
    json rpcResult;
    rctctl::cli::CommandPlan plan;

    try
    {
        auto cli = rctctl::cli::ParseCli(argc, argv);
        if (cli.helpRequested)
        {
            if (cli.resourceHelpRequested)
            {
                if (!rctctl::commands::PrintResourceUsage(cli.resource))
                {
                    return 1;
                }
            }
            else
            {
                rctctl::commands::PrintUsage();
            }
            return 0;
        }

        auto split = rctctl::cli::SplitCommandTokens(cli.commandTokens);
        std::vector<std::string> commandPath;
        commandPath.push_back(cli.action);
        commandPath.insert(commandPath.end(), split.subcommands.begin(), split.subcommands.end());

        const auto* spec = rctctl::commands::FindCommandSpec(cli.resource, commandPath);
        if (spec == nullptr)
        {
            // If --help was requested on a subcommand prefix, try to show subcommand help
            if (split.helpRequested)
            {
                if (rctctl::commands::PrintSubcommandUsage(cli.resource, commandPath))
                {
                    return 0;
                }
            }

            // Check if the resource itself exists to provide better error messaging
            bool resourceExists = false;
            for (const auto& registeredSpec : rctctl::commands::GetCommandRegistry())
            {
                if (registeredSpec.resource == rctctl::util::ToLower(cli.resource))
                {
                    resourceExists = true;
                    break;
                }
            }

            std::string errorMsg = "Invalid: Unknown command: " + rctctl::cli::BuildCommandLabel(cli.resource, commandPath);
            if (resourceExists)
            {
                errorMsg += ". Try 'rctctl " + cli.resource + " --help' for available commands.";
            }
            else
            {
                errorMsg += ". Try 'rctctl --help' to see available resources.";
            }
            throw std::runtime_error(errorMsg);
        }

        if (split.helpRequested)
        {
            rctctl::commands::PrintCommandHelp(*spec);
            return 0;
        }

        auto parsedArgs = rctctl::cli::ParseCommandArguments(split.flagTokens);
        plan = spec->buildPlan(parsedArgs);

        // Check if this is a local command (no RPC call needed)
        if (plan.method == "__LOCAL__")
        {
            rpcResult = plan.params;
        }
        else
        {
            rctctl::rpc::JsonRpcClient client(cli.host, cli.port);
            rpcResult = client.Call(plan.method, plan.params);
        }

        if (cli.jsonOutput || !spec->textRenderer)
        {
            std::cout << rpcResult.dump(2) << '\n';
        }
        else
        {
            rctctl::renderers::RenderContext renderContext;
            renderContext.columns = &cli.columns;
            if (cli.filter)
            {
                renderContext.filter = &cli.filter;
            }
            renderContext.watch = cli.watch;
            renderContext.watchIntervalSeconds = cli.watchIntervalSeconds;
            rctctl::renderers::ScopedRenderContext scoped(renderContext);
            spec->textRenderer(rpcResult);
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "rctctl: " << ex.what() << '\n';
        exitCode = 1;
    }

    return exitCode;
}
