/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SessionFileMonitor.h"

#include "../Diagnostic.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>

// Simple JSON parsing - we only need to extract "type" field
// Using a minimal approach to avoid adding dependencies

namespace OpenRCT2::Terminal
{
    namespace
    {
        // Extract the top-level "type" field from a Claude JSONL line
        // The JSON has nested "type" fields (in message.type, content[].type), but we need
        // the top-level one which appears at the end: ..."type":"assistant","uuid":"..."
        std::optional<std::string> ExtractTopLevelType(const std::string& json)
        {
            // Look for "type":"value","uuid" pattern which is the top-level type
            std::regex re("\"type\"\\s*:\\s*\"([^\"]+)\"\\s*,\\s*\"uuid\"");
            std::smatch results;

            if (std::regex_search(json, results, re) && results.size() > 1)
            {
                return results[1].str();
            }
            return std::nullopt;
        }

        // Check if this assistant message contains tool_use (meaning Claude is waiting for tool results)
        bool ContainsToolUse(const std::string& json)
        {
            // Look for "type":"tool_use" within the content array
            return json.find("\"type\":\"tool_use\"") != std::string::npos;
        }

        // Check if this is a final turn completion (stop_reason is "end_turn")
        // Returns true only if the turn is truly complete
        bool IsTurnEndMessage(const std::string& json)
        {
            // "stop_reason":"end_turn" = Claude finished its turn, no more responses coming
            // "stop_reason":"tool_use" = Claude is waiting for tool results, NOT done
            // "stop_reason":null = Still streaming
            return json.find("\"stop_reason\":\"end_turn\"") != std::string::npos;
        }
    } // namespace

    SessionFileMonitor::SessionFileMonitor(
        const std::filesystem::path& workspacePath, std::chrono::milliseconds inactivityThresholdMs)
        : _workspacePath(workspacePath)
        , _inactivityThreshold(inactivityThresholdMs)
        , _lastAssistantActivityTime(std::chrono::steady_clock::now())
    {
        _projectsDir = GetClaudeProjectsDir();
        _workspaceProjectDir = _projectsDir / WorkspaceToProjectDirName(workspacePath);

        // Signal file written by Stop hook (agent_turn_complete.sh)
        _signalFilePath = workspacePath / ".turn-complete";

        FindProjectDirectory();
        FindLatestSessionFile();

        // Initialize signal timestamp if file already exists
        std::error_code ec;
        if (std::filesystem::exists(_signalFilePath, ec))
        {
            std::ifstream signalFile(_signalFilePath);
            if (signalFile)
            {
                signalFile >> _lastSignalTimestamp;
                LOG_INFO("SessionFileMonitor: Initial signal timestamp: %lld", static_cast<long long>(_lastSignalTimestamp));
            }
        }
    }

    SessionFileMonitor::~SessionFileMonitor() = default;

    void SessionFileMonitor::SetTurnCompleteCallback(TurnCompleteCallback callback)
    {
        _turnCompleteCallback = std::move(callback);
    }

    bool SessionFileMonitor::Poll()
    {
        // PRIMARY: Check signal file from Stop hook (most reliable method)
        // The Stop hook fires when Claude finishes responding, and writes a timestamp
        // to .turn-complete in the workspace directory.
        if (CheckSignalFile())
        {
            return true;
        }

        // FALLBACK: JSONL session file monitoring (less reliable for subagents)
        // Note: In subagent sessions, stop_reason is always null, so this path
        // primarily relies on inactivity detection.

        // Always check for a newer session file - Claude may create a new one mid-conversation
        FindLatestSessionFile();
        if (!_currentSessionFile)
        {
            return false;
        }

        // Check if file has been modified since last poll
        // This is the authoritative signal that Claude is still writing
        std::error_code ec;
        auto currentModTime = std::filesystem::last_write_time(*_currentSessionFile, ec);
        if (!ec)
        {
            if (currentModTime > _lastKnownModTime)
            {
                // File has been modified - Claude is actively writing
                _lastKnownModTime = currentModTime;
                _lastAssistantActivityTime = std::chrono::steady_clock::now();
                LOG_VERBOSE("SessionFileMonitor: File modified, resetting activity timer");
            }
        }

        // Process any new lines in the session file (for type detection)
        ProcessNewLines();

        // Check for inactivity-based turn completion (fallback only)
        // Don't use inactivity detection if we're waiting for tool results -
        // tool execution can take a while and we don't want false positives.
        // Turn completion is primarily detected via signal file or ParseJsonLine.
        if (_sawAssistantMessage && !_turnComplete && !_waitingForToolResult)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastAssistantActivityTime);

