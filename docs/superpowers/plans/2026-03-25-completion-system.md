# Completion System & SGR Dim Support Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add SGR 2 (dim/faint) text attribute support and a fish-style ghost text autocomplete system with Lua plugin extensibility.

**Architecture:** SGR 2 adds a new `AttrDim` flag to the existing cell attribute bitmask, with 50% brightness reduction in all 3 platform renderers. The completion system introduces a `CompletionManager` with a provider pattern (built-in `HistoryProvider` + Lua-extensible providers), rendering ghost text at the cursor position. Shell integration (OSC 133) drives input tracking and history collection.

**Tech Stack:** C++17, sol2 (Lua bindings), D3D11/OpenGL/Metal renderers, TUnit (testing)

**Spec:** `docs/superpowers/specs/2026-03-25-completion-system-design.md`

---

## Chunk 1: SGR 2 (Dim/Faint) Support

### Task 1: Add AttrDim to CellAttribute enum

**Files:**
- Modify: `core/include/termcore/term_cell.h:11-19`

- [ ] **Step 1: Add AttrDim flag**

In `core/include/termcore/term_cell.h`, add `AttrDim = 128` after `AttrStrikethrough = 64`:

```cpp
enum CellAttribute : uint16_t {
    AttrBold          = 1,
    AttrItalic        = 2,
    AttrUnderline     = 4,
    AttrBlink         = 8,
    AttrInverse       = 16,
    AttrHidden        = 32,
    AttrStrikethrough = 64,
    AttrDim           = 128,
};
```

- [ ] **Step 2: Commit**

```bash
git add core/include/termcore/term_cell.h
git commit -m "feat: add AttrDim flag to CellAttribute enum for SGR 2 support"
```

### Task 2: Handle SGR 2 in screen_csi.cpp

**Files:**
- Modify: `core/src/screen_csi.cpp:287-317`

- [ ] **Step 1: Add p == 2 (dim) and p == 5 (blink) handling in handleSGR()**

In `core/src/screen_csi.cpp`, after the `p == 1` (bold) block at line 288, add `p == 2` handling. Also add `p == 5` (blink) since `AttrBlink` exists in the enum but is not set in handleSGR.

After line 288 (`pen_.attributes |= AttrBold;`), add:
```cpp
} else if (p == 2) {
    pen_.attributes |= AttrDim;
```

After line 289 (the italic block), add blink handling:
```cpp
} else if (p == 5) {
    pen_.attributes |= AttrBlink;
```

- [ ] **Step 2: Fix SGR 22 to clear both Bold and Dim**

At line 317, change:
```cpp
} else if (p == 22) {
    pen_.attributes &= ~AttrBold;
```
to:
```cpp
} else if (p == 22) {
    pen_.attributes &= ~(AttrBold | AttrDim);
```

- [ ] **Step 3: Commit**

```bash
git add core/src/screen_csi.cpp
git commit -m "feat: handle SGR 2 (dim) and SGR 5 (blink) in handleSGR, fix SGR 22 reset"
```

### Task 3: Render dim text in D3DCellBuilder (Windows)

**Files:**
- Modify: `platform/windows/src/D3DCellBuilder.cpp`

- [ ] **Step 1: Apply dim to foreground in Pass 1 (background pass)**

In `buildCellBuffer()`, after the AttrInverse swap in Pass 1 (around line 109), and after the selection swap (line 117), add dim handling before the search highlight check:

```cpp
if (cell.attributes & AttrDim) {
    inst.fg_color[0] *= 0.5f;
    inst.fg_color[1] *= 0.5f;
    inst.fg_color[2] *= 0.5f;
}
```

- [ ] **Step 2: Apply dim to foreground in Pass 2 (glyph pass)**

In Pass 2, there are three places where fg_color is set: box drawing glyphs (around line 268), Powerline glyphs (around line 347), and regular glyphs (around line 398). After each fg_color + AttrInverse + selection handling block, add the same dim check.

For regular glyphs (after selection swap, before search highlight check at line 415):
```cpp
if (cell.attributes & AttrDim) {
    inst.fg_color[0] *= 0.5f;
    inst.fg_color[1] *= 0.5f;
    inst.fg_color[2] *= 0.5f;
}
```

Apply the same pattern after the AttrInverse/selection handling for box drawing glyphs and Powerline glyphs.

- [ ] **Step 3: Commit**

```bash
git add platform/windows/src/D3DCellBuilder.cpp
git commit -m "feat: render dim/faint text at 50% brightness in D3D renderer"
```

### Task 4: Render dim text in GLCellBuilder (Linux)

**Files:**
- Modify: `platform/linux/src/GLCellBuilder.cpp`

- [ ] **Step 1: Apply dim handling in Pass 1 and Pass 2**

