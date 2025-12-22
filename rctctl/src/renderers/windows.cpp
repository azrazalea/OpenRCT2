#include "rctctl/renderers/windows.hpp"

#include "rctctl/renderers/text.hpp"

#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;
}

void RenderWindowList(const json& result)
{
    const auto& windows = result.value("windows", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Windows");
    canvas.KeyValue("Open", static_cast<int>(windows.size()));

    TableView table;
    table.headers = { "ID", "Class", "Bounds", "Locked", "Title" };
    for (const auto& window : windows)
    {
        uint64_t id = window.value("id", 0ull);
        auto className = window.value("class", std::string("window"));
        auto number = window.value("number", -1);
        auto title = window.value("title", std::string());
        std::ostringstream classLabel;
        classLabel << className;
        if (number >= 0)
        {
            classLabel << '#' << number;
        }
        std::ostringstream bounds;
        bounds << window.value("x", 0) << "," << window.value("y", 0) << " " << window.value("width", 0)
               << 'x' << window.value("height", 0);
        table.rows.push_back({ std::to_string(id), classLabel.str(), bounds.str(),
            window.value("protected", false) ? "yes" : "no", title });
    }
    canvas.Table(table);
}

void RenderWindowClose(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Close Windows");
    canvas.KeyValue("Closed", result.value("closed", 0));
    canvas.KeyValue("Still open", static_cast<int>(result.value("windows", json::array()).size()));
    RenderWindowList(result);
}

} // namespace rctctl::renderers
