#include "rctctl/renderers/paths.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;

std::string DescribePathObject(const json& node)
{
    if (!node.is_object())
    {
        return "-";
    }
    auto identifier = node.value("identifier", std::string());
    if (!identifier.empty())
    {
        return identifier;
    }
    int entryIndex = node.value("entryIndex", -1);
    if (entryIndex >= 0)
    {
        return std::string("entry#") + std::to_string(entryIndex);
    }
    return "-";
}
} // namespace

void RenderPathPlace(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Path Placement");

    const auto& tile = result.value("tile", json::object());
    std::ostringstream tileLabel;
    tileLabel << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ')';
    canvas.KeyValue("Tile", tileLabel.str());
    canvas.KeyValue("Type", result.value("queue", false) ? "queue" : "footpath");
    canvas.KeyValue("Height", result.value("height", 0));

    // Show slope info
    if (result.contains("slope") && result["slope"].is_string())
    {
        canvas.KeyValue("Slope", result.value("slope", "flat"));
    }

    // Show if elevated
    if (result.value("elevated", false))
    {
        canvas.KeyValue("Mode", "elevated");
    }

    const auto& surface = result.value("surface", json::object());
    if (!surface.empty())
    {
        canvas.KeyValue("Surface", DescribePathObject(surface));
    }
    if (result.contains("railings"))
    {
        canvas.KeyValue("Railings", DescribePathObject(result["railings"]));
    }

    double cost = result.value("cost", 0.0);
    if (cost != 0.0)
    {
        canvas.KeyValue("Cost", util::FormatCurrency(cost));
    }
}

void RenderPathCatalog(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Path Catalog");

    const auto& surfaces = result.value("surfaces", json::array());
    const auto& railings = result.value("railings", json::array());

    canvas.KeyValue("Surfaces", static_cast<int>(surfaces.size()));
    canvas.KeyValue("Railings", static_cast<int>(railings.size()));

    if (!surfaces.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Surfaces (use with --surface):");
        TableView surfaceTable;
        surfaceTable.headers = { "Name", "Identifier" };
        for (const auto& entry : surfaces)
        {
            auto name = entry.value("name", std::string());
            auto identifier = entry.value("identifier", std::string());
            if (name.empty())
            {
                name = identifier;
            }
            surfaceTable.rows.push_back({ name, identifier });
        }
        canvas.Table(surfaceTable);
    }

    if (!railings.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Railings (use with --railings):");
        TableView railingsTable;
        railingsTable.headers = { "Name", "Identifier" };
        for (const auto& entry : railings)
        {
            auto name = entry.value("name", std::string());
            auto identifier = entry.value("identifier", std::string());
            if (name.empty())
            {
                name = identifier;
            }
            railingsTable.rows.push_back({ name, identifier });
        }
        canvas.Table(railingsTable);
    }
}

void RenderPathRemove(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Path Removed");

    const auto& tile = result.value("tile", json::object());
    std::ostringstream tileLabel;
    tileLabel << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ')';
    canvas.KeyValue("Tile", tileLabel.str());
    canvas.KeyValue("Type", result.value("queue", false) ? "queue" : "footpath");
    canvas.KeyValue("Height", result.value("height", 0));

    auto surfaceName = result.value("surfaceName", std::string());
    if (!surfaceName.empty())
    {
        canvas.KeyValue("Surface", surfaceName);
    }

    auto railingsName = result.value("railingsName", std::string());
    if (!railingsName.empty())
    {
        canvas.KeyValue("Railings", railingsName);
    }

    double cost = result.value("cost", 0.0);
    if (cost != 0.0)
    {
        canvas.KeyValue("Refund", util::FormatCurrency(cost));
    }
}

void RenderPathsList(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Path Additions");

    const auto& items = result.value("pathAdditions", json::array());
    canvas.KeyValue("Shown", static_cast<int>(items.size()));
    canvas.KeyValue("Total", result.value("total", 0));

    if (result.value("hasMore", false))
    {
        auto nextCursor = result.value("nextCursor", std::string());
        if (!nextCursor.empty())
        {
            canvas.KeyValue("Next cursor (--after)", nextCursor);
        }
    }

    if (items.empty())
    {
        canvas.Paragraph("No path additions found matching the criteria.");
        return;
    }

    TableView table;
    table.headers = { "ID", "Type", "Object", "Location", "Broken", "Bin Fullness" };

    for (const auto& item : items)
    {
        std::ostringstream location;
        location << '(' << item.value("x", 0) << ", " << item.value("y", 0)
                 << ", z" << item.value("z", 0) << ')';

        std::string binFullness = item.value("binFullness", std::string());
        if (binFullness.empty())
        {
            binFullness = "-";
        }

        table.rows.push_back({
            item.value("id", std::string()),
            item.value("type", std::string()),
            item.value("objectName", std::string()),
            location.str(),
            item.value("broken", false) ? "yes" : "no",
            binFullness
        });
    }
    canvas.Table(table);
}

} // namespace rctctl::renderers

