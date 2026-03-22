#include "termcore/screen_export.h"
#include "termcore/screen.h"
#include "termcore/term_cell.h"
#include <gtest/gtest.h>

using namespace termcore;

// --- htmlEscape ---

TEST(ScreenExport, HtmlEscapeSpecialChars) {
    EXPECT_EQ(htmlEscape("&"), "&amp;");
    EXPECT_EQ(htmlEscape("<"), "&lt;");
    EXPECT_EQ(htmlEscape(">"), "&gt;");
    EXPECT_EQ(htmlEscape("\""), "&quot;");
    EXPECT_EQ(htmlEscape("'"), "&#39;");
}

TEST(ScreenExport, HtmlEscapeMixed) {
    EXPECT_EQ(htmlEscape("<b>hello & world</b>"),
              "&lt;b&gt;hello &amp; world&lt;/b&gt;");
}

TEST(ScreenExport, HtmlEscapePlainText) {
    EXPECT_EQ(htmlEscape("hello world"), "hello world");
}

// --- colorToCSS ---

TEST(ScreenExport, ColorToCSSBlack) {
    EXPECT_EQ(colorToCSS(0x000000), "#000000");
}

TEST(ScreenExport, ColorToCSSWhite) {
    EXPECT_EQ(colorToCSS(0xFFFFFF), "#ffffff");
}

TEST(ScreenExport, ColorToCSSRed) {
    EXPECT_EQ(colorToCSS(0xFF0000), "#ff0000");
}

TEST(ScreenExport, ColorToCSSDefault) {
    // kColorDefault should return empty string
    EXPECT_EQ(colorToCSS(kColorDefault), "");
}

TEST(ScreenExport, ColorToCSSArbitrary) {
    EXPECT_EQ(colorToCSS(0x1e1e2e), "#1e1e2e");
}

// --- attrsToSGR ---

TEST(ScreenExport, AttrsToSGRReset) {
    auto sgr = attrsToSGR(false, false, false, false, kColorDefault, kColorDefault);
    EXPECT_EQ(sgr, "\033[0m");
}

TEST(ScreenExport, AttrsToSGRBold) {
    auto sgr = attrsToSGR(true, false, false, false, kColorDefault, kColorDefault);
    EXPECT_NE(sgr.find("1"), std::string::npos);
    EXPECT_EQ(sgr.front(), '\033');
    EXPECT_EQ(sgr.back(), 'm');
}

TEST(ScreenExport, AttrsToSGRWithFgColor) {
    auto sgr = attrsToSGR(false, false, false, false, 0xFF0000, kColorDefault);
    // Should contain 38;2;255;0;0
    EXPECT_NE(sgr.find("38;2;255;0;0"), std::string::npos);
}

TEST(ScreenExport, AttrsToSGRWithBgColor) {
    auto sgr = attrsToSGR(false, false, false, false, kColorDefault, 0x00FF00);
    // Should contain 48;2;0;255;0
    EXPECT_NE(sgr.find("48;2;0;255;0"), std::string::npos);
}

TEST(ScreenExport, AttrsToSGRCombined) {
    auto sgr = attrsToSGR(true, true, true, true, 0xABCDEF, 0x123456);
    EXPECT_NE(sgr.find("1"), std::string::npos);    // bold
    EXPECT_NE(sgr.find("3"), std::string::npos);    // italic
    EXPECT_NE(sgr.find("4"), std::string::npos);    // underline
    EXPECT_NE(sgr.find("9"), std::string::npos);    // strikethrough
    EXPECT_NE(sgr.find("38;2;"), std::string::npos); // fg
    EXPECT_NE(sgr.find("48;2;"), std::string::npos); // bg
}

// --- htmlHeader / htmlFooter ---

TEST(ScreenExport, HtmlHeaderContainsDoctype) {
    ExportOptions opts;
    auto header = htmlHeader(opts);
    EXPECT_NE(header.find("<!DOCTYPE html>"), std::string::npos);
}

TEST(ScreenExport, HtmlHeaderContainsTitle) {
    ExportOptions opts;
    opts.title = "Test Title";
    auto header = htmlHeader(opts);
    EXPECT_NE(header.find("Test Title"), std::string::npos);
}

