#include "rctctl/renderers/shops.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;

std::vector<std::string> ExtractItemLabels(const json& items)
{
    std::vector<std::string> labels;
    for (const auto& item : items)
    {
        std::string label = item.value("label", std::string());
        if (label.empty())
        {
            auto id = item.value("id", -1);
            label = id >= 0 ? ("item#" + std::to_string(id)) : std::string("item");
        }
        labels.push_back(std::move(label));
    }
    return labels;
}

std::string JoinItemList(const std::vector<std::string>& labels)
{
    if (labels.empty())
    {
        return std::string();
    }
    std::ostringstream oss;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        if (i != 0)
        {
            oss << ", ";
        }
        oss << labels[i];
    }
    return oss.str();
}

std::string BuildSelectorLabel(const json& entry)
{
    std::vector<std::string> selectors;
    auto name = entry.value("name", std::string());
    if (!name.empty())
    {
        selectors.push_back("--name \"" + name + "\"");
    }

    auto entryIndex = entry.value("entryIndex", -1);
    if (entryIndex >= 0)
    {
        selectors.push_back("--entry-index " + std::to_string(entryIndex));
    }

    auto descriptorId = entry.value("descriptorIdentifier", std::string());
    if (!descriptorId.empty())
    {
        selectors.push_back("--type " + descriptorId);
    }

    auto legacyId = entry.value("identifier", std::string());
    if (!legacyId.empty() && legacyId != descriptorId)
    {
        selectors.push_back("--type " + legacyId);
    }

    if (selectors.empty())
    {
        return std::string("-");
    }

    std::ostringstream oss;
    for (size_t i = 0; i < selectors.size(); ++i)
    {
        if (i != 0)
        {
            oss << " or ";
        }
        oss << selectors[i];
    }
    return oss.str();
}
} // namespace

