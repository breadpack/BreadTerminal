#ifndef TERMCORE_TAB_CONTROLLER_H
#define TERMCORE_TAB_CONTROLLER_H

#include "termcore/agent.h"
#include "termcore/config.h"
#include "termcore/mux.h"
#include "termcore/profile.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

namespace termcore {

struct TabTitleInfo;  // defined in lua_bindings/lua_tab_module.h

struct PaneState {
    PaneId id = kInvalidPane;
    std::string profile_id;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;
};

// Called when a new pane needs a PTY
using PtyFactory = std::function<std::unique_ptr<Pty>(const Profile& profile, int rows, int cols)>;

class TabController {
public:
    TabController(std::unique_ptr<Mux> mux, WorkspaceId wsId,
                  PtyFactory ptyFactory, const Config& config);

    // Tab operations
    void createTab(int rows, int cols, const std::string& profile_id = "");
    void closeTab();
    void nextTab();
    void prevTab();
    void switchToTab(int index);

    // Pane operations
    void splitRight(int rows, int cols, const std::string& profile_id = "");
    void splitDown(int rows, int cols, const std::string& profile_id = "");
    void closePane();

    // Active pane access
    Screen* activeScreen();
    const Screen* activeScreen() const;
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
    struct TabInfo {
        std::string title;
        std::string icon_name;       // OSC 1 icon name (e.g. process indicator)
        std::string process_name;    // foreground process name (from PTY)
        bool active = false;
        bool has_unread = false;     // unread output in background tab
        bool needs_attention = false; // process requesting attention
        AgentState agent_state = AgentState::Inactive;  // agent lifecycle state
        float progress_value = -1.0f;                   // -1 = hidden, 0.0-1.0 = percentage
    };
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

    void setProfileManager(ProfileManager* mgr) { profileMgr_ = mgr; }
    void setAgentTracker(const AgentTracker* tracker) { agent_tracker_ = tracker; }

    // Callback invoked after each new pane's Screen is created.
    using OnPaneCreatedFn = std::function<void(Screen*)>;
    void setOnPaneCreated(OnPaneCreatedFn fn) { onPaneCreated_ = std::move(fn); }

    // Lua callback for custom tab title formatting.
    using TitleFormatFn = std::function<std::string(const TabTitleInfo&)>;
    void setTitleFormatCallback(TitleFormatFn fn) { titleFormatFn_ = std::move(fn); }

    // Per-tab title override set by Lua terminal.tab.set_title(tab_id, title).
    // tab_id is 0-based index into tabBarInfo().
    void setCustomTitle(int tab_index, const std::string& title);

    // Retrieve per-tab custom title (empty string = no override).
    std::string customTitle(int tab_index) const;

    // Process icon mapping for Lua defaults/icons.lua
    void setProcessIcon(const std::string& process, const std::string& icon);
    std::string getProcessIcon(const std::string& process) const;

private:
    std::unique_ptr<Mux> mux_;
    WorkspaceId wsId_;
    PtyFactory ptyFactory_;
    ProfileManager* profileMgr_ = nullptr;
    const AgentTracker* agent_tracker_ = nullptr;
    std::string pendingProfileId_;

    std::unordered_map<PaneId, std::unique_ptr<PaneState>> panes_;
    PaneId nextPaneId_ = 1;

    // Active pane cache
    Screen* activeScreen_ = nullptr;
    Pty* activePty_ = nullptr;

    Config config_;
    mutable TitleFormatFn titleFormatFn_;
    mutable bool inTitleFormat_ = false;  // reentrancy guard
    std::unordered_map<int, std::string> customTitles_;  // tab_index -> override title
    std::unordered_map<std::string, std::string> processIcons_;
    OnPaneCreatedFn onPaneCreated_;
};

} // namespace termcore
#endif
