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

#if defined(__APPLE__)
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
    } // namespace

    bool ShellProcess::Write(std::string_view text)
    {
        return Write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    }

    std::unique_ptr<ShellProcess> LaunchShellProcess(const ShellLaunchOptions& options, std::string& errorOut)
    {
#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        return LaunchPosixProcess(options, errorOut);
#else
        errorOut = "Agent terminal is only supported on POSIX builds right now.";
        return nullptr;
#endif
    }
} // namespace OpenRCT2::Terminal
