#pragma once

#include <string>

namespace rctctl::util {

std::string ToLower(std::string value);
std::string ToUpper(std::string value);

// Strip RCT2 format codes like {WINDOW_COLOUR_2}, {TOPAZ}, etc.
std::string StripFormatCodes(const std::string& value);

} // namespace rctctl::util
