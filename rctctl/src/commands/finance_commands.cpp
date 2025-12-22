#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/finance.hpp"

#include <nlohmann/json.hpp>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;
}

void AppendFinanceCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "finance",
        { "status" },
        "Show finance status.",
        "Displays cash, loan, profit, and per-category breakdown.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "finance.status", json::object() };
        },
        renderers::RenderFinanceSummary });

    specs.push_back(CommandSpec{
        "finance",
        { "history" },
        "Show finance histories.",
        "Returns cash, profit, and park value history arrays.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "finance.history", json::object() };
        },
        renderers::RenderFinanceHistory });

    specs.push_back(CommandSpec{
        "loans",
        { "status" },
        "Show loan status.",
        "Displays current loan, limit, interest rate, and cash.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "loans.status", json::object() };
        },
        renderers::RenderLoanStatus });

    specs.push_back(CommandSpec{
        "loans",
        { "set" },
        "Change loan amount.",
        "Sets the current loan amount (borrows or repays).",
        { CommandArgSpec{ "value", "Loan value (e.g., 5000).", true, "AMOUNT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            params["value"] = cli::RequireDoubleOption(args, { "value" }, "loan amount");
            return CommandPlan{ "loans.set", params };
        },
        renderers::RenderLoanStatus });
}

} // namespace rctctl::commands
