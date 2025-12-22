#include "rctctl/renderers/scenery.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;

std::string JoinFlags(const json& flags)
{
    if (!flags.is_array() || flags.empty())
    {
        return "-";
    }
    std::ostringstream oss;
    bool first = true;
    for (const auto& flag : flags)
    {
        if (!first)
        {
            oss << ", ";
        }
        oss << flag.get<std::string>();
        first = false;
    }
    return oss.str();
}
} // namespace

void RenderSceneryCatalog(const json& result)
{
    const auto& entries = result.value("entries", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Scenery Catalog");
    canvas.KeyValue("Entries", static_cast<int>(entries.size()));

    if (entries.empty())
    {
        canvas.Paragraph("No scenery items available.");
        return;
    }

    TableView table;
    table.headers = { "Name", "Price", "Height", "Flags", "Use with" };
    for (const auto& entry : entries)
    {
        auto name = entry.value("name", entry.value("identifier", std::string()));
        auto identifier = entry.value("identifier", std::string());
        auto price = entry.value("price", 0.0);
        auto height = entry.value("height", 0);
        auto flags = entry.value("flags", json::array());

        std::ostringstream useWith;
        useWith << "--scenery-id " << identifier;

        table.rows.push_back({ name, util::FormatCurrency(price), std::to_string(height), JoinFlags(flags),
            useWith.str() });
    }
    canvas.Table(table);
}

void RenderSceneryPlace(const json& result)
{
    const auto& scenery = result.value("scenery", json::object());
    const auto& tile = result.value("tile", json::object());
    auto identifier = scenery.value("identifier", std::string());
    int entryIndex = scenery.value("entryIndex", -1);
    auto name = !identifier.empty() ? identifier : ("entry#" + std::to_string(entryIndex));
    auto cost = result.value("cost", 0.0);

    TextCanvas canvas(std::cout);
    canvas.Section("Scenery Placed");
    canvas.KeyValue("Scenery", name);
    std::ostringstream location;
    location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ')';
    canvas.KeyValue("Tile", location.str());
    if (cost != 0.0)
    {
        canvas.KeyValue("Cost", util::FormatCurrency(cost));
    }
}

void RenderSceneryRemove(const json& result)
{
    const auto& scenery = result.value("scenery", json::object());
    const auto& tile = result.value("tile", json::object());
    auto identifier = scenery.value("identifier", std::string());
    int entryIndex = scenery.value("entryIndex", -1);
    auto name = !identifier.empty() ? identifier : ("entry#" + std::to_string(entryIndex));
    auto cost = result.value("cost", 0.0);

    TextCanvas canvas(std::cout);
    canvas.Section("Scenery Removed");
    canvas.KeyValue("Scenery", name);
    std::ostringstream location;
    location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ')';
    canvas.KeyValue("Tile", location.str());
    if (cost != 0.0)
    {
        canvas.KeyValue("Refund", util::FormatCurrency(cost));
    }
}

void RenderPathItemsCatalog(const json& result)
{
    const auto& entries = result.value("entries", json::array());
    auto categoryFilter = result.value("categoryFilter", std::string());
    TextCanvas canvas(std::cout);

    if (categoryFilter.empty())
    {
        canvas.Section("Path Items Catalog");
    }
    else
    {
        canvas.Section("Path Items Catalog (" + categoryFilter + ")");
    }
    canvas.KeyValue("Entries", static_cast<int>(entries.size()));

    if (entries.empty())
    {
        canvas.Paragraph("No path items available.");
        return;
    }

    TableView table;
    table.headers = { "Name", "Category", "Price", "Use with" };
    for (const auto& entry : entries)
    {
        auto name = entry.value("name", entry.value("identifier", std::string()));
        auto identifier = entry.value("identifier", std::string());
        auto category = entry.value("category", std::string());
        auto price = entry.value("price", 0.0);

        // Show unique identifier first, with alias hint for common categories
        std::string useWith = "--item-id " + identifier;
        if (category == "bench" || category == "bin" || category == "lamp" || category == "fountain")
        {
            useWith += "  (or alias: " + category + ")";
        }

        table.rows.push_back({ name, category, util::FormatCurrency(price), useWith });
    }
    canvas.Table(table);
}

void RenderPathItemsPlace(const json& result)
{
    const auto& item = result.value("item", json::object());
    const auto& tile = result.value("tile", json::object());
    auto identifier = item.value("identifier", std::string());
    auto category = item.value("category", std::string());
    int entryIndex = item.value("entryIndex", -1);
    auto name = item.value("name", !identifier.empty() ? identifier : ("entry#" + std::to_string(entryIndex)));
    auto cost = result.value("cost", 0.0);
    auto z = result.value("z", 0);

    TextCanvas canvas(std::cout);
    canvas.Section("Path Item Placed");
    canvas.KeyValue("Item", name);
    if (!category.empty())
    {
        canvas.KeyValue("Category", category);
    }
    std::ostringstream location;
    location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ") z=" << z;
    canvas.KeyValue("Tile", location.str());
    if (cost != 0.0)
    {
        canvas.KeyValue("Cost", util::FormatCurrency(cost));
    }
}

void RenderPathItemsRemove(const json& result)
{
    const auto& item = result.value("item", json::object());
    const auto& tile = result.value("tile", json::object());
    auto identifier = item.value("identifier", std::string());
    auto category = item.value("category", std::string());
    int entryIndex = item.value("entryIndex", -1);
    auto name = item.value("name", !identifier.empty() ? identifier : ("entry#" + std::to_string(entryIndex)));
    auto cost = result.value("cost", 0.0);
    auto z = result.value("z", 0);

    TextCanvas canvas(std::cout);
    canvas.Section("Path Item Removed");
    canvas.KeyValue("Item", name);
    if (!category.empty())
    {
        canvas.KeyValue("Category", category);
    }
    std::ostringstream location;
    location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ") z=" << z;
    canvas.KeyValue("Tile", location.str());
    if (cost != 0.0)
    {
        canvas.KeyValue("Refund", util::FormatCurrency(cost));
    }
}

} // namespace rctctl::renderers
