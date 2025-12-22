#include "rctctl/util/string_utils.hpp"

#include <algorithm>
#include <cctype>

namespace rctctl::util {

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string StripFormatCodes(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    size_t i = 0;
    while (i < value.size())
    {
        if (value[i] == '{')
        {
            // Skip to closing brace
            size_t end = value.find('}', i);
            if (end != std::string::npos)
            {
                i = end + 1;
                continue;
            }
        }
        result += value[i];
        ++i;
    }
    // Trim leading/trailing whitespace that might result from removed codes
    size_t start = result.find_first_not_of(" \t\n\r");
    size_t end = result.find_last_not_of(" \t\n\r");
    if (start == std::string::npos)
    {
        return "";
    }
    return result.substr(start, end - start + 1);
}

} // namespace rctctl::util