Follow the same pattern as Task 3. The GL renderer has identical pass structure. After each AttrInverse/selection swap block, add:
```cpp
if (cell.attributes & AttrDim) {
    inst.fg_color[0] *= 0.5f;
    inst.fg_color[1] *= 0.5f;
    inst.fg_color[2] *= 0.5f;
}
```

- [ ] **Step 2: Commit**

```bash
git add platform/linux/src/GLCellBuilder.cpp
git commit -m "feat: render dim/faint text at 50% brightness in GL renderer"
```

### Task 5: Render dim text in MetalCellBuilder (macOS)

**Files:**
- Modify: `platform/macos/src/MetalCellBuilder.mm`

- [ ] **Step 1: Apply dim handling**

The Metal renderer uses `uint32_t` color format instead of `float[4]`. Apply dim by reducing the RGB components of the foreground before packing into the CellInstance struct. The exact method depends on the Metal renderer's color handling — check if it uses float[4] or packed uint32_t and apply accordingly.

If float-based (same as D3D):
```cpp
if (cell.attributes & AttrDim) {
    inst.fg_color[0] *= 0.5f;
    inst.fg_color[1] *= 0.5f;
    inst.fg_color[2] *= 0.5f;
}
```

If uint32_t-based, apply the dim before packing:
```cpp
if (cell.attributes & AttrDim) {
    fg = ((fg >> 1) & 0x7F7F7F00) | (fg & 0xFF); // halve RGB, keep alpha
}
```

- [ ] **Step 2: Commit**

```bash
git add platform/macos/src/MetalCellBuilder.mm
git commit -m "feat: render dim/faint text at 50% brightness in Metal renderer"
```

---

## Chunk 2: HistoryProvider

### Task 6: Create HistoryProvider class

**Files:**
- Create: `core/include/termcore/history_provider.h`
- Create: `core/src/history_provider.cpp`

- [ ] **Step 1: Write HistoryProvider header**

```cpp
// core/include/termcore/history_provider.h
#ifndef TERMCORE_HISTORY_PROVIDER_H
#define TERMCORE_HISTORY_PROVIDER_H

#include <string>
#include <vector>

namespace termcore {

class HistoryProvider {
public:
    /// Add a command to history. Duplicates are moved to most-recent position.
    /// Empty or whitespace-only commands are ignored.
    void addEntry(const std::string& command);

    /// Return the most recent history entry matching the given prefix.
    /// Returns empty string if no match.
    std::string suggest(const std::string& prefix) const;

    /// Number of entries in history.
    size_t size() const { return history_.size(); }

    /// Clear all history.
    void clear() { history_.clear(); }

private:
    /// Stored newest-first (index 0 = most recent).
    std::vector<std::string> history_;
    static constexpr size_t kMaxHistory = 1000;
};

} // namespace termcore

#endif // TERMCORE_HISTORY_PROVIDER_H
```

- [ ] **Step 2: Write HistoryProvider implementation**

```cpp
// core/src/history_provider.cpp
#include "termcore/history_provider.h"
#include <algorithm>

namespace termcore {

void HistoryProvider::addEntry(const std::string& command) {
    // Skip empty or whitespace-only commands
    if (command.empty()) return;
    bool allSpace = true;
    for (char c : command) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            allSpace = false;
            break;
        }
    }
    if (allSpace) return;

    // Remove existing duplicate (move to front)
    auto it = std::find(history_.begin(), history_.end(), command);
    if (it != history_.end()) {
        history_.erase(it);
    }

    // Insert at front (most recent)
    history_.insert(history_.begin(), command);

    // Enforce max size
    if (history_.size() > kMaxHistory) {
        history_.pop_back();
    }
}

std::string HistoryProvider::suggest(const std::string& prefix) const {
    if (prefix.empty()) return "";
    for (const auto& entry : history_) {
        if (entry.size() > prefix.size() &&
            entry.compare(0, prefix.size(), prefix) == 0) {
            return entry;
        }
    }
    return "";
}

} // namespace termcore
```

- [ ] **Step 3: Add to CMakeLists.txt**

Find the core library's source list in `core/CMakeLists.txt` and add `src/history_provider.cpp`.

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build --target termcore`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/history_provider.h core/src/history_provider.cpp core/CMakeLists.txt
git commit -m "feat: add HistoryProvider class for command history with prefix matching"
```

---

## Chunk 3: CompletionManager

### Task 7: Create CompletionManager class

**Files:**
- Create: `core/include/termcore/completion_manager.h`
- Create: `core/src/completion_manager.cpp`

- [ ] **Step 1: Write CompletionManager header**

