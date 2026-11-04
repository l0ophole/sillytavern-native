#include "core/markdown.h"

#include <cstddef>
#include <string>
#include <vector>

namespace st {
namespace {

// Maximum inline nesting depth; deeper markers render literally.
constexpr int kMaxInlineDepth = 8;

constexpr char kQuoteGlyph[] = "\xe2\x96\x8e ";   // U+258E LEFT ONE EIGHTH BLOCK + space
constexpr char kBulletGlyph[] = "\xe2\x80\xa2 ";  // U+2022 BULLET + space

// Appends s[begin, end) to out, escaping characters that are unsafe in
// Pango markup text (or, when attribute is set, in an attribute value).
// Control characters other than tab/newline/carriage-return are dropped:
// they are not valid in Pango markup and would make the whole markup
// unparseable, not just the offending character.
void append_escaped(std::string& out, const std::string& s, size_t begin, size_t end, bool attribute) {
    for (size_t i = begin; i < end; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
        switch (s[i]) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                if (attribute) {
                    out += "&quot;";
                } else {
                    out += '"';
                }
                break;
            default:
                out += s[i];
                break;
        }
    }
}

// Finds needle in s[from, end), or npos. The search never crosses end.
size_t find_marker(const std::string& s, size_t from, size_t end, const char* needle, size_t needle_len) {
    if (needle_len == 0 || from > end || needle_len > end - from) return std::string::npos;
    for (size_t i = from; i + needle_len <= end; ++i) {
        size_t j = 0;
        while (j < needle_len && s[i + j] == needle[j]) ++j;
        if (j == needle_len) return i;
    }
    return std::string::npos;
}

// A link target must be non-empty and contain no whitespace or control
// characters; anything else makes the '[' render literally instead.
bool valid_url(const std::string& s, size_t begin, size_t end) {
    if (begin >= end) return false;
    for (size_t i = begin; i < end; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c <= 0x20 || c == 0x7f) return false;
    }
    return true;
}

// Renders s[begin, end) as Pango markup, interpreting inline markers.
// Runs a single left-to-right pass; unmatched or empty markers are emitted
// literally. Recursion is capped by kMaxInlineDepth, so pathological input
// stays bounded in both time and stack use.
void render_inline(const std::string& s, size_t begin, size_t end, int depth, std::string& out) {
    if (depth > kMaxInlineDepth) {
        append_escaped(out, s, begin, end, false);
        return;
    }
    size_t i = begin;
    while (i < end) {
        const char c = s[i];
        bool handled = false;
        if (c == '`') {
            const size_t close = find_marker(s, i + 1, end, "`", 1);
            if (close != std::string::npos && close > i + 1) {
                out += "<tt>";
                append_escaped(out, s, i + 1, close, false);
                out += "</tt>";
                i = close + 1;
                handled = true;
            }
        } else if (c == '[') {
            const size_t label_end = find_marker(s, i + 1, end, "]", 1);
            if (label_end != std::string::npos && label_end > i + 1 && label_end + 1 < end &&
                s[label_end + 1] == '(') {
                const size_t url_end = find_marker(s, label_end + 2, end, ")", 1);
                if (url_end != std::string::npos && url_end > label_end + 2 &&
                    valid_url(s, label_end + 2, url_end)) {
                    out += "<a href=\"";
                    append_escaped(out, s, label_end + 2, url_end, true);
                    out += "\">";
                    render_inline(s, i + 1, label_end, depth + 1, out);
                    out += "</a>";
                    i = url_end + 1;
                    handled = true;
                }
            }
        } else if (c == '*' && i + 1 < end && s[i + 1] == '*') {
            // '**' opens bold. If it does not close, the first '*' is kept
            // literal and scanning resumes one character later, so "**hi*"
            // becomes "*<i>hi</i>" rather than "<i>*hi</i>".
            const size_t close = find_marker(s, i + 2, end, "**", 2);
            if (close != std::string::npos && close > i + 2) {
                out += "<b>";
                render_inline(s, i + 2, close, depth + 1, out);
                out += "</b>";
                i = close + 2;
                handled = true;
            }
        } else if (c == '~' && i + 1 < end && s[i + 1] == '~') {
            const size_t close = find_marker(s, i + 2, end, "~~", 2);
            if (close != std::string::npos && close > i + 2) {
                out += "<s>";
                render_inline(s, i + 2, close, depth + 1, out);
                out += "</s>";
                i = close + 2;
                handled = true;
            }
        } else if (c == '*' || c == '_') {
            const char needle[1] = {c};
            const size_t close = find_marker(s, i + 1, end, needle, 1);
            if (close != std::string::npos && close > i + 1) {
                out += "<i>";
                render_inline(s, i + 1, close, depth + 1, out);
                out += "</i>";
                i = close + 1;
                handled = true;
            }
        }
        if (!handled) {
            append_escaped(out, s, i, i + 1, false);
            ++i;
        }
    }
}

