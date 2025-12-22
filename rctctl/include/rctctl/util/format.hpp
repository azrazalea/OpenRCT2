#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace rctctl::util {

std::string FormatCurrency(double amount);
std::string FormatBool(const std::string& label, bool value);
std::string MonthName(int month);
std::string FormatDateString(const nlohmann::json& date);

} // namespace rctctl::util
