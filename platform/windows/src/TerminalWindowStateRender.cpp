#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "RenderSnapshot.h"
#include "TerminalAccessibility.h"

#include <algorithm>
#include <cctype>
#include <thread>

using termcore::D3DTextRenderer;

// --- Render thread lifecycle ---

void TerminalWindowState::initRenderThread() {
    if (renderRunning_) return;

    invalidateEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset
    renderPausedEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); // manual-reset

    renderRunning_ = true;
    renderThread_ = std::thread([this]() { renderThreadFunc(); });
}

void TerminalWindowState::stopRenderThread() {
    if (!renderRunning_) return;

    renderRunning_ = false;
    if (invalidateEvent_) SetEvent(invalidateEvent_);

    if (renderThread_.joinable()) {
        renderThread_.join();
    }

    if (invalidateEvent_) { CloseHandle(invalidateEvent_); invalidateEvent_ = nullptr; }
    if (renderPausedEvent_) { CloseHandle(renderPausedEvent_); renderPausedEvent_ = nullptr; }
}

void TerminalWindowState::signalInvalidate() {
    if (invalidateEvent_) SetEvent(invalidateEvent_);
}

void TerminalWindowState::renderThreadFunc() {
    ScreenSnapshot screenCopy;  // reused across frames to avoid reallocation

    while (renderRunning_) {
        // Wait for invalidation signal or cursor blink timeout (~500ms)
        DWORD result = WaitForSingleObject(invalidateEvent_, 500);

        if (!renderRunning_) break;

        // Phase 1: Quick snapshot under shared lock.
        // Copy Screen cell data so we can release the lock before expensive shaping.
        AcquireSRWLockShared(&renderLock_);

        RenderSnapshot snap;
        if (controller && renderer) {
            snap = captureRenderSnapshot();
            pushRendererState(snap);
        }

        if (!snap.screen) {
            ReleaseSRWLockShared(&renderLock_);
            continue;
        }

        // If woken by invalidation event, content has changed and needs rebuild.
        // Timeout means only cursor blink toggled (no data arrived).
        if (result == WAIT_OBJECT_0) {
            renderer->markContentDirty();
        } else {
            cursorBlinkOn = !cursorBlinkOn;
            snap.cursorBlinkOn = cursorBlinkOn;
            renderer->setCursorBlink(cursorBlinkOn);
        }

        // Decay notification ring intensities (uses pane_ring_states_, not Screen)
        {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - last_frame_time_).count();
            if (dt > 0.5f) dt = 0.016f;
            last_frame_time_ = now;

            bool anyRingActive = false;
            for (auto& [pane, ring] : pane_ring_states_) {
                if (ring.intensity > 0.0f) {
                    ring.intensity -= dt * 0.33f;
                    if (ring.intensity < 0.0f) ring.intensity = 0.0f;
                    if (ring.intensity > 0.0f) anyRingActive = true;
                }
            }
            if (anyRingActive) signalInvalidate();
        }

        // Deep-copy Screen cell data (fast memcpy-like operation)
        screenCopy.captureFrom(*snap.screen);

        // Clear dirty flags after capturing snapshot
        snap.screen->clearDirty();

        // Release shared lock BEFORE expensive HarfBuzz shaping.
        // This allows the main thread to acquire the exclusive lock for
        // input processing without waiting for text shaping to complete.
        ReleaseSRWLockShared(&renderLock_);

        // Phase 2: Expensive HarfBuzz shaping WITHOUT any lock.
        // prepareFrame reads only from the copied ScreenSnapshot.
        renderer->prepareFrame(screenCopy);

        // Phase 3: GPU work (already was lock-free) — atlas upload, buffer map, draw calls.
        renderer->submitFrame();

        if (swapChain) {
            UINT syncInterval = snap.inLiveResize ? 0 : 1;
            swapChain->Present(syncInterval, 0);
        }
    }
}

// --- PTY / rendering ---

