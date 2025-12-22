#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace rctctl::renderers {

struct TableView
{
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};

class TextCanvas
{
public:
    explicit TextCanvas(std::ostream& out, size_t indent = 0, size_t labelWidth = 12);

    void Section(std::string_view title);
    void Paragraph(std::string_view text);
    void KeyValue(std::string_view label, std::string_view value);
    void KeyValue(std::string_view label, const std::string& value);
    void KeyValue(std::string_view label, const char* value);
    void KeyValue(std::string_view label, double value);
    void KeyValue(std::string_view label, int value);
    void KeyValue(std::string_view label, bool value);
    void Bullet(std::string_view text);
    void BlankLine();
    void Table(const TableView& table);

private:
    std::ostream& m_out;
    size_t m_indent;
    size_t m_labelWidth;
    bool m_sectionOpen = false;

    void WriteIndent();
};

} // namespace rctctl::renderers

