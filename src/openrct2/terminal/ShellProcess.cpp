/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ShellProcess.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>
#include <thread>
#include <chrono>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <sys/ioctl.h>
    #include <sys/types.h>
    #include <termios.h>
    #include <unistd.h>
    #include <util.h>
    #include <fcntl.h>
    #include <sys/wait.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #include <pty.h>
    #include <sys/ioctl.h>
    #include <sys/types.h>
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/wait.h>
#endif

namespace OpenRCT2::Terminal
{
    namespace
    {
#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        class PosixShellProcess final : public ShellProcess
        {
        public:
            PosixShellProcess(int masterFd, pid_t pid, std::string description)
                : _masterFd(masterFd)
                , _pid(pid)
                , _description(std::move(description))
            {
                int flags = fcntl(_masterFd, F_GETFL, 0);
                if (flags != -1)
                {
                    fcntl(_masterFd, F_SETFL, flags | O_NONBLOCK);
                }
            }

            ~PosixShellProcess() override
            {
                if (_pid > 0)
                {
                    KillChild();
                }
                if (_masterFd >= 0)
                {
                    close(_masterFd);
                }
            }

            [[nodiscard]] bool IsRunning() const override
            {
                return !_exited;
            }

            ssize_t Read(uint8_t* buffer, size_t length) override
            {
                if (_masterFd < 0)
                    return -1;

                ssize_t result = ::read(_masterFd, buffer, length);
                if (result == 0)
                {
                    CheckChild();
                }
                else if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    return 0;
                }
                else if (result < 0)
                {
                    CheckChild();
                }
                return result;
            }

            bool Write(std::span<const uint8_t> data) override
            {
                if (_masterFd < 0 || data.empty())
                    return false;

                const uint8_t* ptr = data.data();
                size_t remaining = data.size();
                while (remaining > 0)
                {
                    ssize_t written = ::write(_masterFd, ptr, remaining);
                    if (written < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            continue;
                        }
                        return false;
                    }
                    ptr += written;
                    remaining -= static_cast<size_t>(written);
                }
                return true;
            }

            void Resize(int cols, int rows) override
            {
                if (_masterFd < 0)
                    return;
                struct winsize ws
                {
                };
                ws.ws_col = static_cast<unsigned short>(std::clamp(cols, 2, 500));
                ws.ws_row = static_cast<unsigned short>(std::clamp(rows, 2, 500));
                ioctl(_masterFd, TIOCSWINSZ, &ws);
            }

            [[nodiscard]] int ExitStatus() const override
            {
                return _exitStatus;
            }

            [[nodiscard]] std::string_view CommandDescription() const override
            {
                return _description;
            }

        private:
            int _masterFd;
            pid_t _pid;
            int _exitStatus = 0;
            bool _exited = false;
            std::string _description;

            void KillChild()
            {
                if (_pid <= 0)
                    return;

                if (!_exited)
                {
                    // Kill entire process group (negative PID) to terminate all descendants.
                    // This handles sub-agents, background processes, and any other children
                    // spawned by Claude Code.
                    kill(-_pid, SIGHUP);
                    kill(-_pid, SIGTERM);

                    // Wait up to 500ms for graceful termination
                    for (int i = 0; i < 10; ++i)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        int status = 0;
                        pid_t result = waitpid(_pid, &status, WNOHANG);
                        if (result == _pid)
                        {
                            _exited = true;
                            _exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                            _pid = -1;
                            return;
                        }
                    }

                    // Force kill if still running after grace period
                    kill(-_pid, SIGKILL);
                }

                // Final cleanup - reap zombie or detach waiter thread
                int status = 0;
                pid_t result = waitpid(_pid, &status, WNOHANG);
                if (result == _pid)
                {
                    _exited = true;
                    _exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                }
                else if (result == 0)
                {
                    // Process still running (shouldn't happen after SIGKILL), detach waiter
                    pid_t pidToWait = _pid;
                    std::thread([pidToWait]() {
                        int waitStatus = 0;
                        waitpid(pidToWait, &waitStatus, 0);
                    }).detach();
                }

