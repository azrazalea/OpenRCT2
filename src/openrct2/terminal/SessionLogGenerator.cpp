/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SessionLogGenerator.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "../Diagnostic.h"
#include "../platform/Platform.h"

namespace OpenRCT2::Terminal
{
    namespace
    {
        // Find repo root by searching for CMakeLists.txt from given starting path
        std::optional<std::filesystem::path> FindRepoRoot(const std::filesystem::path& startPath)
        {
            auto searchPath = startPath;
            // Search up to 10 levels (handles app bundles: exe -> MacOS -> Contents -> .app -> build -> repo)
            for (int i = 0; i < 10; ++i)
            {
                if (std::filesystem::exists(searchPath / "CMakeLists.txt"))
                {
                    return searchPath;
                }
                if (searchPath.has_parent_path() && searchPath.parent_path() != searchPath)
                {
                    searchPath = searchPath.parent_path();
                }
                else
                {
                    break;
                }
            }
            return std::nullopt;
        }

        std::optional<std::filesystem::path> FindMarkdownScript()
        {
            // Search for session_to_markdown.py starting from executable location
            // This handles the case where the game is launched as an app bundle
            std::vector<std::filesystem::path> searchRoots;

            // First try: executable directory (most reliable for finding repo root)
            auto exePath = Platform::GetCurrentExecutablePath();
            if (!exePath.empty())
            {
                auto exeDir = std::filesystem::path(exePath).parent_path();
                searchRoots.push_back(exeDir);
            }

            // Second try: current working directory (works when running from repo)
            searchRoots.push_back(std::filesystem::current_path());

            for (const auto& root : searchRoots)
            {
                auto repoRoot = FindRepoRoot(root);
                if (repoRoot)
                {
                    auto scriptPath = *repoRoot / "scripts" / "session_to_markdown.py";
                    if (std::filesystem::exists(scriptPath))
                    {
                        return scriptPath;
                    }
                }
            }

            return std::nullopt;
        }

        bool GenerateMarkdown(
            const std::filesystem::path& inputPath,  // Can be specific JSONL file or directory
            const std::filesystem::path& outputPath)
        {
            auto scriptPath = FindMarkdownScript();
            if (!scriptPath)
            {
                LOG_WARNING("SessionLogGenerator: session_to_markdown.py not found");
                return false;
            }

            // Find python3
            std::string python = "python3";
            if (std::system("command -v python3 >/dev/null 2>&1") != 0)
            {
                LOG_WARNING("SessionLogGenerator: python3 not found");
                return false;
            }

            // Run the markdown converter with the input path (file or directory)
            std::string command = python + " \"" + scriptPath->string() + "\" \"" +
                inputPath.string() + "\" -o \"" + outputPath.string() + "\" 2>&1";

            LOG_INFO("SessionLogGenerator: Running markdown converter on %s", inputPath.string().c_str());

            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe)
            {
                return false;
            }

            std::array<char, 256> buffer;
            std::string cmdOutput;
            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
            {
                cmdOutput += buffer.data();
            }

            int exitCode = pclose(pipe);

            if (exitCode != 0)
            {
                LOG_WARNING("SessionLogGenerator: Markdown generation failed: %s", cmdOutput.c_str());
                return false;
            }

            return true;
        }

        std::optional<std::filesystem::path> FindHtmlScript()
        {
            // Search for markdown_to_html.py starting from executable location
            std::vector<std::filesystem::path> searchRoots;

            auto exePath = Platform::GetCurrentExecutablePath();
            if (!exePath.empty())
            {
                auto exeDir = std::filesystem::path(exePath).parent_path();
                searchRoots.push_back(exeDir);
            }
            searchRoots.push_back(std::filesystem::current_path());

            for (const auto& root : searchRoots)
            {
                auto repoRoot = FindRepoRoot(root);
                if (repoRoot)
                {
                    auto scriptPath = *repoRoot / "scripts" / "markdown_to_html.py";
                    if (std::filesystem::exists(scriptPath))
                    {
                        return scriptPath;
                    }
                }
            }

            return std::nullopt;
        }

        bool GenerateHtmlFromMarkdown(
            const std::filesystem::path& markdownPath,
            const std::filesystem::path& outputPath)
        {
            // Convert markdown to HTML using our own script
            // This is fast and reliable, unlike claude-code-log which processes all sessions
            auto scriptPath = FindHtmlScript();
            if (!scriptPath)
            {
                LOG_WARNING("SessionLogGenerator: markdown_to_html.py not found");
                return false;
            }

            if (!std::filesystem::exists(markdownPath))
            {
                LOG_WARNING("SessionLogGenerator: Markdown file not found: %s", markdownPath.string().c_str());
                return false;
            }

            std::string python = "python3";
            if (std::system("command -v python3 >/dev/null 2>&1") != 0)
            {
                LOG_WARNING("SessionLogGenerator: python3 not found");
                return false;
            }

            std::string command = python + " \"" + scriptPath->string() + "\" \"" +
                markdownPath.string() + "\" -o \"" + outputPath.string() + "\" 2>&1";

            LOG_INFO("SessionLogGenerator: Running HTML converter on %s", markdownPath.string().c_str());

            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe)
            {
                return false;
            }

