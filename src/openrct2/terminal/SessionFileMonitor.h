/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace OpenRCT2::Terminal
{
    /**
     * Monitors Claude Code's native session files in ~/.claude/projects/
     * to detect when a turn (assistant response) is complete.
     *
     * Claude writes JSONL files in real-time, one line per message/event.
     * We monitor for assistant messages and detect completion via inactivity.
     */
    class SessionFileMonitor
    {
    public:
        using TurnCompleteCallback = std::function<void()>;

        /**
         * Create a monitor for the given workspace path.
         * @param workspacePath The agent workspace path (e.g., ~/.openrct2-agent)
         * @param inactivityThresholdMs Milliseconds of inactivity after assistant output to consider turn complete
         */
        explicit SessionFileMonitor(
            const std::filesystem::path& workspacePath, std::chrono::milliseconds inactivityThresholdMs = std::chrono::milliseconds(30000));

        ~SessionFileMonitor();

        // Non-copyable
        SessionFileMonitor(const SessionFileMonitor&) = delete;
        SessionFileMonitor& operator=(const SessionFileMonitor&) = delete;

        /**
         * Set callback to be invoked when a turn is detected as complete.
         */
        void SetTurnCompleteCallback(TurnCompleteCallback callback);

        /**
         * Poll for changes. Call this regularly (e.g., every frame or every 100ms).
         * Returns true if a turn was just completed.
         */
        bool Poll();

        /**
         * Check if a turn is currently complete (no recent assistant activity).
         */
        bool IsTurnComplete() const;

        /**
         * Get the timestamp of the last detected turn completion.
         */
        std::chrono::system_clock::time_point GetLastTurnCompleteTime() const;

        /**
         * Reset state, e.g., when a new prompt is sent.
         * This clears the turn complete flag and resets activity tracking.
         */
        void Reset();

        /**
         * Prepare to discover which session file belongs to our agent.
         * Call this BEFORE sending a prompt. It snapshots all current files.
         */
        void PrepareForDiscovery();

        /**
         * Discover which session file changed since PrepareForDiscovery().
         * Call this AFTER sending a prompt. Once found, we lock onto that file.
         * Returns true if a session file was discovered and locked.
         */
        bool DiscoverActiveSession();

        /**
         * Check if we've locked onto a specific session file.
         */
        bool IsSessionLocked() const;

        /**
         * Get the path to the session file being monitored.
         */
        std::optional<std::filesystem::path> GetSessionFilePath() const;

    private:
        std::filesystem::path _projectsDir;
        std::filesystem::path _workspaceProjectDir;
        std::filesystem::path _workspacePath;
        std::optional<std::filesystem::path> _currentSessionFile;
        std::streampos _lastFilePosition = 0;
        std::filesystem::file_time_type _lastKnownModTime{};
        std::chrono::milliseconds _inactivityThreshold;

        // Signal file-based turn detection (from Stop hook)
        std::filesystem::path _signalFilePath;
        int64_t _lastSignalTimestamp = 0;

        std::chrono::steady_clock::time_point _lastAssistantActivityTime;
        bool _sawAssistantMessage = false;
        bool _turnComplete = false;
        bool _waitingForToolResult = false; // True if Claude issued a tool_use and we're waiting for result
        std::chrono::system_clock::time_point _lastTurnCompleteTime;

        TurnCompleteCallback _turnCompleteCallback;

        // Session discovery state - used to identify which file belongs to our agent
        bool _sessionLocked = false;
        std::map<std::filesystem::path, std::filesystem::file_time_type> _fileSnapshot;

        void FindProjectDirectory();
        void FindLatestSessionFile();
        void ProcessNewLines();
        bool ParseJsonLine(const std::string& line);
        bool CheckSignalFile();
        static std::filesystem::path GetClaudeProjectsDir();
        static std::string WorkspaceToProjectDirName(const std::filesystem::path& workspacePath);
    };

} // namespace OpenRCT2::Terminal
