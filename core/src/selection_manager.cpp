#include "termcore/selection_manager.h"
#include <algorithm>

namespace termcore {

SelectionManager::GridPos SelectionManager::pixelToGrid(
    int px, int py, float cellW, float cellH,
    int offsetX, int offsetY, int maxRows, int maxCols) const
{
    GridPos pos;
    if (cellW <= 0) cellW = 1;
    if (cellH <= 0) cellH = 1;
    pos.col = static_cast<int>((px - offsetX) / cellW);
    pos.row = static_cast<int>((py - offsetY) / cellH);
    pos.col = (std::max)(0, pos.col);
    pos.row = (std::max)(0, pos.row);
    if (maxCols > 0) pos.col = (std::min)(maxCols - 1, pos.col);
    if (maxRows > 0) pos.row = (std::min)(maxRows - 1, pos.row);
    return pos;
}

void SelectionManager::onMouseDown(int px, int py, float cellW, float cellH,
                                   int offsetX, int offsetY)
{
    GridPos pos = pixelToGrid(px, py, cellW, cellH, offsetX, offsetY);
    start_ = pos;
    end_ = pos;
    hasSelection_ = false;
    isDragging_ = true;
}

void SelectionManager::onMouseMove(int px, int py, float cellW, float cellH,
                                   int offsetX, int offsetY)
{
    if (!isDragging_) return;
    GridPos pos = pixelToGrid(px, py, cellW, cellH, offsetX, offsetY);
    end_ = pos;
    hasSelection_ = (start_.row != end_.row || start_.col != end_.col);
}

void SelectionManager::onMouseUp(int px, int py, float cellW, float cellH,
                                 int offsetX, int offsetY)
{
    if (!isDragging_) return;
    isDragging_ = false;
    GridPos pos = pixelToGrid(px, py, cellW, cellH, offsetX, offsetY);
    end_ = pos;
    hasSelection_ = (start_.row != end_.row || start_.col != end_.col);
}

void SelectionManager::onDoubleClick(int px, int py, float cellW, float cellH,
                                     int offsetX, int offsetY,
                                     const Screen& screen)
{
    GridPos pos = pixelToGrid(px, py, cellW, cellH, offsetX, offsetY,
                              screen.rows(), screen.cols());
    int row = pos.row;
    int startCol = pos.col;
    int endCol = pos.col;
    int cols = screen.cols();

    auto isWordChar = [](char32_t cp) {
        return (cp >= 'A' && cp <= 'Z') ||
               (cp >= 'a' && cp <= 'z') ||
               (cp >= '0' && cp <= '9') ||
               cp == '_' || cp == '-' || cp == '.' ||
               cp > 127;
    };

    while (startCol > 0) {
        const TermCell& c = screen.cellAt(row, startCol - 1);
        if (!isWordChar(c.codepoint)) break;
        --startCol;
    }
    while (endCol < cols - 1) {
        const TermCell& c = screen.cellAt(row, endCol + 1);
        if (!isWordChar(c.codepoint)) break;
        ++endCol;
    }

    start_ = {row, startCol};
    end_ = {row, endCol};
    hasSelection_ = true;
    isDragging_ = false;
}

void SelectionManager::selectAll(int rows, int cols) {
    start_ = {0, 0};
    end_ = {rows - 1, cols - 1};
    hasSelection_ = true;
    isDragging_ = false;
}

void SelectionManager::clear() {
    hasSelection_ = false;
    isDragging_ = false;
    start_ = {};
    end_ = {};
}

std::string SelectionManager::getSelectedText(const Screen& screen) const {
    if (!hasSelection_) return {};

    int sr = start_.row, sc = start_.col;
    int er = end_.row, ec = end_.col;

    // Normalize so start <= end in reading order
    if (sr > er || (sr == er && sc > ec)) {
        std::swap(sr, er);
        std::swap(sc, ec);
    }

    std::string result;
    for (int row = sr; row <= er; ++row) {
        int colStart = (row == sr) ? sc : 0;
        int colEnd = (row == er) ? ec : screen.cols() - 1;

        // Trim trailing spaces
        int lastNonSpace = colStart - 1;
        for (int col = colStart; col <= colEnd; ++col) {
            const TermCell& cell = screen.cellAt(row, col);
            if (cell.codepoint != ' ' && cell.codepoint != 0) {
                lastNonSpace = col;
            }
        }

        for (int col = colStart; col <= lastNonSpace; ++col) {
            const TermCell& cell = screen.cellAt(row, col);
            char32_t cp = cell.codepoint;
            if (cp == 0) cp = ' ';

            // UTF-8 encode
            if (cp < 0x80) {
                result += static_cast<char>(cp);
            } else if (cp < 0x800) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | (cp >> 18));
                result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        if (row < er) {
            result += '\n';
        }
    }
    return result;
}

} // namespace termcore
