#include "AIAgentGlyphLogger.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "../Diagnostic.h"
#include "../core/Path.hpp"
#include "../core/UTF8.h"
#include "../platform/Platform.h"

namespace OpenRCT2::AIAgent
{
namespace
{
    constexpr const char* kEnvExplicitPath = "AGENT_GLYPH_LOG_PATH";
    constexpr const char* kEnvImplicitPath = "AGENT_GLYPH_LOG";
    constexpr const utf8 kDefaultFileName[] = "agent-glyph-log.ndjson";
}

AIAgentGlyphLogger& AIAgentGlyphLogger::Instance()
{
    static AIAgentGlyphLogger logger;
    return logger;
}

void AIAgentGlyphLogger::RecordSnapshot(const Terminal::TerminalSnapshot& snapshot)
{
    if (snapshot.cells.empty())
        return;

    std::vector<char32_t> pending;
    std::string logPathCopy;
    {
        std::scoped_lock lock(_mutex);
        EnsureInitialisedLocked();
        if (!_enabled)
        {
            return;
        }
        logPathCopy = _logPath;
        for (const auto& cell : snapshot.cells)
        {
            const char32_t codepoint = cell.codepoint;
            if (!ShouldTrack(codepoint))
            {
                continue;
            }
            if (_seen.insert(codepoint).second)
            {
                pending.push_back(codepoint);
            }
        }
    }

    for (const auto codepoint : pending)
    {
        WriteDiscovery(logPathCopy, codepoint);
    }
}

void AIAgentGlyphLogger::RecordCodepoint(char32_t codepoint)
{
    if (!ShouldTrack(codepoint))
        return;

    std::string logPathCopy;
    bool shouldLog = false;
    {
        std::scoped_lock lock(_mutex);
        EnsureInitialisedLocked();
        if (!_enabled)
        {
            return;
        }
        logPathCopy = _logPath;
        shouldLog = _seen.insert(codepoint).second;
    }

    if (shouldLog)
    {
        WriteDiscovery(logPathCopy, codepoint);
    }
}

void AIAgentGlyphLogger::EnsureInitialisedLocked()
{
    if (_initialised)
        return;

    _initialised = true;

    std::string path = Platform::GetEnvironmentVariable(kEnvExplicitPath);
    if (path.empty())
    {
        path = Platform::GetEnvironmentVariable(kEnvImplicitPath);
    }

    if (path.empty())
    {
        const auto installPath = Platform::GetInstallPath();
        if (!installPath.empty())
        {
            u8string installU8(installPath.begin(), installPath.end());
            u8string defaultFile(kDefaultFileName);
            auto combined = Path::Combine(installU8, defaultFile);
            path.assign(combined.begin(), combined.end());
        }
    }

    if (path.empty())
    {
        LOG_VERBOSE("AIAgentGlyphLogger disabled: no writable path");
        return;
    }

    u8string pathU8(path.begin(), path.end());
    auto directory = Path::GetDirectory(pathU8);
    if (!directory.empty())
    {
        Path::CreateDirectory(directory);
    }

    _logPath = path;
    _enabled = true;
}

bool AIAgentGlyphLogger::ShouldTrack(char32_t codepoint)
{
    return codepoint >= 0x21 && codepoint <= 0x10FFFF;
}

std::string AIAgentGlyphLogger::EncodeUtf8(char32_t codepoint)
{
    if (codepoint == 0)
        return {};
    utf8 buffer[8] = {};
    utf8* cursor = UTF8WriteCodepoint(buffer, static_cast<uint32_t>(codepoint));
    return std::string(buffer, cursor);
}

std::string AIAgentGlyphLogger::EscapeJson(std::string_view input)
{
    std::string output;
    output.reserve(input.size() + 4);
    for (const char ch : input)
    {
        switch (ch)
        {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20)
                {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch & 0xFF);
                    output.append(buffer);
                }
                else
                {
                    output += ch;
                }
                break;
        }
    }
    return output;
}

std::string AIAgentGlyphLogger::CodepointTag(char32_t codepoint)
{
    std::ostringstream oss;
    oss << "U+" << std::uppercase << std::setfill('0');
    const int width = (codepoint <= 0xFFFF) ? 4 : 6;
    oss << std::setw(width) << std::hex << static_cast<uint32_t>(codepoint);
    return oss.str();
}

std::string AIAgentGlyphLogger::IsoTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buffer);
}

void AIAgentGlyphLogger::WriteDiscovery(const std::string& path, char32_t codepoint)
{
    if (path.empty())
        return;

    std::ofstream stream(path, std::ios::app);
    if (!stream)
    {
        LOG_WARNING("AIAgentGlyphLogger: Unable to append to %s", path.c_str());
        return;
    }

    const auto glyph = EncodeUtf8(codepoint);
    stream << '{'
           << "\"ts\":\"" << IsoTimestamp() << "\",";
    stream << "\"source\":\"agent-terminal\",";
    stream << "\"event\":\"glyph\",";
    stream << "\"codepoint\":\"" << CodepointTag(codepoint) << "\",";
    stream << "\"char\":\"" << EscapeJson(glyph) << "\"}"
           << '\n';
}
}