```cpp
// core/include/termcore/completion_manager.h
#ifndef TERMCORE_COMPLETION_MANAGER_H
#define TERMCORE_COMPLETION_MANAGER_H

#include "termcore/history_provider.h"
#include <functional>
#include <string>
#include <vector>

namespace termcore {

class CompletionManager {
public:
    struct Provider {
        std::string name;
        int priority = 50;  // Higher = checked first. Ties: first-registered wins.
        std::function<std::string(const std::string& input,
                                   const std::string& cwd)> getSuggestion;
    };

    CompletionManager();

    void registerProvider(Provider provider);
    void removeProvider(const std::string& name);

    /// Called when input text changes. Queries providers and updates ghost text.
    void onInputChanged(const std::string& currentInput,
                        const std::string& cwd);

    /// Async provider reports a suggestion. Ignored if a higher-priority
    /// sync provider already provided one, or if input has changed.
    void setSuggestion(const std::string& providerName,
                       const std::string& suggestion);

    /// The ghost text to display (the part AFTER currentInput_).
    const std::string& ghostText() const { return ghostText_; }
    bool hasGhostText() const { return !ghostText_.empty(); }

    /// Accept full ghost text. Returns the text that should be sent to PTY.
    std::string acceptFull();

    /// Accept next word of ghost text. Returns the word to send to PTY.
    /// Word boundaries: '/', '.', '-', '_', ' '
    std::string acceptWord();

    void clear();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /// Access the built-in history provider.
    HistoryProvider& historyProvider() { return historyProvider_; }

private:
    std::vector<Provider> providers_;
    HistoryProvider historyProvider_;
    std::string currentInput_;
    std::string ghostText_;
    std::string ghostProviderName_;
    bool enabled_ = true;
    bool hasSyncResult_ = false;  // true if a sync provider gave a result
};

} // namespace termcore

#endif // TERMCORE_COMPLETION_MANAGER_H
```

- [ ] **Step 2: Write CompletionManager implementation**

```cpp
// core/src/completion_manager.cpp
#include "termcore/completion_manager.h"
#include <algorithm>

namespace termcore {

CompletionManager::CompletionManager() {
    // Register built-in history provider at highest priority
    Provider historyProv;
    historyProv.name = "history";
    historyProv.priority = 100;
    historyProv.getSuggestion = [this](const std::string& input,
                                       const std::string& /*cwd*/) -> std::string {
        return historyProvider_.suggest(input);
    };
    registerProvider(std::move(historyProv));
}

void CompletionManager::registerProvider(Provider provider) {
    // Remove existing provider with same name
    removeProvider(provider.name);

    // Insert sorted by priority (descending). Ties: new one goes after existing.
    auto it = std::find_if(providers_.begin(), providers_.end(),
        [&](const Provider& p) { return p.priority < provider.priority; });
    providers_.insert(it, std::move(provider));
}

void CompletionManager::removeProvider(const std::string& name) {
    providers_.erase(
        std::remove_if(providers_.begin(), providers_.end(),
            [&](const Provider& p) { return p.name == name; }),
        providers_.end());
}

void CompletionManager::onInputChanged(const std::string& currentInput,
                                        const std::string& cwd) {
    currentInput_ = currentInput;
    ghostText_.clear();
    ghostProviderName_.clear();
    hasSyncResult_ = false;

    if (!enabled_ || currentInput.empty()) return;

    for (const auto& provider : providers_) {
        if (!provider.getSuggestion) continue;
        std::string suggestion = provider.getSuggestion(currentInput, cwd);
        if (!suggestion.empty() && suggestion.size() > currentInput.size()) {
            // suggestion is the full command; ghost text is the remaining part
            ghostText_ = suggestion.substr(currentInput.size());
            ghostProviderName_ = provider.name;
            hasSyncResult_ = true;
            return;
        }
    }
}

void CompletionManager::setSuggestion(const std::string& providerName,
                                       const std::string& suggestion) {
    // Ignore if sync provider already has a result
    if (hasSyncResult_) return;
    // Ignore if from a lower-priority provider when we already have one
    if (!ghostText_.empty()) return;

    if (!suggestion.empty() && suggestion.size() > currentInput_.size() &&
        suggestion.compare(0, currentInput_.size(), currentInput_) == 0) {
        ghostText_ = suggestion.substr(currentInput_.size());
        ghostProviderName_ = providerName;
    }
}

std::string CompletionManager::acceptFull() {
    std::string result = ghostText_;
    ghostText_.clear();
    ghostProviderName_.clear();
    return result;
}

std::string CompletionManager::acceptWord() {
    if (ghostText_.empty()) return "";

    // Find next word boundary: '/', '.', '-', '_', ' '
    auto isWordBoundary = [](char c) {
        return c == '/' || c == '.' || c == '-' || c == '_' || c == ' ';
    };

    size_t pos = 0;
    // Skip leading boundary characters
    while (pos < ghostText_.size() && isWordBoundary(ghostText_[pos])) {
        ++pos;
    }
    // Find next boundary
    while (pos < ghostText_.size() && !isWordBoundary(ghostText_[pos])) {
        ++pos;
    }

    std::string word = ghostText_.substr(0, pos);
    ghostText_ = ghostText_.substr(pos);
    if (ghostText_.empty()) {
        ghostProviderName_.clear();
    }
    return word;
}

void CompletionManager::clear() {
    ghostText_.clear();
    ghostProviderName_.clear();
    currentInput_.clear();
    hasSyncResult_ = false;
}

} // namespace termcore
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/completion_manager.cpp` to the core library source list.

