/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../interface/Colour.h"

namespace OpenRCT2::Terminal
{
    struct TerminalColourRGB
    {
        uint8_t r{ 0 };
        uint8_t g{ 0 };
        uint8_t b{ 0 };
    };

    struct TerminalCell
    {
        char32_t codepoint{};
        colour_t foreground{ COLOUR_WHITE };
        colour_t background{ COLOUR_BLACK };
        TerminalColourRGB foregroundRgb{ 255, 255, 255 };
        TerminalColourRGB backgroundRgb{ 0, 0, 0 };
        bool bold{};
        bool underline{};
        bool inverse{};
        bool wide{};
        bool continuation{};
    };

    struct TerminalSnapshot
    {
        int rows{};
        int cols{};
        std::vector<TerminalCell> cells;

        void Clear()
        {
            rows = 0;
            cols = 0;
            cells.clear();
        }
    };

    class TerminalSession
    {
    public:
        TerminalSession(int cols, int rows);
        ~TerminalSession();

        TerminalSession(const TerminalSession&) = delete;
        TerminalSession& operator=(const TerminalSession&) = delete;

        void Resize(int cols, int rows);
        void FeedOutput(std::span<const uint8_t> bytes);
        bool ConsumeSnapshot(TerminalSnapshot& outSnapshot);
        void ForceFullRefresh();

        [[nodiscard]] int GetCols() const;
        [[nodiscard]] int GetRows() const;
        [[nodiscard]] int GetScrollbackRowCount() const;
        [[nodiscard]] bool IsAltScreenActive() const;
        void CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell>& out) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
} // namespace OpenRCT2::Terminal