                _pid = -1;
            }

            void CheckChild()
            {
                if (_exited || _pid <= 0)
                    return;

                int status = 0;
                pid_t result = waitpid(_pid, &status, WNOHANG);
                if (result == _pid)
                {
                    _exited = true;
                    _exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                    _pid = -1;
                }
            }
        };

        std::string JoinCommand(const std::vector<std::string>& command)
        {
            std::ostringstream ss;
            for (size_t i = 0; i < command.size(); i++)
            {
                if (i != 0)
                {
                    ss << ' ';
                }
                ss << command[i];
            }
            return ss.str();
        }

        std::unique_ptr<ShellProcess> LaunchPosixProcess(const ShellLaunchOptions& options, std::string& errorOut)
        {
            if (options.command.empty())
            {
                errorOut = "No command specified for terminal session.";
                return nullptr;
            }

            int masterFd = -1;
            struct winsize ws
            {
            };
            ws.ws_col = static_cast<unsigned short>(std::clamp(options.cols, 2, 500));
            ws.ws_row = static_cast<unsigned short>(std::clamp(options.rows, 2, 500));

            pid_t pid = forkpty(&masterFd, nullptr, nullptr, &ws);
            if (pid < 0)
            {
                errorOut = std::string("forkpty failed: ") + strerror(errno);
                return nullptr;
            }

            if (pid == 0)
            {
                // Create new session and process group so we can kill entire tree on cleanup.
                // This makes the child the process group leader, allowing kill(-pid, ...) to
                // terminate all descendants (sub-agents, background processes, etc.)
                setsid();

                if (!options.workingDirectory.empty())
                {
                    if (chdir(options.workingDirectory.c_str()) != 0)
                    {
                        // Write error to stderr before exiting so parent can see it
                        fprintf(stderr, "Failed to change to working directory '%s': %s\n",
                            options.workingDirectory.c_str(), strerror(errno));
                        _exit(126);
                    }
                }

                for (const auto& env : options.environment)
                {
                    putenv(strdup(env.c_str()));
                }

                std::vector<char*> argv;
                argv.reserve(options.command.size() + 1);
                for (const auto& arg : options.command)
                {
                    argv.push_back(const_cast<char*>(arg.c_str()));
                }
                argv.push_back(nullptr);

                execvp(argv[0], argv.data());
                _exit(127);
            }

            // Prevent master FD from being inherited by any future child processes
            int fdflags = fcntl(masterFd, F_GETFD, 0);
            if (fdflags != -1)
            {
                fcntl(masterFd, F_SETFD, fdflags | FD_CLOEXEC);
            }

            return std::make_unique<PosixShellProcess>(masterFd, pid, JoinCommand(options.command));
        }
#endif