- [ ] **Step 4: Build to verify**

Run: `cmake --build build --target termcore`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/completion_manager.h core/src/completion_manager.cpp core/CMakeLists.txt
git commit -m "feat: add CompletionManager with provider pattern and word-level acceptance"
```

---

## Chunk 4: Screen Input Tracking

### Task 8: Add input start position tracking to Screen

**Files:**
- Modify: `core/include/termcore/screen.h`
- Modify: `core/src/screen_osc.cpp`

- [ ] **Step 1: Add input tracking members to Screen**

In `core/include/termcore/screen.h`, add to private members (near `prompt_state_` at line 257):

```cpp
int input_start_row_ = -1;
int input_start_col_ = -1;
```

Add public accessor methods (near `promptState()` at line 115):

```cpp
/// Get text between input start position and cursor (the current user input).
/// Returns empty string if not in Input prompt state.
std::string currentInputText() const;

/// Input start column (set by OSC 133;B).
int inputStartCol() const { return input_start_col_; }

/// Input start row (set by OSC 133;B).
int inputStartRow() const { return input_start_row_; }
```

- [ ] **Step 2: Record input start position on OSC 133;B**

In `core/src/screen_osc.cpp`, in the `handleOscShellIntegration()` method, at the `'B'` marker handling (around line 286-289), save the cursor position:

Change:
```cpp
case 'B':
    prompt_state_ = PromptState::Input;
    break;
```
to:
```cpp
case 'B':
    prompt_state_ = PromptState::Input;
    input_start_row_ = static_cast<int>(scrollback_.size()) + cursor_.row;
    input_start_col_ = cursor_.col;
    break;
