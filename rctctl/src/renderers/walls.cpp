#include "rctctl/renderers/walls.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;
}

void RenderWallRemove(const json& result)
{
    const auto& tile = result.value("tile", json::object());
    auto objectName = result.value("objectName", std::string());
    auto direction = result.value("direction", std::string());
    auto height = result.value("height", 0);
    auto cost = result.value("cost", 0.0);

    TextCanvas canvas(std::cout);
    canvas.Section("Wall Removed");
    canvas.KeyValue("Wall", objectName);
    canvas.KeyValue("Direction", direction);
    std::ostringstream location;
    location << '(' << tile.value("x", 0) << ", " << tile.value("y", 0) << ") z=" << height;
    canvas.KeyValue("Tile", location.str());
    if (cost != 0.0)
    {
        canvas.KeyValue("Refund", util::FormatCurrency(cost));
    }
}

} // namespace rctctl::renderers