void TerminalWindowState::pollPty() {
    // NOTE: Caller must hold exclusive renderLock_ (or call via withWriteLock)
    if (!controller) return;

    controller->pollPty();
    if (controller->needsRender()) {
        needsRender = true;
        controller->flushPendingUrlScan();
        controller->clearNeedsRender();
        if (renderer) renderer->markContentDirty();

        // Notify screen readers of content change
        if (accessibilityProvider) {
            accessibilityProvider->setScreen(controller->activeScreen());
            accessibilityProvider->notifyTextChanged();
        }
    }
}

RenderSnapshot TerminalWindowState::captureRenderSnapshot() {
    RenderSnapshot snap;

    termcore::Screen* screen = controller->activeScreen();
    snap.screen = screen;

    // IME overlay: pass composition text to renderer without mutating Screen
    bool imeComposing = !imeCompositionText.empty();
    if (imeComposing && screen) {
        snap.ime.text = imeCompositionText;
        snap.ime.row = screen->cursorRow();
        snap.ime.col = screen->cursorCol();
        // Inverted colors: fg = background color, bg = foreground color
        snap.ime.fg_color = screen->dynamicColors().background;
        snap.ime.bg_color = screen->dynamicColors().foreground;
    }

    snap.cursorBlinkOn = cursorBlinkOn;
    snap.showResizeOverlay = showResizeOverlay;
    snap.resizeOverlayCols = resizeOverlayCols;
    snap.resizeOverlayRows = resizeOverlayRows;
    snap.inLiveResize = inLiveResize;

    return snap;
}

/// Push all controller state to renderer setters. Must be called under shared lock
/// (or from a context where controller state is stable).
void TerminalWindowState::pushRendererState(const RenderSnapshot& snap) {
    if (!renderer || !controller) return;

    termcore::Screen* screen = snap.screen;
    if (!screen) return;

    // Push state from controller to renderer via setters
    updateTabBar();
    updateSidebar();
    updateRendererSelection();
    updateCommandPalette();
    updateProfileDropdown();

    // Update search highlights on renderer
    if (controller->search().isActive()) {
        const auto& matches = controller->search().search().matches();
        int currentIdx = controller->search().currentMatch();
        std::vector<termcore::D3DTextRenderer::SearchHighlight> highlights;
        highlights.reserve(matches.size());
        for (const auto& m : matches) {
            highlights.push_back({m.row, m.start_col, m.end_col});
        }
        renderer->setSearchHighlights(highlights, currentIdx);
    } else {
        renderer->setSearchHighlights({}, -1);
    }

    // Update URL highlights on renderer
    {
        auto hints = controller->urlHighlight().getRenderHints();
        uint32_t urlColor = controller->urlHighlight().urlColor();
        std::vector<termcore::D3DTextRenderer::UrlHighlight> urlHighlights;
        urlHighlights.reserve(hints.size());
        for (const auto& h : hints) {
            urlHighlights.push_back({h.row, h.start_col, h.end_col, h.hovered, urlColor});
        }
        renderer->setUrlHighlights(urlHighlights);
    }

    // Update background opacity and font ligatures on renderer
    {
        const auto& cfg = controller->config();
        renderer->setBackgroundOpacity(cfg.background_opacity);
        renderer->setFontLigatures(cfg.font_ligatures);
    }

    // Ghost text for autocomplete
    {
        auto& cm = controller->completionManager();
        if (cm.hasGhostText()) {
            renderer->setGhostText(cm.ghostText(),
                                    screen->cursorRow(),
                                    screen->cursorCol());
        } else {
            renderer->setGhostText("", -1, -1);
        }
    }

    // IME state: hide cursor and set overlay for virtual rendering
    bool imeComposing = !snap.ime.text.empty();
    renderer->setIMEActive(imeComposing);
    renderer->setImeOverlay(snap.ime);
}