#if defined(_WIN32)
        std::string JoinCommandWindows(const std::vector<std::string>& command)
        {
            std::ostringstream ss;
            for (size_t i = 0; i < command.size(); i++)
            {
                if (i != 0)
                {
                    ss << ' ';
                }
                // Quote arguments that contain spaces
                const auto& arg = command[i];
                if (arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos)
                {
                    ss << '"' << arg << '"';
                }
                else
                {
                    ss << arg;
                }
            }
            return ss.str();
        }

        class WindowsShellProcess final : public ShellProcess
        {
        public:
            WindowsShellProcess(
                HPCON hPC, HANDLE hProcess, HANDLE hThread, HANDLE hPipeIn, HANDLE hPipeOut, std::string description)
                : _hPC(hPC)
                , _hProcess(hProcess)
                , _hThread(hThread)
                , _hPipeIn(hPipeIn)
                , _hPipeOut(hPipeOut)
                , _description(std::move(description))
            {
            }

            ~WindowsShellProcess() override
            {
                Cleanup();
            }

            [[nodiscard]] bool IsRunning() const override
            {
                if (_exited)
                    return false;

                DWORD exitCode = 0;
                if (GetExitCodeProcess(_hProcess, &exitCode))
                {
                    if (exitCode != STILL_ACTIVE)
                    {
                        _exited = true;
                        _exitStatus = static_cast<int>(exitCode);
                        return false;
                    }
                }
                return true;
            }

            ssize_t Read(uint8_t* buffer, size_t length) override
            {
                if (_hPipeIn == INVALID_HANDLE_VALUE)
                    return -1;

                // Check if there's data available (non-blocking)
                DWORD available = 0;
                if (!PeekNamedPipe(_hPipeIn, nullptr, 0, nullptr, &available, nullptr))
                {
                    CheckProcess();
                    return _exited ? -1 : 0;
                }

                if (available == 0)
                {
                    CheckProcess();
                    return 0;
                }

                DWORD toRead = static_cast<DWORD>(std::min<size_t>(length, available));
                DWORD bytesRead = 0;
                if (!ReadFile(_hPipeIn, buffer, toRead, &bytesRead, nullptr))
                {
                    CheckProcess();
                    return _exited ? -1 : 0;
                }

                return static_cast<ssize_t>(bytesRead);
            }

            bool Write(std::span<const uint8_t> data) override
            {
                if (_hPipeOut == INVALID_HANDLE_VALUE || data.empty())
                    return false;

                const uint8_t* ptr = data.data();
                size_t remaining = data.size();
                while (remaining > 0)
                {
                    DWORD written = 0;
                    DWORD toWrite = static_cast<DWORD>(std::min<size_t>(remaining, 0xFFFFFFFF));
                    if (!WriteFile(_hPipeOut, ptr, toWrite, &written, nullptr))
                    {
                        return false;
                    }
                    ptr += written;
                    remaining -= written;
                }
                return true;
            }

            void Resize(int cols, int rows) override
            {
                if (_hPC == nullptr)
                    return;

                COORD size;
                size.X = static_cast<SHORT>(std::clamp(cols, 2, 500));
                size.Y = static_cast<SHORT>(std::clamp(rows, 2, 500));
                ResizePseudoConsole(_hPC, size);
            }

            [[nodiscard]] int ExitStatus() const override
            {
                return _exitStatus;
            }

            [[nodiscard]] std::string_view CommandDescription() const override
            {
                return _description;
            }

        private:
            HPCON _hPC = nullptr;
            HANDLE _hProcess = INVALID_HANDLE_VALUE;
            HANDLE _hThread = INVALID_HANDLE_VALUE;
            HANDLE _hPipeIn = INVALID_HANDLE_VALUE;
            HANDLE _hPipeOut = INVALID_HANDLE_VALUE;
            std::string _description;
            mutable bool _exited = false;
            mutable int _exitStatus = 0;

            void CheckProcess()
            {
                if (_exited)
                    return;

                DWORD exitCode = 0;
                if (GetExitCodeProcess(_hProcess, &exitCode) && exitCode != STILL_ACTIVE)
                {
                    _exited = true;
                    _exitStatus = static_cast<int>(exitCode);
                }
            }

            void Cleanup()
            {
                if (_hProcess != INVALID_HANDLE_VALUE && !_exited)
                {
                    // Try graceful termination first
                    TerminateProcess(_hProcess, 1);
                    WaitForSingleObject(_hProcess, 500);
                }

                if (_hPC != nullptr)
                {
                    ClosePseudoConsole(_hPC);
                    _hPC = nullptr;
                }
                if (_hPipeIn != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(_hPipeIn);
                    _hPipeIn = INVALID_HANDLE_VALUE;
                }
                if (_hPipeOut != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(_hPipeOut);
                    _hPipeOut = INVALID_HANDLE_VALUE;
                }
                if (_hThread != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(_hThread);
                    _hThread = INVALID_HANDLE_VALUE;
                }
                if (_hProcess != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(_hProcess);
                    _hProcess = INVALID_HANDLE_VALUE;
                }
            }
        };

        std::unique_ptr<ShellProcess> LaunchWindowsProcess(const ShellLaunchOptions& options, std::string& errorOut)
        {
            if (options.command.empty())
            {
                errorOut = "No command specified for terminal session.";
                return nullptr;
            }

            HANDLE hPipeInRead = INVALID_HANDLE_VALUE;
            HANDLE hPipeInWrite = INVALID_HANDLE_VALUE;
            HANDLE hPipeOutRead = INVALID_HANDLE_VALUE;
            HANDLE hPipeOutWrite = INVALID_HANDLE_VALUE;
            HPCON hPC = nullptr;

            auto cleanup = [&]() {
                if (hPipeInRead != INVALID_HANDLE_VALUE)
                    CloseHandle(hPipeInRead);
                if (hPipeInWrite != INVALID_HANDLE_VALUE)
                    CloseHandle(hPipeInWrite);
                if (hPipeOutRead != INVALID_HANDLE_VALUE)
                    CloseHandle(hPipeOutRead);
                if (hPipeOutWrite != INVALID_HANDLE_VALUE)
                    CloseHandle(hPipeOutWrite);
                if (hPC != nullptr)
                    ClosePseudoConsole(hPC);
            };

            // Create pipes for stdin and stdout
            SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            if (!CreatePipe(&hPipeInRead, &hPipeInWrite, &sa, 0))
            {
                errorOut = "Failed to create input pipe";
                cleanup();
                return nullptr;
            }
            if (!CreatePipe(&hPipeOutRead, &hPipeOutWrite, &sa, 0))
            {
                errorOut = "Failed to create output pipe";
                cleanup();
                return nullptr;
            }

            // Create the pseudo console
            COORD consoleSize;
            consoleSize.X = static_cast<SHORT>(std::clamp(options.cols, 2, 500));
            consoleSize.Y = static_cast<SHORT>(std::clamp(options.rows, 2, 500));

            HRESULT hr = CreatePseudoConsole(consoleSize, hPipeOutRead, hPipeInWrite, 0, &hPC);
            if (FAILED(hr))
            {
                errorOut = "Failed to create pseudo console (requires Windows 10 1809+)";
                cleanup();
                return nullptr;
            }

            // Close the handles that are now owned by the pseudo console
            CloseHandle(hPipeOutRead);
            hPipeOutRead = INVALID_HANDLE_VALUE;
            CloseHandle(hPipeInWrite);
            hPipeInWrite = INVALID_HANDLE_VALUE;

            // Initialize the startup info with the pseudo console
            STARTUPINFOEXW startupInfo = {};
            startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEXW);

            SIZE_T attrListSize = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

            std::vector<uint8_t> attrListBuffer(attrListSize);
            startupInfo.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrListBuffer.data());

            if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attrListSize))
            {
                errorOut = "Failed to initialize attribute list";
                cleanup();
                return nullptr;
            }

            if (!UpdateProcThreadAttribute(
                    startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(HPCON), nullptr,
                    nullptr))
            {
                errorOut = "Failed to set pseudo console attribute";
                DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
                cleanup();
                return nullptr;
            }

            // Build the command line
            std::string cmdLine = JoinCommandWindows(options.command);
            std::wstring wCmdLine(cmdLine.begin(), cmdLine.end());

            // Build environment block - inherit current environment and add custom vars
            std::wstring envBlock;
            if (!options.environment.empty())
            {
                // Get current environment and copy it
                wchar_t* currentEnv = GetEnvironmentStringsW();
                if (currentEnv != nullptr)
                {
                    // Environment block is double-null terminated, each var is null-terminated
                    const wchar_t* ptr = currentEnv;
                    while (*ptr != L'\0')
                    {
                        size_t len = wcslen(ptr);
                        envBlock.append(ptr, len);
                        envBlock += L'\0';
                        ptr += len + 1;
                    }
                    FreeEnvironmentStringsW(currentEnv);
                }

                // Add custom environment variables
                for (const auto& env : options.environment)
                {
                    std::wstring wEnv(env.begin(), env.end());
                    envBlock += wEnv;
                    envBlock += L'\0';
                }
                envBlock += L'\0';
            }

            // Convert working directory
            std::wstring wWorkingDir;
            if (!options.workingDirectory.empty())
            {
                wWorkingDir = std::wstring(options.workingDirectory.begin(), options.workingDirectory.end());
            }

            PROCESS_INFORMATION procInfo = {};
            BOOL success = CreateProcessW(
                nullptr, wCmdLine.data(),
                nullptr, // Process security attributes
                nullptr, // Thread security attributes
                FALSE,   // Inherit handles
                EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                options.environment.empty() ? nullptr : envBlock.data(),
                wWorkingDir.empty() ? nullptr : wWorkingDir.c_str(), &startupInfo.StartupInfo, &procInfo);

            DeleteProcThreadAttributeList(startupInfo.lpAttributeList);

            if (!success)
            {
                DWORD error = GetLastError();
                errorOut = "Failed to create process, error code: " + std::to_string(error);
                cleanup();
                return nullptr;
            }

            return std::make_unique<WindowsShellProcess>(
                hPC, procInfo.hProcess, procInfo.hThread, hPipeInRead, hPipeOutWrite,
                JoinCommandWindows(options.command));
        }
#endif
    } // namespace

    bool ShellProcess::Write(std::string_view text)
    {
        return Write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    }

    std::unique_ptr<ShellProcess> LaunchShellProcess(const ShellLaunchOptions& options, std::string& errorOut)
    {
#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        return LaunchPosixProcess(options, errorOut);
#elif defined(_WIN32)
        return LaunchWindowsProcess(options, errorOut);
#else
        errorOut = "Agent terminal is not supported on this platform.";
        return nullptr;
#endif
    }
} // namespace OpenRCT2::Terminal
