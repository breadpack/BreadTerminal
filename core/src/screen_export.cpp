#include "termcore/screen_export.h"
#include "termcore/screen.h"
#include "termcore/term_cell.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace termcore {

// --- Helpers ---

static std::string cellToUtf8(const TermCell& cell) {
    std::string result;
    auto encode = [&](char32_t cp) {
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    };

    encode(cell.codepoint);
    for (uint8_t i = 0; i < cell.extra_count; ++i) {
        encode(cell.extra[i]);
    }
    return result;
}

std::string colorToCSS(uint32_t color) {
    if (color == kColorDefault) return "";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  (color >> 16) & 0xFF,
                  (color >> 8) & 0xFF,
                  color & 0xFF);
    return buf;
}

std::string attrsToSGR(bool bold, bool italic, bool underline, bool strikethrough,
                       uint32_t fg, uint32_t bg) {
    std::string seq = "\033[";
    bool first = true;
    auto append = [&](const std::string& code) {
        if (!first) seq += ';';
        seq += code;
        first = false;
    };

    append("0"); // reset first

    if (bold) append("1");
    if (italic) append("3");
    if (underline) append("4");
    if (strikethrough) append("9");

    if (fg != kColorDefault) {
        int r = (fg >> 16) & 0xFF;
        int g = (fg >> 8) & 0xFF;
        int b = fg & 0xFF;
        append("38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b));
    }
    if (bg != kColorDefault) {
        int r = (bg >> 16) & 0xFF;
        int g = (bg >> 8) & 0xFF;
        int b = bg & 0xFF;
        append("48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b));
    }

    seq += 'm';
    return seq;
}

std::string htmlEscape(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&':  result += "&amp;"; break;
        case '<':  result += "&lt;"; break;
        case '>':  result += "&gt;"; break;
        case '"':  result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default:   result += c; break;
        }
    }
    return result;
}

std::string htmlHeader(const ExportOptions& opts) {
    std::ostringstream ss;
    ss << "<!DOCTYPE html>\n"
       << "<html>\n<head>\n"
       << "<meta charset=\"utf-8\">\n"
       << "<title>" << htmlEscape(opts.title) << "</title>\n"
       << "<style>\n"
       << "body { margin: 0; padding: 16px; background: " << colorToCSS(opts.bgColor) << "; }\n"
       << "pre { font-family: " << htmlEscape(opts.fontFamily) << "; "
       << "font-size: " << opts.fontSize << "px; "
       << "line-height: 1.2; color: #cdd6f4; margin: 0; }\n"
       << ".line-number { color: #585b70; user-select: none; padding-right: 1em; }\n"
       << "</style>\n"
       << "</head>\n<body>\n<pre>\n";
    return ss.str();
}

std::string htmlFooter() {
    return "</pre>\n</body>\n</html>\n";
}

std::string svgHeader(int width, int height, const ExportOptions& opts) {
    std::ostringstream ss;
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
       << "width=\"" << width << "\" height=\"" << height << "\" "
       << "viewBox=\"0 0 " << width << " " << height << "\">\n"
       << "<rect width=\"100%\" height=\"100%\" fill=\"" << colorToCSS(opts.bgColor) << "\"/>\n"
       << "<style>\n"
       << "text { font-family: " << htmlEscape(opts.fontFamily) << "; "
       << "font-size: " << opts.fontSize << "px; fill: #cdd6f4; }\n"
       << "</style>\n";
    return ss.str();
}

std::string svgFooter() {
    return "</svg>\n";
}

// --- Export functions ---

static std::string exportRowsPlainText(const Screen& screen, int startRow, int endRow,
                                       const ExportOptions& opts) {
    std::string result;
    int lineNum = 1;
    for (int r = startRow; r < endRow; ++r) {
        if (r > startRow) result += '\n';
        if (opts.includeLineNumbers) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%4d ", lineNum++);
            result += buf;
        }
        result += screen.getLineText(r);
    }
    return result;
}

static std::string exportRowsAnsi(const Screen& screen, int startRow, int endRow,
                                  const ExportOptions& opts) {
    std::string result;
    int lineNum = 1;
    for (int r = startRow; r < endRow; ++r) {
        if (r > startRow) result += '\n';
        if (opts.includeLineNumbers) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%4d ", lineNum++);
            result += buf;
        }
        for (int c = 0; c < screen.cols(); ++c) {
            const auto& cell = screen.cellAt(r, c);
            if (cell.width == 0) continue; // skip continuation cells

            bool bold = (cell.attributes & AttrBold) != 0;
            bool italic = (cell.attributes & AttrItalic) != 0;
            bool underline = (cell.attributes & AttrUnderline) != 0;
            bool strike = (cell.attributes & AttrStrikethrough) != 0;

            bool hasAttrs = bold || italic || underline || strike
                            || cell.fg_color != kColorDefault
                            || cell.bg_color != kColorDefault;
            if (hasAttrs) {
                result += attrsToSGR(bold, italic, underline, strike,
                                     cell.fg_color, cell.bg_color);
            }
            result += cellToUtf8(cell);
            if (hasAttrs) {
                result += "\033[0m";
            }
        }
    }
    return result;
}

