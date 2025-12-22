/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TerminalSession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#ifndef OPENRCT2_HAVE_LIBVTERM
    #define OPENRCT2_HAVE_LIBVTERM 0
#endif

#if OPENRCT2_HAVE_LIBVTERM
    #include <vterm.h>
#endif

using OpenRCT2::Terminal::TerminalCell;
using OpenRCT2::Terminal::TerminalColourRGB;
using OpenRCT2::Terminal::TerminalSession;
using OpenRCT2::Terminal::TerminalSnapshot;

namespace
{
    constexpr colour_t kDefaultForeground = COLOUR_WHITE;
    constexpr colour_t kDefaultBackground = COLOUR_BLACK;
    constexpr TerminalColourRGB kDefaultForegroundRgb{ 229, 229, 229 };
    constexpr TerminalColourRGB kDefaultBackgroundRgb{ 0, 0, 0 };

#if OPENRCT2_HAVE_LIBVTERM
    struct ColourCandidate
    {
        colour_t colour;
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    constexpr std::array<ColourCandidate, 16> kAnsiColourMap = {
        ColourCandidate{ COLOUR_BLACK, 0, 0, 0 },
        ColourCandidate{ COLOUR_BRIGHT_RED, 205, 0, 0 },
        ColourCandidate{ COLOUR_BRIGHT_GREEN, 0, 205, 0 },
        ColourCandidate{ COLOUR_BRIGHT_YELLOW, 205, 205, 0 },
        ColourCandidate{ COLOUR_DARK_BLUE, 0, 0, 238 },
        ColourCandidate{ COLOUR_BRIGHT_PURPLE, 205, 0, 205 },
        ColourCandidate{ COLOUR_TEAL, 0, 205, 205 },
        ColourCandidate{ COLOUR_WHITE, 229, 229, 229 },
        ColourCandidate{ COLOUR_GREY, 127, 127, 127 },
        ColourCandidate{ COLOUR_BRIGHT_RED, 255, 0, 0 },
        ColourCandidate{ COLOUR_SATURATED_GREEN, 0, 255, 0 },
        ColourCandidate{ COLOUR_BRIGHT_YELLOW, 255, 255, 0 },
        ColourCandidate{ COLOUR_LIGHT_BLUE, 92, 92, 255 },
        ColourCandidate{ COLOUR_LIGHT_PURPLE, 255, 92, 255 },
        ColourCandidate{ COLOUR_AQUAMARINE, 92, 255, 255 },
        ColourCandidate{ COLOUR_WHITE, 255, 255, 255 },
    };

    [[nodiscard]] constexpr colour_t MapColour(uint8_t r, uint8_t g, uint8_t b)
    {
        uint32_t bestScore = std::numeric_limits<uint32_t>::max();
        colour_t bestColour = kDefaultForeground;
        for (const auto& candidate : kAnsiColourMap)
        {
            const int32_t dr = static_cast<int32_t>(r) - candidate.r;
            const int32_t dg = static_cast<int32_t>(g) - candidate.g;
            const int32_t db = static_cast<int32_t>(b) - candidate.b;
            const uint32_t score = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
            if (score < bestScore)
            {
                bestScore = score;
                bestColour = candidate.colour;
            }
        }
        return bestColour;
    }
#endif

    constexpr int kMaxScrollbackRows = 2000;

