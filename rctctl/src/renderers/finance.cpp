#include "rctctl/renderers/finance.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;
}

void RenderFinanceSummary(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Finance Snapshot");
    canvas.KeyValue("Cash", util::FormatCurrency(result.value("cash", 0.0)));
    canvas.KeyValue("Loan", util::FormatCurrency(result.value("loan", 0.0)) + " / "
                                 + util::FormatCurrency(result.value("loanMax", 0.0)));
    canvas.KeyValue("Park value", util::FormatCurrency(result.value("parkValue", 0.0)));
    canvas.KeyValue("Company value", util::FormatCurrency(result.value("companyValue", 0.0)));

    const auto& profit = result.value("operatingProfit", json::object());
    canvas.KeyValue("Operating profit",
        util::FormatCurrency(profit.value("thisMonth", 0.0)) + " / "
            + util::FormatCurrency(profit.value("lastMonth", 0.0)));

    const auto& categories = result.value("categories", json::array());
    if (!categories.empty())
    {
        TableView table;
        table.headers = { "Category", "This Month", "Last Month" };
        for (const auto& category : categories)
        {
            auto label = category.value("label", category.value("key", std::string("category")));
            auto current = util::FormatCurrency(category.value("thisMonth", 0.0));
            auto last = util::FormatCurrency(category.value("lastMonth", 0.0));
            table.rows.push_back({ label, current, last });
        }
        canvas.Section("Category Breakdown");
        canvas.Table(table);
    }
}

void RenderFinanceHistory(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Finance History");

    const auto& cashHistory = result.value("cashHistory", json::array());
    const auto& profitHistory = result.value("profitHistory", json::array());
    const auto& valueHistory = result.value("valueHistory", json::array());

    if (!cashHistory.empty())
    {
        canvas.Section("Cash History");
        TableView table;
        table.headers = { "Week", "Amount" };
        for (const auto& record : cashHistory)
        {
            table.rows.push_back(
                { std::to_string(record.value("index", 0)), util::FormatCurrency(record.value("value", 0.0)) });
        }
        canvas.Table(table);
    }

    if (!profitHistory.empty())
    {
        canvas.Section("Weekly Profit History");
        TableView table;
        table.headers = { "Week", "Profit" };
        for (const auto& record : profitHistory)
        {
            table.rows.push_back(
                { std::to_string(record.value("index", 0)), util::FormatCurrency(record.value("value", 0.0)) });
        }
        canvas.Table(table);
    }

    if (!valueHistory.empty())
    {
        canvas.Section("Park Value History");
        TableView table;
        table.headers = { "Week", "Value" };
        for (const auto& record : valueHistory)
        {
            table.rows.push_back(
                { std::to_string(record.value("index", 0)), util::FormatCurrency(record.value("value", 0.0)) });
        }
        canvas.Table(table);
    }

    if (cashHistory.empty() && profitHistory.empty() && valueHistory.empty())
    {
        canvas.Paragraph("No finance history recorded yet.");
    }
}

void RenderLoanStatus(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Loan");
    std::ostringstream loanLine;
    loanLine << util::FormatCurrency(result.value("loan", 0.0)) << " / "
             << util::FormatCurrency(result.value("loanMax", 0.0)) << " @ "
             << result.value("interestRate", 0) << "%";
    canvas.KeyValue("Balance", loanLine.str());
    canvas.KeyValue("Cash on hand", util::FormatCurrency(result.value("cash", 0.0)));
}

} // namespace rctctl::renderers
