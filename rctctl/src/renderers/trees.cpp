#include "rctctl/renderers/trees.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;
}

void RenderTreeCatalog(const json& result)
{
    const auto& entries = result.value("entries", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Tree Catalog");
    canvas.KeyValue("Entries", static_cast<int>(entries.size()));

    if (entries.empty())
    {
        canvas.Paragraph("No tree objects available.");
        return;
    }

    TableView table;
    table.headers = { "Name", "Price", "Height", "Use with" };
    for (const auto& entry : entries)
    {
        auto name = entry.value("name", entry.value("identifier", std::string()));
        auto identifier = entry.value("identifier", std::string());
        auto price = entry.value("price", 0.0);
        auto height = entry.value("height", 0);

        std::ostringstream useWith;
        useWith << "--tree-id " << identifier;

        table.rows.push_back({ name, util::FormatCurrency(price), std::to_string(height), useWith.str() });
    }
    canvas.Table(table);
}

void RenderTreePlant(const json& result)
{
    const auto& tree = result.value("tree", json::object());
    const auto& tile = result.value("tile", json::object());
    auto identifier = tree.value("identifier", std::string());
    int entryIndex = tree.value("entryIndex", -1);
    auto name = !identifier.empty() ? identifier : ("entry#" + std::to_string(entryIndex));
    auto cost = result.value("cost", 0.0);
    TextCanvas canvas(std::cout);
    canvas.Section("Tree Planted");
    canvas.KeyValue("Tree", name);
    std::ostringstream location;
    location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ')';
    canvas.KeyValue("Tile", location.str());
    if (cost != 0.0)
    {
        canvas.KeyValue("Cost", util::FormatCurrency(cost));
    }
}

void RenderTreeRemove(const json& result)
{
    const auto& tree = result.value("tree", json::object());
    const auto& tile = result.value("tile", json::object());
    auto identifier = tree.value("identifier", std::string());
    int entryIndex = tree.value("entryIndex", -1);
    auto name = !identifier.empty() ? identifier : ("entry#" + std::to_string(entryIndex));
    auto cost = result.value("cost", 0.0);
    TextCanvas canvas(std::cout);
    canvas.Section("Tree Removed");
    canvas.KeyValue("Tree", name);
    std::ostringstream location;
    location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ')';
    canvas.KeyValue("Tile", location.str());
    if (cost != 0.0)
    {
        canvas.KeyValue("Refund", util::FormatCurrency(cost));
    }
}

} // namespace rctctl::renderers