    [[nodiscard]] TerminalCell MakeEmptyCell()
    {
        TerminalCell cell;
        cell.codepoint = U' ';
        cell.foreground = kDefaultForeground;
        cell.background = kDefaultBackground;
        cell.foregroundRgb = kDefaultForegroundRgb;
        cell.backgroundRgb = kDefaultBackgroundRgb;
        return cell;
    }
} // namespace

namespace OpenRCT2::Terminal
{
#if OPENRCT2_HAVE_LIBVTERM
    [[nodiscard]] static TerminalCell ConvertVTermCell(VTermScreen* screen, const VTermScreenCell& cell)
    {
        TerminalCell target = MakeEmptyCell();
        target.codepoint = cell.chars[0] != 0 ? static_cast<char32_t>(cell.chars[0]) : U' ';
        auto fg = cell.fg;
        auto bg = cell.bg;
        vterm_screen_convert_color_to_rgb(screen, &fg);
        vterm_screen_convert_color_to_rgb(screen, &bg);
        target.foreground = MapColour(fg.rgb.red, fg.rgb.green, fg.rgb.blue);
        target.background = MapColour(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
        target.foregroundRgb = TerminalColourRGB{
            static_cast<uint8_t>(fg.rgb.red), static_cast<uint8_t>(fg.rgb.green), static_cast<uint8_t>(fg.rgb.blue) };
        target.backgroundRgb = TerminalColourRGB{
            static_cast<uint8_t>(bg.rgb.red), static_cast<uint8_t>(bg.rgb.green), static_cast<uint8_t>(bg.rgb.blue) };
        target.bold = cell.attrs.bold;
        target.underline = cell.attrs.underline != 0;
        target.inverse = cell.attrs.reverse;
        target.wide = cell.width > 1;
        // libvterm uses width == 0 for the trailing cell of a wide glyph.
        // chars[0] == 0 can also indicate a blank cell, so don't treat it as continuation.
        target.continuation = (cell.width == 0);

        if (target.inverse)
        {
            std::swap(target.foreground, target.background);
            std::swap(target.foregroundRgb, target.backgroundRgb);
        }
        return target;
    }

    struct TerminalSession::Impl
    {
        VTerm* term = nullptr;
        VTermScreen* screen = nullptr;
        VTermState* state = nullptr;
        bool dirty = true;
        bool altScreenActive = false;
        TerminalSnapshot snapshot;
        int cols = 0;
        int rows = 0;
        std::deque<std::vector<TerminalCell>> scrollback;

        explicit Impl(int initialCols, int initialRows)
        {
            cols = initialCols;
            rows = initialRows;

            term = vterm_new(rows, cols);
            state = vterm_obtain_state(term);
            screen = vterm_obtain_screen(term);

            vterm_set_utf8(term, 1);
            vterm_state_reset(state, 1);
            vterm_screen_reset(screen, 1);
            vterm_screen_enable_altscreen(screen, 1);

            // All callbacks must be set to avoid NULL pointer crashes in libvterm.
            // libvterm may call any of these without checking for NULL.
            static VTermScreenCallbacks callbacks = {
                // damage
                [](VTermRect, void* user) {
                    auto* impl = static_cast<Impl*>(user);
                    if (impl) impl->dirty = true;
                    return 1;
                },
                // moverect
                [](VTermRect, VTermRect, void*) { return 1; },
                // movecursor
                [](VTermPos, VTermPos, int, void*) { return 1; },
                // settermprop
                [](VTermProp prop, VTermValue* val, void* user) {
                    auto* impl = static_cast<Impl*>(user);
                    if (impl == nullptr || val == nullptr)
                    {
                        return 0;
                    }
                    if (prop == VTERM_PROP_ALTSCREEN)
                    {
                        impl->altScreenActive = (val->boolean != 0);
                        impl->dirty = true;
                        return 1;
                    }
                    return 0;
                },
                // bell
                [](void*) { return 1; },
                // resize
                [](int newRows, int newCols, void* user) {
                    auto* impl = static_cast<Impl*>(user);
                    if (impl) {
                        impl->rows = newRows;
                        impl->cols = newCols;
                        impl->dirty = true;
                    }
                    return 1;
                },
                // sb_pushline
                [](int lineCols, const VTermScreenCell* cells, void* user) {
                    auto* impl = static_cast<Impl*>(user);
                    if (impl == nullptr || lineCols <= 0 || cells == nullptr)
                    {
                        return 0;
                    }
                    impl->PushScrollbackLine(cells, lineCols);
                    return 1;
                },
                // sb_popline
                [](int, VTermScreenCell*, void*) { return 0; },
                // sb_clear
                [](void*) { return 1; },
            };

            vterm_screen_set_callbacks(screen, &callbacks, this);
            vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCROLL);

            snapshot.rows = rows;
            snapshot.cols = cols;
            snapshot.cells.resize(static_cast<size_t>(rows) * cols, MakeEmptyCell());
        }

        ~Impl()
        {
            if (term != nullptr)
            {
                vterm_free(term);
                term = nullptr;
            }
        }

        void ForceRefresh()
        {
            dirty = true;
        }

        void Feed(std::span<const uint8_t> bytes)
        {
            if (bytes.empty())
                return;

            if (term == nullptr || state == nullptr || screen == nullptr || bytes.data() == nullptr)
                return;
            vterm_input_write(term, reinterpret_cast<const char*>(bytes.data()), bytes.size());
            dirty = true;
        }

        void Resize(int newCols, int newRows)
        {
            if (newCols == cols && newRows == rows)
                return;

            cols = std::max(2, newCols);
            rows = std::max(2, newRows);
            vterm_set_size(term, rows, cols);
            scrollback.clear();
            dirty = true;
        }

        bool Snapshot(TerminalSnapshot& outSnapshot)
        {
            if (!dirty)
            {
                return false;
            }

            dirty = false;
            snapshot.rows = rows;
            snapshot.cols = cols;
            snapshot.cells.resize(static_cast<size_t>(rows) * cols, MakeEmptyCell());

            VTermPos pos{};
            VTermScreenCell cell{};
            for (int row = 0; row < rows; row++)
            {
                pos.row = row;
                for (int col = 0; col < cols; col++)
                {
                    pos.col = col;
                    if (!vterm_screen_get_cell(screen, pos, &cell))
                    {
                        continue;
                    }

                    auto& target = snapshot.cells[static_cast<size_t>(row) * cols + col];
                    target = ConvertVTermCell(screen, cell);
                }
            }

            vterm_screen_flush_damage(screen);
            outSnapshot = snapshot;
            return true;
        }

        void PushScrollbackLine(const VTermScreenCell* cells, int numCols)
        {
            if (screen == nullptr || numCols <= 0)
            {
                return;
            }

            std::vector<TerminalCell> row;
            row.reserve(static_cast<size_t>(numCols));
            for (int col = 0; col < numCols; col++)
            {
                row.push_back(ConvertVTermCell(screen, cells[col]));
            }
            scrollback.push_back(std::move(row));
            while (scrollback.size() > kMaxScrollbackRows)
            {
                scrollback.pop_front();
            }
        }

        int GetScrollbackRows() const
        {
            return static_cast<int>(scrollback.size());
        }

        void CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell>& out) const
        {
            if (rowCount <= 0 || startRow < 0)
            {
                out.clear();
                return;
            }

            const int lineWidth = snapshot.cols;
            const size_t clampedCount = static_cast<size_t>(rowCount);
            out.resize(clampedCount * static_cast<size_t>(lineWidth), MakeEmptyCell());

            for (int i = 0; i < rowCount; i++)
            {
                int sourceIndex = startRow + i;
                if (sourceIndex < 0 || sourceIndex >= static_cast<int>(scrollback.size()))
                {
                    continue;
                }

                const auto& row = scrollback[static_cast<size_t>(sourceIndex)];
                auto* dest = &out[static_cast<size_t>(i) * lineWidth];
                const size_t copyCount = std::min<size_t>(row.size(), static_cast<size_t>(lineWidth));
                std::copy_n(row.begin(), copyCount, dest);
                if (copyCount < static_cast<size_t>(lineWidth))
                {
                    std::fill(dest + copyCount, dest + lineWidth, MakeEmptyCell());
                }
            }
        }