void TerminalWindowState::renderFrame() {
    if (!renderer || !controller) return;

    termcore::Screen* screen = controller->activeScreen();
    if (!screen) return;

    // Decay notification ring intensities
    {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_frame_time_).count();
        if (dt > 0.5f) dt = 0.016f; // clamp on first frame or long pauses
        last_frame_time_ = now;

        bool anyRingActive = false;
        for (auto& [pane, ring] : pane_ring_states_) {
            if (ring.intensity > 0.0f) {
                ring.intensity -= dt * 0.33f;
                if (ring.intensity < 0.0f) ring.intensity = 0.0f;
                if (ring.intensity > 0.0f) anyRingActive = true;
            }
        }
        if (anyRingActive) needsRender = true;
    }

    RenderSnapshot snap = captureRenderSnapshot();
    pushRendererState(snap);

    // Render without mutating Screen cells
    renderer->render(*screen);

    if (swapChain) {
        UINT syncInterval = snap.inLiveResize ? 0 : 1;
        swapChain->Present(syncInterval, 0);
    }

    if (hwnd && screen) {
        int offset = screen->viewportOffset();
        if (offset > 0) {
            std::wstring title = L"BreadTerminal [scrollback: +"
                + std::to_wstring(offset) + L" lines]";
            SetWindowTextW(hwnd, title.c_str());
        } else {
            SetWindowTextW(hwnd, L"BreadTerminal");
        }
    }
}

void TerminalWindowState::renderFrame(const RenderSnapshot& snap) {
    if (!renderer || !controller) return;

    termcore::Screen* screen = snap.screen;
    if (!screen) return;

    // Decay notification ring intensities
    {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_frame_time_).count();
        if (dt > 0.5f) dt = 0.016f; // clamp on first frame or long pauses
        last_frame_time_ = now;

        bool anyRingActive = false;
        for (auto& [pane, ring] : pane_ring_states_) {
            if (ring.intensity > 0.0f) {
                ring.intensity -= dt * 0.33f;
                if (ring.intensity < 0.0f) ring.intensity = 0.0f;
                if (ring.intensity > 0.0f) anyRingActive = true;
            }
        }
        if (anyRingActive) signalInvalidate();
    }

    // Build cell buffer + issue draw calls.
    // When called from renderThreadFunc, shared lock is held so Screen is stable.
    renderer->render(*screen);

    // Present() is handled by the caller (renderThreadFunc moves it outside the lock
    // to avoid blocking the main thread during vsync).
}

// --- UI state helpers (tab bar, sidebar, command palette, selection) ---

void TerminalWindowState::updateTabBar() {
    if (!renderer || !controller) return;

    auto tabs = controller->tabBarInfo();
    const auto& config = controller->config();
    float cellH = controller->cellHeight();

    D3DTextRenderer::TabBarInfo tabInfo;
    tabInfo.visible = config.tab_bar_always_visible || static_cast<int>(tabs.size()) > 1;
    tabInfo.height_scale = config.tab_bar_height;

    // Tab bar uses same background as terminal area
    tabInfo.bg_color = config.background;
    tabInfo.active_bg_color = config.background;
    tabInfo.inactive_bg_color = config.background;
    tabInfo.fg_color = config.foreground;
    tabInfo.accent_color = config.palette[4] ? config.palette[4] : 0x007acc;
    tabInfo.process_icon_map = &config.tab_process_icons;

    for (size_t i = 0; i < tabs.size(); ++i) {
        D3DTextRenderer::TabInfo ti;
        ti.title = tabs[i].title;
        ti.icon_name = tabs[i].icon_name;
        ti.process_name = tabs[i].process_name;
        ti.active = tabs[i].active;
        ti.has_unread = tabs[i].has_unread;
        ti.needs_attention = tabs[i].needs_attention;
        ti.agent_state = static_cast<int>(tabs[i].agent_state);
        ti.progress_value = tabs[i].progress_value;
        tabInfo.tabs.push_back(ti);
    }

    renderer->setTabBar(tabInfo);
}

