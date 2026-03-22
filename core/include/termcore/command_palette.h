#ifndef TERMCORE_COMMAND_PALETTE_H
#define TERMCORE_COMMAND_PALETTE_H

#include "termcore/keybinding.h"
#include <string>
#include <vector>

namespace termcore {

/// A single command entry in the command palette.
struct PaletteCommand {
    std::string name;           // Human-readable name (e.g. "New Tab")
    std::string description;    // Brief description
    Action action;              // Associated KeyAction
    std::string shortcut_hint;  // e.g. "Ctrl+T"
};

/// VS Code-style command palette with fuzzy filtering.
class CommandPalette {
public:
    CommandPalette();
    ~CommandPalette() = default;

    /// Open the palette (resets query and selection).
    void open();

    /// Close the palette.
    void close();

    /// Whether the palette is currently visible.
    bool isOpen() const { return open_; }

    /// Set the filter query string.
    void setQuery(const std::string& query);

    /// Get the current query string.
    const std::string& query() const { return query_; }

    /// Get filtered and scored command results.
    const std::vector<PaletteCommand>& filteredCommands() const { return filtered_; }

    /// Navigate selection down.
    void selectNext();

    /// Navigate selection up.
    void selectPrev();

    /// Get the index of the currently selected item.
    int selectedIndex() const { return selectedIndex_; }

    /// Get the action of the currently selected command.
    /// Returns Action::None if nothing is selected.
    Action selectedAction() const;

    /// Process a character typed into the palette input.
    void onChar(char ch);

    /// Delete the last character from the query.
    void onBackspace();

    /// Update shortcut hints from the current keybinding manager.
    void updateShortcuts(const KeybindingManager& mgr);

    /// Maximum number of visible items in the list.
    static constexpr int kMaxVisibleItems = 10;

private:
    void populateCommands();
    void applyFilter();
    int fuzzyScore(const std::string& name, const std::string& query) const;
    std::string formatShortcut(const KeyCombo& combo) const;

    bool open_ = false;
    std::string query_;
    int selectedIndex_ = 0;

    std::vector<PaletteCommand> allCommands_;
    std::vector<PaletteCommand> filtered_;
};

} // namespace termcore

#endif // TERMCORE_COMMAND_PALETTE_H