        bool IsAltScreenActive() const
        {
            return altScreenActive;
        }
    };
#else
    namespace
    {
        constexpr std::array<colour_t, 8> kAnsiBasicColours = {
            COLOUR_BLACK, COLOUR_BRIGHT_RED, COLOUR_BRIGHT_GREEN, COLOUR_BRIGHT_YELLOW,
            COLOUR_DARK_BLUE, COLOUR_BRIGHT_PURPLE, COLOUR_TEAL, COLOUR_WHITE,
        };

        constexpr std::array<colour_t, 8> kAnsiBrightColours = {
            COLOUR_GREY, COLOUR_BRIGHT_RED, COLOUR_BRIGHT_GREEN, COLOUR_BRIGHT_YELLOW,
            COLOUR_LIGHT_BLUE, COLOUR_LIGHT_PURPLE, COLOUR_AQUAMARINE, COLOUR_WHITE,
        };

        constexpr std::array<TerminalColourRGB, 8> kAnsiBasicColourRgb = {
            TerminalColourRGB{ 0, 0, 0 },        TerminalColourRGB{ 205, 0, 0 },   TerminalColourRGB{ 0, 205, 0 },
            TerminalColourRGB{ 205, 205, 0 },    TerminalColourRGB{ 0, 0, 238 },   TerminalColourRGB{ 205, 0, 205 },
            TerminalColourRGB{ 0, 205, 205 },    TerminalColourRGB{ 229, 229, 229 },
        };

