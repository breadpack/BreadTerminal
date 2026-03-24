// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_mux_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_mux_module.h"
#include "termcore/mux.h"
#include "termcore/tab_controller.h"

#include <string>
#include <unordered_map>

namespace termcore {

struct LuaMuxModule::LayoutCallbacks {
    std::unordered_map<std::string, std::shared_ptr<sol::protected_function>> map;
};

static LayoutPreset parseLayoutPreset(const std::string& name) {
    if (name == "even_horizontal" || name == "horizontal") return LayoutPreset::EvenHorizontal;
    if (name == "even_vertical"   || name == "vertical")   return LayoutPreset::EvenVertical;
    if (name == "tiled"           || name == "grid")       return LayoutPreset::Tiled;
    if (name == "main_left"       || name == "main-left")  return LayoutPreset::MainLeft;
    if (name == "main_top"        || name == "main-top")   return LayoutPreset::MainTop;
    return LayoutPreset::Tiled;  // default fallback
}

static BroadcastMode parseBroadcastMode(const std::string& mode) {
    if (mode == "all")      return BroadcastMode::All;
    if (mode == "selected") return BroadcastMode::Selected;
    return BroadcastMode::Off;
}

LuaMuxModule::LuaMuxModule(Mux* mux, TabController* tabCtrl)
    : mux_(mux), tabCtrl_(tabCtrl),
      layoutCallbacks_(std::make_shared<LayoutCallbacks>()) {}

void LuaMuxModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    sol::state* luaPtr = &lua;

    auto muxTbl = terminal.create_named("mux");

    // terminal.mux.layout("tiled") -- apply a layout preset to the active tab
    muxTbl.set_function("layout",
        [this](std::string name) {
            if (!mux_) return;
            auto wsId = mux_->activeWorkspaceId();
            auto* tab = mux_->activeTab(wsId);
            if (!tab) return;
            mux_->applyLayout(wsId, tab->id, parseLayoutPreset(name));
        });

    // terminal.mux.split("right", 0.3) -- split active pane
    muxTbl.set_function("split",
        [this](std::string direction, sol::optional<double> /*ratio*/) {
            if (!tabCtrl_) return;
            // ratio is stored in the split node; TabController uses fixed default
            if (direction == "right" || direction == "horizontal") {
                tabCtrl_->splitRight(24, 80);
            } else {
                tabCtrl_->splitDown(24, 80);
            }
        });

    // terminal.mux.broadcast("all" | "selected" | "off")
    muxTbl.set_function("broadcast",
        [this](std::string mode) {
            if (!mux_) return;
            mux_->setBroadcastMode(parseBroadcastMode(mode));
        });

    // terminal.mux.zoom_toggle() -- toggle zoom on active pane
    muxTbl.set_function("zoom_toggle",
        [this]() {
            if (!mux_) return;
            auto wsId = mux_->activeWorkspaceId();
            auto* tab = mux_->activeTab(wsId);
            if (!tab) return;
            mux_->toggleZoom(wsId, tab->id);
        });

    // terminal.mux.define_layout("name", function(panes) end) -- store callback
    muxTbl.set_function("define_layout",
        [this](std::string name, sol::protected_function fn) {
            layoutCallbacks_->map[name] =
                std::make_shared<sol::protected_function>(std::move(fn));
        });

    // terminal.mux.apply_custom_layout("name") -- invoke a defined Lua layout
    muxTbl.set_function("apply_custom_layout",
        [this, luaPtr](std::string name) {
            auto it = layoutCallbacks_->map.find(name);
            if (it == layoutCallbacks_->map.end() || !it->second) return;
            if (!mux_) return;
            auto wsId = mux_->activeWorkspaceId();
            auto* tab = mux_->activeTab(wsId);
            if (!tab) return;
            auto panes = mux_->allPanes(wsId, tab->id);
            sol::table tbl = luaPtr->create_table();
            for (size_t i = 0; i < panes.size(); ++i) {
                tbl[static_cast<int>(i) + 1] = static_cast<int>(panes[i]);
            }
            auto result = (*it->second)(tbl);
            (void)result;
        });
}

void LuaMuxModule::clearCallbacks() {
    layoutCallbacks_->map.clear();
}

} // namespace termcore