static std::string exportRowsHTML(const Screen& screen, int startRow, int endRow,
                                  const ExportOptions& opts) {
    std::string result = htmlHeader(opts);
    int lineNum = 1;

    for (int r = startRow; r < endRow; ++r) {
        if (opts.includeLineNumbers) {
            result += "<span class=\"line-number\">";
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%4d", lineNum++);
            result += buf;
            result += "</span>";
        }

        for (int c = 0; c < screen.cols(); ++c) {
            const auto& cell = screen.cellAt(r, c);
            if (cell.width == 0) continue; // skip continuation cells

            bool bold = (cell.attributes & AttrBold) != 0;
            bool italic = (cell.attributes & AttrItalic) != 0;
            bool underline = (cell.attributes & AttrUnderline) != 0;
            bool strike = (cell.attributes & AttrStrikethrough) != 0;
            bool hasStyle = bold || italic || underline || strike
                            || cell.fg_color != kColorDefault
                            || cell.bg_color != kColorDefault;

            if (hasStyle) {
                result += "<span style=\"";
                if (bold) result += "font-weight:bold;";
                if (italic) result += "font-style:italic;";
                if (underline) result += "text-decoration:underline;";
                if (strike) result += "text-decoration:line-through;";
                if (cell.fg_color != kColorDefault) {
                    result += "color:" + colorToCSS(cell.fg_color) + ";";
                }
                if (cell.bg_color != kColorDefault) {
                    result += "background:" + colorToCSS(cell.bg_color) + ";";
                }
                result += "\">";
            }

            result += htmlEscape(cellToUtf8(cell));

            if (hasStyle) {
                result += "</span>";
            }
        }
        result += '\n';
    }

    result += htmlFooter();
    return result;
}

static std::string exportRowsSVG(const Screen& screen, int startRow, int endRow,
                                 const ExportOptions& opts) {
    int charWidth = static_cast<int>(opts.fontSize * 0.6);
    int lineHeight = static_cast<int>(opts.fontSize * 1.2);
    int totalRows = endRow - startRow;
    int svgWidth = screen.cols() * charWidth + 20;
    int svgHeight = totalRows * lineHeight + 20;

    std::string result = svgHeader(svgWidth, svgHeight, opts);

    int y = lineHeight + 10; // baseline of first line
    for (int r = startRow; r < endRow; ++r) {
        int x = 10;
        for (int c = 0; c < screen.cols(); ++c) {
            const auto& cell = screen.cellAt(r, c);
            if (cell.width == 0) continue;

            std::string text = cellToUtf8(cell);
            if (text == " ") {
                x += charWidth * cell.width;
                continue;
            }

            std::string style;
            bool bold = (cell.attributes & AttrBold) != 0;
            bool italic = (cell.attributes & AttrItalic) != 0;
            if (bold) style += "font-weight:bold;";
            if (italic) style += "font-style:italic;";
            if (cell.fg_color != kColorDefault) {
                style += "fill:" + colorToCSS(cell.fg_color) + ";";
            }

            result += "<text x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\"";
            if (!style.empty()) {
                result += " style=\"" + style + "\"";
            }
            result += ">" + htmlEscape(text) + "</text>\n";

            x += charWidth * cell.width;
        }
        y += lineHeight;
    }

    result += svgFooter();
    return result;
}

std::string exportRows(const Screen& screen, int startRow, int endRow,
                       const ExportOptions& opts) {
    // Clamp range
    startRow = std::max(0, startRow);
    endRow = std::min(endRow, screen.rows());
    if (startRow >= endRow) return "";

    switch (opts.format) {
    case ExportFormat::PlainText: return exportRowsPlainText(screen, startRow, endRow, opts);
    case ExportFormat::AnsiText:  return exportRowsAnsi(screen, startRow, endRow, opts);
    case ExportFormat::HTML:      return exportRowsHTML(screen, startRow, endRow, opts);
    case ExportFormat::SVG:       return exportRowsSVG(screen, startRow, endRow, opts);
    }
    return "";
}

std::string exportScreen(const Screen& screen, const ExportOptions& opts) {
    return exportRows(screen, 0, screen.rows(), opts);
}

} // namespace termcore
