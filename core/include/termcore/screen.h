#ifndef TERMCORE_SCREEN_H
#define TERMCORE_SCREEN_H

#include "termcore/dynamic_colors.h"
#include "termcore/kitty_keyboard.h"
#include "termcore/vt_parser.h"
#include <cstdint>
#include <deque>
#include <functional>
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
    uint32_t fg_color = kColorDefault;
    uint32_t bg_color = kColorDefault;
    uint16_t attributes = 0;
    uint8_t width = 1;
};

/// Cursor shape for DECSCUSR.
enum class CursorShape : uint8_t { Block, Underline, Bar };

/// Cursor state.
struct CursorState {
    int row = 0;
    int col = 0;
    bool visible = true;
    bool blink = true;
    CursorShape shape = CursorShape::Block;
};

/// Current SGR pen attributes applied to new cells.
struct Pen {
    uint32_t fg_color = kColorDefault;
    uint32_t bg_color = kColorDefault;
    uint16_t attributes = 0;
};

/// Mouse tracking mode.
enum class MouseMode : uint8_t { None, X10, ButtonEvent, AnyEvent };

/// Mouse encoding format.
enum class MouseEncoding : uint8_t { Default, SGR };

/// Shell integration prompt state (OSC 133).
enum class PromptState : uint8_t { None, Prompt, Input, Output };

/// Notification from the terminal (OSC 9/99/777).
struct TermNotification {
    int type = 0;         // OSC number (9, 99, 777)
    std::string title;
    std::string body;
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
    CursorShape cursorShape() const { return cursor_.shape; }
    bool cursorBlink() const { return cursor_.blink; }

    // --- Resize ---
    void resize(int rows, int cols);

    // --- Scrollback ---
    size_t scrollbackSize() const { return scrollback_.size(); }
    void setMaxScrollback(size_t max) { max_scrollback_ = max; }

    // --- Mode accessors ---
    bool appCursorKeys() const { return app_cursor_keys_; }
    bool bracketedPaste() const { return bracketed_paste_; }
    bool altScreenActive() const { return alt_screen_active_; }
    MouseMode mouseMode() const { return mouse_mode_; }
    MouseEncoding mouseEncoding() const { return mouse_encoding_; }
    bool focusEvents() const { return focus_events_; }
    bool syncUpdate() const { return sync_update_; }
    KittyKeyboardState& kittyKeyboard() { return kitty_keyboard_; }

    // --- OSC state accessors ---
    const std::string& title() const { return title_; }
    const std::string& iconName() const { return icon_name_; }
    const std::string& workingDirectory() const { return working_directory_; }
    const std::string& currentHyperlink() const { return current_hyperlink_; }
    PromptState promptState() const { return prompt_state_; }
    const TermNotification& lastNotification() const { return last_notification_; }

    // --- Response callback (for writing back to PTY) ---
    using ResponseCallback = std::function<void(const std::string&)>;
    void setResponseCallback(ResponseCallback cb) { response_callback_ = std::move(cb); }

    // --- Notification callback ---
    using NotificationCallback = std::function<void(const TermNotification&)>;
    void setNotificationCallback(NotificationCallback cb) { notification_callback_ = std::move(cb); }

    // --- Clipboard event callback ---
    struct ClipboardEvent {
        char selection = 'c';  // 'c' for clipboard, 'p' for primary
        bool is_read = false;  // true = query, false = write
        std::string data;      // base64-decoded data for write
    };
    using ClipboardCallback = std::function<void(const ClipboardEvent&)>;
    void setClipboardCallback(ClipboardCallback cb) { clipboard_callback_ = std::move(cb); }

    // --- Dynamic colors ---
    struct DynamicColorEvent {
        int index;        // -1 = palette changed, 0..9 = OSC 10..19 slot
        uint32_t color;   // new color value
    };
    using DynamicColorCallback = std::function<void(const DynamicColorEvent&)>;
    void setDynamicColorCallback(DynamicColorCallback cb) { dynamic_color_callback_ = std::move(cb); }

    const DynamicColors& dynamicColors() const { return dynamic_colors_; }
    void initDynamicColors(const Config& cfg);

    // --- Utility ---
    std::string getLineText(int row) const;
    std::string getScrollbackLineText(int line) const;  // line 0 = most recent

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

    // Mode flags
    bool app_cursor_keys_ = false;   // DECCKM ?1
    bool bracketed_paste_ = false;   // ?2004
    bool focus_events_ = false;      // ?1004
    bool sync_update_ = false;       // ?2026
    bool alt_screen_active_ = false;

    // Mouse mode
    MouseMode mouse_mode_ = MouseMode::None;
    MouseEncoding mouse_encoding_ = MouseEncoding::Default;

    // Kitty keyboard protocol
    KittyKeyboardState kitty_keyboard_;

    // OSC state
    std::string title_;
    std::string icon_name_;
    std::string working_directory_;
    std::string current_hyperlink_;
    PromptState prompt_state_ = PromptState::None;
    TermNotification last_notification_;

    // REP (repeat character)
    char32_t last_printed_ = 0;

    // Tab stops
    std::vector<bool> tab_stops_;

    // Dynamic colors
    DynamicColors dynamic_colors_;

    // Callbacks
    ResponseCallback response_callback_;
    NotificationCallback notification_callback_;
    ClipboardCallback clipboard_callback_;
    DynamicColorCallback dynamic_color_callback_;

    // Alternate screen buffer
    struct ScreenState {
        std::vector<Row> grid;
        CursorState cursor;
        Pen pen;
        int scroll_top = 0;
        int scroll_bottom = 0;
        bool autowrap = true;
        bool wrap_pending = false;
    };
    ScreenState saved_primary_;

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

    // CSI ext handlers (defined in screen_csi_ext.cpp)
    void handleDeviceStatusReport(const std::vector<int>& params,
                                  const std::string& intermediates);
    void handleDeviceAttributes(const std::vector<int>& params,
                                const std::string& intermediates);
    void handleCursorStyle(const std::vector<int>& params);
    void handleRepeatChar(const std::vector<int>& params);
    void handleCursorNextPrevLine(char32_t final_char,
                                  const std::vector<int>& params);
    void handleTabMovement(char32_t final_char,
                           const std::vector<int>& params);
    void handleTabClear(const std::vector<int>& params);
    void initTabStops();

    // OSC handlers (defined in screen_osc.cpp)
    void handleOscWorkingDirectory(const std::string& str);
    void handleOscHyperlink(const std::string& str);
    void handleOscClipboard(const std::string& str);
    void handleOscNotification(int type, const std::string& str);
    void handleOscShellIntegration(const std::string& str);
    void handleOscPaletteColor(const std::string& str);
    void handleOscDynamicColor(int osc_number, const std::string& str);
    void handleOscResetColor(int osc_number, const std::string& str);

    // Alt screen helpers
    void switchToAltScreen(bool save_cursor);
    void switchToPrimaryScreen(bool restore_cursor);
    void clearScreen();
};

} // namespace termcore

#endif // TERMCORE_SCREEN_H
