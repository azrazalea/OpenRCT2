#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "rctctl/renderers/park.hpp"
#include "rctctl/renderers/context.hpp"

#include <iostream>
#include <sstream>

using nlohmann::json;

namespace {

std::string CaptureParkStatus(const json& payload)
{
    std::ostringstream oss;
    auto* oldBuf = std::cout.rdbuf(oss.rdbuf());
    rctctl::renderers::RenderContext context;
    rctctl::renderers::ScopedRenderContext scoped(context);
    rctctl::renderers::RenderParkStatus(payload);
    std::cout.rdbuf(oldBuf);
    return oss.str();
}

} // namespace

TEST(RctctlRendererSnapshot, ParkStatus)
{
    json payload;
    payload["name"] = "Dreamland";
    payload["scenario"] = { { "name", "Forest Frontiers" }, { "goalSummary", "Reach 700 guests" } };
    payload["isOpen"] = true;
    payload["guests"] = 742;
    payload["guestsHeading"] = 15;
    payload["parkRating"] = 999;
    payload["date"] = { { "day", 3 }, { "month", 0 }, { "year", 1 } };
    payload["entranceFee"] = 25.50;
    payload["cash"] = 1200.0;
    payload["loan"] = 5000.0;
    payload["loanMax"] = 10000.0;
    payload["parkValue"] = 15000.0;
    payload["companyValue"] = 9000.0;

    const char* expected =
        "Park Overview\n"
        "-------------\n"
        "Name        : Dreamland\n"
        "Scenario    : Forest Frontiers\n"
        "Reach 700 guests\n"
        "Gate        : open\n"
        "Guests      : 742 (15 heading)\n"
        "Rating      : 999\n"
        "Date        : March 3, Year 1\n"
        "\n"
        "Finances\n"
        "--------\n"
        "Entrance fee : $25.50\n"
        "Cash        : $1200.00\n"
        "Loan        : $5000.00 / $10000.00\n"
        "Park value  : $15000.00\n"
        "Company value : $9000.00\n";

    EXPECT_EQ(CaptureParkStatus(payload), expected);
}
