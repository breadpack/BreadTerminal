// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_url_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_url_module.h"
#include "termcore/url_detector.h"
#include "termcore/url_highlight.h"

#include <cstdint>
#include <string>

namespace termcore {

// Parse a CSS-style hex color string like "#ff6600" or "0xff6600" or decimal.
static uint32_t parseColor(const std::string& s) {
    if (s.empty()) return 0;
    size_t start = 0;
    if (s[0] == '#') start = 1;
    else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) start = 2;
    return static_cast<uint32_t>(std::stoul(s.substr(start), nullptr, 16));
}

LuaUrlModule::LuaUrlModule(UrlDetector* detector, UrlHighlightManager* highlight)
    : detector_(detector), highlight_(highlight) {}

void LuaUrlModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto url = terminal.create_named("url");

    // terminal.url.add_scheme("magnet", "obsidian")
    url.set_function("add_scheme",
        [this](sol::variadic_args args) {
            for (auto arg : args) {
                if (arg.get_type() == sol::type::string) {
                    if (detector_) detector_->addCustomScheme(arg.get<std::string>());
                }
            }
        });

    // terminal.url.on_click(function(url) end)
    url.set_function("on_click",
        [this](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            if (highlight_) {
                highlight_->setClickCallback([luaFn](const std::string& u) -> bool {
                    auto result = (*luaFn)(u);
                    if (result.valid()) {
                        sol::object val = result;
                        if (val.is<bool>()) return val.as<bool>();
                    }
                    return false;
                });
            }
        });

    // terminal.url.set_color(0x89b4fa) -- set default URL color
    url.set_function("set_color",
        [this](uint32_t color) {
            if (highlight_) highlight_->setUrlColor(color);
        });

    // terminal.url.set_terminators(" \t\n<>\"'`")
    url.set_function("set_terminators",
        [this](const std::string& chars) {
            if (detector_) detector_->setTerminators(chars);
        });

    // terminal.url.set_trailing_punctuation(".,:;!?")
    url.set_function("set_trailing_punctuation",
        [this](const std::string& chars) {
            if (detector_) detector_->setTrailingPunctuation(chars);
        });

    // terminal.url.set_color_by_scheme("ssh", "#ff6600")
    url.set_function("set_color_by_scheme",
        [this](std::string scheme, sol::object colorVal) {
            if (!highlight_) return;
            uint32_t color = 0;
            if (colorVal.is<uint32_t>()) {
                color = colorVal.as<uint32_t>();
            } else if (colorVal.is<std::string>()) {
                color = parseColor(colorVal.as<std::string>());
            }
            highlight_->setSchemeColor(scheme, color);
        });
}

void LuaUrlModule::clearCallbacks() {
    if (highlight_) {
        highlight_->setClickCallback(nullptr);
    }
}

} // namespace termcore