void RenderShopCatalog(const json& result)
{
    const auto& entries = result.value("entries", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Shop Catalog");
    canvas.KeyValue("Entries", static_cast<int>(entries.size()));

    TableView table;
    table.headers = { "Name", "Type", "Items", "Build", "Use with" };
    for (const auto& entry : entries)
    {
        auto name = entry.value("name", entry.value("identifier", std::string()));
        auto rideType = entry.value("rideType", std::string());
        auto classification = entry.value("classification", std::string());
        auto buildCost = entry.value("buildCost", 0.0);
        auto labels = ExtractItemLabels(entry.value("items", json::array()));

        std::string typeLabel = rideType;
        if (!classification.empty())
        {
            if (!typeLabel.empty())
            {
                typeLabel += " / ";
            }
            typeLabel += classification;
        }

        table.rows.push_back({ name, typeLabel, JoinItemList(labels), util::FormatCurrency(buildCost),
            BuildSelectorLabel(entry) });
    }
    canvas.Table(table);
}

void RenderShopList(const json& result)
{
    const auto& entries = result.value("entries", json::array());
    if (entries.empty())
    {
        TextCanvas canvas(std::cout);
        canvas.Section("Shops");
        canvas.Paragraph("No active shops/stalls.");
        return;
    }

    TextCanvas canvas(std::cout);
    canvas.Section("Shops");
    canvas.KeyValue("Active", static_cast<int>(entries.size()));

    TableView table;
    table.headers = { "Ride", "Status", "Price", "Queue", "Income/hr", "Profit/hr", "Items" };

    for (const auto& entry : entries)
    {
        const auto& tile = entry.value("tile", json::object());
        const auto& object = entry.value("object", json::object());
        auto labels = ExtractItemLabels(entry.value("items", json::array()));

        auto id = entry.value("id", -1);
        auto name = entry.value("name", std::string("(unnamed)"));
        auto identifier = object.value("identifier", std::string());
        auto status = entry.value("status", std::string());
        auto direction = tile.value("direction", std::string());
        auto price = entry.value("price", 0.0);
        auto queueLength = entry.value("queueLength", 0);
        auto customers = entry.value("customersInterval", 0);
        auto incomePerHour = entry.value("incomePerHour", 0.0);
        // Use profitPerHour if available, otherwise fall back to profitThisMonth
        auto profitHour = entry.contains("profitPerHour") ? entry.value("profitPerHour", 0.0)
                                                          : entry.value("profitThisMonth", 0.0);

        std::ostringstream rideLabel;
        rideLabel << "#" << id << " " << name;
        if (!identifier.empty())
        {
            rideLabel << " [" << identifier << ']';
        }
        if (tile.contains("x") && tile.contains("y"))
        {
            rideLabel << " @(" << tile.value("x", 0) << ", " << tile.value("y", 0);
            if (tile.contains("z"))
            {
                rideLabel << ", z=" << tile.value("z", 0);
            }
            rideLabel << ')';
        }
        if (!direction.empty())
        {
            rideLabel << " facing " << direction;
        }

        std::string priceLabel = util::FormatCurrency(price);
        if (entry.contains("secondaryPrice"))
        {
            priceLabel += " / " + util::FormatCurrency(entry.value("secondaryPrice", 0.0));
        }

        std::ostringstream queueInfo;
        queueInfo << queueLength << " guests / interval " << customers;

        table.rows.push_back({ rideLabel.str(), status, priceLabel, queueInfo.str(),
            util::FormatCurrency(incomePerHour), util::FormatCurrency(profitHour), JoinItemList(labels) });
    }

    canvas.Table(table);
}

void RenderShopPlacement(const json& result)
{
    const auto& ride = result.value("ride", json::object());
    const auto& object = result.value("object", json::object());
    const auto& tile = result.value("tile", json::object());
    const auto& costs = result.value("costBreakdown", json::object());

    auto rideName = ride.value("name", std::string("stall"));
    auto rideType = ride.value("rideType", std::string());
    auto classification = ride.value("classification", std::string());
    auto rideStatus = ride.value("status", std::string());
    auto identifier = object.value("identifier", std::string());
    auto direction = result.value("direction", std::string());
    int tileX = tile.value("x", 0);
    int tileY = tile.value("y", 0);
    int tileZ = tile.value("z", 0);

    auto totalCost = result.value("cost", 0.0);
    auto createCost = costs.value("create", 0.0);
    auto buildCost = costs.value("build", 0.0);

    auto labels = ExtractItemLabels(ride.value("items", json::array()));

    TextCanvas canvas(std::cout);
    canvas.Section("Shop Placement");
    std::ostringstream rideLabel;
    rideLabel << (classification.empty() ? "stall" : classification) << " \"" << rideName << "\" ("
              << identifier;
    if (!rideType.empty())
    {
        rideLabel << ", type=" << rideType;
    }
    rideLabel << ')';
    canvas.KeyValue("Ride", rideLabel.str());

    if (!rideStatus.empty())
    {
        canvas.KeyValue("Status", rideStatus);
    }

    std::ostringstream location;
    location << '(' << tileX << ", " << tileY << ") z=" << tileZ;
    if (!direction.empty())
    {
        location << " facing " << direction << " (auto)";
    }
    canvas.KeyValue("Location", location.str());

    if (!labels.empty())
    {
        canvas.KeyValue("Items", JoinItemList(labels));
    }

    canvas.KeyValue("Total cost", util::FormatCurrency(totalCost));
    canvas.KeyValue("Create cost", util::FormatCurrency(createCost));
    canvas.KeyValue("Build cost", util::FormatCurrency(buildCost));
}

void RenderShopRemoval(const json& result)
{
    const auto& ride = result.value("ride", json::object());
    auto rideName = ride.value("name", std::string());
    int rideId = ride.value("id", -1);
    auto classification = ride.value("typeName", std::string());
    auto cost = result.value("cost", 0.0);

    TextCanvas canvas(std::cout);
    canvas.Section("Shop Removal");
    std::ostringstream rideLabel;
    rideLabel << (classification.empty() ? "shop" : classification) << ' ';
    if (!rideName.empty())
    {
        rideLabel << '"' << rideName << '"';
    }
    else
    {
        rideLabel << "ride#" << rideId;
    }
    if (rideId >= 0)
    {
        rideLabel << " (id " << rideId << ')';
    }
    canvas.KeyValue("Ride", rideLabel.str());

    if (auto tileIt = result.find("tile"); tileIt != result.end() && tileIt->is_object())
    {
        const auto& tile = *tileIt;
        std::ostringstream location;
        location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ')';
        if (tile.contains("z"))
        {
            location << " z=" << tile.value("z", 0);
        }
        canvas.KeyValue("From", location.str());
    }

    if (cost != 0.0)
    {
        canvas.KeyValue("Refund", util::FormatCurrency(cost));
    }
}

void RenderShopPrice(const json& result, bool announceChange)
{
    auto id = result.value("id", -1);
    auto name = result.value("name", std::string("(unnamed shop)"));
    const auto& items = result.value("items", json::array());

    TextCanvas canvas(std::cout);
    canvas.Section(announceChange ? "Shop Price Updated" : "Shop Pricing");

    std::ostringstream shopLabel;
    shopLabel << "#" << id << " " << name;
    canvas.KeyValue("Shop", shopLabel.str());

    if (announceChange)
    {
        // Price change view - show before/after
        auto price = result.value("price", 0.0);
        auto previousPrice = result.value("previousPrice", 0.0);
        auto itemLabel = result.value("itemLabel", std::string());
        bool secondary = result.value("secondary", false);

        std::string priceLabel = secondary ? "Secondary price" : "Price";
        if (!itemLabel.empty())
        {
            priceLabel = itemLabel;
        }

        canvas.KeyValue(priceLabel, util::FormatCurrency(previousPrice) + " -> " + util::FormatCurrency(price));
    }
    else
    {
        // Price view - show all items with their prices
        if (items.empty())
        {
            canvas.Paragraph("No priced items.");
        }
        else
        {
            TableView table;
            table.headers = { "Item", "Current Price", "Default Price" };
            for (const auto& item : items)
            {
                auto label = item.value("label", std::string("item"));
                auto price = item.value("price", 0.0);
                auto defaultPrice = item.value("defaultPrice", 0.0);
                auto priceIndex = item.value("priceIndex", 0);

                std::string indexNote = priceIndex == 0 ? "" : " (secondary)";
                table.rows.push_back({ label + indexNote, util::FormatCurrency(price), util::FormatCurrency(defaultPrice) });
            }
            canvas.Table(table);
        }
    }
}

void RenderShopFinances(const json& result)
{
    const auto& shops = result.value("shops", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Shop Finances");
    canvas.KeyValue("Shops", static_cast<int>(shops.size()));

    if (shops.empty())
    {
        canvas.Paragraph("No shops in this park.");
        return;
    }

    TableView table;
    table.headers = { "Shop", "Status", "Profit/hr", "Income/hr", "Cost/hr", "Total Profit" };

    for (const auto& shop : shops)
    {
        auto id = shop.value("id", -1);
        auto name = shop.value("name", std::string("(unnamed)"));
        auto status = shop.value("status", std::string());

        std::ostringstream shopLabel;
        shopLabel << "#" << id << " " << name;

        std::string profit = shop["profit"].is_null() ? "-" : util::FormatCurrency(shop.value("profit", 0.0));
        std::string income = shop["income"].is_null() ? "-" : util::FormatCurrency(shop.value("income", 0.0));
        std::string cost = shop["runningCost"].is_null() ? "-" : util::FormatCurrency(shop.value("runningCost", 0.0));
        std::string totalProfit = shop["totalProfit"].is_null() ? "-" : util::FormatCurrency(shop.value("totalProfit", 0.0));

        table.rows.push_back({ shopLabel.str(), status, profit, income, cost, totalProfit });
    }

    canvas.Table(table);
}

void RenderShopPerformance(const json& result)
{
    const auto& shops = result.value("shops", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Shop Performance");
    canvas.KeyValue("Shops", static_cast<int>(shops.size()));

    if (shops.empty())
    {
        canvas.Paragraph("No shops in this park.");
        return;
    }

    TableView table;
    table.headers = { "Shop", "Status", "Pop%", "Sat%", "Total Cust.", "Cust/hr" };

    for (const auto& shop : shops)
    {
        auto id = shop.value("id", -1);
        auto name = shop.value("name", std::string("(unnamed)"));
        auto status = shop.value("status", std::string());

        std::ostringstream shopLabel;
        shopLabel << "#" << id << " " << name;

        std::string pop = shop["popularity"].is_null() ? "-" : std::to_string(shop.value("popularity", 0)) + "%";
        std::string sat = shop["satisfaction"].is_null() ? "-" : std::to_string(shop.value("satisfaction", 0)) + "%";
        std::string totalCust = std::to_string(shop.value("totalCustomers", 0));
        std::string custHr = std::to_string(shop.value("customersPerHour", 0));

        table.rows.push_back({ shopLabel.str(), status, pop, sat, totalCust, custHr });
    }

    canvas.Table(table);
}

// ============================================================================
// Facilities Renderers
// ============================================================================

void RenderFacilitiesList(const json& result)
{
    const auto& facilities = result.value("facilities", json::array());
    if (facilities.empty())
    {
        TextCanvas canvas(std::cout);
        canvas.Section("Facilities");
        canvas.Paragraph("No active facilities (kiosks, toilets, ATMs).");
        return;
    }

    TextCanvas canvas(std::cout);
    canvas.Section("Facilities");
    canvas.KeyValue("Active", static_cast<int>(facilities.size()));

    TableView table;
    table.headers = { "Facility", "Type", "Status", "Price", "Total Cust.", "Income/hr" };

    for (const auto& facility : facilities)
    {
        const auto& tile = facility.value("tile", json::object());

        auto id = facility.value("id", -1);
        auto name = facility.value("name", std::string("(unnamed)"));
        auto rideType = facility.value("rideType", std::string());
        auto status = facility.value("status", std::string());
        auto price = facility.value("price", 0.0);
        auto totalCustomers = facility.value("totalCustomers", 0);
        auto incomePerHour = facility.value("incomePerHour", 0.0);

        std::ostringstream facilityLabel;
        facilityLabel << "#" << id << " " << name;
        if (tile.contains("x") && tile.contains("y"))
        {
            facilityLabel << " @(" << tile.value("x", 0) << ", " << tile.value("y", 0) << ")";
        }

        table.rows.push_back({
            facilityLabel.str(),
            rideType,
            status,
            util::FormatCurrency(price),
            std::to_string(totalCustomers),
            util::FormatCurrency(incomePerHour)
        });
    }

    canvas.Table(table);
}

void RenderFacilityFinances(const json& result)
{
    const auto& facilities = result.value("facilities", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Facility Finances");
    canvas.KeyValue("Facilities", static_cast<int>(facilities.size()));

    if (facilities.empty())
    {
        canvas.Paragraph("No facilities in this park.");
        return;
    }

    TableView table;
    table.headers = { "Facility", "Status", "Profit/hr", "Income/hr", "Cost/hr", "Total Profit" };

    for (const auto& facility : facilities)
    {
        auto id = facility.value("id", -1);
        auto name = facility.value("name", std::string("(unnamed)"));
        auto status = facility.value("status", std::string());

        std::ostringstream facilityLabel;
        facilityLabel << "#" << id << " " << name;

        std::string profit = facility["profit"].is_null() ? "-" : util::FormatCurrency(facility.value("profit", 0.0));
        std::string income = facility["income"].is_null() ? "-" : util::FormatCurrency(facility.value("income", 0.0));
        std::string cost = facility["runningCost"].is_null() ? "-" : util::FormatCurrency(facility.value("runningCost", 0.0));
        std::string totalProfit = facility["totalProfit"].is_null() ? "-" : util::FormatCurrency(facility.value("totalProfit", 0.0));

        table.rows.push_back({ facilityLabel.str(), status, profit, income, cost, totalProfit });
    }

    canvas.Table(table);
}

void RenderFacilityPerformance(const json& result)
{
    const auto& facilities = result.value("facilities", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Facility Performance");
    canvas.KeyValue("Facilities", static_cast<int>(facilities.size()));

    if (facilities.empty())
    {
        canvas.Paragraph("No facilities in this park.");
        return;
    }

    TableView table;
    table.headers = { "Facility", "Status", "Pop%", "Sat%", "Total Cust.", "Cust/hr" };

    for (const auto& facility : facilities)
    {
        auto id = facility.value("id", -1);
        auto name = facility.value("name", std::string("(unnamed)"));
        auto status = facility.value("status", std::string());

        std::ostringstream facilityLabel;
        facilityLabel << "#" << id << " " << name;

        std::string pop = facility["popularity"].is_null() ? "-" : std::to_string(facility.value("popularity", 0)) + "%";
        std::string sat = facility["satisfaction"].is_null() ? "-" : std::to_string(facility.value("satisfaction", 0)) + "%";
        std::string totalCust = std::to_string(facility.value("totalCustomers", 0));
        std::string custHr = std::to_string(facility.value("customersPerHour", 0));

        table.rows.push_back({ facilityLabel.str(), status, pop, sat, totalCust, custHr });
    }

    canvas.Table(table);
}

} // namespace rctctl::renderers
