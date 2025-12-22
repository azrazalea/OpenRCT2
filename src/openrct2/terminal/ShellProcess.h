/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace OpenRCT2::Terminal
{
    struct ShellLaunchOptions
    {
        std::vector<std::string> command;
        std::vector<std::string> environment;
        std::string workingDirectory;
        int cols = 80;
        int rows = 24;
    };

    class ShellProcess
    {
    public:
        virtual ~ShellProcess() = default;

        [[nodiscard]] virtual bool IsRunning() const = 0;
        virtual ssize_t Read(uint8_t* buffer, size_t length) = 0;
        virtual bool Write(std::span<const uint8_t> data) = 0;
        bool Write(std::string_view text);
        virtual void Resize(int cols, int rows) = 0;
        [[nodiscard]] virtual int ExitStatus() const = 0;
        [[nodiscard]] virtual std::string_view CommandDescription() const = 0;
    };

    std::unique_ptr<ShellProcess> LaunchShellProcess(const ShellLaunchOptions& options, std::string& errorOut);
} // namespace OpenRCT2::Terminal

