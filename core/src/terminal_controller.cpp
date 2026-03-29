#include "termcore/terminal_controller.h"
#include "termcore/input_handler.h"
#include "termcore/lua_config.h"
#include "termcore/search_history.h"
#include "termcore/theme_loader.h"

#include <unordered_map>

// Lua binding modules
#include "lua_bindings/lua_tab_module.h"
#include "lua_bindings/lua_command_module.h"
#include "lua_bindings/lua_event_module.h"
#include "lua_bindings/lua_theme_module.h"
#include "lua_bindings/lua_url_module.h"
#include "lua_bindings/lua_mux_module.h"
#include "lua_bindings/lua_shader_module.h"
#include "lua_bindings/lua_search_module.h"
#include "lua_bindings/lua_clipboard_module.h"
#include "lua_bindings/lua_paste_module.h"
#include "lua_bindings/lua_notify_module.h"
#include "lua_bindings/lua_status_module.h"
#include "lua_bindings/lua_git_module.h"
#include "lua_bindings/lua_session_module.h"
#include "lua_bindings/lua_annotation_module.h"
#include "lua_bindings/lua_shell_module.h"
#include "lua_bindings/lua_workspace_module.h"
#include "lua_bindings/lua_settings_module.h"
#include "lua_bindings/lua_vi_module.h"
#include "lua_bindings/lua_quick_module.h"
#include "lua_bindings/lua_config_api_module.h"
#include "lua_bindings/lua_completion_module.h"
#include "lua_bindings/lua_provider_module.h"
#include "lua_bindings/lua_hooks_module.h"
#include "termcore/provider_registry.h"

