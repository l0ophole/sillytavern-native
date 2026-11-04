#pragma once

#include <string>

namespace st {

// Converts a small, well-defined Markdown subset into balanced Pango markup.
//
// Supported subset:
//   - `**bold**`, `*italic*`, `_italic_`, `~~strike~~`, `` `inline code` ``
//   - fenced ``` blocks (monospace, newlines preserved)
//   - `#`..`######` headings (bold line)
//   - `>` blockquotes (prefixed with a left bar glyph)
//   - `- ` / `* ` list items (prefixed with a bullet glyph)
//   - `[label](url)` links
//
// Guarantees:
//   - Input `<`, `>`, `&` never survive unescaped, so user text can never
//     inject markup or break Pango parsing.
//   - An unclosed marker renders literally.
//   - The output contains only `b`, `i`, `s`, `tt`, `span` and `a` tags,
//     always balanced. Inline nesting is capped (see markdown.cpp).
std::string markdown_to_pango(const std::string& text);

}  // namespace st
