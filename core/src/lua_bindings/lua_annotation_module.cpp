// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_annotation_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_annotation_module.h"
#include "termcore/annotations.h"

namespace termcore {

LuaAnnotationModule::LuaAnnotationModule(AnnotationManager* annotMgr)
    : annotMgr_(annotMgr) {}

void LuaAnnotationModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto annotation = terminal.create_named("annotation");

    // terminal.annotation.add(row, "text", {color="#ffff00"})
    // Returns the annotation ID.
    annotation.set_function("add",
        [this](int row, const std::string& text, sol::optional<sol::table> opts) -> int {
            if (!annotMgr_) return -1;
            uint32_t color = 0xFFFF00;
            if (opts) {
                sol::object colorObj = (*opts)["color"];
                if (colorObj.is<std::string>()) {
                    std::string colorStr = colorObj.as<std::string>();
                    if (!colorStr.empty() && colorStr[0] == '#') {
                        colorStr = colorStr.substr(1);
                    }
                    try {
                        color = static_cast<uint32_t>(
                            std::stoul(colorStr, nullptr, 16));
                    } catch (...) {}
                } else if (colorObj.is<int>()) {
                    color = static_cast<uint32_t>(colorObj.as<int>());
                }
            }
            return annotMgr_->addAnnotation(row, text, -1, -1, color);
        });

    // terminal.annotation.remove(id)
    annotation.set_function("remove",
        [this](int id) -> bool {
            if (!annotMgr_) return false;
            return annotMgr_->removeAnnotation(id);
        });

    // terminal.annotation.set_badge_format("{branch} | {cwd}")
    annotation.set_function("set_badge_format",
        [this](const std::string& fmt) {
            if (annotMgr_) {
                annotMgr_->setBadgeFormat(fmt);
            }
        });

    // terminal.annotation.on_pattern("ERROR", function(row, text) end)
    annotation.set_function("on_pattern",
        [this](const std::string& pattern, sol::protected_function fn) {
            if (!annotMgr_) return;
            auto fnPtr = std::make_shared<sol::protected_function>(std::move(fn));
            patternFns_.push_back(fnPtr);
            annotMgr_->addPatternCallback(pattern,
                [fnPtr](int row, const std::string& text) {
                    if (fnPtr && fnPtr->valid()) {
                        (*fnPtr)(row, text);
                    }
                });
        });
}

void LuaAnnotationModule::clearCallbacks() {
    patternFns_.clear();
    if (annotMgr_) {
        annotMgr_->clearPatternCallbacks();
    }
}

} // namespace termcore