            std::array<char, 256> buffer;
            std::string cmdOutput;
            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
            {
                cmdOutput += buffer.data();
            }

            int exitCode = pclose(pipe);

            if (exitCode != 0)
            {
                LOG_WARNING("SessionLogGenerator: HTML generation failed: %s", cmdOutput.c_str());
                return false;
            }

            return true;
        }
    } // anonymous namespace

    SessionLogGenerator::GenerationResult SessionLogGenerator::GenerateLog(
        const std::filesystem::path& workspacePath,
        const std::string& parkName,
        const std::optional<std::filesystem::path>& sessionFile)
    {
        GenerationResult result;

        // Determine the input path for log generation
        // If a specific session file is provided, use it directly
        // Otherwise, fall back to the project directory (which will use most recent file)
        std::filesystem::path inputPath;
        std::string claudeSessionId;

        if (sessionFile && std::filesystem::exists(*sessionFile))
        {
            inputPath = *sessionFile;
            // Extract session ID from filename (e.g., "abc123.jsonl" -> "abc123")
            claudeSessionId = sessionFile->stem().string();
            LOG_INFO("SessionLogGenerator: Using specific session file: %s", inputPath.string().c_str());
        }
        else
        {
            // Fall back to finding the most recent session file in the project directory
            auto projectDir = FindClaudeProjectDir(workspacePath);
            if (!projectDir)
            {
                result.error = "Could not find Claude project directory";
                LOG_WARNING("SessionLogGenerator: %s", result.error.c_str());
                return result;
            }

            // Find the most recent .jsonl file in the project directory
            // This ensures we pass a SPECIFIC file, not the whole directory,
            // which is critical for HTML generation isolation
            std::filesystem::path latestFile;
            std::filesystem::file_time_type latestTime{};
            std::error_code ec;

            for (const auto& entry : std::filesystem::directory_iterator(*projectDir, ec))
            {
                if (!entry.is_regular_file())
                    continue;
                auto filename = entry.path().filename().string();
                if (filename.ends_with(".jsonl"))
                {
                    auto modTime = entry.last_write_time(ec);
                    if (!ec && (latestFile.empty() || modTime > latestTime))
                    {
                        latestFile = entry.path();
                        latestTime = modTime;
                    }
                }
            }

            if (latestFile.empty())
            {
                result.error = "No session files found in Claude project directory";
                LOG_WARNING("SessionLogGenerator: %s", result.error.c_str());
                return result;
            }

            inputPath = latestFile;
            claudeSessionId = latestFile.stem().string();
            LOG_INFO("SessionLogGenerator: Using most recent session file (fallback): %s", inputPath.string().c_str());
        }

        // Generate output paths
        auto logsDir = GetAgentLogsDir();
        auto sessionFilename = GenerateSessionFilename(parkName);
        result.markdownPath = logsDir / (sessionFilename + ".md");
        result.htmlPath = logsDir / (sessionFilename + ".html");
        result.jsonPath = logsDir / (sessionFilename + ".json");

        LOG_INFO("SessionLogGenerator: Input path: %s", inputPath.string().c_str());
        LOG_INFO("SessionLogGenerator: Output dir: %s", logsDir.string().c_str());

        // Always generate Markdown first (fast, no external dependencies beyond python3)
        bool markdownOk = GenerateMarkdown(inputPath, result.markdownPath);
        if (markdownOk)
        {
            LOG_INFO("SessionLogGenerator: Markdown saved to %s", result.markdownPath.string().c_str());
        }

        // Generate HTML from the markdown (fast, uses our own converter)
        // This replaces the old claude-code-log approach which was unreliable
        bool htmlOk = false;
        if (markdownOk)
        {
            LOG_INFO("SessionLogGenerator: Generating HTML from markdown...");
            htmlOk = GenerateHtmlFromMarkdown(result.markdownPath, result.htmlPath);
            if (htmlOk)
            {
                LOG_INFO("SessionLogGenerator: HTML saved to %s", result.htmlPath.string().c_str());
            }
        }
        else
        {
            LOG_INFO("SessionLogGenerator: Skipping HTML (markdown generation failed)");
            result.htmlPath.clear();  // Clear path since not generated
        }

        // Write metadata JSON
        std::string timestamp = GetCurrentTimestamp();
        std::ofstream jsonFile(result.jsonPath);
        if (jsonFile.is_open())
        {
            jsonFile << "{\n";
            jsonFile << "  \"session_id\": \"" << sessionFilename << "\",\n";
            jsonFile << "  \"timestamp\": \"" << timestamp << "\",\n";
            jsonFile << "  \"park_name\": \"" << parkName << "\",\n";
            jsonFile << "  \"workspace\": \"" << workspacePath.string() << "\",\n";
            if (!claudeSessionId.empty())
            {
                jsonFile << "  \"claude_session_id\": \"" << claudeSessionId << "\",\n";
            }
            jsonFile << "  \"markdown_path\": \"" << result.markdownPath.string() << "\",\n";
            jsonFile << "  \"markdown_generated\": " << (markdownOk ? "true" : "false") << ",\n";
            if (!result.htmlPath.empty())
            {
                jsonFile << "  \"html_path\": \"" << result.htmlPath.string() << "\",\n";
                jsonFile << "  \"html_generated\": " << (htmlOk ? "true" : "false") << ",\n";
            }
            jsonFile << "  \"status\": \"completed\"\n";
            jsonFile << "}\n";
            jsonFile.close();
        }

        // Success if at least markdown was generated
        result.success = markdownOk;
        if (!result.success)
        {
            result.error = "Failed to generate markdown log";
        }

        return result;
    }

    std::filesystem::path SessionLogGenerator::GetAgentLogsDir()
    {
        // Find repo root by looking for CMakeLists.txt going up from executable
        // Fall back to user's home directory
        std::filesystem::path logsDir;

        // First try: find repo root from executable directory
        auto exePath = Platform::GetCurrentExecutablePath();
        if (!exePath.empty())
        {
            auto exeDir = std::filesystem::path(exePath).parent_path();
            auto repoRoot = FindRepoRoot(exeDir);
            if (repoRoot)
            {
                logsDir = *repoRoot / "agent-logs";
            }
        }

        // Second try: find repo root from current working directory
        if (logsDir.empty())
        {
            auto cwd = std::filesystem::current_path();
            auto repoRoot = FindRepoRoot(cwd);
            if (repoRoot)
            {
                logsDir = *repoRoot / "agent-logs";
            }
        }

        // Fallback: user's home directory
        if (logsDir.empty())
        {
            const char* home = std::getenv("HOME");
            if (home)
            {
                logsDir = std::filesystem::path(home) / "OpenRCT2-agent-logs";
            }
            else
            {
                logsDir = std::filesystem::current_path() / "agent-logs";
            }
        }

        // Create directory if needed
        std::error_code ec;
        std::filesystem::create_directories(logsDir, ec);

        return logsDir;
    }

    std::optional<std::filesystem::path> SessionLogGenerator::FindClaudeProjectDir(
        const std::filesystem::path& workspacePath)
    {
        // Claude stores projects in ~/.claude/projects/-{workspace-path-with-dashes}
        const char* home = std::getenv("HOME");
        if (!home)
        {
            return std::nullopt;
        }

        std::filesystem::path projectsDir = std::filesystem::path(home) / ".claude" / "projects";
        if (!std::filesystem::exists(projectsDir))
        {
            return std::nullopt;
        }

        // Convert workspace path to project dir name
        // e.g., /Users/foo/.openrct2-agent
        // becomes -Users-foo-.openrct2-agent
        std::string workspaceStr = workspacePath.string();
        if (workspaceStr.front() == '/')
        {
            workspaceStr = workspaceStr.substr(1);
        }
        std::string projectDirName = "-";
        for (char c : workspaceStr)
        {
            if (c == '/' || c == ' ' || c == '.')
            {
                // Claude converts slashes, spaces, and dots to dashes in project dir names
                projectDirName += '-';
            }
            else
            {
                projectDirName += c;
            }
        }

        auto expectedPath = projectsDir / projectDirName;
        if (std::filesystem::exists(expectedPath))
        {
            return expectedPath;
        }

        // Fall back to finding the most recently modified project
        std::filesystem::path mostRecentDir;
        std::filesystem::file_time_type mostRecentTime;

        for (const auto& entry : std::filesystem::directory_iterator(projectsDir))
        {
            if (!entry.is_directory())
                continue;
            if (entry.path().filename().string().front() != '-')
                continue;

            auto modTime = entry.last_write_time();
            if (mostRecentDir.empty() || modTime > mostRecentTime)
            {
                mostRecentDir = entry.path();
                mostRecentTime = modTime;
            }
        }

        if (!mostRecentDir.empty())
        {
            return mostRecentDir;
        }

        return std::nullopt;
    }

    std::string SessionLogGenerator::GenerateSessionFilename(const std::string& parkName)
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);

        std::ostringstream oss;
        oss << "agent-session-";
        oss << std::put_time(&tm, "%Y%m%d-%H%M%S");

        if (!parkName.empty())
        {
            oss << "-" << SanitizeFilename(parkName);
        }

        return oss.str();
    }

    std::string SessionLogGenerator::SanitizeFilename(const std::string& name)
    {
        std::string result;
        result.reserve(name.size());
        for (char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
            {
                result += c;
            }
            else if (c == ' ')
            {
                result += '-';
            }
            // Skip other characters
        }
        return result;
    }

    std::string SessionLogGenerator::GetCurrentTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::gmtime(&time);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

} // namespace OpenRCT2::Terminal
