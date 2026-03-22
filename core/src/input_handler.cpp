#include "termcore/input_handler.h"
#include "termcore/screen.h"

namespace termcore {

InputHandler::InputHandler(Deps deps)
    : d_(std::move(deps))
{
}

void InputHandler::onKeyEvent(const KeyEvent& e) {
    // 1. If search active and Escape pressed, close search
    if (d_.searchCtrl && d_.searchCtrl->isActive() && e.keycode == 0xF70A) { // Escape
        d_.searchCtrl->close();
        if (d_.host) d_.host->hideSearchBar();
        d_.needsRender() = true;
        return;
    }

    // 2. If copy mode active, delegate to vi copy mode
    ViCopyMode* cm = d_.getCopyMode ? d_.getCopyMode() : nullptr;
    if (cm && cm->isActive()) {
        char key = 0;
        if (!e.text.empty()) key = e.text[0];
        else if (e.keycode < 128) key = static_cast<char>(e.keycode);
        bool ctrl = (e.modifiers & ModCtrl) != 0;
        bool shift = (e.modifiers & ModShift) != 0;
        ViAction result = cm->processKey(key, ctrl, shift);
        if (result == ViAction::Exit) {
            cm->exitCopyMode();
        } else if (result == ViAction::Yank) {
            std::string text = cm->yankSelection();
            if (!text.empty() && d_.host) {
                d_.host->setClipboardText(text);
            }
            cm->exitCopyMode();
        }
        d_.needsRender() = true;
        return;
    }

    // 3. Keybinding lookup
    if (d_.keybindings) {
        KeyCombo combo{e.keycode, e.modifiers};
        Action action = d_.keybindings->lookup(combo);

        // Ctrl->Super fallback (cross-platform keybinding compat)
        if (action == Action::None && (e.modifiers & ModCtrl)) {
            uint8_t superMods = (e.modifiers & ~ModCtrl) | ModSuper;
            KeyCombo superCombo{e.keycode, superMods};
            action = d_.keybindings->lookup(superCombo);
        }

        if (action != Action::None) {
            d_.handleAction(action);
            return;
        }
    }

    // 4. Send VT key sequence for special keys
    sendVtKey(e.keycode);
}

void InputHandler::onCharInput(const std::string& utf8) {
    if (d_.searchCtrl && d_.searchCtrl->isActive()) return;
    {
        ViCopyMode* cm = d_.getCopyMode ? d_.getCopyMode() : nullptr;
        if (cm && cm->isActive()) return;
    }

    Screen* scr = d_.activeScreen();
    if (scr && !scr->isViewportAtBottom()) {
        scr->scrollViewportToBottom();
        d_.needsRender() = true;
    }

    d_.sendPtyData(utf8.c_str(), utf8.size());
}

void InputHandler::onMouseEvent(const InputMouseEvent& e) {
    Screen* scr = d_.activeScreen();

    // Check mouse protocol
    if (scr && scr->mouseMode() != MouseMode::None) {
        int gridCol = static_cast<int>(e.x / d_.cellWidth());
        int gridRow = static_cast<int>(e.y / d_.cellHeight());

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

        std::string seq = encodeMouseEvent(me, scr->mouseMode(), scr->mouseEncoding());
        if (!seq.empty()) {
            d_.sendPtyData(seq.data(), seq.size());
            return;
        }
    }

    // Selection handling
    float cw = d_.cellWidth();
    float ch = d_.cellHeight();
    int offsetX = 0, offsetY = 0;

    // Tab bar offset
    if (d_.tabCount && d_.tabCount() > 1) {
        offsetY = static_cast<int>(ch);
    }

    // Grid coordinates for URL hover/click
    int gridCol = static_cast<int>((e.x - offsetX) / cw);
    int gridRow = static_cast<int>((e.y - offsetY) / ch);

    // URL hover tracking on mouse move
    if (e.type == InputMouseEvent::Move && d_.urlHighlight) {
        UrlHighlightManager* mgr = d_.urlHighlight();
        if (mgr && mgr->isEnabled()) {
            bool hoverChanged = mgr->updateHover(gridRow, gridCol);
            if (hoverChanged) {
                d_.needsRender() = true;
                // Update cursor to hand when hovering a URL
                if (d_.host) {
                    auto hovered = mgr->getHoveredUrl();
                    d_.host->setMouseCursor(hovered.has_value()
                        ? IPlatformHost::CursorType::Hand
                        : IPlatformHost::CursorType::Arrow);
                }
            }
        }
    }

    // Ctrl+Click (Cmd+Click on macOS) to open URL
    if (e.type == InputMouseEvent::Press && e.button == 0 && d_.urlHighlight) {
        bool ctrlHeld = (e.modifiers & ModCtrl) != 0 || (e.modifiers & ModSuper) != 0;
        if (ctrlHeld) {
            UrlHighlightManager* mgr = d_.urlHighlight();
            if (mgr && mgr->isEnabled()) {
                mgr->updateHover(gridRow, gridCol);
                auto hovered = mgr->getHoveredUrl();
                if (hovered.has_value() && d_.host) {
                    d_.host->openUrl(hovered->url);
                    return;  // Consume the click — don't start selection
                }
            }
        }
    }

    switch (e.type) {
        case InputMouseEvent::Press:
            d_.selMgr->onMouseDown(e.x, e.y, cw, ch, offsetX, offsetY);
            d_.needsRender() = true;
            break;
        case InputMouseEvent::Move:
            d_.selMgr->onMouseMove(e.x, e.y, cw, ch, offsetX, offsetY);
            if (d_.selMgr->isDragging()) d_.needsRender() = true;
            break;
        case InputMouseEvent::Release:
            d_.selMgr->onMouseUp(e.x, e.y, cw, ch, offsetX, offsetY);
            d_.needsRender() = true;
            break;
        case InputMouseEvent::DoubleClick:
            if (scr) {
                d_.selMgr->onDoubleClick(e.x, e.y, cw, ch, offsetX, offsetY, *scr);
                d_.needsRender() = true;
            }
            break;
        case InputMouseEvent::ScrollUp:
            if (scr) {
                scr->scrollViewportUp(e.scrollLines > 0 ? e.scrollLines : 3);
                d_.needsRender() = true;
            }
            break;
        case InputMouseEvent::ScrollDown:
            if (scr) {
                scr->scrollViewportDown(e.scrollLines > 0 ? e.scrollLines : 3);
                d_.needsRender() = true;
            }
            break;
    }
}

void InputHandler::sendVtKey(uint32_t keycode) {
    Screen* scr = d_.activeScreen();
    bool appCursor = scr && scr->appCursorKeys();
    const char* pfx = appCursor ? "\x1bO" : "\x1b[";

    switch (keycode) {
        case 0xF700: { char s[3]={pfx[0],pfx[1],'A'}; d_.sendPtyData(s,3); } return;
        case 0xF701: { char s[3]={pfx[0],pfx[1],'B'}; d_.sendPtyData(s,3); } return;
        case 0xF702: { char s[3]={pfx[0],pfx[1],'C'}; d_.sendPtyData(s,3); } return;
        case 0xF703: { char s[3]={pfx[0],pfx[1],'D'}; d_.sendPtyData(s,3); } return;
        case 0xF704: d_.sendPtyData("\x1b[H", 3); return;   // Home
        case 0xF705: d_.sendPtyData("\x1b[F", 3); return;   // End
        case 0xF706: d_.sendPtyData("\x1b[5~", 4); return;  // PageUp
        case 0xF707: d_.sendPtyData("\x1b[6~", 4); return;  // PageDown
        case 0xF708: d_.sendPtyData("\t", 1); return;        // Tab
        case 0xF709: d_.sendPtyData("\r", 1); return;        // Return
        case 0xF70A: d_.sendPtyData("\x1b", 1); return;      // Escape
        case 0xF70B: d_.sendPtyData("\x7f", 1); return;      // Backspace
        case 0xF70D: d_.sendPtyData("\x1b[3~", 4); return;   // Delete
        // F-keys
        case 0xF710: d_.sendPtyData("\x1bOP", 3); return;
        case 0xF711: d_.sendPtyData("\x1bOQ", 3); return;
        case 0xF712: d_.sendPtyData("\x1bOR", 3); return;
        case 0xF713: d_.sendPtyData("\x1bOS", 3); return;
        case 0xF714: d_.sendPtyData("\x1b[15~", 5); return;
        case 0xF715: d_.sendPtyData("\x1b[17~", 5); return;
        case 0xF716: d_.sendPtyData("\x1b[18~", 5); return;
        case 0xF717: d_.sendPtyData("\x1b[19~", 5); return;
        case 0xF718: d_.sendPtyData("\x1b[20~", 5); return;
        case 0xF719: d_.sendPtyData("\x1b[21~", 5); return;
        case 0xF71A: d_.sendPtyData("\x1b[23~", 5); return;
        case 0xF71B: d_.sendPtyData("\x1b[24~", 5); return;
        default:
            break;
    }
}

} // namespace termcore