bool starts_with_fence(const std::string& line) {
    return line.size() >= 3 && line.compare(0, 3, "```") == 0;
}

// Renders one non-fence line: headings, blockquotes and list items are
// recognised at line start; everything else goes through the inline pass.
std::string render_block_line(const std::string& line) {
    size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') ++hashes;
    if (hashes >= 1 && hashes <= 6 && (hashes == line.size() || line[hashes] == ' ')) {
        std::string rest = line.substr(hashes);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        std::string out = "<b>";
        render_inline(rest, 0, rest.size(), 0, out);
        out += "</b>";
        return out;
    }
    if (!line.empty() && line[0] == '>') {
        std::string rest = line.substr(1);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        std::string out = kQuoteGlyph;
        render_inline(rest, 0, rest.size(), 0, out);
        return out;
    }
    if ((line.size() >= 2 && line[0] == '-' && line[1] == ' ') ||
        (line.size() >= 2 && line[0] == '*' && line[1] == ' ')) {
        std::string out = kBulletGlyph;
        render_inline(line, 2, line.size(), 0, out);
        return out;
    }
    std::string out;
    render_inline(line, 0, line.size(), 0, out);
    return out;
}

// Joins raw fence lines into one <tt> element, escaping each line but
// performing no inline interpretation. Newlines are preserved.
std::string render_fence(const std::vector<std::string>& fence_lines) {
    std::string out = "<tt>";
    for (size_t k = 0; k < fence_lines.size(); ++k) {
        if (k > 0) out += '\n';
        append_escaped(out, fence_lines[k], 0, fence_lines[k].size(), false);
    }
    out += "</tt>";
    return out;
}

}  // namespace

std::string markdown_to_pango(const std::string& text) {
    // Split into lines on '\n'; a trailing '\r' (CRLF input) is dropped.
    std::vector<std::string> lines;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t nl = text.find('\n', begin);
        std::string line =
            text.substr(begin, nl == std::string::npos ? std::string::npos : nl - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (nl == std::string::npos) break;
        begin = nl + 1;
    }

    std::string result;
    bool first = true;
    auto append_line = [&](const std::string& rendered) {
        if (!first) result += '\n';
        result += rendered;
        first = false;
    };

    bool in_fence = false;
    std::vector<std::string> fence_lines;
    for (const std::string& line : lines) {
        if (!in_fence && starts_with_fence(line)) {
            // A second ``` on the same line (with nothing but whitespace
            // after it) makes it a one-line fence; trailing text instead
            // keeps the line as ordinary text so nothing is dropped.
            const size_t close = line.find("```", 3);
            const std::string tail =
                close == std::string::npos ? "x" : line.substr(close + 3);
            if (close != std::string::npos && tail.find_first_not_of(" \t") == std::string::npos) {
                std::string out = "<tt>";
                append_escaped(out, line, 3, close, false);
                out += "</tt>";
                append_line(out);
            } else if (close == std::string::npos) {
                in_fence = true;
                fence_lines.clear();
            } else {
                append_line(render_block_line(line));
            }
            continue;
        }
        if (in_fence) {
            if (starts_with_fence(line)) {
                append_line(render_fence(fence_lines));
                in_fence = false;
            } else {
                fence_lines.push_back(line);
            }
            continue;
        }
        append_line(render_block_line(line));
    }
    // An unclosed fence is closed at end of input, keeping markup balanced.
    if (in_fence) append_line(render_fence(fence_lines));

    return result;
}

}  // namespace st
