#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "../terminal/TerminalSession.h"

namespace OpenRCT2::AIAgent
{
    class AIAgentGlyphLogger
    {
    public:
        static AIAgentGlyphLogger& Instance();

        void RecordSnapshot(const Terminal::TerminalSnapshot& snapshot);
        void RecordCodepoint(char32_t codepoint);

    private:
        AIAgentGlyphLogger() = default;

        void EnsureInitialisedLocked();
        static bool ShouldTrack(char32_t codepoint);
        static std::string EncodeUtf8(char32_t codepoint);
        static std::string EscapeJson(std::string_view input);
        static std::string CodepointTag(char32_t codepoint);
        static std::string IsoTimestamp();
        static void WriteDiscovery(const std::string& path, char32_t codepoint);

        std::mutex _mutex;
        bool _initialised = false;
        bool _enabled = false;
        std::string _logPath;
        std::unordered_set<char32_t> _seen;
    };
}