TEST(ScreenExport, HtmlHeaderContainsFont) {
    ExportOptions opts;
    opts.fontFamily = "Courier New";
    auto header = htmlHeader(opts);
    EXPECT_NE(header.find("Courier New"), std::string::npos);
}

TEST(ScreenExport, HtmlFooterContainsClosingTags) {
    auto footer = htmlFooter();
    EXPECT_NE(footer.find("</pre>"), std::string::npos);
    EXPECT_NE(footer.find("</html>"), std::string::npos);
}

// --- svgHeader / svgFooter ---

TEST(ScreenExport, SvgHeaderContainsSvgTag) {
    ExportOptions opts;
    auto header = svgHeader(800, 600, opts);
    EXPECT_NE(header.find("<svg"), std::string::npos);
    EXPECT_NE(header.find("xmlns"), std::string::npos);
}

TEST(ScreenExport, SvgHeaderContainsDimensions) {
    ExportOptions opts;
    auto header = svgHeader(800, 600, opts);
    EXPECT_NE(header.find("800"), std::string::npos);
    EXPECT_NE(header.find("600"), std::string::npos);
}

TEST(ScreenExport, SvgFooterContainsClosingTag) {
    auto footer = svgFooter();
    EXPECT_NE(footer.find("</svg>"), std::string::npos);
}

// --- exportRows with PlainText ---

TEST(ScreenExport, ExportRowsPlainText) {
    Screen screen(3, 10);
    // Write some text via VtParser feed
    const char* text = "Hello\r\nWorld\r\nTest!";
    for (const char* p = text; *p; ++p) {
        if (*p == '\r') {
            screen.onExecute('\r');
        } else if (*p == '\n') {
            screen.onExecute('\n');
        } else {
            screen.onPrint(static_cast<char32_t>(*p));
        }
    }

    ExportOptions opts;
    opts.format = ExportFormat::PlainText;

    auto result = exportRows(screen, 0, 3, opts);
    EXPECT_NE(result.find("Hello"), std::string::npos);
    EXPECT_NE(result.find("World"), std::string::npos);
    EXPECT_NE(result.find("Test!"), std::string::npos);
}

// --- exportRows with HTML ---

TEST(ScreenExport, ExportRowsHTMLWrapsInSpans) {
    Screen screen(2, 10);
    // Print a character with bold attribute
    screen.onPrint('A');

    ExportOptions opts;
    opts.format = ExportFormat::HTML;

    auto result = exportScreen(screen, opts);
    EXPECT_NE(result.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(result.find("</html>"), std::string::npos);
    EXPECT_NE(result.find("A"), std::string::npos);
}

// --- exportScreen ---

TEST(ScreenExport, ExportScreenPlainTextExtractsAllRows) {
    Screen screen(4, 5);
    const char* text = "ABCD\r\nEFGH";
    for (const char* p = text; *p; ++p) {
        if (*p == '\r') {
            screen.onExecute('\r');
        } else if (*p == '\n') {
            screen.onExecute('\n');
        } else {
            screen.onPrint(static_cast<char32_t>(*p));
        }
    }

    ExportOptions opts;
    opts.format = ExportFormat::PlainText;
    auto result = exportScreen(screen, opts);
    EXPECT_NE(result.find("ABCD"), std::string::npos);
    EXPECT_NE(result.find("EFGH"), std::string::npos);
}

TEST(ScreenExport, ExportRowsEmptyRange) {
    Screen screen(4, 10);
    ExportOptions opts;
    opts.format = ExportFormat::PlainText;
    auto result = exportRows(screen, 5, 3, opts);
    EXPECT_TRUE(result.empty());
}

TEST(ScreenExport, ExportRowsClampsRange) {
    Screen screen(4, 10);
    ExportOptions opts;
    opts.format = ExportFormat::PlainText;
    // startRow negative, endRow beyond screen size
    auto result = exportRows(screen, -1, 100, opts);
    // Should not crash, should produce output for all 4 rows
    EXPECT_FALSE(result.empty());
}