        constexpr std::array<TerminalColourRGB, 8> kAnsiBrightColourRgb = {
            TerminalColourRGB{ 127, 127, 127 }, TerminalColourRGB{ 255, 0, 0 },   TerminalColourRGB{ 0, 255, 0 },
            TerminalColourRGB{ 255, 255, 0 },   TerminalColourRGB{ 92, 92, 255 }, TerminalColourRGB{ 255, 92, 255 },
            TerminalColourRGB{ 92, 255, 255 },  TerminalColourRGB{ 255, 255, 255 },
        };

        [[nodiscard]] colour_t MapEightColour(int index, bool bright)
        {
            index = std::clamp(index, 0, 7);
            return bright ? kAnsiBrightColours[static_cast<size_t>(index)] : kAnsiBasicColours[static_cast<size_t>(index)];
        }

        [[nodiscard]] TerminalColourRGB MapEightColourRgb(int index, bool bright)
        {
            index = std::clamp(index, 0, 7);
            return bright ? kAnsiBrightColourRgb[static_cast<size_t>(index)]
                          : kAnsiBasicColourRgb[static_cast<size_t>(index)];
        }
    } // namespace

    struct TerminalSession::Impl
    {
        TerminalSnapshot snapshot;
        int cols = 0;
        int rows = 0;
        int cursorRow = 0;
        int cursorCol = 0;
        int savedCursorRow = 0;
        int savedCursorCol = 0;
        bool dirty = true;
        colour_t currentForeground = kDefaultForeground;
        colour_t currentBackground = kDefaultBackground;
        TerminalColourRGB currentForegroundRgb = kDefaultForegroundRgb;
        TerminalColourRGB currentBackgroundRgb = kDefaultBackgroundRgb;
        bool currentBold = false;
        bool currentUnderline = false;
        bool currentInverse = false;
        std::deque<std::vector<TerminalCell>> scrollback;

        enum class EscapeState
        {
            Text,
            EscapeIntroducer,
            CSI,
            OSC,
        };

        EscapeState escapeState = EscapeState::Text;
        std::string csiBuffer;
        bool csiPrivate = false;
        bool oscEscapePending = false;

        explicit Impl(int initialCols, int initialRows)
        {
            Resize(initialCols, initialRows);
        }

        void ForceRefresh()
        {
            dirty = true;
        }

        void ResetAttributes()
        {
            currentForeground = kDefaultForeground;
            currentBackground = kDefaultBackground;
            currentForegroundRgb = kDefaultForegroundRgb;
            currentBackgroundRgb = kDefaultBackgroundRgb;
            currentBold = false;
            currentUnderline = false;
            currentInverse = false;
        }

        void Feed(std::span<const uint8_t> bytes)
        {
            for (uint8_t byte : bytes)
            {
                char32_t ch = static_cast<char32_t>(byte);
                switch (escapeState)
                {
                    case EscapeState::Text:
                        HandleTextChar(ch);
                        break;
                    case EscapeState::EscapeIntroducer:
                        HandleEscapeIntroducer(ch);
                        break;
                    case EscapeState::CSI:
                        HandleCsiChar(ch);
                        break;
                    case EscapeState::OSC:
                        HandleOscChar(ch);
                        break;
                }
            }
            dirty = true;
        }

        void HandleTextChar(char32_t ch)
        {
            switch (ch)
            {
                case '\r':
                    cursorCol = 0;
                    break;
                case '\n':
                    cursorCol = 0;
                    cursorRow++;
                    if (cursorRow >= rows)
                    {
                        Scroll();
                        cursorRow = rows - 1;
                    }
                    break;
                case '\b':
                    cursorCol = std::max(0, cursorCol - 1);
                    WriteChar(U' ');
                    cursorCol = std::max(0, cursorCol - 1);
                    break;
                case '\t':
                {
                    constexpr int tab = 4;
                    int spaces = tab - (cursorCol % tab);
                    for (int i = 0; i < spaces; i++)
                    {
                        WriteChar(U' ');
                    }
                    break;
                }
                case 0x1B:
                    escapeState = EscapeState::EscapeIntroducer;
                    break;
                default:
                    if (ch >= 0x20)
                    {
                        WriteChar(ch);
                    }
                    break;
            }
        }

