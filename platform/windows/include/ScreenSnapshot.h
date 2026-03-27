#ifndef BREAD_SCREEN_SNAPSHOT_H
#define BREAD_SCREEN_SNAPSHOT_H

#if defined(_WIN32)

#include "termcore/dynamic_colors.h"
#include "termcore/kitty_graphics.h"
#include "termcore/screen.h"
#include "termcore/term_cell.h"

#include <cstdint>
#include <string>
#include <vector>

/// Deep copy of Screen cell data for lock-free rendering.
///
/// The render thread captures a ScreenSnapshot under the shared SRWLock,
/// then releases the lock before the expensive HarfBuzz shaping pass.
/// This eliminates lock contention between the render thread and the
/// main thread's exclusive-lock input processing.
struct ScreenSnapshot {
    int rows_ = 0;
    int cols_ = 0;
    std::vector<termcore::TermCell> cells_;  // flat array: cells_[row * cols_ + col]
    termcore::DynamicColors colors_;
    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = false;
    termcore::CursorShape cursorShape_ = termcore::CursorShape::Block;
    int viewportOffset_ = 0;
    size_t scrollbackSize_ = 0;
    int64_t viewportTopAbsoluteRow_ = 0;

    // Kitty graphics: snapshot the manager pointer and scalar values.
    // The pointer remains valid because kittyGraphics lives on Screen which
    // outlives the render frame. The exclusive lock is only held briefly by
    // the main thread for input, not during object destruction.
    const termcore::KittyGraphicsManager* kittyGfx_ = nullptr;

    // Dirty row state (copied from Screen for dirty row caching in renderer)
    bool dirty_ = true;
    std::vector<bool> dirtyRows_;

    // --- API matching Screen's const interface ---
    int rows() const { return rows_; }
    int cols() const { return cols_; }

    const termcore::TermCell& cellAt(int row, int col) const {
        return cells_[row * cols_ + col];
    }

    const termcore::DynamicColors& dynamicColors() const { return colors_; }
    int cursorRow() const { return cursorRow_; }
    int cursorCol() const { return cursorCol_; }
    bool cursorVisible() const { return cursorVisible_; }
    termcore::CursorShape cursorShape() const { return cursorShape_; }
    int viewportOffset() const { return viewportOffset_; }
    size_t scrollbackSize() const { return scrollbackSize_; }
    int64_t viewportTopAbsoluteRow() const { return viewportTopAbsoluteRow_; }

    const termcore::KittyGraphicsManager& kittyGraphics() const { return *kittyGfx_; }
    bool hasKittyGraphics() const { return kittyGfx_ != nullptr; }

    // Dirty row API (matching Screen's interface for template compatibility)
    bool isDirty() const { return dirty_; }
    bool isRowDirty(int row) const {
        if (row >= 0 && row < static_cast<int>(dirtyRows_.size()))
            return dirtyRows_[row];
        return true;
    }

    /// Capture render-relevant data from a Screen.
    /// Incremental: only copies dirty rows when grid dimensions haven't changed.
    /// On a 4K terminal (e.g., 250x80 = 20,000 cells), this reduces lock-held
    /// copy time from ~20K cells to just the 1-2 rows that actually changed.
    void captureFrom(const termcore::Screen& screen) {
        int newRows = screen.rows();
        int newCols = screen.cols();

        colors_ = screen.dynamicColors();
        cursorRow_ = screen.cursorRow();
        cursorCol_ = screen.cursorCol();
        cursorVisible_ = screen.cursorVisible();
        cursorShape_ = screen.cursorShape();
        viewportOffset_ = screen.viewportOffset();
        scrollbackSize_ = screen.scrollbackSize();
        viewportTopAbsoluteRow_ = screen.viewportTopAbsoluteRow();
        kittyGfx_ = &screen.kittyGraphics();

        // Copy dirty state for row-level caching in renderer
        dirty_ = screen.isDirty();
        dirtyRows_.resize(newRows);
        for (int r = 0; r < newRows; ++r) {
            dirtyRows_[r] = screen.isRowDirty(r);
        }

        bool gridChanged = (newRows != rows_ || newCols != cols_);
        rows_ = newRows;
        cols_ = newCols;

        const int total = rows_ * cols_;
        cells_.resize(total);

        if (gridChanged || !dirty_) {
            // Full copy on resize or when not dirty (e.g., selection change)
            for (int r = 0; r < rows_; ++r) {
                for (int c = 0; c < cols_; ++c) {
                    cells_[r * cols_ + c] = screen.cellAt(r, c);
                }
            }
        } else {
            // Incremental: only copy dirty rows
            for (int r = 0; r < rows_; ++r) {
                if (!dirtyRows_[r]) continue;
                int base = r * cols_;
                for (int c = 0; c < cols_; ++c) {
                    cells_[base + c] = screen.cellAt(r, c);
                }
            }
        }
    }
};

#endif // _WIN32
#endif // BREAD_SCREEN_SNAPSHOT_H
