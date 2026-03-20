#ifndef TERMCORE_TAB_CONTROLLER_H
#define TERMCORE_TAB_CONTROLLER_H

#include "termcore/config.h"
#include "termcore/mux.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

namespace termcore {

struct PaneState {
    PaneId id = kInvalidPane;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;
};

// Called when a new pane needs a PTY
using PtyFactory = std::function<std::unique_ptr<Pty>(int rows, int cols)>;

class TabController {
public:
    TabController(std::unique_ptr<Mux> mux, WorkspaceId wsId,
                  PtyFactory ptyFactory, const Config& config);

    // Tab operations
    void createTab(int rows, int cols);
    void closeTab();
    void nextTab();
    void prevTab();
    void switchToTab(int index);

    // Pane operations
    void splitRight(int rows, int cols);
    void splitDown(int rows, int cols);
    void closePane();

    // Active pane access
    Screen* activeScreen();
    Pty* activePty();
    PaneState* activePane();
    PaneState* paneById(PaneId id);

    // Iterate all panes
    template<typename F>
    void forEachPane(F&& fn) {
        for (auto& [id, ps] : panes_) {
            fn(*ps);
        }
    }

    // Tab bar info for renderer
    struct TabInfo { std::string title; bool active; };
    std::vector<TabInfo> tabBarInfo() const;
    int tabCount() const;

    // Pane lifecycle
    PaneId createPaneState(int rows, int cols);
    void destroyPaneState(PaneId id);
    bool hasAnyAlivePty() const;
    void syncActivePointers();

    // Poll all PTYs, returns true if any data was read
    bool pollAllPtys();

    // Resize all panes
    void resizeAllPanes(int rows, int cols);

    // Dead pane cleanup - returns true if window should close
    bool cleanupDeadPanes();

    Mux* mux() { return mux_.get(); }
    WorkspaceId workspaceId() const { return wsId_; }

private:
    std::unique_ptr<Mux> mux_;
    WorkspaceId wsId_;
    PtyFactory ptyFactory_;

    std::unordered_map<PaneId, std::unique_ptr<PaneState>> panes_;
    PaneId nextPaneId_ = 1;

    // Active pane cache
    Screen* activeScreen_ = nullptr;
    Pty* activePty_ = nullptr;

    Config config_;
};

} // namespace termcore
#endif