        void HandleEscapeIntroducer(char32_t ch)
        {
            if (ch == '[')
            {
                escapeState = EscapeState::CSI;
                csiBuffer.clear();
                csiPrivate = false;
            }
            else if (ch == ']')
            {
                escapeState = EscapeState::OSC;
                oscEscapePending = false;
            }
            else
            {
                escapeState = EscapeState::Text;
            }
        }

        void HandleCsiChar(char32_t ch)
        {
            if (ch == '?')
            {
                csiPrivate = true;
                return;
            }
            if (ch >= 0x40 && ch <= 0x7E)
            {
                HandleCsiCommand(static_cast<char>(ch));
                escapeState = EscapeState::Text;
                csiBuffer.clear();
                csiPrivate = false;
            }
            else
            {
                csiBuffer.push_back(static_cast<char>(ch));
            }
        }

        void HandleOscChar(char32_t ch)
        {
            if (ch == 0x07)
            {
                escapeState = EscapeState::Text;
                return;
            }
            if (oscEscapePending)
            {
                if (ch == '\\')
                {
                    escapeState = EscapeState::Text;
                }
                else if (ch == '[')
                {
                    // ESC was start of a new CSI sequence
                    escapeState = EscapeState::CSI;
                    csiBuffer.clear();
                    csiPrivate = false;
                }
                else if (ch == ']')
                {
                    // Start new OSC
                    escapeState = EscapeState::OSC;
                }
                else
                {
                    escapeState = EscapeState::Text;
                }
                oscEscapePending = false;
                return;
            }

            if (ch == 0x1B)
            {
                oscEscapePending = true;
            }
        }

        [[nodiscard]] std::vector<int> ParseCsiParams() const
        {
            std::vector<int> params;
            std::string current;
            for (char c : csiBuffer)
            {
                if (c == ';')
                {
                    params.push_back(current.empty() ? -1 : std::stoi(current));
                    current.clear();
                }
                else if (c >= '0' && c <= '9')
                {
                    current.push_back(c);
                }
            }

            if (!current.empty() || csiBuffer.find(';') != std::string::npos)
            {
                params.push_back(current.empty() ? -1 : std::stoi(current));
            }

            return params;
        }

        void HandleCsiCommand(char finalByte)
        {
            if (csiPrivate)
            {
                return;
            }

            auto params = ParseCsiParams();
            switch (finalByte)
            {
                case 'A':
                    cursorRow = std::max(0, cursorRow - GetParam(params, 0, 1));
                    break;
                case 'B':
                    cursorRow = std::min(rows - 1, cursorRow + GetParam(params, 0, 1));
                    break;
                case 'C':
                    cursorCol = std::min(cols - 1, cursorCol + GetParam(params, 0, 1));
                    break;
                case 'D':
                    cursorCol = std::max(0, cursorCol - GetParam(params, 0, 1));
                    break;
                case 'H':
                case 'f':
                    MoveCursorTo(GetParam(params, 0, 1), GetParam(params, 1, 1));
                    break;
                case 'J':
                    EraseInDisplay(GetParam(params, 0, 0));
                    break;
                case 'K':
                    EraseInLine(GetParam(params, 0, 0));
                    break;
                case 'm':
                    ApplySgr(params);
                    break;
                case 's':
                    savedCursorRow = cursorRow;
                    savedCursorCol = cursorCol;
                    break;
                case 'u':
                    cursorRow = std::clamp(savedCursorRow, 0, rows - 1);
                    cursorCol = std::clamp(savedCursorCol, 0, cols - 1);
                    break;
                default:
                    break;
            }
        }

        static int GetParam(const std::vector<int>& params, size_t index, int defaultValue)
        {
            if (index >= params.size() || params[index] < 0)
            {
                return defaultValue;
            }
            return params[index];
        }