void TerminalWindowState::updateCommandPalette() {
    if (!renderer || !controller) return;

    auto& cp = controller->commandPalette();
    const auto& config = controller->config();
    D3DTextRenderer::CommandPaletteInfo info;
    info.visible = cp.isOpen();
    info.width_percent = config.command_palette_width_percent;
    info.max_items = config.command_palette_max_items;
    info.backdrop_opacity = config.command_palette_backdrop_opacity;

    if (info.visible) {
        info.query = cp.query();
        info.selectedIndex = cp.selectedIndex();

        const auto& filtered = cp.filteredCommands();
        int maxItems = (std::min)(static_cast<int>(filtered.size()),
                                  info.max_items);
        for (int i = 0; i < maxItems; ++i) {
            D3DTextRenderer::CommandPaletteInfo::Item item;
            item.name = filtered[i].name;
            item.shortcut_hint = filtered[i].shortcut_hint;
            info.items.push_back(std::move(item));
        }
    }

    renderer->setCommandPalette(info);
}

void TerminalWindowState::updateProfileDropdown() {
    if (!renderer || !controller) return;

    auto& pd = controller->profileDropdown();
    D3DTextRenderer::ProfileDropdownInfo info;
    info.visible = pd.isOpen();

    if (info.visible) {
        info.selectedIndex = pd.selectedIndex();

        const auto& items = pd.items();
        int maxItems = (std::min)(static_cast<int>(items.size()),
                                  termcore::ProfileDropdown::kMaxVisibleItems);
        for (int i = 0; i < maxItems; ++i) {
            D3DTextRenderer::ProfileDropdownInfo::Item item;
            item.name = items[i].name;
            item.icon = items[i].icon;
            info.items.push_back(std::move(item));
        }
    }

    renderer->setProfileDropdown(info);
}

// Static helper: flatten a subagent tree into render entries.
static void flattenSubagentTree(
        const termcore::SubagentNode& node, int depth,
        std::vector<termcore::D3DTextRenderer::SidebarSubagentEntry>& out) {
    termcore::D3DTextRenderer::SidebarSubagentEntry sub;
    sub.name = node.description.empty() ? node.agent_type : node.description;
    sub.state = static_cast<int>(node.state);
    sub.indent_level = depth;

    // Reuse AgentTracker::stateToString for the status label
    auto stateStr = termcore::AgentTracker::stateToString(node.state);
    if (node.state == termcore::AgentState::Exited) {
        sub.status = "[Done]";
    } else if (!stateStr.empty() && stateStr != "inactive") {
        // Capitalize first letter: "running" → "[Running]"
        stateStr[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(stateStr[0])));
        // Normalize underscore-separated names: "tool_use" → "ToolUse"
        for (size_t i = 0; i < stateStr.size(); ++i) {
            if (stateStr[i] == '_' && i + 1 < stateStr.size()) {
                stateStr.erase(i, 1);
                stateStr[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(stateStr[i])));
            }
        }
        sub.status = "[" + stateStr + "]";
    }

    out.push_back(std::move(sub));
    for (const auto& child : node.children) {
        flattenSubagentTree(child, depth + 1, out);
    }
}

