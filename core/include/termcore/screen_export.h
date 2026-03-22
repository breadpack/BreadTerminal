#ifndef TERMCORE_SCREEN_EXPORT_H
#define TERMCORE_SCREEN_EXPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

// Forward declaration
class Screen;

/// Export format
enum class ExportFormat {
    PlainText,   // No colors, just text
    AnsiText,    // Text with ANSI escape sequences preserved
    HTML,        // Styled HTML with inline CSS
    SVG,         // SVG with positioned text elements
};

/// Export options
struct ExportOptions {
    ExportFormat format = ExportFormat::HTML;
    bool includeScrollback = false;   // include scrollback buffer
    int scrollbackLines = 0;          // 0 = all scrollback if includeScrollback
    bool includeLineNumbers = false;
    std::string fontFamily = "monospace";
    int fontSize = 14;
    uint32_t bgColor = 0x1e1e2e;     // default background
    std::string title = "BreadTerminal Export";
};

/// Export visible screen content
std::string exportScreen(const Screen& screen, const ExportOptions& opts);

/// Export a range of rows
std::string exportRows(const Screen& screen, int startRow, int endRow,
                       const ExportOptions& opts);

/// Helper: convert a single cell's attributes to CSS color string
std::string colorToCSS(uint32_t color);

/// Helper: convert a single cell's attributes to ANSI SGR sequence
std::string attrsToSGR(bool bold, bool italic, bool underline, bool strikethrough,
                       uint32_t fg, uint32_t bg);

/// Helper: generate HTML header with styles
std::string htmlHeader(const ExportOptions& opts);

/// Helper: generate HTML footer
std::string htmlFooter();

/// Helper: generate SVG header
std::string svgHeader(int width, int height, const ExportOptions& opts);

/// Helper: generate SVG footer
std::string svgFooter();

/// Escape text for HTML output
std::string htmlEscape(const std::string& text);

} // namespace termcore

#endif // TERMCORE_SCREEN_EXPORT_H