        void ApplySgr(std::vector<int> params)
        {
            if (params.empty())
            {
                ResetAttributes();
                return;
            }

            for (int rawCode : params)
            {
                const int code = rawCode < 0 ? 0 : rawCode;
                if (code == 0)
                {
                    ResetAttributes();
                }
                else if (code == 1)
                {
                    currentBold = true;
                }
                else if (code == 4)
                {
                    currentUnderline = true;
                }
                else if (code == 7)
                {
                    currentInverse = true;
                }
                else if (code == 22)
                {
                    currentBold = false;
                }
                else if (code == 24)
                {
                    currentUnderline = false;
                }
                else if (code == 27)
                {
                    currentInverse = false;
                }
                else if (code == 39)
                {
                    currentForeground = kDefaultForeground;
                    currentForegroundRgb = kDefaultForegroundRgb;
                }
                else if (code == 49)
                {
                    currentBackground = kDefaultBackground;
                    currentBackgroundRgb = kDefaultBackgroundRgb;
                }
                else if (code >= 30 && code <= 37)
                {
                    currentForeground = MapEightColour(code - 30, false);
                    currentForegroundRgb = MapEightColourRgb(code - 30, false);
                }
                else if (code >= 40 && code <= 47)
                {
                    currentBackground = MapEightColour(code - 40, false);
                    currentBackgroundRgb = MapEightColourRgb(code - 40, false);
                }
                else if (code >= 90 && code <= 97)
                {
                    currentForeground = MapEightColour(code - 90, true);
                    currentForegroundRgb = MapEightColourRgb(code - 90, true);
                }
                else if (code >= 100 && code <= 107)
                {
                    currentBackground = MapEightColour(code - 100, true);
                    currentBackgroundRgb = MapEightColourRgb(code - 100, true);
                }
            }
        }

        void MoveCursorTo(int rowParam, int colParam)
        {
            int targetRow = (rowParam <= 0 ? 1 : rowParam) - 1;
            int targetCol = (colParam <= 0 ? 1 : colParam) - 1;
            cursorRow = std::clamp(targetRow, 0, rows - 1);
            cursorCol = std::clamp(targetCol, 0, cols - 1);
        }

        void EraseInDisplay(int mode)
        {
            switch (mode)
            {
                case 0:
                    ClearRange(cursorRow, cursorCol, rows - 1, cols - 1);
                    break;
                case 1:
                    ClearRange(0, 0, cursorRow, cursorCol);
                    break;
                case 2:
                    ClearRange(0, 0, rows - 1, cols - 1);
                    cursorRow = 0;
                    cursorCol = 0;
                    break;
                default:
                    break;
            }
        }

        void EraseInLine(int mode)
        {
            const int row = cursorRow;
            int startCol = 0;
            int endCol = cols - 1;
            switch (mode)
            {
                case 0:
                    startCol = cursorCol;
                    break;
                case 1:
                    endCol = cursorCol;
                    break;
                case 2:
                default:
                    break;
            }

            for (int col = startCol; col <= endCol && row < rows; col++)
            {
                snapshot.cells[static_cast<size_t>(row) * cols + col] = MakeEmptyCell();
            }
        }

        void ClearRange(int startRow, int startCol, int endRow, int endCol)
        {
            startRow = std::clamp(startRow, 0, rows - 1);
            endRow = std::clamp(endRow, 0, rows - 1);
            startCol = std::clamp(startCol, 0, cols - 1);
            endCol = std::clamp(endCol, 0, cols - 1);

            for (int row = startRow; row <= endRow; row++)
            {
                int beginCol = (row == startRow) ? startCol : 0;
                int finishCol = (row == endRow) ? endCol : cols - 1;
                for (int col = beginCol; col <= finishCol; col++)
                {
                    snapshot.cells[static_cast<size_t>(row) * cols + col] = MakeEmptyCell();
                }
            }
        }

        void Scroll()
        {
            if (rows <= 1)
                return;

            PushScrollbackRow(0);
            auto begin = snapshot.cells.begin();
            auto end = begin + static_cast<int64_t>(cols);
            std::rotate(begin, end, snapshot.cells.end());
            std::fill(snapshot.cells.end() - cols, snapshot.cells.end(), MakeEmptyCell());
        }

        void WriteChar(char32_t ch)
        {
            if (cursorCol >= cols)
            {
                cursorCol = 0;
                cursorRow++;
            }
            if (cursorRow >= rows)
            {
                Scroll();
                cursorRow = rows - 1;
            }

            auto& cell = snapshot.cells[static_cast<size_t>(cursorRow) * cols + cursorCol];
            cell = MakeEmptyCell();
            cell.codepoint = ch;
            cell.foreground = currentForeground;
            cell.background = currentBackground;
            cell.foregroundRgb = currentForegroundRgb;
            cell.backgroundRgb = currentBackgroundRgb;
            cell.bold = currentBold;
            cell.underline = currentUnderline;
            cell.inverse = currentInverse;
            if (cell.inverse)
            {
                std::swap(cell.foreground, cell.background);
                std::swap(cell.foregroundRgb, cell.backgroundRgb);
            }
            cursorCol++;
        }

