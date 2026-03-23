#ifndef TERMCORE_SELECTION_MANAGER_H
#define TERMCORE_SELECTION_MANAGER_H

#include "termcore/screen.h"
#include <string>

namespace termcore {

class SelectionManager {
public:
    struct GridPos { int row = 0; int col = 0; };

    void onMouseDown(int px, int py, float cellW, float cellH,
                     int offsetX, int offsetY);
    void onMouseMove(int px, int py, float cellW, float cellH,
                     int offsetX, int offsetY);
    void onMouseUp(int px, int py, float cellW, float cellH,
                   int offsetX, int offsetY);
    void onDoubleClick(int px, int py, float cellW, float cellH,
                       int offsetX, int offsetY, const Screen& screen);
    void selectAll(int rows, int cols);
    void clear();

    bool hasSelection() const { return hasSelection_; }
    bool isDragging() const { return isDragging_; }
    GridPos start() const { return start_; }
    GridPos end() const { return end_; }

    /// Adjust selection coordinates when viewport scrolls.
    /// rowDelta > 0 means content moved down (scroll up), selection rows increase.
    void adjustForScroll(int rowDelta);

    std::string getSelectedText(const Screen& screen) const;

private:
    GridPos pixelToGrid(int px, int py, float cellW, float cellH,
                        int offsetX, int offsetY, int maxRows = 0, int maxCols = 0) const;

    GridPos start_;
    GridPos end_;
    bool hasSelection_ = false;
    bool isDragging_ = false;
};

} // namespace termcore
#endif
