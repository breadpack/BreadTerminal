#ifndef TERMCORE_INPUT_HANDLER_H
#define TERMCORE_INPUT_HANDLER_H

#include "termcore/platform_host.h"
#include "termcore/keybinding.h"
#include "termcore/selection_manager.h"
#include "termcore/search_controller.h"
#include "termcore/vi_copy_mode.h"
#include "termcore/url_highlight.h"
#include "termcore/mouse.h"
#include <cstdint>
#include <string>

namespace termcore {

class Screen;

/// Handles keyboard, character, and mouse input events.
/// Delegates actions/PTY writes back to TerminalController via callbacks.
class InputHandler {
public:
    /// Dependencies injected from the controller (non-owning pointers).
    struct Deps {
        IPlatformHost* host = nullptr;
        KeybindingManager* keybindings = nullptr;
        SearchController* searchCtrl = nullptr;
        SelectionManager* selMgr = nullptr;

        // Callbacks into the controller
        std::function<void(Action)> handleAction;
        std::function<void(const char*, size_t)> sendPtyData;
        std::function<Screen*()> activeScreen;
        std::function<ViCopyMode*()> getCopyMode;
        std::function<int()> tabCount;
        std::function<float()> cellWidth;
        std::function<float()> cellHeight;
        std::function<bool&()> needsRender;
        std::function<UrlHighlightManager*()> urlHighlight;
    };

    explicit InputHandler(Deps deps);

    void onKeyEvent(const KeyEvent& e);
    void onCharInput(const std::string& utf8);
    void onMouseEvent(const InputMouseEvent& e);

private:
    void sendVtKey(uint32_t keycode);

    Deps d_;
};

} // namespace termcore
#endif
