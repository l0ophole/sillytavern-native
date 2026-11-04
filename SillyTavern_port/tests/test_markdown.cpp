#include "test_util.h"

#include "core/markdown.h"

#include <string>

TEST(markdown_escapes_html) {
    CHECK_EQ(st::markdown_to_pango("<b>&"), "&lt;b&gt;&amp;");
    CHECK_EQ(st::markdown_to_pango(""), "");
    // Control characters are invalid in Pango markup; they are dropped
    // rather than passed through and breaking the whole markup string.
    CHECK_EQ(st::markdown_to_pango("a\x01" "b"), "ab");
}

TEST(markdown_renders_bold_and_inline_code) {
    CHECK_EQ(st::markdown_to_pango("**hi** `x<y`"), "<b>hi</b> <tt>x&lt;y</tt>");
}

TEST(markdown_unclosed_marker_is_literal) {
    CHECK_EQ(st::markdown_to_pango("**hi"), "**hi");
}

TEST(markdown_fenced_block_is_monospace_and_preserves_newlines) {
    // "```\na<b\nc\n```" renders one <tt> span containing "a&lt;b\nc"
    CHECK_EQ(st::markdown_to_pango("```\na<b\nc\n```"), "<tt>a&lt;b\nc</tt>");
}

TEST(markdown_link_escapes_href_and_label) {
    CHECK_EQ(st::markdown_to_pango("[a&b](https://x/?a=1&b=2)"),
             "<a href=\"https://x/?a=1&amp;b=2\">a&amp;b</a>");
}

TEST(markdown_pathological_nesting_stays_bounded) {
    // 10k '*' characters must return quickly and produce balanced markup
    const std::string stars(10000, '*');
    CHECK_EQ(st::markdown_to_pango(stars), stars);
}

TEST(markdown_renders_italic_and_strike) {
    CHECK_EQ(st::markdown_to_pango("*a* _b_ ~~c~~"), "<i>a</i> <i>b</i> <s>c</s>");
}

TEST(markdown_renders_headings_quotes_and_lists) {
    CHECK_EQ(st::markdown_to_pango("# Title"), "<b>Title</b>");
    CHECK_EQ(st::markdown_to_pango("## Sub"), "<b>Sub</b>");
    // More than six leading '#' is not a heading; it renders literally.
    CHECK_EQ(st::markdown_to_pango("####### too deep"), "####### too deep");
    CHECK_EQ(st::markdown_to_pango("> quoted"), "\xe2\x96\x8e quoted");
    CHECK_EQ(st::markdown_to_pango("- item"), "\xe2\x80\xa2 item");
    CHECK_EQ(st::markdown_to_pango("* item"), "\xe2\x80\xa2 item");
}

TEST(markdown_unclosed_fence_still_balanced) {
    CHECK_EQ(st::markdown_to_pango("```\ncode"), "<tt>code</tt>");
}

TEST(markdown_link_escapes_quotes_in_href) {
    CHECK_EQ(st::markdown_to_pango("[x](https://e/\"q\")"),
             "<a href=\"https://e/&quot;q&quot;\">x</a>");
}