        void PushScrollbackRow(int rowIndex)
        {
            if (rowIndex < 0 || rowIndex >= rows || cols <= 0)
            {
                return;
            }

            std::vector<TerminalCell> row;
            row.reserve(static_cast<size_t>(cols));
            const auto* src = &snapshot.cells[static_cast<size_t>(rowIndex) * cols];
            row.insert(row.end(), src, src + cols);
            scrollback.push_back(std::move(row));
            while (scrollback.size() > kMaxScrollbackRows)
            {
                scrollback.pop_front();
            }
        }

        void Resize(int newCols, int newRows)
        {
            cols = std::max(2, newCols);
            rows = std::max(2, newRows);
            snapshot.rows = rows;
            snapshot.cols = cols;
            snapshot.cells.assign(static_cast<size_t>(rows) * cols, MakeEmptyCell());
            cursorRow = std::min(cursorRow, rows - 1);
            cursorCol = std::min(cursorCol, cols - 1);
            savedCursorRow = std::clamp(savedCursorRow, 0, rows - 1);
            savedCursorCol = std::clamp(savedCursorCol, 0, cols - 1);
            scrollback.clear();
            ResetAttributes();
            dirty = true;
        }

        int GetScrollbackRows() const
        {
            return static_cast<int>(scrollback.size());
        }

        void CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell>& out) const
        {
            if (rowCount <= 0 || startRow < 0)
            {
                out.clear();
                return;
            }

            const int lineWidth = snapshot.cols;
            if (lineWidth <= 0)
            {
                out.clear();
                return;
            }

            const size_t clampedCount = static_cast<size_t>(rowCount);
            out.resize(clampedCount * static_cast<size_t>(lineWidth), MakeEmptyCell());
            for (int i = 0; i < rowCount; i++)
            {
                int sourceIndex = startRow + i;
                if (sourceIndex < 0 || sourceIndex >= static_cast<int>(scrollback.size()))
                {
                    continue;
                }

                const auto& row = scrollback[static_cast<size_t>(sourceIndex)];
                auto* dest = &out[static_cast<size_t>(i) * lineWidth];
                const size_t copyCount = std::min(row.size(), static_cast<size_t>(lineWidth));
                std::copy_n(row.begin(), copyCount, dest);
                if (copyCount < static_cast<size_t>(lineWidth))
                {
                    std::fill(dest + copyCount, dest + lineWidth, MakeEmptyCell());
                }
            }
        }

        bool Snapshot(TerminalSnapshot& outSnapshot)
        {
            if (!dirty)
            {
                return false;
            }

            dirty = false;
            outSnapshot = snapshot;
            return true;
        }

        bool IsAltScreenActive() const
        {
            return false;
        }
    };
#endif

    TerminalSession::TerminalSession(int cols, int rows)
        : _impl(std::make_unique<Impl>(cols, rows))
    {
    }

    TerminalSession::~TerminalSession() = default;

    void TerminalSession::Resize(int cols, int rows)
    {
        _impl->Resize(cols, rows);
    }

    void TerminalSession::FeedOutput(std::span<const uint8_t> bytes)
    {
        if (bytes.empty())
            return;
        _impl->Feed(bytes);
    }

    bool TerminalSession::ConsumeSnapshot(TerminalSnapshot& outSnapshot)
    {
        return _impl->Snapshot(outSnapshot);
    }

    void TerminalSession::ForceFullRefresh()
    {
        _impl->ForceRefresh();
    }

    int TerminalSession::GetCols() const
    {
        return _impl->snapshot.cols;
    }

int TerminalSession::GetRows() const
{
    return _impl->snapshot.rows;
}

int TerminalSession::GetScrollbackRowCount() const
{
    return _impl->GetScrollbackRows();
}

bool TerminalSession::IsAltScreenActive() const
{
    return _impl->IsAltScreenActive();
}

void TerminalSession::CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell>& out) const
{
    _impl->CopyScrollbackRows(startRow, rowCount, out);
}
} // namespace OpenRCT2::Terminal
