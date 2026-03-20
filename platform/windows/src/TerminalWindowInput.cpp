#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "termcore/theme_loader.h"

#include <algorithm>

// --- Mouse protocol reporting ---

bool TerminalWindowState::sendMouseEvent(MouseEventType type,
                                         MouseButton button,
                                         int x, int y) {
    if (!screen || !pty) return false;
    if (screen->mouseMode() == MouseMode::None) return false;

    GridPos pos = pixelToGrid(x, y);
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    MouseEvent me;
    me.type = type;
    me.button = button;
    me.col = pos.col;
    me.row = pos.row;
    me.shift = shift;
    me.alt = alt;
    me.ctrl = ctrl;

    std::string seq = encodeMouseEvent(
        me, screen->mouseMode(), screen->mouseEncoding());
    if (seq.empty()) return false;

    sendPtyData(seq.data(), seq.size());
    return true;
}

// --- Keyboard input ---

void TerminalWindowState::handleKeyDown(WPARAM wParam, LPARAM /*lParam*/) {
    // Try keybinding lookup first
    if (keybindings) {
        uint8_t mods = 0;
        if (GetKeyState(VK_SHIFT) & 0x8000) mods |= termcore::ModShift;
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            mods |= termcore::ModCtrl;
        }
        if (GetKeyState(VK_MENU) & 0x8000) mods |= termcore::ModAlt;

        uint32_t keycode = static_cast<uint32_t>(wParam);
        // Map virtual keycodes to ASCII for printable keys
        if (keycode >= 'A' && keycode <= 'Z') {
            keycode = keycode - 'A' + 'a';  // lowercase
        }
        // Map Windows OEM virtual keys to ASCII equivalents
        // so keybindings using characters like ], [, =, -, etc. work
        switch (keycode) {
            case VK_OEM_4:     keycode = '['; break;
            case VK_OEM_6:     keycode = ']'; break;
            case VK_OEM_PLUS:  keycode = '='; break;
            case VK_OEM_MINUS: keycode = '-'; break;
            case VK_OEM_COMMA: keycode = ','; break;
            case VK_OEM_PERIOD:keycode = '.'; break;
            case VK_OEM_1:     keycode = ';'; break;
            case VK_OEM_2:     keycode = '/'; break;
            case VK_OEM_3:     keycode = '`'; break;
            case VK_OEM_5:     keycode = '\\'; break;
            case VK_OEM_7:     keycode = '\''; break;
            default: break;
        }

        termcore::KeyCombo combo{keycode, mods};
        auto action = keybindings->lookup(combo);

        // On Windows, also try with Ctrl remapped to Super (Cmd).
        // Default keybindings use "cmd" (ModSuper) which maps to Cmd on macOS.
        // This allows cross-platform keybinding configs to work on Windows.
        if (action == termcore::Action::None && (mods & termcore::ModCtrl)) {
            uint8_t superMods = (mods & ~termcore::ModCtrl) | termcore::ModSuper;
            termcore::KeyCombo superCombo{keycode, superMods};
            action = keybindings->lookup(superCombo);
        }

        if (action != termcore::Action::None) {
            switch (action) {
                case termcore::Action::Copy:
                    copySelectionToClipboard();
                    return;
                case termcore::Action::Paste:
                    pasteFromClipboard();
                    return;
                case termcore::Action::SearchOpen:
                    openSearch();
                    return;
                case termcore::Action::SearchClose:
                    closeSearch();
                    return;
                case termcore::Action::FontIncrease:
                    changeFontSize(1.0f);
                    return;
                case termcore::Action::FontDecrease:
                    changeFontSize(-1.0f);
                    return;
                case termcore::Action::FontReset:
                    resetFontSize();
                    return;
                case termcore::Action::ToggleFullscreen:
                    toggleFullscreen();
                    return;
                case termcore::Action::ScrollPageUp:
                    if (screen) screen->scrollViewportUp(screen->rows());
                    needsRender = true;
                    return;
                case termcore::Action::ScrollPageDown:
                    if (screen) screen->scrollViewportDown(screen->rows());
                    needsRender = true;
                    return;
                case termcore::Action::ScrollToTop:
                    if (screen) screen->scrollViewportToTop();
                    needsRender = true;
                    return;
                case termcore::Action::ScrollToBottom:
                    if (screen) screen->scrollViewportToBottom();
                    needsRender = true;
                    return;
                case termcore::Action::NewTab:
                    if (mux) {
                        mux->createTab(wsId, termRows, termCols);
                        syncActivePointers();
                        updateTabBar();
                        needsRender = true;
                    }
                    return;
                case termcore::Action::CloseTab:
                    if (mux) {
                        auto* tab = mux->activeTab(wsId);
                        if (tab) {
                            auto tabIds = mux->allTabIds(wsId);
                            if (tabIds.size() <= 1) {
                                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                            } else {
                                mux->destroyTab(wsId, tab->id);
                                syncActivePointers();
                                updateTabBar();
                                needsRender = true;
                            }
                        }
                    }
                    return;
                case termcore::Action::NextTab:
                    if (mux) {
                        auto* ws = mux->getWorkspace(wsId);
                        if (ws && !ws->tabs.empty()) {
                            size_t next = (ws->active_tab_index + 1) % ws->tabs.size();
                            mux->setActiveTab(wsId, ws->tabs[next]->id);
                            syncActivePointers();
                            updateTabBar();
                            needsRender = true;
                        }
                    }
                    return;
                case termcore::Action::PrevTab:
                    if (mux) {
                        auto* ws = mux->getWorkspace(wsId);
                        if (ws && !ws->tabs.empty()) {
                            size_t prev = (ws->active_tab_index + ws->tabs.size() - 1)
                                          % ws->tabs.size();
                            mux->setActiveTab(wsId, ws->tabs[prev]->id);
                            syncActivePointers();
                            updateTabBar();
                            needsRender = true;
                        }
                    }
                    return;
                case termcore::Action::SplitRight:
                    if (mux) {
                        auto* tab = mux->activeTab(wsId);
                        if (tab) {
                            mux->splitPane(wsId, tab->id, tab->active_pane,
                                           SplitDirection::Vertical, termRows, termCols);
                            syncActivePointers();
                            needsRender = true;
                        }
                    }
                    return;
                case termcore::Action::SplitDown:
                    if (mux) {
                        auto* tab = mux->activeTab(wsId);
                        if (tab) {
                            mux->splitPane(wsId, tab->id, tab->active_pane,
                                           SplitDirection::Horizontal, termRows, termCols);
                            syncActivePointers();
                            needsRender = true;
                        }
                    }
                    return;
                case termcore::Action::ClosePane:
                    if (mux) {
                        auto* tab = mux->activeTab(wsId);
                        if (tab) {
                            auto allP = mux->allPanes(wsId, tab->id);
                            auto tabIds = mux->allTabIds(wsId);
                            if (allP.size() <= 1 && tabIds.size() <= 1) {
                                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                            } else {
                                mux->closePane(wsId, tab->id, tab->active_pane);
                                syncActivePointers();
                                updateTabBar();
                                needsRender = true;
                            }
                        }
                    }
                    return;
                default:
                    break;
            }
        }
    }

    bool appCursor = screen && screen->appCursorKeys();
    const char* pfx = appCursor ? "\x1bO" : "\x1b[";
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Shift+key scrollback navigation
    if (shift && screen) {
        switch (wParam) {
            case VK_PRIOR:
                screen->scrollViewportUp(termRows);
                needsRender = true;
                return;
            case VK_NEXT:
                screen->scrollViewportDown(termRows);
                needsRender = true;
                return;
            case VK_HOME:
                screen->scrollViewportToTop();
                needsRender = true;
                return;
            case VK_END:
                screen->scrollViewportToBottom();
                needsRender = true;
                return;
        }
    }

    switch (wParam) {
        case VK_UP:
            { char s[3]={pfx[0],pfx[1],'A'}; sendPtyData(s,3); } return;
        case VK_DOWN:
            { char s[3]={pfx[0],pfx[1],'B'}; sendPtyData(s,3); } return;
        case VK_RIGHT:
            { char s[3]={pfx[0],pfx[1],'C'}; sendPtyData(s,3); } return;
        case VK_LEFT:
            { char s[3]={pfx[0],pfx[1],'D'}; sendPtyData(s,3); } return;
        case VK_RETURN:
            sendPtyData("\r", 1); return;
        case VK_BACK:
            sendPtyData("\x7f", 1); return;
        case VK_TAB:
            sendPtyData("\t", 1); return;
        case VK_ESCAPE:
            if (searchActive) { closeSearch(); return; }
            sendPtyData("\x1b", 1); return;
        case VK_HOME:
            sendPtyData("\x1b[H", 3); return;
        case VK_END:
            sendPtyData("\x1b[F", 3); return;
        case VK_PRIOR:
            sendPtyData("\x1b[5~", 4); return;
        case VK_NEXT:
            sendPtyData("\x1b[6~", 4); return;
        case VK_DELETE:
            sendPtyData("\x1b[3~", 4); return;
        case VK_F1:
            sendPtyData("\x1bOP", 3); return;
        case VK_F2:
            sendPtyData("\x1bOQ", 3); return;
        case VK_F3:
            if (searchActive) {
                if (shift) { searchPrev(); } else { searchNext(); }
                return;
            }
            sendPtyData("\x1bOR", 3); return;
        case VK_F4:
            sendPtyData("\x1bOS", 3); return;
        case VK_F5:
            sendPtyData("\x1b[15~", 5); return;
        case VK_F6:
            sendPtyData("\x1b[17~", 5); return;
        case VK_F7:
            sendPtyData("\x1b[18~", 5); return;
        case VK_F8:
            sendPtyData("\x1b[19~", 5); return;
        case VK_F9:
            sendPtyData("\x1b[20~", 5); return;
        case VK_F10:
            sendPtyData("\x1b[21~", 5); return;
        case VK_F11:
            sendPtyData("\x1b[23~", 5); return;
        case VK_F12:
            sendPtyData("\x1b[24~", 5); return;
        default:
            break;
    }

    // Ctrl+key
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl) {
        if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
            changeFontSize(1.0f);
            return;
        }
        if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
            changeFontSize(-1.0f);
            return;
        }
        if (wParam == '0' || wParam == VK_NUMPAD0) {
            resetFontSize();
            return;
        }
        if (wParam == 'F') {
            openSearch();
            return;
        }
        if (wParam == 'T' && shift) {
            // Ctrl+Shift+T: open Theme Hub
            if (!themeHub) {
                themeHub = std::make_unique<termcore::ThemeHubWindow>();
            }
            themeHub->setConfig(config);
            themeHub->setApplyCallback([this](const std::string& name,
                                                const termcore::ThemeMetadata* meta) {
                config.theme = name;
                auto theme = termcore::findTheme(name);
                if (theme) {
                    termcore::applyTheme(config, *theme);
                } else if (meta) {
                    // Fallback: apply colors from theme index metadata
                    config.background = meta->background;
                    config.foreground = meta->foreground;
                    for (int i = 0; i < 16; ++i)
                        config.palette[i] = meta->palette[i];
                }
                // Update all panes with new colors
                for (auto& [id, ps] : panes) {
                    if (ps->screen) ps->screen->initDynamicColors(config);
                }
                updateTabBar();
                applyTitleBarTheme(hwnd);
                // Update the ThemeHub popup itself with new theme colors
                themeHub->setConfig(config);
                needsRender = true;
            });
            themeHub->show(hwnd);
            return;
        }
        if (wParam == 'P' && shift) {
            // Ctrl+Shift+P: open Font Hub
            if (!fontHub) {
                fontHub = std::make_unique<termcore::FontHubWindow>();
            }
            fontHub->setConfig(config);
            fontHub->setApplyCallback([this](const std::string& name) {
                config.font_family = name;
                fontFamily = name;
                fontCollection->setPrimaryFont(fontFamily, currentFontSize);
                auto metrics = fontCollection->primaryMetrics();
                cellWidth = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
                cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;
                if (cache) cache->clear();
                RECT rc;
                GetClientRect(hwnd, &rc);
                int w = rc.right - rc.left, h = rc.bottom - rc.top;
                int cols = (std::max)(1, static_cast<int>(w / cellWidth));
                int rows = (std::max)(1, static_cast<int>(h / cellHeight));
                if (rows != termRows || cols != termCols) {
                    termRows = rows; termCols = cols;
                    for (auto& [id, ps] : panes) {
                        if (ps->screen) ps->screen->resize(rows, cols);
                        if (ps->pty && ps->pty->isAlive()) ps->pty->resize(rows, cols);
                    }
                }
                needsRender = true;
            });
            fontHub->show(hwnd);
            return;
        }
        if (wParam == VK_OEM_COMMA) {
            // Ctrl+, : open Settings
            if (!settingsWin) {
                settingsWin = std::make_unique<termcore::SettingsWindow>();
            }
            settingsWin->setConfig(config);
            settingsWin->setSaveCallback([this](const termcore::Config& updated) {
                config = updated;
                // Apply font changes
                std::string newFont = config.font_family.empty() ? "Consolas" : config.font_family;
                if (newFont != fontFamily || config.font_size != currentFontSize) {
                    fontFamily = newFont;
                    currentFontSize = config.font_size > 0 ? config.font_size : 14.0f;
                    fontCollection->setPrimaryFont(fontFamily, currentFontSize);
                    auto m = fontCollection->primaryMetrics();
                    cellWidth = m.cell_width > 0 ? m.cell_width : 8.0f;
                    cellHeight = m.cell_height > 0 ? m.cell_height : 16.0f;
                    if (cache) cache->clear();
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    int w2 = rc.right - rc.left, h2 = rc.bottom - rc.top;
                    int c2 = (std::max)(1, static_cast<int>(w2 / cellWidth));
                    int r2 = (std::max)(1, static_cast<int>(h2 / cellHeight));
                    if (r2 != termRows || c2 != termCols) {
                        termRows = r2; termCols = c2;
                        for (auto& [id, ps] : panes) {
                            if (ps->screen) ps->screen->resize(r2, c2);
                            if (ps->pty && ps->pty->isAlive()) ps->pty->resize(r2, c2);
                        }
                    }
                }
                // Apply color changes to all panes
                for (auto& [id, ps] : panes) {
                    if (ps->screen) ps->screen->initDynamicColors(config);
                }
                updateTabBar();
                needsRender = true;
            });
            settingsWin->show(hwnd);
            return;
        }
        if (wParam == 'C' && hasSelection) {
            copySelectionToClipboard();
            clearSelection();
            return;
        }
        if (wParam == 'V') {
            pasteFromClipboard();
            return;
        }
        if (wParam >= 'A' && wParam <= 'Z') {
            char c = static_cast<char>(wParam - 'A' + 1);
            sendPtyData(&c, 1);
        }
    }
}

void TerminalWindowState::handleChar(WPARAM wParam) {
    if (searchActive) return;

    wchar_t wc = static_cast<wchar_t>(wParam);
    // Filter control chars; \r and \t are already handled in handleKeyDown
    if (wc < 0x20) return;

    if (screen && !screen->isViewportAtBottom()) {
        screen->scrollViewportToBottom();
        needsRender = true;
    }

    char utf8[4];
    int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1,
                                  utf8, sizeof(utf8),
                                  nullptr, nullptr);
    if (len > 0) {
        sendPtyData(utf8, static_cast<size_t>(len));
    }
}

#endif // _WIN32
