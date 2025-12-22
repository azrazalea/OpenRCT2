#include "rctctl/renderers/staff.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;
}

void RenderStaffList(const json& result)
{
    const auto& staff = result.value("staff", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Staff");
    canvas.KeyValue("Count", static_cast<int>(staff.size()));

    TableView table;
    table.headers = { "ID", "Role", "State", "Energy", "Wage", "Name" };
    for (const auto& s : staff)
    {
        auto energy = s.value("energy", 0);
        std::ostringstream energyLabel;
        energyLabel << energy;
        auto wage = s.contains("wage") ? util::FormatCurrency(s.value("wage", 0.0)) : std::string("-");
        table.rows.push_back({ std::to_string(s.value("id", -1)), s.value("type", std::string("staff")),
            s.value("state", std::string("")), energyLabel.str(), wage, s.value("name", std::string("")) });
    }
    canvas.Table(table);
}

void RenderStaffDetail(const json& staff)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Staff Detail");
    canvas.KeyValue("ID", staff.value("id", -1));
    canvas.KeyValue("Role", staff.value("type", std::string("")));
    canvas.KeyValue("Name", staff.value("name", std::string("")));
    canvas.KeyValue("State", staff.value("state", std::string("")));
    canvas.KeyValue("Energy", staff.value("energy", 0));

    if (staff.contains("coords") && staff["coords"].is_object())
    {
        const auto& coords = staff["coords"];
        std::ostringstream oss;
        oss << coords.value("x", 0) << ", " << coords.value("y", 0) << " (z=" << coords.value("z", 0)
            << ")";
        canvas.KeyValue("Tile", oss.str());
    }

    if (staff.contains("wage"))
    {
        canvas.KeyValue("Wage", util::FormatCurrency(staff.value("wage", 0.0)));
    }

    const auto& orders = staff.value("orders", json::object());
    if (!orders.empty())
    {
        canvas.Section("Orders");
        for (const auto& [key, value] : orders.items())
        {
            if (value.is_boolean())
            {
                canvas.KeyValue(key, value.get<bool>());
            }
            else
            {
                canvas.KeyValue(key, value.dump());
            }
        }
    }

    if (staff.contains("patrol") && staff["patrol"].is_object())
    {
        const auto& patrol = staff["patrol"];
        canvas.Section("Patrol");
        canvas.KeyValue("Tiles assigned", patrol.value("tileCount", 0));
        const auto& sample = patrol.value("sample", json::array());
        size_t preview = std::min<size_t>(sample.size(), 5);
        for (size_t i = 0; i < preview; ++i)
        {
            const auto& tile = sample[i];
            std::ostringstream oss;
            oss << "(" << tile.value("x", 0) << ", " << tile.value("y", 0) << ")";
            canvas.Bullet(oss.str());
        }
        if (sample.size() > preview)
        {
            canvas.Bullet("...");
        }
    }
}

} // namespace rctctl::renderers
