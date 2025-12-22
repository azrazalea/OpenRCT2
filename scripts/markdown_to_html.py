#!/usr/bin/env python3
"""
markdown_to_html.py - Convert Markdown to a self-contained HTML file

Simple converter that produces clean, readable HTML from Markdown.
Uses Python's built-in markdown library if available, otherwise falls back
to a basic conversion.

Usage:
    markdown_to_html.py <input.md> -o <output.html>
"""

import argparse
import html
import re
import sys
from pathlib import Path


def basic_markdown_to_html(md_content: str) -> str:
    """Basic markdown to HTML conversion without external dependencies."""
    lines = md_content.split('\n')
    html_lines = []
    in_code_block = False
    code_lang = ""
    code_lines = []
    in_list = False

    for line in lines:
        # Code blocks
        if line.startswith('```'):
            if in_code_block:
                # End code block
                code_content = html.escape('\n'.join(code_lines))
                html_lines.append(f'<pre><code class="language-{code_lang}">{code_content}</code></pre>')
                code_lines = []
                in_code_block = False
                code_lang = ""
            else:
                # Start code block
                in_code_block = True
                code_lang = line[3:].strip() or "text"
            continue

        if in_code_block:
            code_lines.append(line)
            continue

        # Close list if we hit a non-list line
        if in_list and not line.strip().startswith(('-', '*', '1.', '2.', '3.', '4.', '5.', '6.', '7.', '8.', '9.')):
            if not line.strip():
                continue  # Skip empty lines in list context
            html_lines.append('</ul>')
            in_list = False

        # Headers
        if line.startswith('# '):
            html_lines.append(f'<h1>{html.escape(line[2:])}</h1>')
        elif line.startswith('## '):
            html_lines.append(f'<h2>{html.escape(line[3:])}</h2>')
        elif line.startswith('### '):
            html_lines.append(f'<h3>{html.escape(line[4:])}</h3>')
        elif line.startswith('#### '):
            html_lines.append(f'<h4>{html.escape(line[5:])}</h4>')
        # Horizontal rule
        elif line.strip() in ('---', '***', '___'):
            html_lines.append('<hr>')
        # List items
        elif line.strip().startswith(('- ', '* ')):
            if not in_list:
                html_lines.append('<ul>')
                in_list = True
            item_content = line.strip()[2:]
            item_html = format_inline(item_content)
            html_lines.append(f'<li>{item_html}</li>')
        # Empty line
        elif not line.strip():
            if html_lines and not html_lines[-1].startswith('<'):
                html_lines.append('<br>')
        # Regular paragraph
        else:
            para_html = format_inline(line)
            html_lines.append(f'<p>{para_html}</p>')

    # Close any open list
    if in_list:
        html_lines.append('</ul>')

    return '\n'.join(html_lines)


def format_inline(text: str) -> str:
    """Format inline markdown elements."""
    # Escape HTML first
    text = html.escape(text)

    # Bold: **text** or __text__
    text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
    text = re.sub(r'__(.+?)__', r'<strong>\1</strong>', text)

    # Italic: *text* or _text_
    text = re.sub(r'\*(.+?)\*', r'<em>\1</em>', text)
    text = re.sub(r'_(.+?)_', r'<em>\1</em>', text)

    # Code: `text`
    text = re.sub(r'`([^`]+)`', r'<code>\1</code>', text)

    # Links: [text](url)
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', text)

    return text


def convert_with_markdown_lib(md_content: str) -> str:
    """Convert markdown using the markdown library if available."""
    try:
        import markdown
        return markdown.markdown(
            md_content,
            extensions=['fenced_code', 'tables', 'toc']
        )
    except ImportError:
        return basic_markdown_to_html(md_content)


HTML_TEMPLATE = '''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{title}</title>
    <style>
        :root {{
            --bg-color: #1a1a2e;
            --text-color: #e8e8e8;
            --code-bg: #16213e;
            --border-color: #0f3460;
            --accent-color: #e94560;
            --user-color: #00d9ff;
            --assistant-color: #7cfc00;
        }}

        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            line-height: 1.6;
            max-width: 900px;
            margin: 0 auto;
            padding: 20px;
        }}

        h1 {{
            color: var(--accent-color);
            border-bottom: 2px solid var(--border-color);
            padding-bottom: 10px;
        }}

        h2 {{
            color: var(--user-color);
            margin-top: 30px;
            border-left: 4px solid var(--user-color);
            padding-left: 10px;
        }}

        h2:has(+ p):first-of-type,
        h2:contains("User") {{
            color: var(--user-color);
            border-left-color: var(--user-color);
        }}

        h3 {{
            color: var(--assistant-color);
        }}

        pre {{
            background-color: var(--code-bg);
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 15px;
            overflow-x: auto;
            font-size: 13px;
        }}

        code {{
            font-family: 'SF Mono', Monaco, 'Courier New', monospace;
            background-color: var(--code-bg);
            padding: 2px 6px;
            border-radius: 3px;
            font-size: 0.9em;
        }}

        pre code {{
            padding: 0;
            background: none;
        }}

        p {{
            margin: 10px 0;
        }}

        strong {{
            color: var(--accent-color);
        }}

        em {{
            color: #888;
        }}

        hr {{
            border: none;
            border-top: 1px solid var(--border-color);
            margin: 30px 0;
        }}

        ul {{
            padding-left: 25px;
        }}

        li {{
            margin: 5px 0;
        }}

        .metadata {{
            background-color: var(--code-bg);
            padding: 10px 15px;
            border-radius: 6px;
            font-size: 0.85em;
            margin-bottom: 20px;
        }}

        a {{
            color: var(--accent-color);
            text-decoration: none;
        }}

        a:hover {{
            text-decoration: underline;
        }}
    </style>
</head>
<body>
{content}
<footer style="margin-top: 40px; padding-top: 20px; border-top: 1px solid var(--border-color); font-size: 0.85em; color: #666;">
    Generated by OpenRCT2 AI Agent Session Logger
</footer>
</body>
</html>
'''


def main():
    parser = argparse.ArgumentParser(description='Convert Markdown to HTML')
    parser.add_argument('input', help='Input markdown file')
    parser.add_argument('-o', '--output', required=True, help='Output HTML file')

    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.exists():
        print(f"Error: Input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    # Read markdown content
    md_content = input_path.read_text(encoding='utf-8')

    # Convert to HTML
    html_content = convert_with_markdown_lib(md_content)

    # Extract title from first h1
    title = input_path.stem
    title_match = re.search(r'^#\s+(.+)$', md_content, re.MULTILINE)
    if title_match:
        title = title_match.group(1)

    # Generate full HTML document
    full_html = HTML_TEMPLATE.format(title=html.escape(title), content=html_content)

    # Write output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(full_html, encoding='utf-8')

    print(f"Converted {input_path} -> {output_path}")


if __name__ == '__main__':
    main()