namespace termcore {

static Action parseActionName(const std::string& name) {
    static const std::unordered_map<std::string, Action> kMap = {
        {"new_tab", Action::NewTab},
        {"close_tab", Action::CloseTab},
        {"next_tab", Action::NextTab},
        {"prev_tab", Action::PrevTab},
        {"move_tab_left", Action::MoveTabLeft},
        {"move_tab_right", Action::MoveTabRight},
        {"split_right", Action::SplitRight},
        {"split_down", Action::SplitDown},
        {"close_pane", Action::ClosePane},
        {"focus_up", Action::FocusUp},
        {"focus_down", Action::FocusDown},
        {"focus_left", Action::FocusLeft},
        {"focus_right", Action::FocusRight},
        {"close_window", Action::CloseWindow},
        {"copy", Action::Copy},
        {"paste", Action::Paste},
        {"paste_from_history", Action::PasteFromHistory},
        {"select_all", Action::SelectAll},
        {"search_open", Action::SearchOpen},
        {"search_close", Action::SearchClose},
        {"search_next", Action::SearchNext},
        {"search_prev", Action::SearchPrev},
        {"font_increase", Action::FontIncrease},
        {"font_decrease", Action::FontDecrease},
        {"font_reset", Action::FontReset},
        {"toggle_fullscreen", Action::ToggleFullscreen},
        {"scroll_page_up", Action::ScrollPageUp},
        {"scroll_page_down", Action::ScrollPageDown},
        {"scroll_to_top", Action::ScrollToTop},
        {"scroll_to_bottom", Action::ScrollToBottom},
        {"scroll_up", Action::ScrollUp},
        {"scroll_down", Action::ScrollDown},
        {"new_window", Action::NewWindow},
        {"jump_prompt_up", Action::JumpPromptUp},
        {"jump_prompt_down", Action::JumpPromptDown},
        {"reset_terminal", Action::ResetTerminal},
        {"clear_scrollback", Action::ClearScrollback},
        {"reload_config", Action::ReloadConfig},
        {"enter_copy_mode", Action::EnterCopyMode},
        {"toggle_sidebar", Action::ToggleSidebar},
        {"open_settings", Action::OpenSettings},
        {"open_theme_hub", Action::OpenThemeHub},
        {"open_font_hub", Action::OpenFontHub},
        {"open_command_palette", Action::OpenCommandPalette},
        {"toggle_broadcast", Action::ToggleBroadcast},
        {"show_profile_dropdown", Action::ShowProfileDropdown},
        {"switch_tab_1", Action::SwitchTab1},
        {"switch_tab_2", Action::SwitchTab2},
        {"switch_tab_3", Action::SwitchTab3},
        {"switch_tab_4", Action::SwitchTab4},
        {"switch_tab_5", Action::SwitchTab5},
        {"switch_tab_6", Action::SwitchTab6},
        {"switch_tab_7", Action::SwitchTab7},
        {"switch_tab_8", Action::SwitchTab8},
        {"switch_tab_9", Action::SwitchTab9},
        {"new_tab_profile_1", Action::NewTabProfile1},
        {"new_tab_profile_2", Action::NewTabProfile2},
        {"new_tab_profile_3", Action::NewTabProfile3},
        {"new_tab_profile_4", Action::NewTabProfile4},
        {"new_tab_profile_5", Action::NewTabProfile5},
        {"new_tab_profile_6", Action::NewTabProfile6},
        {"new_tab_profile_7", Action::NewTabProfile7},
        {"new_tab_profile_8", Action::NewTabProfile8},
        {"new_tab_profile_9", Action::NewTabProfile9},
        {"show_notifications", Action::ShowNotifications},
        {"ssh_connect", Action::SshConnect},
        {"toggle_inspector", Action::ToggleInspector},
        {"enter_instant_replay", Action::EnterInstantReplay},
        {"exit_instant_replay", Action::ExitInstantReplay},
        {"add_annotation", Action::AddAnnotation},
        {"open_password_manager", Action::OpenPasswordManager},
        {"export_screen", Action::ExportScreen},
    };
    auto it = kMap.find(name);
    return it != kMap.end() ? it->second : Action::None;
}

TerminalController::TerminalController(IPlatformHost* host, Config config,
                                       FontCollection* fontCollection)
    : host_(host)
    , config_(std::move(config))
{
    // Font manager
    std::string family = config_.font_family.empty() ? "Consolas" : config_.font_family;
    float size = config_.font_size > 0 ? config_.font_size : 14.0f;
    fontMgr_ = std::make_unique<FontManager>(fontCollection, family, size);
    if (!config_.font_fallback.empty()) {
        fontMgr_->setFallbackFonts(config_.font_fallback);
    }

    // Keybindings
    keybindings_ = std::make_unique<KeybindingManager>();
    if (!config_.keybinding_preset.empty()) {
        auto preset = parseKeymapPreset(config_.keybinding_preset);
        keybindings_->loadPreset(preset);
    }
    if (!config_.keybindings.empty()) {
        std::vector<std::pair<std::string, std::string>> bindings;
        for (const auto& kb : config_.keybindings) {
            bindings.emplace_back(kb.trigger, kb.action);
        }
        keybindings_->loadFromConfig(bindings);
    }

    // ProfileManager
    profileMgr_ = std::make_unique<ProfileManager>();
    for (const auto& p : config_.profiles) profileMgr_->setProfile(p);
    if (!config_.default_profile_id.empty()) profileMgr_->setDefaultProfile(config_.default_profile_id);
    for (const auto& id : config_.hidden_profile_ids) profileMgr_->hideProfile(id);

    // URL highlight manager
    urlHighlightMgr_.applyConfig(config_);

    initInputHandler();

    // Load search history from config directory
    std::string histPath = SearchHistory::defaultPath();
    if (!histPath.empty()) {
        searchCtrl_.history().loadFromDisk(histPath);
    }
}

// --- Lifecycle ---

void TerminalController::initTerminal() {
    // Calculate initial grid size
    if (host_) {
        int vpW = 0, vpH = 0;
        host_->getViewportSize(vpW, vpH);
        fontMgr_->recalcGrid(vpW, vpH, termRows_, termCols_);
    }

    // Detect shells and populate ProfileManager
    // NOTE: Spec defines detectShells() as a ProfileManager member, but the plan
    // intentionally separates ShellDetector (detection) from ProfileManager (storage).
    // This improves testability — ShellDetector can be tested independently.
    auto detected = ShellDetector::detect();
    profileMgr_->setDetectedProfiles(std::move(detected));

    // Apply Config::shell — find matching profile or create one
    if (!config_.shell.empty()) {
        bool matched = false;
        for (const auto& p : profileMgr_->allProfiles()) {
            if (p.command == config_.shell) {
                profileMgr_->setDefaultProfile(p.id);
                matched = true;
                break;
            }
        }
        if (!matched) {
            Profile custom;
            custom.id = "__custom_shell__";
            custom.name = "Custom Shell";
            custom.command = config_.shell;
            custom.icon = "shell";
            profileMgr_->setProfile(custom);
            profileMgr_->setDefaultProfile(custom.id);
        }
    }

    // Create Mux and TabController
    auto mux = std::make_unique<Mux>();
    auto wsId = mux->createWorkspace("default");

    PtyFactory factory = [this](const Profile& profile, int rows, int cols) -> std::unique_ptr<Pty> {
        if (!host_) return nullptr;
        return host_->createPty(profile, rows, cols);
    };

    tabCtrl_ = std::make_unique<TabController>(
        std::move(mux), wsId, std::move(factory), config_);
    tabCtrl_->setProfileManager(profileMgr_.get());
    tabCtrl_->setOnPaneCreated([this](Screen* screen) {
        screen->setCommandCaptureCallback([this](const std::string& cmd) {
            completionManager_.historyProvider().addEntry(cmd);
        });
        // Invoke any externally registered screen-created callbacks
        for (const auto& cb : screen_created_callbacks_) {
            cb(screen);
        }
    });

    // Create initial tab
    tabCtrl_->createTab(termRows_, termCols_);
    tabCtrl_->syncActivePointers();
    needsRender_ = true;

    // --- Lua Plugin Module Registration ---
    luaEngine_ = std::make_unique<LuaEngine>();

    // Modules backed by components owned by this controller
    luaEngine_->registerModule(std::make_shared<LuaTabModule>(tabCtrl_.get()));
    luaEngine_->registerModule(std::make_shared<LuaCommandModule>(&commandPalette_));
    luaEngine_->registerModule(std::make_shared<LuaEventModule>());
    luaEngine_->registerModule(std::make_shared<LuaThemeModule>(&config_));
    luaEngine_->registerModule(std::make_shared<LuaUrlModule>(&urlDetector_, &urlHighlightMgr_));
    luaEngine_->registerModule(std::make_shared<LuaMuxModule>(tabCtrl_->mux(), tabCtrl_.get()));
    luaEngine_->registerModule(std::make_shared<LuaSearchModule>(&searchCtrl_));
    luaEngine_->registerModule(std::make_shared<LuaClipboardModule>(&clipboardHistory_));
    luaEngine_->registerModule(std::make_shared<LuaPasteModule>(&pasteGuard_));
    luaEngine_->registerModule(std::make_shared<LuaStatusModule>(tabCtrl_.get()));
    luaEngine_->registerModule(std::make_shared<LuaQuickModule>(&config_));

    // Modules backed by components not yet owned — pass nullptr (safe, functions return nil+error)
    luaEngine_->registerModule(std::make_shared<LuaShaderModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaNotifyModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaGitModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaSessionModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaAnnotationModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaShellModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaWorkspaceModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaSettingsModule>(nullptr));
    luaEngine_->registerModule(std::make_shared<LuaViModule>(nullptr));

    // Config API module: exposes terminal.config(), terminal.keymap(), terminal.colorscheme()
    // Must be registered before loadDefaults() so embedded Lua scripts can use them.
    luaEngine_->registerModule(std::make_shared<LuaConfigApiModule>(&config_, keybindings_.get()));
    luaEngine_->registerModule(std::make_shared<LuaCompletionModule>(&completionManager_));
    luaEngine_->registerModule(std::make_shared<LuaProviderModule>(&providerRegistry_));
    auto hooksModule = std::make_shared<LuaHooksModule>(&providerRegistry_);
    luaEngine_->registerModule(hooksModule);

    luaEngine_->initializeModules();

    // Wire TabController provider detection to Lua hooks module
    tabCtrl_->setProviderRegistry(&providerRegistry_);
    tabCtrl_->setOnProviderDetected(
        [hooksModule](const std::string& provider_id, uint32_t pane_id) {
            hooksModule->fireProviderDetected(provider_id, pane_id);
        });

    // Register terminal.action() to dispatch C++ actions from Lua
    luaEngine_->setActionHandler([this](const std::string& name) {
        Action a = parseActionName(name);
        if (a != Action::None) {
            handleAction(a);
        }
    });

    // Load embedded Lua defaults (before user config)
    luaEngine_->loadDefaults();

    // Safety fallback if defaults failed to load
    if (config_.font_size <= 0) config_.font_size = 14.0f;
    if (config_.foreground == 0) config_.foreground = 0xffffff;
    if (config_.background == 0) config_.background = 0x000000;
    if (config_.scrollback_limit <= 0) config_.scrollback_limit = 10000;
    if (config_.cursor_style.empty()) config_.cursor_style = "block";

    // --- Plugin Discovery & Loading ---
    pluginMgr_ = std::make_unique<PluginManager>(*luaEngine_);
    std::string pluginsDir = pluginsDirectory();
    if (!pluginsDir.empty()) {
        pluginMgr_->scanDirectory(pluginsDir);
        for (const auto& info : pluginMgr_->plugins()) {
            if (info.state == PluginState::Discovered) {
                pluginMgr_->loadPlugin(info.metadata.name);
            }
        }
    }
}

void TerminalController::addScreenCreatedCallback(std::function<void(Screen*)> cb) {
    screen_created_callbacks_.push_back(std::move(cb));
}

void TerminalController::pollPty() {
    if (!tabCtrl_) return;

    bool dataRead = tabCtrl_->pollAllPtys();
    if (dataRead) {
        needsRender_ = true;
        urlScanPending_ = true;
    }

    // Cleanup dead panes
    bool wasVisible = isTabBarVisible();
    if (tabCtrl_->cleanupDeadPanes()) {
        if (host_) host_->closeWindow();
        return;
    }
    if (wasVisible && !isTabBarVisible()) recalcGrid();
}

void TerminalController::flushPendingUrlScan() {
    if (!urlScanPending_ || !tabCtrl_) return;
    urlScanPending_ = false;

    Screen* scr = tabCtrl_->activeScreen();
    if (scr) {
        detectedUrls_ = urlDetector_.detectInScreen(*scr);
        urlHighlightMgr_.markDirty();
        urlHighlightMgr_.scanScreen(*scr, scr->rows());
    }
}

void TerminalController::tick() {
    // Flush debounced incremental search
    Screen* scr = activeScreen();
    if (scr && searchCtrl_.isActive()) {
        if (searchCtrl_.flushIncremental(*scr)) {
            if (host_) {
                host_->updateSearchResults(searchCtrl_.currentMatch(),
                                           searchCtrl_.totalMatches());
            }
            needsRender_ = true;
        }
    }

    // Update autocomplete ghost text
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
}

// --- Event entry points (delegated to InputHandler) ---

void TerminalController::onKeyEvent(const KeyEvent& e) {
    // If command palette is open, handle its input first
    if (commandPalette_.isOpen()) {
        if (e.keycode == 0xF70A) { // Escape
            commandPalette_.close();
            needsRender_ = true;
            return;
        }
        if (e.keycode == 0xF700) { // Up
            commandPalette_.selectPrev();
            needsRender_ = true;
            return;
        }
        if (e.keycode == 0xF701) { // Down
            commandPalette_.selectNext();
            needsRender_ = true;
            return;
        }
        if (e.keycode == 0xF709) { // Enter
            Action action = commandPalette_.selectedAction();
            commandPalette_.close();
            if (action != Action::None) {
                handleAction(action);
            }
            needsRender_ = true;
            return;
        }
        if (e.keycode == 0xF70B) { // Backspace
            commandPalette_.onBackspace();
            needsRender_ = true;
            return;
        }
        return;
    }

    // If profile dropdown is open, handle its input
    if (profileDropdown_.isOpen()) {
        if (e.keycode == 0xF70A) { // Escape
            profileDropdown_.close();
            needsRender_ = true;
            return;
        }
        if (e.keycode == 0xF700) { // Up
            profileDropdown_.selectPrev();
            needsRender_ = true;
            return;
        }
        if (e.keycode == 0xF701) { // Down
            profileDropdown_.selectNext();
            needsRender_ = true;
            return;
        }
        if (e.keycode == 0xF709) { // Enter
            std::string profileId = profileDropdown_.selectedProfileId();
            profileDropdown_.close();
            if (!profileId.empty() && tabCtrl_) {
                bool wasVisible = isTabBarVisible();
                tabCtrl_->createTab(termRows_, termCols_, profileId);
                if (!wasVisible && isTabBarVisible()) recalcGrid();
            }
            needsRender_ = true;
            return;
        }
        // Number keys 1-9: quick select
        if (e.keycode >= '1' && e.keycode <= '9') {
            int idx = static_cast<int>(e.keycode - '1');
            const auto& items = profileDropdown_.items();
            if (idx < static_cast<int>(items.size())) {
                std::string profileId = items[idx].id;
                profileDropdown_.close();
                if (!profileId.empty() && tabCtrl_) {
                    bool wasVisible = isTabBarVisible();
                    tabCtrl_->createTab(termRows_, termCols_, profileId);
                    if (!wasVisible && isTabBarVisible()) recalcGrid();
                }
                needsRender_ = true;
            }
            return;
        }
        return;
    }

    inputHandler_->onKeyEvent(e);
}

void TerminalController::onCharInput(const std::string& utf8) {
    if (commandPalette_.isOpen()) {
        for (char c : utf8) {
            commandPalette_.onChar(c);
        }
        needsRender_ = true;
        return;
    }
    inputHandler_->onCharInput(utf8);
}

void TerminalController::onMouseEvent(const InputMouseEvent& e) {
    Screen* scr = activeScreen();

    // --- Mouse mode detection (self-managed) ---
    // ConPTY on Windows filters mouse mode sequences (?1000h/?1003h/?1006h)
    // from the output, so Screen::mouseMode() may report None even when
    // the child app has enabled mouse tracking.  We work around this by
    // inferring mouse-forwarding state from the foreground process:
    //   - Shell is foreground  → no mouse forwarding → viewport scrollback
    //   - Child app is running → forward mouse as SGR to PTY
    // Shift bypasses forwarding so the user can always select/scroll locally.

    bool forwardMouse = (scr && scr->mouseMode() != MouseMode::None);

    // Shift overrides: let user select text / scroll viewport even in TUI apps
    if (forwardMouse && (e.modifiers & ModShift) != 0) {
        forwardMouse = false;
    }

    // In alt screen, scroll events go to viewport scrollback (like Windows Terminal)
    // instead of being forwarded to the app.  The user can scroll up to see
    // content that scrolled off the alt screen.  Other mouse events (click,
    // move, drag) are still forwarded so TUI apps remain interactive.
    if (forwardMouse && scr && scr->altScreenActive()
        && (e.type == InputMouseEvent::ScrollUp || e.type == InputMouseEvent::ScrollDown)) {
        forwardMouse = false;
    }

    if (forwardMouse) {
        int offsetY = 0;
        if (tabCtrl_ && tabCtrl_->tabCount() > 1) {
            offsetY = static_cast<int>(cellHeight());
        }
        int gridCol = static_cast<int>(e.x / cellWidth());
        int gridRow = static_cast<int>((e.y - offsetY) / cellHeight());
        if (gridCol < 0) gridCol = 0;
        if (gridRow < 0) gridRow = 0;
        if (scr) {
            if (gridCol >= scr->cols()) gridCol = scr->cols() - 1;
            if (gridRow >= scr->rows()) gridRow = scr->rows() - 1;
        }

        // Use the actual mouse mode if known, otherwise assume AnyEvent+SGR
        // (modern TUI apps almost universally use these).
        MouseMode mode = scr->mouseMode();
        MouseEncoding enc = scr->mouseEncoding();
        if (mode == MouseMode::None) {
            mode = MouseMode::AnyEvent;
            enc = MouseEncoding::SGR;
        }

        termcore::MouseEvent me;
        me.col = gridCol;
        me.row = gridRow;
        me.shift = (e.modifiers & ModShift) != 0;
        me.alt = (e.modifiers & ModAlt) != 0;
        me.ctrl = (e.modifiers & ModCtrl) != 0;

        switch (e.type) {
            case InputMouseEvent::Press:
                me.type = MouseEventType::Press;
                me.button = static_cast<MouseButton>(e.button);
                break;
            case InputMouseEvent::Release:
                me.type = MouseEventType::Release;
                me.button = MouseButton::Release;
                break;
            case InputMouseEvent::Move:
                me.type = MouseEventType::Move;
                me.button = static_cast<MouseButton>(e.button);
                break;
            case InputMouseEvent::ScrollUp:
                me.type = MouseEventType::ScrollUp;
                me.button = MouseButton::ScrollUp;
                break;
            case InputMouseEvent::ScrollDown:
                me.type = MouseEventType::ScrollDown;
                me.button = MouseButton::ScrollDown;
                break;
            default:
                return;
        }

        // For scroll events, batch all lines into a single PTY write
        if (e.type == InputMouseEvent::ScrollUp || e.type == InputMouseEvent::ScrollDown) {
            int lines = e.scrollLines > 0 ? e.scrollLines : 3;
            std::string scrollSeq = encodeMouseEvent(me, mode, enc);
            if (!scrollSeq.empty()) {
                std::string batch;
                batch.reserve(scrollSeq.size() * lines);
                for (int i = 0; i < lines; ++i) {
                    batch += scrollSeq;
                }
                sendPtyData(batch.data(), batch.size());
            }
            return;
        }

        std::string seq = encodeMouseEvent(me, mode, enc);
        if (!seq.empty()) {
            sendPtyData(seq.data(), seq.size());
            return;
        }
    }

    // Selection handling
    float cw = cellWidth();
    float ch = cellHeight();
    int offsetX = 0, offsetY = 0;

    // Tab bar offset
    if (tabCtrl_ && tabCtrl_->tabCount() > 1) {
        offsetY = static_cast<int>(ch);
    }

    // Grid coordinates for URL hover/click
    int gridCol = static_cast<int>((e.x - offsetX) / cw);
    int gridRow = static_cast<int>((e.y - offsetY) / ch);

    // URL hover tracking on mouse move
    if (e.type == InputMouseEvent::Move && urlHighlightMgr_.isEnabled()) {
        bool hoverChanged = urlHighlightMgr_.updateHover(gridRow, gridCol);
        if (hoverChanged) {
            needsRender_ = true;
            if (host_) {
                auto hovered = urlHighlightMgr_.getHoveredUrl();
                host_->setMouseCursor(hovered.has_value()
                    ? IPlatformHost::CursorType::Hand
                    : IPlatformHost::CursorType::Arrow);
            }
        }
    }

    // Ctrl+Click (Cmd+Click on macOS) to open URL
    if (e.type == InputMouseEvent::Press && e.button == 0 && urlHighlightMgr_.isEnabled()) {
        bool ctrlHeld = (e.modifiers & ModCtrl) != 0 || (e.modifiers & ModSuper) != 0;
        if (ctrlHeld) {
            urlHighlightMgr_.updateHover(gridRow, gridCol);
            auto hovered = urlHighlightMgr_.getHoveredUrl();
            if (hovered.has_value() && host_) {
                host_->openUrl(hovered->url);
                return;  // Consume the click — don't start selection
            }
        }
    }

    switch (e.type) {
        case InputMouseEvent::Press:
            selMgr_.onMouseDown(e.x, e.y, cw, ch, offsetX, offsetY);
            needsRender_ = true;
            break;
        case InputMouseEvent::Move:
            selMgr_.onMouseMove(e.x, e.y, cw, ch, offsetX, offsetY);
            if (selMgr_.isDragging()) needsRender_ = true;
            break;
        case InputMouseEvent::Release:
            selMgr_.onMouseUp(e.x, e.y, cw, ch, offsetX, offsetY);
            needsRender_ = true;
            break;
        case InputMouseEvent::DoubleClick:
            if (scr) {
                selMgr_.onDoubleClick(e.x, e.y, cw, ch, offsetX, offsetY, *scr);
                needsRender_ = true;
            }
            break;
        case InputMouseEvent::ScrollUp:
            if (scr) {
                int lines = e.scrollLines > 0 ? e.scrollLines : 3;
                int oldOffset = scr->viewportOffset();
                scr->scrollViewportUp(lines);
                int delta = scr->viewportOffset() - oldOffset;
                if (delta != 0 && (selMgr_.hasSelection() || selMgr_.isDragging())) {
                    selMgr_.adjustForScroll(delta);
                }
                needsRender_ = true;
            }
            break;
        case InputMouseEvent::ScrollDown:
            if (scr) {
                int lines = e.scrollLines > 0 ? e.scrollLines : 3;
                int oldOffset = scr->viewportOffset();
                scr->scrollViewportDown(lines);
                int delta = scr->viewportOffset() - oldOffset;
                if (delta != 0 && (selMgr_.hasSelection() || selMgr_.isDragging())) {
                    selMgr_.adjustForScroll(delta);
                }
                needsRender_ = true;
            }
            break;
    }
}

bool TerminalController::isTabBarVisible() const {
    return tabCtrl_ && tabCtrl_->tabCount() > 1;
}

void TerminalController::recalcGrid() {
    if (!fontMgr_ || lastPixelW_ <= 0 || lastPixelH_ <= 0) return;

    int effectiveW = lastPixelW_;
    int effectiveH = lastPixelH_;
    if (isTabBarVisible()) {
        float tabBarH = fontMgr_->cellHeight() * kTabBarHeightScale;
        effectiveH -= static_cast<int>(tabBarH);
        if (effectiveH < 1) effectiveH = 1;
    }

    // Subtract sidebar width when visible
    if (config_.sidebar_visible) {
        int sidebarW = config_.sidebar_width > 0 ? config_.sidebar_width : 220;
        effectiveW -= sidebarW;
        if (effectiveW < 1) effectiveW = 1;
    }

    int rows = 0, cols = 0;
    fontMgr_->recalcGrid(effectiveW, effectiveH, rows, cols);

    if (rows != termRows_ || cols != termCols_) {
        termRows_ = rows;
        termCols_ = cols;
        if (tabCtrl_) {
            tabCtrl_->resizeAllPanes(rows, cols);
        }
        if (host_) {
            host_->onGridSizeChanged(rows, cols);
        }
    }
    needsRender_ = true;
}

void TerminalController::onResize(int pixelW, int pixelH) {
    lastPixelW_ = pixelW;
    lastPixelH_ = pixelH;
    recalcGrid();
}

void TerminalController::onFocusChange(bool focused) {
    auto* screen = activeScreen();
    if (!screen || !screen->focusEvents()) return;

    const char* seq = focused ? "\x1b[I" : "\x1b[O";
    sendPtyData(seq, 3);
}

// --- Search ---

void TerminalController::onSearchQuery(const std::string& query) {
    Screen* scr = activeScreen();
    if (!scr) return;

    // Use incremental (debounced) search for live updates
    searchCtrl_.setQueryIncremental(query);

    // For short queries (<=2 chars) or empty, execute immediately
    if (query.empty() || query.size() <= 2) {
        searchCtrl_.setQuery(query, *scr);
        if (host_) {
            host_->updateSearchResults(searchCtrl_.currentMatch(),
                                       searchCtrl_.totalMatches());
        }
    }
    needsRender_ = true;
}

void TerminalController::onSearchNext() {
    Screen* scr = activeScreen();
    if (!scr) return;
    searchCtrl_.submitQuery(*scr);
    saveSearchHistory();
    searchCtrl_.next(*scr);
    if (host_) {
        host_->updateSearchResults(searchCtrl_.currentMatch(),
                                   searchCtrl_.totalMatches());
    }
    needsRender_ = true;
}

void TerminalController::onSearchPrev() {
    Screen* scr = activeScreen();
    if (!scr) return;
    searchCtrl_.submitQuery(*scr);
    saveSearchHistory();
    searchCtrl_.prev(*scr);
    if (host_) {
        host_->updateSearchResults(searchCtrl_.currentMatch(),
                                   searchCtrl_.totalMatches());
    }
    needsRender_ = true;
}

void TerminalController::onSearchHistoryPrev() {
    if (searchCtrl_.historyUp()) {
        if (host_) {
            host_->setSearchBarText(searchCtrl_.pendingQuery());
        }
        // Trigger incremental search with the history query
        Screen* scr = activeScreen();
        if (scr) {
            searchCtrl_.setQuery(searchCtrl_.pendingQuery(), *scr);
            if (host_) {
                host_->updateSearchResults(searchCtrl_.currentMatch(),
                                           searchCtrl_.totalMatches());
            }
            needsRender_ = true;
        }
    }
}

void TerminalController::onSearchHistoryNext() {
    if (searchCtrl_.historyDown()) {
        if (host_) {
            host_->setSearchBarText(searchCtrl_.pendingQuery());
        }
        Screen* scr = activeScreen();
        if (scr) {
            searchCtrl_.setQuery(searchCtrl_.pendingQuery(), *scr);
            if (host_) {
                host_->updateSearchResults(searchCtrl_.currentMatch(),
                                           searchCtrl_.totalMatches());
            }
            needsRender_ = true;
        }
    } else {
        // Back to empty (typing position)
        if (host_) {
            host_->setSearchBarText("");
        }
        Screen* scr = activeScreen();
        if (scr) {
            searchCtrl_.setQuery("", *scr);
            if (host_) {
                host_->updateSearchResults(searchCtrl_.currentMatch(),
                                           searchCtrl_.totalMatches());
            }
            needsRender_ = true;
        }
    }
}

void TerminalController::saveSearchHistory() {
    std::string histPath = SearchHistory::defaultPath();
    if (!histPath.empty()) {
        searchCtrl_.history().saveToDisk(histPath);
    }
}

// --- Config changes ---

void TerminalController::onConfigChanged(const Config& newConfig) {
    if (tabCtrl_ && fontMgr_) {
        configApplier_.applyFull(config_, newConfig, *tabCtrl_, *fontMgr_, host_);
        configApplier_.persist(config_);
    }
    urlHighlightMgr_.applyConfig(config_);
    needsRender_ = true;
}

void TerminalController::onThemeChanged(const std::string& name) {
    config_.theme = name;
    auto theme = findTheme(name);
    if (theme) {
        applyTheme(config_, *theme);
    }
    if (tabCtrl_) {
        configApplier_.applyColors(config_, config_, *tabCtrl_, host_);
        configApplier_.persist(config_);
    }
    needsRender_ = true;
}

void TerminalController::onFontChanged(const std::string& family) {
    if (tabCtrl_ && fontMgr_) {
        configApplier_.applyFont(config_, family, *tabCtrl_, *fontMgr_, host_);
        configApplier_.persist(config_);
    }
    needsRender_ = true;
}

// --- Accessors ---

bool TerminalController::needsRender() const {
    if (!needsRender_) return false;
    const Screen* scr = tabCtrl_ ? tabCtrl_->activeScreen() : nullptr;
    if (scr && scr->syncUpdate()) {
        // ConPTY may filter ?2026l (synchronized update end), leaving sync mode
        // stuck permanently. Use a short timeout (50ms) instead of the previous
        // 1-second timeout so rendering isn't blocked for too long.
        auto elapsed = std::chrono::steady_clock::now() - scr->syncStartTime();
        if (elapsed < std::chrono::milliseconds(50)) return false;
    }
    return true;
}

Screen* TerminalController::activeScreen() {
    return tabCtrl_ ? tabCtrl_->activeScreen() : nullptr;
}

std::vector<TabController::TabInfo> TerminalController::tabBarInfo() const {
    return tabCtrl_ ? tabCtrl_->tabBarInfo() : std::vector<TabController::TabInfo>{};
}

int TerminalController::tabCount() const {
    return tabCtrl_ ? tabCtrl_->tabCount() : 0;
}

bool TerminalController::inCopyMode() const {
    return copyMode_ && copyMode_->isActive();
}

// --- Broadcast input ---

void TerminalController::broadcastWrite(const std::string& data) {
    if (!tabCtrl_) return;
    Mux* m = tabCtrl_->mux();
    if (!m) return;

    auto paneIds = m->getBroadcastPaneIds();
    for (PaneId id : paneIds) {
        PaneState* ps = tabCtrl_->paneById(id);
        if (ps && ps->pty && ps->pty->isAlive()) {
            ps->pty->write(data.data(), data.size());
        }
    }
}

void TerminalController::toggleBroadcast() {
    if (!tabCtrl_) return;
    Mux* m = tabCtrl_->mux();
    if (m) m->toggleBroadcast();
    needsRender_ = true;
}

BroadcastMode TerminalController::broadcastMode() const {
    if (!tabCtrl_) return BroadcastMode::Off;
    Mux* m = tabCtrl_->mux();
    if (!m) return BroadcastMode::Off;
    return m->broadcastMode();
}

// --- Action dispatch ---

void TerminalController::handleAction(Action action) {
    Screen* scr = activeScreen();

    switch (action) {
        case Action::Copy:
            if (selMgr_.hasSelection() && scr) {
                std::string text = selMgr_.getSelectedText(*scr);
                if (!text.empty()) {
                    clipboardHistory_.addEntry(text);
                    if (host_) {
                        host_->setClipboardText(text);
                    }
                }
                selMgr_.clear();
                needsRender_ = true;
            } else {
                // No selection: send ^C (ETX, 0x03) to PTY as interrupt signal
                auto* pty = tabCtrl_ ? tabCtrl_->activePty() : nullptr;
                if (pty) {
                    const char etx = '\x03';
                    pty->write(&etx, 1);
                }
            }
            break;

        case Action::Paste:
            if (host_) {
                std::string text = host_->getClipboardText();
                if (!text.empty()) {
                    pasteText(text);
                }
            }
            break;

        case Action::PasteFromHistory:
            if (host_) {
                host_->showClipboardHistory(clipboardHistory_.getEntries());
            }
            break;

        case Action::SelectAll:
            selMgr_.selectAll(termRows_, termCols_);
            needsRender_ = true;
            break;

        case Action::SearchOpen:
            searchCtrl_.open();
            if (host_) host_->showSearchBar();
            break;

        case Action::SearchClose:
            searchCtrl_.close();
            if (host_) host_->hideSearchBar();
            needsRender_ = true;
            break;

        case Action::SearchNext:
            onSearchNext();
            break;

        case Action::SearchPrev:
            onSearchPrev();
            break;

        case Action::FontIncrease:
            if (fontMgr_) {
                fontMgr_->changeFontSize(1.0f);
                if (host_) {
                    host_->onFontChanged(fontMgr_->cellWidth(), fontMgr_->cellHeight());
                    int vpW = 0, vpH = 0;
                    host_->getViewportSize(vpW, vpH);
                    if (vpW > 0 && vpH > 0) onResize(vpW, vpH);
                }
            }
            break;

        case Action::FontDecrease:
            if (fontMgr_) {
                fontMgr_->changeFontSize(-1.0f);
                if (host_) host_->onFontChanged(fontMgr_->cellWidth(), fontMgr_->cellHeight());
                int vpW = 0, vpH = 0;
                if (host_) host_->getViewportSize(vpW, vpH);
                if (vpW > 0 && vpH > 0) onResize(vpW, vpH);
            }
            break;

        case Action::FontReset:
            if (fontMgr_) {
                fontMgr_->resetFontSize();
                if (host_) host_->onFontChanged(fontMgr_->cellWidth(), fontMgr_->cellHeight());
                int vpW = 0, vpH = 0;
                if (host_) host_->getViewportSize(vpW, vpH);
                if (vpW > 0 && vpH > 0) onResize(vpW, vpH);
            }
            break;

        case Action::ToggleFullscreen:
            if (host_) host_->toggleFullscreen();
            break;

        case Action::ScrollPageUp:
            if (scr) { scr->scrollViewportUp(scr->rows()); needsRender_ = true; }
            break;

        case Action::ScrollPageDown:
            if (scr) { scr->scrollViewportDown(scr->rows()); needsRender_ = true; }
            break;

        case Action::ScrollToTop:
            if (scr) { scr->scrollViewportToTop(); needsRender_ = true; }
            break;

        case Action::ScrollToBottom:
            if (scr) { scr->scrollViewportToBottom(); needsRender_ = true; }
            break;

        case Action::ScrollUp:
            if (scr) { scr->scrollViewportUp(3); needsRender_ = true; }
            break;

        case Action::ScrollDown:
            if (scr) { scr->scrollViewportDown(3); needsRender_ = true; }
            break;

        // Tab operations
        case Action::NewTab:
            if (tabCtrl_) {
                bool wasVisible = isTabBarVisible();
                tabCtrl_->createTab(termRows_, termCols_);
                if (!wasVisible && isTabBarVisible()) recalcGrid();
                needsRender_ = true;
            }
            break;

        case Action::CloseTab:
            if (tabCtrl_) {
                if (tabCtrl_->tabCount() <= 1) {
                    if (host_) host_->closeWindow();
                } else {
                    bool wasVisible = isTabBarVisible();
                    tabCtrl_->closeTab();
                    if (wasVisible && !isTabBarVisible()) recalcGrid();
                    needsRender_ = true;
                }
            }
            break;

        case Action::NextTab:
            if (tabCtrl_) { tabCtrl_->nextTab(); needsRender_ = true; }
            break;

        case Action::PrevTab:
            if (tabCtrl_) { tabCtrl_->prevTab(); needsRender_ = true; }
            break;

        case Action::MoveTabLeft:
            if (tabCtrl_) { tabCtrl_->moveTabLeft(); needsRender_ = true; }
            break;

        case Action::MoveTabRight:
            if (tabCtrl_) { tabCtrl_->moveTabRight(); needsRender_ = true; }
            break;

        case Action::SwitchTab1: case Action::SwitchTab2: case Action::SwitchTab3:
        case Action::SwitchTab4: case Action::SwitchTab5: case Action::SwitchTab6:
        case Action::SwitchTab7: case Action::SwitchTab8: case Action::SwitchTab9:
            if (tabCtrl_) {
                int idx = static_cast<int>(action) - static_cast<int>(Action::SwitchTab1);
                tabCtrl_->switchToTab(idx);
                needsRender_ = true;
            }
            break;

        // Pane operations
        case Action::SplitRight:
            if (tabCtrl_) { tabCtrl_->splitRight(termRows_, termCols_); needsRender_ = true; }
            break;

        case Action::SplitDown:
            if (tabCtrl_) { tabCtrl_->splitDown(termRows_, termCols_); needsRender_ = true; }
            break;

        case Action::ClosePane:
            if (tabCtrl_) {
                auto* tab = tabCtrl_->mux()->activeTab(tabCtrl_->workspaceId());
                if (tab) {
                    auto allP = tabCtrl_->mux()->allPanes(tabCtrl_->workspaceId(), tab->id);
                    if (allP.size() <= 1 && tabCtrl_->tabCount() <= 1) {
                        if (host_) host_->closeWindow();
                    } else {
                        tabCtrl_->closePane();
                        needsRender_ = true;
                    }
                }
            }
            break;

        case Action::CloseWindow:
            if (host_) host_->closeWindow();
            break;

        // Settings / Hub
        case Action::OpenSettings:
            if (host_) host_->openSettingsWindow(config_);
            break;

        case Action::OpenThemeHub:
            if (host_) host_->openSettingsWindow(config_);
            break;

        case Action::OpenFontHub:
            if (host_) host_->openSettingsWindow(config_);
            break;

        // Copy mode
        case Action::EnterCopyMode:
            if (scr && !copyMode_) {
                copyMode_ = std::make_unique<ViCopyMode>(*scr);
            }
            if (copyMode_) {
                copyMode_->enterCopyMode();
                needsRender_ = true;
            }
            break;

        // Prompt navigation (uses shell integration OSC 133 marks)
        case Action::JumpPromptUp:
            if (scr) {
                int scrollbackSize = static_cast<int>(scr->scrollbackSize());
                int topAbsRow = scrollbackSize - scr->viewportOffset();
                int target = scr->previousPromptRow(topAbsRow);
                if (target >= 0) {
                    int newOffset = std::max(0, std::min(scrollbackSize,
                                                          scrollbackSize - target));
                    // Compute delta from current position
                    int delta = newOffset - scr->viewportOffset();
                    if (delta > 0) scr->scrollViewportUp(delta);
                    else if (delta < 0) scr->scrollViewportDown(-delta);
                }
                needsRender_ = true;
            }
            break;

        case Action::JumpPromptDown:
            if (scr) {
                int scrollbackSize = static_cast<int>(scr->scrollbackSize());
                int topAbsRow = scrollbackSize - scr->viewportOffset();
                int target = scr->nextPromptRow(topAbsRow);
                if (target >= 0) {
                    int newOffset = std::max(0, std::min(scrollbackSize,
                                                          scrollbackSize - target));
                    int delta = newOffset - scr->viewportOffset();
                    if (delta > 0) scr->scrollViewportUp(delta);
                    else if (delta < 0) scr->scrollViewportDown(-delta);
                } else {
                    scr->scrollViewportToBottom();
                }
                needsRender_ = true;
            }
            break;

        case Action::ResetTerminal:
            // Send VT reset sequence to PTY; let the terminal re-init via VT parser
            sendPtyData("\x1b" "c", 2);  // ESC c = RIS (Reset to Initial State)
            needsRender_ = true;
            break;

        case Action::ClearScrollback:
            // Send ED 3 to clear scrollback via VT parser
            sendPtyData("\x1b[3J", 4);
            needsRender_ = true;
            break;

        case Action::ReloadConfig: {
            Config newCfg = loadConfig();
            onConfigChanged(newCfg);
            break;
        }

        case Action::NewTabProfile1: case Action::NewTabProfile2: case Action::NewTabProfile3:
        case Action::NewTabProfile4: case Action::NewTabProfile5: case Action::NewTabProfile6:
        case Action::NewTabProfile7: case Action::NewTabProfile8: case Action::NewTabProfile9: {
            if (tabCtrl_ && profileMgr_) {
                int idx = static_cast<int>(action) - static_cast<int>(Action::NewTabProfile1);
                auto visible = profileMgr_->visibleProfiles();
                if (idx >= 0 && idx < static_cast<int>(visible.size())) {
                    bool wasVisible = isTabBarVisible();
                    tabCtrl_->createTab(termRows_, termCols_, visible[idx]->id);
                    if (!wasVisible && isTabBarVisible()) recalcGrid();
                    needsRender_ = true;
                }
            }
            break;
        }

        case Action::ShowProfileDropdown:
            if (profileMgr_) {
                auto visible = profileMgr_->visibleProfiles();
                std::vector<ProfileDropdownItem> items;
                for (const auto* p : visible) {
                    items.push_back({p->id, p->name, p->icon});
                }
                if (!items.empty()) {
                    profileDropdown_.open(std::move(items));
                    needsRender_ = true;
                }
            }
            break;

        case Action::ToggleBroadcast:
            toggleBroadcast();
            break;

        case Action::ToggleSidebar:
            config_.sidebar_visible = !config_.sidebar_visible;
            needsRender_ = true;
            // Trigger resize so grid columns adjust for sidebar width
            if (host_) {
                int vpW = 0, vpH = 0;
                host_->getViewportSize(vpW, vpH);
                if (vpW > 0 && vpH > 0) onResize(vpW, vpH);
            }
            break;

        case Action::OpenCommandPalette:
            if (keybindings_) {
                commandPalette_.updateShortcuts(*keybindings_);
            }
            commandPalette_.open();
            needsRender_ = true;
            break;

        default:
            break;
    }

    if (host_) host_->invalidate();
}

// --- PTY helpers ---

void TerminalController::sendPtyData(const char* data, size_t len) {
    if (!tabCtrl_) return;
    Pty* p = tabCtrl_->activePty();
    if (p && p->isAlive()) {
        p->write(data, len);
    }
}

void TerminalController::pasteText(const std::string& text) {
    Screen* scr = activeScreen();
    bool bracketed = scr && scr->bracketedPaste();

    // Paste guard
    PasteAnalysis analysis = pasteGuard_.analyze(text, bracketed);
    if (analysis.danger == PasteDanger::Warn) {
        std::string msg = "The clipboard content may be dangerous:\n\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::MultiLine))
            msg += "  - Contains multiple lines\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::TrailingNewline))
            msg += "  - Ends with a newline (will execute immediately)\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::SudoCommand))
            msg += "  - Contains sudo command\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::RmRf))
            msg += "  - Contains rm -rf command\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::CurlPipe))
            msg += "  - Contains curl piped to shell\n";
        msg += "\nDo you want to paste anyway?";

        if (host_) {
            // Async dialog - capture text for the callback
            std::string capturedText = text;
            bool capturedBracketed = bracketed;
            host_->showConfirmDialog(msg, [this, capturedText, capturedBracketed](bool confirmed) {
                if (!confirmed) return;
                if (capturedBracketed) {
                    std::string wrapped = "\x1b[200~" + capturedText + "\x1b[201~";
                    sendPtyData(wrapped.c_str(), wrapped.size());
                } else {
                    sendPtyData(capturedText.c_str(), capturedText.size());
                }
            });
        }
        return;
    }

    if (bracketed) {
        std::string wrapped = "\x1b[200~" + text + "\x1b[201~";
        sendPtyData(wrapped.c_str(), wrapped.size());
    } else {
        sendPtyData(text.c_str(), text.size());
    }
}

void TerminalController::initInputHandler() {
    InputHandler::Deps deps;
    deps.host = host_;
    deps.keybindings = keybindings_.get();
    deps.searchCtrl = &searchCtrl_;
    deps.selMgr = &selMgr_;
    deps.handleAction = [this](Action a) { handleAction(a); };
    deps.sendPtyData = [this](const char* d, size_t n) { sendPtyData(d, n); };
    deps.activeScreen = [this]() -> Screen* { return activeScreen(); };
    deps.getCopyMode = [this]() -> ViCopyMode* { return copyMode_.get(); };
    deps.tabCount = [this]() -> int { return tabCount(); };
    deps.cellWidth = [this]() { return cellWidth(); };
    deps.cellHeight = [this]() { return cellHeight(); };
    deps.needsRender = [this]() -> bool& { return needsRender_; };
    deps.urlHighlight = [this]() -> UrlHighlightManager* { return &urlHighlightMgr_; };
    deps.getCompletionManager = [this]() -> CompletionManager* { return &completionManager_; };
    inputHandler_ = std::make_unique<InputHandler>(std::move(deps));
}

} // namespace termcore
