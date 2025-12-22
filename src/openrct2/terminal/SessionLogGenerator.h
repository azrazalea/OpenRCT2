/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace OpenRCT2::Terminal
{
    /**
     * Generates session logs from Claude Code's native JSONL session files.
     *
     * Outputs:
     * - Markdown (.md) - dense, LLM-friendly format
     * - HTML (.html) - browser-viewable format with dark theme
     *
     * Both formats are always generated (requires only python3).
     */
    class SessionLogGenerator
    {
    public:
        struct GenerationResult
        {
            bool success = false;
            std::filesystem::path markdownPath;  // Always generated
            std::filesystem::path htmlPath;      // Always generated (converted from markdown)
            std::filesystem::path jsonPath;      // Metadata
            std::string error;
        };

        /**
         * Generate session logs from the current Claude session.
         * Generates both Markdown and HTML formats.
         * @param workspacePath The agent workspace path (e.g., ~/.openrct2-agent)
         * @param parkName Optional park name to include in the log filename
         * @param sessionFile Optional specific JSONL file to convert (if not provided, uses most recent)
         * @return Result containing paths to generated files or error
         */
        static GenerationResult GenerateLog(
            const std::filesystem::path& workspacePath,
            const std::string& parkName = "",
            const std::optional<std::filesystem::path>& sessionFile = std::nullopt);

        /**
         * Get the directory where agent logs are stored.
         * Creates the directory if it doesn't exist.
         */
        static std::filesystem::path GetAgentLogsDir();

    private:
        static std::optional<std::filesystem::path> FindClaudeProjectDir(
            const std::filesystem::path& workspacePath);
        static std::string GenerateSessionFilename(const std::string& parkName);
        static std::string SanitizeFilename(const std::string& name);
        static std::string GetCurrentTimestamp();
    };

} // namespace OpenRCT2::Terminal