```

- [ ] **Step 3: Implement currentInputText()**

Add to `core/src/screen.cpp`:

```cpp
std::string Screen::currentInputText() const {
    if (prompt_state_ != PromptState::Input) return "";
    if (input_start_row_ < 0 || input_start_col_ < 0) return "";

    int absRow = static_cast<int>(scrollback_.size()) + cursor_.row;

    std::string result;
    for (int r = input_start_row_; r <= absRow; ++r) {
        int viewRow = r - static_cast<int>(scrollback_.size());
        if (viewRow < 0 || viewRow >= rows_) continue;

        int startCol = (r == input_start_row_) ? input_start_col_ : 0;
        int endCol = (r == absRow) ? cursor_.col : cols_;

        for (int c = startCol; c < endCol && c < cols_; ++c) {
            const TermCell& cell = grid_[viewRow][c];
            if (cell.width == 0) continue;  // skip continuation cells
            if (cell.codepoint == 0) continue;

            // Convert char32_t to UTF-8
            char32_t cp = cell.codepoint;
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
        if (r < absRow) result += '\n';  // multi-line commands
    }
    return result;
}
```

- [ ] **Step 4: Reset input tracking on OSC 133;C and 133;A**

In `handleOscShellIntegration()`, when receiving 'C' (output start) or 'A' (new prompt), reset the input tracking:

For the 'A' marker handler:
```cpp
input_start_row_ = -1;
input_start_col_ = -1;
```

For the 'C' marker handler, before changing prompt_state_, capture the command text for history. Add a callback:

In `core/include/termcore/screen.h`, add a public setter and private member:

Public method:
```cpp
void setCommandCaptureCallback(std::function<void(const std::string&)> cb) {
    onCommandCapture_ = std::move(cb);
}
```

Private member:
```cpp
std::function<void(const std::string&)> onCommandCapture_;
```

In `screen_osc.cpp` 'C' handler, before state change:
```cpp
case 'C': {
    if (prompt_state_ == PromptState::Input && onCommandCapture_) {
        std::string cmd = currentInputText();
        if (!cmd.empty()) {
            onCommandCapture_(cmd);
        }
    }
    prompt_state_ = PromptState::Output;
    // ... existing code ...
    break;
}
```

- [ ] **Step 5: Build to verify**

Run: `cmake --build build --target termcore`

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/screen.h core/src/screen_osc.cpp core/src/screen.cpp
git commit -m "feat: track input start position for OSC 133 and implement currentInputText()"
```

---

## Chunk 5: Ghost Text Rendering

### Task 9: Add ghost text state to renderer Impl structs

**Files:**
- Modify: `platform/windows/src/D3DTextRendererImpl.h`
- Modify: `platform/windows/include/D3DTextRenderer.h`

- [ ] **Step 1: Add GhostText struct to D3DTextRendererImpl.h**

Add to the Impl struct members (near the selection/search highlight state area, around line 30):

```cpp
struct GhostText {
    std::string text;  // UTF-8 ghost text to display
    int row = -1;      // row to display at
    int col = -1;      // starting column
};
GhostText ghostText;
```

- [ ] **Step 2: Add setGhostText() to D3DTextRenderer public API**

In `platform/windows/include/D3DTextRenderer.h`, add a public method:

```cpp
void setGhostText(const std::string& text, int row, int col);
```

Implementation (in D3DTextRenderer.cpp or similar):
```cpp
void D3DTextRenderer::setGhostText(const std::string& text, int row, int col) {
    impl_->ghostText.text = text;
    impl_->ghostText.row = row;
    impl_->ghostText.col = col;
}
```

- [ ] **Step 3: Commit**

```bash
git add platform/windows/src/D3DTextRendererImpl.h platform/windows/include/D3DTextRenderer.h platform/windows/src/D3DTextRenderer.cpp
git commit -m "feat: add ghost text state to D3D renderer"
```

### Task 10: Render ghost text in D3DCellBuilder

**Files:**
- Modify: `platform/windows/src/D3DCellBuilder.cpp`

- [ ] **Step 1: Add ghost text rendering pass after Pass 2**

In `buildCellBuffer()`, after Pass 2 (glyph quads) ends and before Pass 3 (underlines) at line 428, add a Ghost Text Pass:

```cpp
// Pass 2b: Ghost text (dim suggestion text after cursor)
if (!ghostText.text.empty() && ghostText.row >= 0 && ghostText.col >= 0 &&
    ghostText.row < rows) {
    const DynamicColors& gtColors = screen.dynamicColors();
    int gtCol = ghostText.col;

    // Decode UTF-8 ghost text to codepoints
    const std::string& gt = ghostText.text;
    size_t i = 0;
    while (i < gt.size() && gtCol < cols) {
        char32_t cp = 0;
        uint8_t b = static_cast<uint8_t>(gt[i]);
        int seqLen = 1;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            seqLen = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            seqLen = 3;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            seqLen = 4;
        }
        for (int j = 1; j < seqLen && (i + j) < gt.size(); ++j) {
            cp = (cp << 6) | (static_cast<uint8_t>(gt[i + j]) & 0x3F);
        }
        i += seqLen;

        if (cp == ' ' || cp == 0) {
            ++gtCol;
            continue;
        }

        // Check if the cell at this position already has content
        const TermCell& existing = screen.cellAt(ghostText.row, gtCol);
        if (existing.codepoint != ' ' && existing.codepoint != 0) {
            break;  // Don't overlay on existing text
        }

        // Look up glyph in font
        auto faceId = fontCollection->resolveFace(cp);
        if (faceId == kInvalidCollectionFace) {
            ++gtCol;
            continue;
        }
        auto rastFace = fontCollection->rasterizerFaceId(faceId);
        uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
        if (glyphIdx == 0) {
            ++gtCol;
            continue;
        }

        GlyphKey key{rastFace, glyphIdx, {0, 0}};
        auto info = glyphCache->getOrRasterize(key, fontSize, *rasterizer, *glyphAtlas);
        if (!info || info->region.width <= 0 || info->region.height <= 0) {
            ++gtCol;
            continue;
        }

        D3DCellInstance inst = {};
        float offsetX = static_cast<float>(info->region.bearing_x);
        float offsetY = ascent - static_cast<float>(info->region.bearing_y);

        inst.position[0] = gtCol * cellW + offsetX;
        inst.position[1] = ghostText.row * cellH + offsetY + gridOffsetY;
        inst.atlas_uv[0] = static_cast<float>(info->region.x);
        inst.atlas_uv[1] = static_cast<float>(info->region.y);
        inst.atlas_size[0] = static_cast<float>(info->region.width);
        inst.atlas_size[1] = static_cast<float>(info->region.height);

        // Ghost text color: 35% brightness of default foreground
        float fgFull[4];
        colorFromRGBA(gtColors.resolveFg(kColorDefault), fgFull);
        inst.fg_color[0] = fgFull[0] * 0.35f;
        inst.fg_color[1] = fgFull[1] * 0.35f;
        inst.fg_color[2] = fgFull[2] * 0.35f;
        inst.fg_color[3] = fgFull[3];

        inst.flags = 1;  // has_glyph
        cellInstances.push_back(inst);

        ++gtCol;
    }
}
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --target BreadTerminal`

- [ ] **Step 3: Commit**

```bash
git add platform/windows/src/D3DCellBuilder.cpp
git commit -m "feat: render ghost text as dim glyphs at cursor position"
```

### Task 11: Add ghost text rendering to GL and Metal renderers

**Files:**
- Modify: `platform/linux/src/GLCellBuilder.cpp`
- Modify: `platform/macos/src/MetalCellBuilder.mm`

- [ ] **Step 1: Add GhostText struct and setGhostText() to GL renderer**

Follow the same pattern as Task 9 for the Linux GL renderer. Check `platform/linux/include/` for the renderer header and impl files.

- [ ] **Step 2: Add ghost text rendering pass to GLCellBuilder.cpp**

Same logic as Task 10, adapted to GL renderer's color format.

- [ ] **Step 3: Add GhostText struct and setGhostText() to Metal renderer**

Follow the same pattern for macOS Metal renderer.

- [ ] **Step 4: Add ghost text rendering pass to MetalCellBuilder.mm**

Same logic, adapted to Metal renderer's CellInstance format (may use uint32_t colors).

- [ ] **Step 5: Build on each platform to verify (or cross-compile check)**

- [ ] **Step 6: Commit**

```bash
git add platform/linux/ platform/macos/
git commit -m "feat: add ghost text rendering to GL and Metal renderers"
```

---

## Chunk 6: Key Binding Integration

### Task 12: Add CompletionManager dependency to InputHandler

**Files:**
- Modify: `core/include/termcore/input_handler.h:23-39`
- Modify: `core/src/input_handler.cpp`

- [ ] **Step 1: Add getCompletionManager to InputHandler::Deps**

In `core/include/termcore/input_handler.h`, add to the Deps struct (after line 38):

```cpp
std::function<CompletionManager*()> getCompletionManager;
```

Add the include at the top:
```cpp
#include "termcore/completion_manager.h"
```

- [ ] **Step 2: Intercept Right Arrow for ghost text acceptance**

In `core/src/input_handler.cpp`, in the `onKeyEvent()` method, add ghost text handling **before** the keybinding lookup (before line 42). After the vi copy mode check:

```cpp
// Ghost text acceptance (note: member name is d_, not deps_)
if (d_.getCompletionManager) {
    auto* compMgr = d_.getCompletionManager();
    if (compMgr && compMgr->hasGhostText()) {
        Screen* screen = d_.activeScreen();
        bool cursorAtEnd = false;
        if (screen && screen->promptState() == PromptState::Input) {
            // Check if cursor is at end of input (no non-space chars after cursor)
            int curCol = screen->cursorCol();
            bool hasTextAfter = false;
            for (int c = curCol; c < screen->cols(); ++c) {
                const auto& cell = screen->cellAt(screen->cursorRow(), c);
                if (cell.codepoint != ' ' && cell.codepoint != 0) {
                    hasTextAfter = true;
                    break;
                }
            }
            cursorAtEnd = !hasTextAfter;
        }

        // Right Arrow: accept ghost text (Ctrl+Right = word, Right = full)
        if (cursorAtEnd && e.keycode == 0xF703) {
            if (e.modifiers & ModCtrl) {
                std::string text = compMgr->acceptWord();
                if (!text.empty()) {
                    d_.sendPtyData(text.c_str(), text.size());
                    d_.needsRender() = true;
                    return;
                }
            } else if (e.modifiers == 0) {
                std::string text = compMgr->acceptFull();
                if (!text.empty()) {
                    d_.sendPtyData(text.c_str(), text.size());
                    d_.needsRender() = true;
                    return;
                }
            }
        }

        // Escape clears ghost text (0xF70A is the platform Escape keycode)
        if (e.keycode == 0xF70A && !d_.searchCtrl->isActive()) {
            compMgr->clear();
            d_.needsRender() = true;
            return;
        }
    }
}
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --target termcore`

- [ ] **Step 4: Commit**

```bash
git add core/include/termcore/input_handler.h core/src/input_handler.cpp
git commit -m "feat: intercept Right Arrow and Escape for ghost text acceptance"
```

---

## Chunk 7: Lua Completion Module

### Task 13: Create LuaCompletionModule

**Files:**
- Create: `core/src/lua_bindings/lua_completion_module.h`
- Create: `core/src/lua_bindings/lua_completion_module.cpp`

- [ ] **Step 1: Write module header**

```cpp
// core/src/lua_bindings/lua_completion_module.h
#ifndef TERMCORE_LUA_COMPLETION_MODULE_H
#define TERMCORE_LUA_COMPLETION_MODULE_H

#include "termcore/lua_module.h"
#include <memory>

namespace termcore {

class CompletionManager;

class LuaCompletionModule : public ILuaModule {
public:
    explicit LuaCompletionModule(CompletionManager* manager);

    std::string_view moduleName() const override { return "completion"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneWrite;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    CompletionManager* manager_;
    std::vector<std::string> luaProviderNames_;
};

} // namespace termcore

#endif // TERMCORE_LUA_COMPLETION_MODULE_H
```

- [ ] **Step 2: Write module implementation**

```cpp
// core/src/lua_bindings/lua_completion_module.cpp
#include "lua_completion_module.h"
#include "termcore/completion_manager.h"
#include <sol/sol.hpp>

namespace termcore {

LuaCompletionModule::LuaCompletionModule(CompletionManager* manager)
    : manager_(manager) {}

void LuaCompletionModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    sol::table completion = terminal["completion"].get_or_create<sol::table>();

    completion["register_provider"] = [this](const std::string& name,
                                              sol::table options) {
        if (!manager_) return;

        int priority = options.get_or("priority", 50);
        bool async = options.get_or("async", false);

        sol::optional<sol::protected_function> onInputOpt =
            options["on_input"];

        if (!async && onInputOpt) {
            auto onInput = std::make_shared<sol::protected_function>(*onInputOpt);

            CompletionManager::Provider prov;
            prov.name = name;
            prov.priority = priority;
            prov.getSuggestion = [onInput](const std::string& input,
                                            const std::string& cwd) -> std::string {
                sol::table ctx = onInput->lua_state().create_table();
                ctx["text"] = input;
                ctx["cwd"] = cwd;
                auto result = (*onInput)(ctx);
                if (result.valid()) {
                    sol::object val = result;
                    if (val.is<std::string>()) {
                        return val.as<std::string>();
                    }
                }
                return "";
            };
            manager_->registerProvider(std::move(prov));
        } else if (async) {
            // Async provider: register with no sync getSuggestion
            CompletionManager::Provider prov;
            prov.name = name;
            prov.priority = priority;
            prov.getSuggestion = nullptr;  // no sync callback
            manager_->registerProvider(std::move(prov));

            // Async on_input is not called automatically in this version.
            // Async providers should use external mechanisms (timers, etc.)
            // to call terminal.completion.set_suggestion() with results.
            // Future: hook into CompletionManager::onInputChanged to
            // dispatch async on_input callbacks.
        }
        luaProviderNames_.push_back(name);
    };

    completion["remove_provider"] = [this](const std::string& name) {
        if (!manager_) return;
        manager_->removeProvider(name);
        luaProviderNames_.erase(
            std::remove(luaProviderNames_.begin(), luaProviderNames_.end(), name),
            luaProviderNames_.end());
    };

    completion["set_suggestion"] = [this](const std::string& providerName,
                                           const std::string& text) {
        if (!manager_) return;
        manager_->setSuggestion(providerName, text);
    };

    completion["set_enabled"] = [this](bool enabled) {
        if (!manager_) return;
        manager_->setEnabled(enabled);
    };
}

void LuaCompletionModule::clearCallbacks() {
    if (manager_) {
        for (const auto& name : luaProviderNames_) {
            manager_->removeProvider(name);
        }
    }
    luaProviderNames_.clear();
}

} // namespace termcore
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/lua_bindings/lua_completion_module.cpp` to core library sources.

- [ ] **Step 4: Build to verify**

Run: `cmake --build build --target termcore`

- [ ] **Step 5: Commit**

```bash
git add core/src/lua_bindings/lua_completion_module.h core/src/lua_bindings/lua_completion_module.cpp core/CMakeLists.txt
git commit -m "feat: add LuaCompletionModule for terminal.completion.* Lua API"
```

---

## Chunk 8: Integration (TerminalController + Platform)

### Task 14: Wire CompletionManager into TerminalController

**Files:**
- Modify: `core/include/termcore/terminal_controller.h`
- Modify: `core/src/terminal_controller.cpp`

- [ ] **Step 1: Add CompletionManager member to TerminalController**

In `core/include/termcore/terminal_controller.h`:

Add include:
```cpp
#include "termcore/completion_manager.h"
```

Add to private members (around line 132):
```cpp
CompletionManager completionManager_;
```

Add public accessor:
```cpp
CompletionManager& completionManager() { return completionManager_; }
```

- [ ] **Step 2: Register LuaCompletionModule in TerminalController**

In `core/src/terminal_controller.cpp`, add include:
```cpp
#include "lua_bindings/lua_completion_module.h"
```

In the initialization code where other Lua modules are registered (find the `registerModule` calls), add:
```cpp
luaEngine_->registerModule(
    std::make_shared<LuaCompletionModule>(&completionManager_));
```

- [ ] **Step 3: Wire onCommandCapture_ callback for history**

In the TerminalController initialization (where Screen or TabController is set up), set the `onCommandCapture_` callback on the active screen to feed the history provider:

```cpp
screen->setCommandCaptureCallback([this](const std::string& cmd) {
    completionManager_.historyProvider().addEntry(cmd);
});
```

This needs to be set whenever a new screen/tab is created. Look for where screens are initialized in TabController and add this callback.

- [ ] **Step 4: Wire getCompletionManager in InputHandler::Deps**

Where the InputHandler is constructed and its Deps are set up, add:
```cpp
deps.getCompletionManager = [this]() -> CompletionManager* {
    return &completionManager_;
};
```

- [ ] **Step 5: Update completion on input changes**

In the TerminalController's tick/polling loop (wherever `needsRender_` is checked or screen dirty state is processed), add completion update logic. **Important:** Only call `onInputChanged` when the input actually changes to avoid unnecessary provider queries every frame:

```cpp
Screen* screen = activeScreen();
if (screen && screen->promptState() == PromptState::Input) {
    std::string input = screen->currentInputText();
    if (input != lastCompletionInput_) {
        lastCompletionInput_ = input;
        completionManager_.onInputChanged(input, screen->workingDirectory());
        needsRender_ = true;
    }
} else {
    if (completionManager_.hasGhostText()) {
        completionManager_.clear();
        lastCompletionInput_.clear();
        needsRender_ = true;
    }
}
```

Also add `std::string lastCompletionInput_;` to TerminalController's private members in the header.

- [ ] **Step 6: Build and verify**

Run: `cmake --build build --target BreadTerminal`

- [ ] **Step 7: Commit**

```bash
git add core/include/termcore/terminal_controller.h core/src/terminal_controller.cpp
git commit -m "feat: wire CompletionManager into TerminalController with history and Lua module"
```

### Task 15: Pass ghost text from TerminalController to renderer (Platform layer)

**Files:**
- Modify: `platform/windows/src/TerminalWindowState.cpp`

- [ ] **Step 1: Pass ghost text to renderer in renderFrame()**

In `TerminalWindowState::renderFrame()`, in the state update section (around lines 304-348, where selection and search highlights are updated), add:

```cpp
// Ghost text for autocomplete
{
    auto& cm = controller->completionManager();
    if (cm.hasGhostText()) {
        Screen* screen = controller->activeScreen();
        renderer->setGhostText(cm.ghostText(),
                                screen->cursorRow(),
                                screen->cursorCol());
    } else {
        renderer->setGhostText("", -1, -1);
    }
}
```

- [ ] **Step 2: Do the same for Linux and macOS platform layers**

Find the equivalent renderFrame/render loop in:
- `platform/linux/src/` (likely `TerminalWindowState.cpp` or similar)
- `platform/macos/src/` (likely `TerminalView.mm` or similar)

Apply the same ghost text passing pattern.

- [ ] **Step 3: Build and test**

Run: `cmake --build build --target BreadTerminal`

- [ ] **Step 4: Commit**

```bash
git add platform/
git commit -m "feat: pass ghost text from CompletionManager to renderer in platform layers"
```

### Task 16: End-to-end manual testing

- [ ] **Step 1: Build the full application**

Run: `cmake --build build`

- [ ] **Step 2: Test SGR 2 (dim)**

In the terminal, run:
```bash
echo -e "\e[2mThis should be dim\e[0m and this is normal"
```
Expected: "This should be dim" is rendered at 50% brightness.

- [ ] **Step 3: Test SGR 22 reset**

```bash
echo -e "\e[1;2mBold+Dim\e[22mNeither\e[0m"
```
Expected: "Bold+Dim" is bold and dim, "Neither" has neither.

- [ ] **Step 4: Test ghost text autocomplete**

1. Type `echo hello` and press Enter
2. Type `echo` — ghost text "hello" should appear after the cursor in dim color
3. Press Right Arrow — the full suggestion is accepted
4. Type `ec` — ghost text should show the completion
5. Press Ctrl+Right — accept one word at a time
6. Press Escape — ghost text should disappear

- [ ] **Step 5: Test Lua API**

In config.lua or via command palette:
```lua
terminal.completion.register_provider("test", {
    priority = 50,
    on_input = function(ctx)
        if ctx.text:sub(1, 2) == "cd" then
            return "cd /home/user"
        end
        return nil
    end
})
```
Type `cd` — ghost text should show ` /home/user`.

- [ ] **Step 6: Commit any fixes needed**

```bash
git add -A
git commit -m "fix: address issues found during end-to-end testing"
```
