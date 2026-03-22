#include "termcore/terminal_controller.h"
#include "termcore/input_handler.h"
#include "termcore/lua_config.h"
#include "termcore/theme_loader.h"

namespace termcore {

TerminalController::TerminalController(IPlatformHost* host, Config config,
                                       FontCollection* fontCollection)
    : host_(host)
    , config_(std::move(config))
{
    // Font manager
    std::string family = config_.font_family.empty() ? "Consolas" : config_.font_family;
    float size = config_.font_size > 0 ? config_.font_size : 14.0f;
    fontMgr_ = std::make_unique<FontManager>(fontCollection, family, size);

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

    initInputHandler();
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

    // Config::shell backward compatibility
    if (profileMgr_->allProfiles().empty() && !config_.shell.empty()) {
        Profile legacy;
        legacy.id = "__legacy_shell__";
        legacy.name = "Shell";
        legacy.command = config_.shell;
        legacy.icon = "shell";
        profileMgr_->setProfile(legacy);
        profileMgr_->setDefaultProfile(legacy.id);
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

    // Create initial tab
    tabCtrl_->createTab(termRows_, termCols_);
    tabCtrl_->syncActivePointers();
    needsRender_ = true;
}

void TerminalController::pollPty() {
    if (!tabCtrl_) return;

    bool dataRead = tabCtrl_->pollAllPtys();
    if (dataRead) {
        needsRender_ = true;

        // Update URL detection on active screen
        Screen* scr = tabCtrl_->activeScreen();
        if (scr) {
            detectedUrls_ = urlDetector_.detectInScreen(*scr);
        }
    }

    // Cleanup dead panes
    if (tabCtrl_->cleanupDeadPanes()) {
        if (host_) host_->closeWindow();
        return;
    }
}

void TerminalController::tick() {
    // Cursor blink, resize overlay timeout, etc.
    // Platform calls this per frame; currently a placeholder for future use.
}

// --- Event entry points (delegated to InputHandler) ---

void TerminalController::onKeyEvent(const KeyEvent& e) {
    inputHandler_->onKeyEvent(e);
}

void TerminalController::onCharInput(const std::string& utf8) {
    inputHandler_->onCharInput(utf8);
}

void TerminalController::onMouseEvent(const InputMouseEvent& e) {
    inputHandler_->onMouseEvent(e);
}

void TerminalController::onResize(int pixelW, int pixelH) {
    if (!fontMgr_) return;
    int rows = 0, cols = 0;
    fontMgr_->recalcGrid(pixelW, pixelH, rows, cols);

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

// --- Search ---

void TerminalController::onSearchQuery(const std::string& query) {
    Screen* scr = activeScreen();
    if (!scr) return;
    searchCtrl_.setQuery(query, *scr);
    if (host_) {
        host_->updateSearchResults(searchCtrl_.currentMatch(),
                                   searchCtrl_.totalMatches());
    }
    needsRender_ = true;
}

void TerminalController::onSearchNext() {
    Screen* scr = activeScreen();
    if (!scr) return;
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
    searchCtrl_.prev(*scr);
    if (host_) {
        host_->updateSearchResults(searchCtrl_.currentMatch(),
                                   searchCtrl_.totalMatches());
    }
    needsRender_ = true;
}

// --- Config changes ---

void TerminalController::onConfigChanged(const Config& newConfig) {
    if (tabCtrl_ && fontMgr_) {
        configApplier_.applyFull(config_, newConfig, *tabCtrl_, *fontMgr_, host_);
        configApplier_.persist(config_);
    }
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

// --- Action dispatch ---

void TerminalController::handleAction(Action action) {
    Screen* scr = activeScreen();

    switch (action) {
        case Action::Copy:
            if (selMgr_.hasSelection() && scr) {
                std::string text = selMgr_.getSelectedText(*scr);
                if (!text.empty() && host_) {
                    host_->setClipboardText(text);
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
                tabCtrl_->createTab(termRows_, termCols_);
                needsRender_ = true;
            }
            break;

        case Action::CloseTab:
            if (tabCtrl_) {
                if (tabCtrl_->tabCount() <= 1) {
                    if (host_) host_->closeWindow();
                } else {
                    tabCtrl_->closeTab();
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
                    tabCtrl_->createTab(termRows_, termCols_, visible[idx]->id);
                    needsRender_ = true;
                }
            }
            break;
        }

        case Action::ShowProfileDropdown:
            // Platform-specific UI will handle this. No-op at controller level for now.
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
    PasteGuard guard;
    PasteAnalysis analysis = guard.analyze(text, bracketed);
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
    inputHandler_ = std::make_unique<InputHandler>(std::move(deps));
}

} // namespace termcore