            if (elapsed >= _inactivityThreshold)
            {
                _turnComplete = true;
                _lastTurnCompleteTime = std::chrono::system_clock::now();
                LOG_INFO("SessionFileMonitor: Turn complete (inactivity timeout after %lld ms, not waiting for tools)",
                    static_cast<long long>(elapsed.count()));

                if (_turnCompleteCallback)
                {
                    _turnCompleteCallback();
                }
                return true;
            }
        }

        return false;
    }

    bool SessionFileMonitor::CheckSignalFile()
    {
        // Check if the signal file exists and has a newer timestamp
        std::error_code ec;
        if (!std::filesystem::exists(_signalFilePath, ec) || ec)
        {
            return false;
        }

        // Read the timestamp from the signal file
        std::ifstream signalFile(_signalFilePath);
        if (!signalFile)
        {
            return false;
        }

        int64_t currentTimestamp = 0;
        signalFile >> currentTimestamp;

        if (currentTimestamp > _lastSignalTimestamp)
        {
            // New signal detected - turn is complete!
            _lastSignalTimestamp = currentTimestamp;
            _turnComplete = true;
            _waitingForToolResult = false;
            _lastTurnCompleteTime = std::chrono::system_clock::now();

            LOG_INFO("SessionFileMonitor: Turn complete (Stop hook signal, timestamp %lld)",
                static_cast<long long>(currentTimestamp));

            if (_turnCompleteCallback)
            {
                _turnCompleteCallback();
            }
            return true;
        }

        return false;
    }

    bool SessionFileMonitor::IsTurnComplete() const
    {
        return _turnComplete;
    }

    std::chrono::system_clock::time_point SessionFileMonitor::GetLastTurnCompleteTime() const
    {
        return _lastTurnCompleteTime;
    }

    void SessionFileMonitor::Reset()
    {
        _sawAssistantMessage = false;
        _turnComplete = false;
        _waitingForToolResult = false;
        _lastAssistantActivityTime = std::chrono::steady_clock::now();

        // Update signal file timestamp so we don't trigger on old signals
        std::error_code ec;
        if (std::filesystem::exists(_signalFilePath, ec))
        {
            std::ifstream signalFile(_signalFilePath);
            if (signalFile)
            {
                signalFile >> _lastSignalTimestamp;
                LOG_INFO("SessionFileMonitor: Reset - updated signal timestamp to %lld",
                    static_cast<long long>(_lastSignalTimestamp));
            }
        }

        // Seek to end of current file so we only detect NEW assistant messages
        // This prevents re-processing old messages after a reset
        if (_currentSessionFile)
        {
            auto fileSize = std::filesystem::file_size(*_currentSessionFile, ec);
            if (!ec)
            {
                _lastFilePosition = static_cast<std::streampos>(fileSize);
                LOG_INFO("SessionFileMonitor: Reset - seeking to end of file (pos %lld)",
                    static_cast<long long>(_lastFilePosition));
            }

            // Update mod time so we detect future writes
            _lastKnownModTime = std::filesystem::last_write_time(*_currentSessionFile, ec);
        }
        else
        {
            _lastKnownModTime = {};
        }

        // Also re-check for a new session file (in case Claude restarted)
        FindLatestSessionFile();
    }

    std::optional<std::filesystem::path> SessionFileMonitor::GetSessionFilePath() const
    {
        return _currentSessionFile;
    }

    void SessionFileMonitor::PrepareForDiscovery()
    {
        _fileSnapshot.clear();
        _sessionLocked = false;

        std::error_code ec;
        if (!std::filesystem::exists(_workspaceProjectDir, ec))
        {
            LOG_INFO("SessionFileMonitor: PrepareForDiscovery - project dir does not exist yet");
            return;
        }

        // Snapshot all .jsonl files and their modification times
        for (const auto& entry : std::filesystem::directory_iterator(_workspaceProjectDir, ec))
        {
            if (!entry.is_regular_file())
                continue;

            auto filename = entry.path().filename().string();
            if (filename.ends_with(".jsonl"))
            {
                auto modTime = entry.last_write_time(ec);
                if (!ec)
                {
                    _fileSnapshot[entry.path()] = modTime;
                }
            }
        }

        LOG_INFO("SessionFileMonitor: PrepareForDiscovery - snapshotted %zu session files",
            _fileSnapshot.size());
    }

    bool SessionFileMonitor::DiscoverActiveSession()
    {
        if (_sessionLocked)
        {
            return true; // Already locked
        }

        std::error_code ec;
        if (!std::filesystem::exists(_workspaceProjectDir, ec))
        {
            return false;
        }

        // Look for a file that either:
        // 1. Is new (wasn't in snapshot)
        // 2. Has been modified since snapshot
        for (const auto& entry : std::filesystem::directory_iterator(_workspaceProjectDir, ec))
        {
            if (!entry.is_regular_file())
                continue;

            auto filename = entry.path().filename().string();
            if (!filename.ends_with(".jsonl"))
                continue;

            auto currentModTime = entry.last_write_time(ec);
            if (ec)
                continue;

            auto it = _fileSnapshot.find(entry.path());
            bool isNewOrModified = (it == _fileSnapshot.end()) || (currentModTime > it->second);

            if (isNewOrModified)
            {
                // Found our session file - lock onto it
                _currentSessionFile = entry.path();
                _sessionLocked = true;
                _sawAssistantMessage = false;
                _turnComplete = false;

                // Seek to end of file so we only detect NEW assistant messages
                std::error_code sizeEc;
                auto fileSize = std::filesystem::file_size(entry.path(), sizeEc);
                if (!sizeEc)
                {
                    _lastFilePosition = static_cast<std::streampos>(fileSize);
                }
                else
                {
                    _lastFilePosition = 0;
                }

                _lastKnownModTime = currentModTime;

                LOG_INFO("SessionFileMonitor: Discovered and locked session file: %s (pos %lld)",
                    entry.path().filename().string().c_str(), static_cast<long long>(_lastFilePosition));
                return true;
            }
        }

        return false;
    }

    bool SessionFileMonitor::IsSessionLocked() const
    {
        return _sessionLocked;
    }

    void SessionFileMonitor::FindProjectDirectory()
    {
        std::error_code ec;
        if (!std::filesystem::exists(_workspaceProjectDir, ec))
        {
            // Directory doesn't exist yet - Claude hasn't created it
            return;
        }
    }

    void SessionFileMonitor::FindLatestSessionFile()
    {
        // If we've locked onto a session file, don't switch away from it
        if (_sessionLocked && _currentSessionFile)
        {
            return;
        }

        std::error_code ec;
        if (!std::filesystem::exists(_workspaceProjectDir, ec))
        {
            LOG_VERBOSE("SessionFileMonitor: Project dir does not exist: %s", _workspaceProjectDir.string().c_str());
            _currentSessionFile = std::nullopt;
            return;
        }

        // Find the most recently modified .jsonl session file
        std::filesystem::path latestFile;
        std::filesystem::file_time_type latestTime{};

        for (const auto& entry : std::filesystem::directory_iterator(_workspaceProjectDir, ec))
        {
            if (!entry.is_regular_file())
                continue;

            auto filename = entry.path().filename().string();
            // Match any .jsonl file - Claude Code uses various naming patterns:
            // - agent-*.jsonl (agentic mode)
            // - UUID.jsonl like b4af1e40-5ded-4667-96b3-d34bbb783cdf.jsonl
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

        if (!latestFile.empty())
        {
            if (_currentSessionFile != latestFile)
            {
                // New or different session file - start from END to only detect new messages
                // This prevents false turn-complete detection from existing content
                _currentSessionFile = latestFile;
                _sawAssistantMessage = false;
                _turnComplete = false;

                // Seek to end of file so we only detect NEW assistant messages
                std::error_code sizeEc;
                auto fileSize = std::filesystem::file_size(latestFile, sizeEc);
                if (!sizeEc)
                {
                    _lastFilePosition = static_cast<std::streampos>(fileSize);
                }
                else
                {
                    _lastFilePosition = 0;
                }

                // Initialize mod time to current file's mod time so we detect future changes
                std::error_code modEc;
                _lastKnownModTime = std::filesystem::last_write_time(latestFile, modEc);
                if (modEc)
                {
                    _lastKnownModTime = {};
                }

                LOG_INFO("SessionFileMonitor: Switched to session file: %s (starting from pos %lld)",
                    latestFile.filename().string().c_str(), static_cast<long long>(_lastFilePosition));
            }
        }
    }

    void SessionFileMonitor::ProcessNewLines()
    {
        if (!_currentSessionFile)
            return;

        std::ifstream file(*_currentSessionFile);
        if (!file)
            return;

        // Seek to last known position
        file.seekg(_lastFilePosition);
        if (!file)
            return;

        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty())
            {
                ParseJsonLine(line);
            }
        }

        // Update position for next poll
        file.clear(); // Clear EOF flag
        _lastFilePosition = file.tellg();
    }

    bool SessionFileMonitor::ParseJsonLine(const std::string& line)
    {
        // Extract the top-level "type" field from the JSON
        auto typeOpt = ExtractTopLevelType(line);
        if (!typeOpt)
            return false;

        const auto& type = *typeOpt;

        if (type == "assistant")
        {
            // Assistant message - record activity
            _sawAssistantMessage = true;
            _lastAssistantActivityTime = std::chrono::steady_clock::now();
            _turnComplete = false;

            // Check if this message contains tool_use (Claude is making a tool call)
            if (ContainsToolUse(line))
            {
                _waitingForToolResult = true;
                LOG_VERBOSE("SessionFileMonitor: Got assistant message with tool_use, waiting for result");
                return true;
            }

            // Check if this is a final response (stop_reason is "end_turn")
            // Note: "stop_reason":"tool_use" means waiting for tools, NOT done
            if (IsTurnEndMessage(line))
            {
                // This is a final assistant message - turn is complete!
                _turnComplete = true;
                _waitingForToolResult = false;
                _lastTurnCompleteTime = std::chrono::system_clock::now();
                LOG_INFO("SessionFileMonitor: Turn complete (assistant message with stop_reason:end_turn)");

                if (_turnCompleteCallback)
                {
                    _turnCompleteCallback();
                }
                return true;
            }

            LOG_VERBOSE("SessionFileMonitor: Got assistant message, turn in progress");
            return true;
        }
        else if (type == "user")
        {
            // User message (typically tool results) - reset waiting flag
            _waitingForToolResult = false;

            // If we saw assistant messages before and it wasn't tool-related, turn was complete
            // This is a secondary detection method for edge cases
            if (_sawAssistantMessage && !_turnComplete && !_waitingForToolResult)
            {
                _turnComplete = true;
                _lastTurnCompleteTime = std::chrono::system_clock::now();
                LOG_INFO("SessionFileMonitor: Turn complete (detected user message after assistant)");

                if (_turnCompleteCallback)
                {
                    _turnCompleteCallback();
                }
            }
            // Note: Don't reset _sawAssistantMessage here - let assistant messages set it
            return true;
        }

        return false;
    }

    std::filesystem::path SessionFileMonitor::GetClaudeProjectsDir()
    {
        // Claude stores projects in ~/.claude/projects/
        const char* homeDir = std::getenv("HOME");
        if (!homeDir)
        {
            return {};
        }
        return std::filesystem::path(homeDir) / ".claude" / "projects";
    }

    std::string SessionFileMonitor::WorkspaceToProjectDirName(const std::filesystem::path& workspacePath)
    {
        // Claude converts workspace paths to directory names by:
        // 1. Taking the absolute path
        // 2. Replacing leading / with -
        // 3. Replacing all / with -
        // 4. Replacing all spaces with -
        // Example: /Users/foo/Library/Application Support/bar -> -Users-foo-Library-Application-Support-bar

        std::string pathStr = workspacePath.string();

        // Ensure we have an absolute path
        if (!pathStr.empty() && pathStr[0] != '/')
        {
            std::error_code ec;
            pathStr = std::filesystem::absolute(workspacePath, ec).string();
        }

        // Replace leading / with -
        if (!pathStr.empty() && pathStr[0] == '/')
        {
            pathStr[0] = '-';
        }

        // Replace remaining / with -
        std::replace(pathStr.begin(), pathStr.end(), '/', '-');

        // Replace spaces with - (Claude also does this)
        std::replace(pathStr.begin(), pathStr.end(), ' ', '-');

        // Replace dots with - (Claude also does this)
        std::replace(pathStr.begin(), pathStr.end(), '.', '-');

        return pathStr;
    }

} // namespace OpenRCT2::Terminal
