#ifndef TERMCORE_VI_COPY_MODE_H
#define TERMCORE_VI_COPY_MODE_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace termcore {

class Screen;

enum class ViMode : uint8_t { Normal, Visual, VisualLine, VisualBlock };

enum class ViAction : uint8_t { None, Exit, Yank, SearchForward, SearchBackward };

/// Selection region for rendering highlights
struct ViSelection {
    int start_row = 0, start_col = 0;
    int end_row = 0, end_col = 0;
    ViMode mode = ViMode::Normal;
};

/// Vi copy mode for tmux-style terminal text selection.
class ViCopyMode {
public:
    explicit ViCopyMode(Screen& screen);
    ~ViCopyMode() = default;

    void enterCopyMode();
    void exitCopyMode();
    ViAction processKey(char key, bool ctrl, bool shift);
    ViSelection getSelection() const;
    void selectAll();
    std::string yankSelection() const;

    bool isActive() const { return active_; }
    ViMode mode() const { return mode_; }
    int cursorRow() const { return cursor_row_; }
    int cursorCol() const { return cursor_col_; }

    // --- Lua extension hooks ---

    /// Override the set of characters considered "word" characters for w/b/e.
    /// Pass an empty string to restore the default behaviour.
    void setWordChars(const std::string& chars) { word_chars_ = chars; }

    /// Callback invoked when text is yanked (y or Y).
    using YankCallback = std::function<void(const std::string&)>;
    void setOnYank(YankCallback cb) { onYank_ = std::move(cb); }

    /// Register a custom single-key mapping executed when the key is pressed
    /// in Normal mode.  The action string is ignored by the built-in handler
    /// and the callback is called instead.
    using CustomKeyCallback = std::function<void()>;
    void mapKey(const std::string& key, CustomKeyCallback cb);

    /// Remove all custom key mappings and yank callback.
    void clearLuaCallbacks();

private:
    Screen& screen_;
    bool active_ = false;
    ViMode mode_ = ViMode::Normal;

    // Lua extension state
    std::string word_chars_;
    mutable YankCallback onYank_;
    std::unordered_map<std::string, CustomKeyCallback> custom_keys_;
    int cursor_row_ = 0;   // Negative = scrollback
    int cursor_col_ = 0;
    int anchor_row_ = 0;
    int anchor_col_ = 0;
    char pending_key_ = 0; // For multi-key commands (g, f, F)

    // Motions
    void moveLeft();
    void moveRight();
    void moveUp();
    void moveDown();
    void moveWordForward();
    void moveWordBackward();
    void moveWordEnd();
    void moveLineBegin();
    void moveLineEnd();
    void moveFirstNonBlank();
    void moveToTop();
    void moveToBottom();
    void moveScreenTop();
    void moveScreenMiddle();
    void moveScreenBottom();
    void moveParagraphUp();
    void moveParagraphDown();

    // Helpers
    void clampCursor();
    char32_t cellAt(int row, int col) const;
    bool isWordChar(char32_t ch) const;
    void appendUtf8(std::string& out, char32_t ch) const;
    std::string extractText(int r0, int c0, int r1, int c1) const;
    std::string extractLineText(int r0, int r1) const;
    std::string extractBlockText(int r0, int c0, int r1, int c1) const;
};

} // namespace termcore
#endif // TERMCORE_VI_COPY_MODE_H
