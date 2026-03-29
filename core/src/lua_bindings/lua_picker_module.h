// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_picker_module.h
#pragma once

#include "termcore/lua_module.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace termcore {

/// Complete state for an active picker / input / confirm dialog.
/// All fields are plain C++ types so the renderer can read them without sol.
/// Callbacks stored as shared_ptr<void> (actual type: sol::protected_function).
struct PickerState {
    uint64_t id = 0;
    bool open = false;
    std::string title;
    std::string type; // "list", "input", "confirm"

    // List picker fields
    std::vector<std::string> items;
    std::vector<std::string> filtered_items;
    int selected_index = 0;
    std::string query;
    bool filter_enabled = false;
    bool fuzzy = false;

    // Input prompt fields
    std::string prompt;
    std::string input_text;

    // Confirm dialog fields
    std::string message;

    // Callbacks stored as shared_ptr<void> to avoid sol.hpp in header.
    std::shared_ptr<void> on_select;
    std::shared_ptr<void> on_cancel;
    std::shared_ptr<void> on_filter;
    std::shared_ptr<void> on_confirm;
    std::shared_ptr<void> on_yes;
    std::shared_ptr<void> on_no;
    std::shared_ptr<void> format_fn;
};

/// Lua module: terminal.picker
class LuaPickerModule : public ILuaModule {
public:
    LuaPickerModule();

    std::string_view moduleName() const override { return "picker"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::UI;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Host-facing API ---------------------------------------------------
    const PickerState* activePicker() const;
    void selectItem(int index);
    void cancelPicker();
    void updateQuery(const std::string& query);
    void confirmInput(const std::string& text);
    void confirmYes();
    void confirmNo();

private:
    PickerState picker_;
    uint64_t next_id_ = 1;
};

} // namespace termcore