void TerminalWindowState::updateSidebar() {
    if (!renderer || !controller) return;

    const auto& config = controller->config();
    termcore::D3DTextRenderer::SidebarRenderInfo info;
    info.visible = config.sidebar_visible;
    info.width = config.sidebar_width > 0 ? config.sidebar_width : 220;

    if (!info.visible) {
        renderer->setSidebar(info);
        return;
    }

    // Style from config colors
    info.bg_color = config.background;
    info.fg_color = config.foreground;
    info.accent_color = config.palette[4] ? config.palette[4] : 0x007acc;
    info.color_running = config.sidebar_color_running;
    info.color_thinking = config.sidebar_color_thinking;
    info.color_tool_use = config.sidebar_color_tool_use;
    info.color_waiting = config.sidebar_color_waiting;
    info.color_error = config.sidebar_color_error;
    info.color_idle = config.sidebar_color_idle;
    // Separator is a blend toward foreground
    {
        uint32_t bg = config.background;
        uint32_t fg = config.foreground;
        auto blend = [](uint32_t base, uint32_t target, float t) -> uint32_t {
            int bR = (base >> 16) & 0xFF, bG = (base >> 8) & 0xFF, bB = base & 0xFF;
            int tR = (target >> 16) & 0xFF, tG = (target >> 8) & 0xFF, tB = target & 0xFF;
            return ((uint32_t)(bR + (tR - bR) * t) << 16) |
                   ((uint32_t)(bG + (tG - bG) * t) << 8) |
                    (uint32_t)(bB + (tB - bB) * t);
        };
        info.separator_color = blend(bg, fg, 0.15f);
    }

    // Use SidebarModel for rich data (subagents, pills, progress)
    if (sidebarModel && commandDispatcher) {
        // Dirty check: skip full SidebarModel rebuild if agent tree hasn't changed
        uint64_t gen = agentTreeTracker->generation();
        if (gen != lastSidebarGeneration_) {
            lastSidebarGeneration_ = gen;
            sidebarModel->update(*agentTracker, *notifications,
                                 *controller->tabs(), *commandDispatcher);
        }

        auto tabInfos = controller->tabBarInfo();
        const auto& entries = sidebarModel->entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& se = entries[i];
            termcore::D3DTextRenderer::SidebarRenderEntry entry;
            entry.pane_id = se.pane_id;
            entry.title = se.title;
            entry.active = (i < tabInfos.size()) ? tabInfos[i].active : false;
            entry.has_unread = se.has_unread;
            entry.agent_state = static_cast<int>(se.agent_state);
            entry.progress_value = se.progress.value;
            entry.progress_label = se.progress.label;
            entry.subagents_expanded = se.expanded;

            for (const auto& root : se.subagents) {
                flattenSubagentTree(root, 0, entry.subagents);
            }

            info.entries.push_back(std::move(entry));
        }
    } else {
        // Fallback: basic tab data only (no SidebarModel available yet)
        auto tabs = controller->tabBarInfo();
        for (size_t i = 0; i < tabs.size(); ++i) {
            termcore::D3DTextRenderer::SidebarRenderEntry entry;
            entry.pane_id = static_cast<uint32_t>(i);
            entry.title = tabs[i].title;
            entry.active = tabs[i].active;
            entry.has_unread = tabs[i].has_unread;
            entry.agent_state = static_cast<int>(tabs[i].agent_state);
            info.entries.push_back(std::move(entry));
        }
    }

    renderer->setSidebar(info);
}

bool TerminalWindowState::handleSidebarClick(int x, int y) {
    if (!controller || !renderer) return false;
    const auto& config = controller->config();
    if (!config.sidebar_visible) return false;

    int sidebarW = config.sidebar_width > 0 ? config.sidebar_width : 220;
    if (x >= sidebarW) return false;

    // Determine which entry was clicked based on Y position
    // For now, use the tab index as a simple mapping
    auto tabs = controller->tabBarInfo();
    float cellH = controller->cellHeight();
    float tabBarH = cellH * config.tab_bar_height;
    float topY = (config.tab_bar_always_visible || tabs.size() > 1) ? tabBarH : 0.0f;
    float entryH = cellH * 2.5f; // approximate entry height

    int entryIdx = static_cast<int>((y - topY) / entryH);
    if (entryIdx >= 0 && entryIdx < static_cast<int>(tabs.size())) {
        controller->tabs()->switchToTab(entryIdx);
        needsRender = true;
    }
    return true;
}

void TerminalWindowState::handleSidebarHover(int x, int y) {
    // Basic hover tracking - could be expanded later
    (void)x; (void)y;
}

void TerminalWindowState::handleSidebarWheel(int delta) {
    // Could scroll sidebar if many entries
    (void)delta;
}

void TerminalWindowState::updateRendererSelection() {
    if (!renderer || !controller) return;
    const auto& sel = controller->selection();
    D3DTextRenderer::Selection dSel;
    dSel.active = sel.hasSelection();
    dSel.startRow = sel.start().row;
    dSel.startCol = sel.start().col;
    dSel.endRow = sel.end().row;
    dSel.endCol = sel.end().col;
    renderer->setSelection(dSel);
}

#endif // _WIN32
