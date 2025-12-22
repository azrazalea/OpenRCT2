#include "rctctl/util/format.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace rctctl::util {
namespace {
constexpr std::array<const char*, 8> kParkMonthNames = {
    "March", "April", "May", "June", "July", "August", "September", "October",
};
}

std::string FormatCurrency(double amount)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    if (amount < 0)
    {
        oss << "-$" << std::abs(amount);
    }
    else
    {
        oss << "$" << amount;
    }
    return oss.str();
}

std::string FormatBool(const std::string& label, bool value)
{
    std::ostringstream oss;
    oss << label << ": " << (value ? "yes" : "no");
    return oss.str();
}

std::string MonthName(int month)
{
    if (month >= 0 && month < static_cast<int>(kParkMonthNames.size()))
    {
        return kParkMonthNames[static_cast<size_t>(month)];
    }
    return "Month " + std::to_string(month);
}

std::string FormatDateString(const nlohmann::json& date)
{
    const int day = date.value("day", 1);
    const int month = date.value("month", 0);
    const int year = date.value("year", 1);
    std::ostringstream oss;
    oss << MonthName(month) << ' ' << day << ", Year " << year;
    return oss.str();
}

} // namespace rctctl::util
