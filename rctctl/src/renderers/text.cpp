#include "rctctl/renderers/text.hpp"

#include "rctctl/renderers/context.hpp"
#include "rctctl/util/string_utils.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

namespace rctctl::renderers {

namespace {
constexpr std::string_view kTrueValue = "yes";
constexpr std::string_view kFalseValue = "no";

void PrintTableDivider(std::ostream& out, size_t indent, const std::vector<size_t>& widths)
{
    out << std::string(indent, ' ');
    for (size_t i = 0; i < widths.size(); ++i)
    {
        out << std::string(widths[i], '-') ;
        if (i + 1 != widths.size())
        {
            out << "  ";
        }
    }
    out << '\n';
}

std::vector<size_t> BuildColumnOrder(const TableView& table)
{
    const auto& ctx = CurrentRenderContext();
    if (ctx.columns && !ctx.columns->empty())
    {
        std::vector<size_t> order;
        order.reserve(ctx.columns->size());
        for (const auto& requested : *ctx.columns)
        {
            auto needle = util::ToLower(requested);
            for (size_t i = 0; i < table.headers.size(); ++i)
            {
                if (util::ToLower(table.headers[i]) == needle)
                {
                    order.push_back(i);
                    break;
                }
            }
        }
        if (!order.empty())
        {
            return order;
        }
    }

    std::vector<size_t> defaultOrder(table.headers.size());
    std::iota(defaultOrder.begin(), defaultOrder.end(), 0);
    return defaultOrder;
}

bool RowMatchesFilter(const std::vector<std::string>& row, const std::string& filter)
{
    for (const auto& cell : row)
    {
        if (util::ToLower(cell).find(filter) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

std::optional<std::string> BuildFilterNeedle(std::string& filterLabel)
{
    const auto& ctx = CurrentRenderContext();
    if (ctx.filter && ctx.filter->has_value())
    {
        filterLabel = ctx.filter->value();
        auto lowered = util::ToLower(filterLabel);
        if (!lowered.empty())
        {
            return lowered;
        }
    }
    return std::nullopt;
}

} // namespace

TextCanvas::TextCanvas(std::ostream& out, size_t indent, size_t labelWidth)
    : m_out(out)
    , m_indent(indent)
    , m_labelWidth(labelWidth)
{
}

void TextCanvas::Section(std::string_view title)
{
    if (m_sectionOpen)
    {
        m_out << '\n';
    }
    WriteIndent();
    m_out << title << '\n';
    WriteIndent();
    for (size_t i = 0; i < title.size(); ++i)
    {
        m_out << '-';
    }
    m_out << '\n';
    m_sectionOpen = true;
}

void TextCanvas::Paragraph(std::string_view text)
{
    WriteIndent();
    m_out << text << '\n';
}

void TextCanvas::KeyValue(std::string_view label, std::string_view value)
{
    WriteIndent();
    auto previous = m_out.flags();
    m_out << std::left << std::setw(static_cast<int>(m_labelWidth)) << label << " : " << value << '\n';
    m_out.flags(previous);
}

void TextCanvas::KeyValue(std::string_view label, const std::string& value)
{
    KeyValue(label, std::string_view(value));
}

void TextCanvas::KeyValue(std::string_view label, const char* value)
{
    KeyValue(label, std::string_view(value));
}

void TextCanvas::KeyValue(std::string_view label, double value)
{
    std::ostringstream oss;
    oss << value;
    KeyValue(label, oss.str());
}

void TextCanvas::KeyValue(std::string_view label, int value)
{
    KeyValue(label, std::to_string(value));
}

void TextCanvas::KeyValue(std::string_view label, bool value)
{
    KeyValue(label, value ? kTrueValue : kFalseValue);
}

void TextCanvas::Bullet(std::string_view text)
{
    WriteIndent();
    m_out << " - " << text << '\n';
}

void TextCanvas::BlankLine()
{
    m_out << '\n';
}

void TextCanvas::Table(const TableView& table)
{
    if (table.headers.empty())
    {
        return;
    }

    auto columnOrder = BuildColumnOrder(table);
    std::vector<std::string> headers;
    headers.reserve(columnOrder.size());
    for (size_t index : columnOrder)
    {
        if (index < table.headers.size())
        {
            headers.push_back(table.headers[index]);
        }
        else
        {
            headers.emplace_back();
        }
    }

    std::string filterLabel;
    auto filterNeedle = BuildFilterNeedle(filterLabel);
    std::vector<std::vector<std::string>> rows;
    rows.reserve(table.rows.size());
    for (const auto& row : table.rows)
    {
        std::vector<std::string> displayRow;
        displayRow.reserve(columnOrder.size());
        for (size_t index : columnOrder)
        {
            displayRow.push_back(index < row.size() ? row[index] : std::string());
        }
        if (filterNeedle && !RowMatchesFilter(displayRow, *filterNeedle))
        {
            continue;
        }
        rows.emplace_back(std::move(displayRow));
    }

    if (rows.empty())
    {
        WriteIndent();
        if (filterNeedle)
        {
            m_out << "(no rows match filter \"" << filterLabel << "\")\n";
        }
        else
        {
            m_out << "(no entries)\n";
        }
        return;
    }

    const size_t columnCount = headers.size();
    std::vector<size_t> widths(columnCount, 0);
    for (size_t i = 0; i < columnCount; ++i)
    {
        widths[i] = headers[i].size();
    }
    for (const auto& row : rows)
    {
        for (size_t i = 0; i < columnCount; ++i)
        {
            if (i < row.size())
            {
                widths[i] = std::max(widths[i], row[i].size());
            }
        }
    }

    WriteIndent();
    auto previous = m_out.flags();
    for (size_t i = 0; i < columnCount; ++i)
    {
        m_out << std::left << std::setw(static_cast<int>(widths[i])) << headers[i];
        if (i + 1 != columnCount)
        {
            m_out << "  ";
        }
    }
    m_out << '\n';
    m_out.flags(previous);

    PrintTableDivider(m_out, m_indent, widths);

    for (const auto& row : rows)
    {
        WriteIndent();
        auto rowFlags = m_out.flags();
        for (size_t i = 0; i < columnCount; ++i)
        {
            const std::string cell = i < row.size() ? row[i] : std::string();
            m_out << std::left << std::setw(static_cast<int>(widths[i])) << cell;
            if (i + 1 != columnCount)
            {
                m_out << "  ";
            }
        }
        m_out << '\n';
        m_out.flags(rowFlags);
    }
}

void TextCanvas::WriteIndent()
{
    if (m_indent > 0)
    {
        m_out << std::string(m_indent, ' ');
    }
}

} // namespace rctctl::renderers
