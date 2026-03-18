#ifndef TERMCORE_SCREEN_H
#define TERMCORE_SCREEN_H

#include "termcore/vt_parser.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace termcore {

/// Attribute flags for TermCell.
enum CellAttribute : uint16_t {
    AttrBold          = 1,
    AttrItalic        = 2,
    AttrUnderline     = 4,
    AttrBlink         = 8,
    AttrInverse       = 16,
    AttrHidden        = 32,
    AttrStrikethrough = 64,
};

/// A single cell in the terminal grid.
struct TermCell {
    char32_t codepoint = ' ';
    uint32_t fg_color = 0xFFFFFF;
    uint32_t bg_color = 0x000000;
    uint16_t attributes = 0;
    uint8_t width = 1;
};

/// Cursor state.
struct CursorState {
    int row = 0;
    int col = 0;
    bool visible = true;
    bool blink = true;
};

/// Current SGR pen attributes applied to new cells.
struct Pen {
    uint32_t fg_color = 0xFFFFFF;
    uint32_t bg_color = 0x000000;
    uint16_t attributes = 0;
};

/// Terminal screen model: cell grid, cursor, scrollback.
/// Implements VtParserHandler so it can be connected to VtParser.
class Screen : public VtParserHandler {
public:
    explicit Screen(int rows = 24, int cols = 80);
    ~Screen() override = default;

    // --- Grid access ---
    const TermCell& cellAt(int row, int col) const;
    int rows() const { return rows_; }
    int cols() const { return cols_; }

    // --- Cursor ---
    int cursorRow() const { return cursor_.row; }
    int cursorCol() const { return cursor_.col; }
    bool cursorVisible() const { return cursor_.visible; }

    // --- Resize ---
    void resize(int rows, int cols);

    // --- Scrollback ---
    size_t scrollbackSize() const { return scrollback_.size(); }
    void setMaxScrollback(size_t max) { max_scrollback_ = max; }

    // --- Utility ---
    std::string getLineText(int row) const;

    // --- VtParserHandler implementation ---
    void onPrint(char32_t codepoint) override;
    void onExecute(uint8_t byte) override;
    void onCsiDispatch(char32_t final_char,
                       const std::vector<int>& params,
                       const std::string& intermediates) override;
    void onEscDispatch(char32_t final_char,
                       const std::string& intermediates) override;
    void onOscDispatch(int osc_number,
                       const std::string& osc_string) override;

private:
    using Row = std::vector<TermCell>;

    // Grid
    int rows_;
    int cols_;
    std::vector<Row> grid_;
    std::deque<Row> scrollback_;
    size_t max_scrollback_ = 10000;

    // Cursor
    CursorState cursor_;
    Pen pen_;

    // Saved cursor (DECSC/DECRC)
    CursorState saved_cursor_;
    Pen saved_pen_;

    // Scroll region (0-based, inclusive)
    int scroll_top_ = 0;
    int scroll_bottom_;  // initialized in constructor

    // Auto-wrap
    bool autowrap_ = true;
    bool wrap_pending_ = false;

    // --- Internal helpers ---
    Row makeRow() const;
    void scrollUp(int top, int bottom, int count = 1);
    void scrollDown(int top, int bottom, int count = 1);
    void clampCursor();
    TermCell& mutableCellAt(int row, int col);
    void eraseCell(TermCell& cell) const;
    void advanceCursorAfterPrint();

    // CSI handlers (defined in screen_csi.cpp)
    void handleCursorMovement(char32_t final_char,
                              const std::vector<int>& params);
    void handleEraseDisplay(const std::vector<int>& params);
    void handleEraseLine(const std::vector<int>& params);
    void handleSGR(const std::vector<int>& params);
    void handleScrollRegion(const std::vector<int>& params);
    void handleMode(char32_t final_char,
                    const std::vector<int>& params,
                    const std::string& intermediates);
    void handleInsertDeleteLines(char32_t final_char,
                                 const std::vector<int>& params);
    void handleInsertDeleteChars(char32_t final_char,
                                 const std::vector<int>& params);
    void handleScrollUpDown(char32_t final_char,
                            const std::vector<int>& params);
    void handleEraseChars(const std::vector<int>& params);
    void handleAbsolutePosition(char32_t final_char,
                                const std::vector<int>& params);
};

} // namespace termcore

#endif // TERMCORE_SCREEN_H
