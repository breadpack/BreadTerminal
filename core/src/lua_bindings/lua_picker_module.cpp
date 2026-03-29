// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_picker_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_picker_module.h"

#include <algorithm>
#include <string>

namespace termcore {

// Helper: cast shared_ptr<void> to shared_ptr<sol::protected_function>
static auto asFn(const std::shared_ptr<void>& p)
    -> std::shared_ptr<sol::protected_function> {
    return std::static_pointer_cast<sol::protected_function>(p);
}

static std::shared_ptr<void> makeFn(sol::protected_function fn) {
    return std::make_shared<sol::protected_function>(std::move(fn));
}

// ---------------------------------------------------------------------------
// Simple fuzzy-match: every character in query must appear (in order) in item.
// ---------------------------------------------------------------------------
static bool fuzzyMatch(const std::string& query, const std::string& item) {
    size_t qi = 0;
    for (size_t i = 0; i < item.size() && qi < query.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(item[i])) ==
            std::tolower(static_cast<unsigned char>(query[qi]))) {
            ++qi;
        }
    }
    return qi == query.size();
}

// Simple substring match (case-insensitive).
static bool substringMatch(const std::string& query, const std::string& item) {
    if (query.empty()) return true;
    std::string lq = query, li = item;
    std::transform(lq.begin(), lq.end(), lq.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(li.begin(), li.end(), li.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return li.find(lq) != std::string::npos;
}

// ---------------------------------------------------------------------------
LuaPickerModule::LuaPickerModule() = default;

const PickerState* LuaPickerModule::activePicker() const {
    if (picker_.open) return &picker_;
    return nullptr;
}

void LuaPickerModule::selectItem(int index) {
    if (!picker_.open || picker_.type != "list") return;

    const auto& src = picker_.filtered_items.empty() && picker_.query.empty()
                          ? picker_.items
                          : picker_.filtered_items;
    if (index < 0 || index >= static_cast<int>(src.size())) return;

    picker_.open = false;
    if (picker_.on_select) {
        auto fn = asFn(picker_.on_select);
        // Lua is 1-indexed
        auto r = (*fn)(index + 1, src[static_cast<size_t>(index)]);
        (void)r;
    }
}

void LuaPickerModule::cancelPicker() {
    if (!picker_.open) return;
    picker_.open = false;
    if (picker_.type == "list" && picker_.on_cancel) {
        auto fn = asFn(picker_.on_cancel);
        auto r = (*fn)();
        (void)r;
    }
}

void LuaPickerModule::updateQuery(const std::string& query) {
    if (!picker_.open || picker_.type != "list") return;
    picker_.query = query;
    picker_.selected_index = 0;

    // Use custom filter if provided
    if (picker_.on_filter) {
        auto fn = asFn(picker_.on_filter);
        sol::table result_tbl = (*fn)(query);
        picker_.filtered_items.clear();
        for (auto& kv : result_tbl) {
            picker_.filtered_items.push_back(kv.second.as<std::string>());
        }
        return;
    }

    // Built-in filtering
    picker_.filtered_items.clear();
    for (const auto& item : picker_.items) {
        bool match = picker_.fuzzy ? fuzzyMatch(query, item)
                                   : substringMatch(query, item);
        if (match) {
            picker_.filtered_items.push_back(item);
        }
    }
}

void LuaPickerModule::confirmInput(const std::string& text) {
    if (!picker_.open || picker_.type != "input") return;
    picker_.open = false;
    if (picker_.on_confirm) {
        auto fn = asFn(picker_.on_confirm);
        auto r = (*fn)(text);
        (void)r;
    }
}

void LuaPickerModule::confirmYes() {
    if (!picker_.open || picker_.type != "confirm") return;
    picker_.open = false;
    if (picker_.on_yes) {
        auto fn = asFn(picker_.on_yes);
        auto r = (*fn)();
        (void)r;
    }
}

void LuaPickerModule::confirmNo() {
    if (!picker_.open || picker_.type != "confirm") return;
    picker_.open = false;
    if (picker_.on_no) {
        auto fn = asFn(picker_.on_no);
        auto r = (*fn)();
        (void)r;
    }
}

// ---------------------------------------------------------------------------
void LuaPickerModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto picker = terminal.create_named("picker");

    // terminal.picker.show(opts)
    picker.set_function("show",
        [this](sol::table opts) {
            // Close any existing picker first
            if (picker_.open) {
                cancelPicker();
            }

            picker_ = PickerState{};
            picker_.id = next_id_++;
            picker_.open = true;
            picker_.type = "list";
            picker_.title = opts.get_or<std::string>("title", "");
            picker_.filter_enabled = opts.get_or("filter", false);
            picker_.fuzzy = opts.get_or("fuzzy", false);

            // Collect items
            sol::optional<sol::table> items_opt =
                opts.get<sol::optional<sol::table>>("items");
            if (items_opt) {
                for (auto& kv : *items_opt) {
                    if (kv.second.is<std::string>()) {
                        picker_.items.push_back(kv.second.as<std::string>());
                    } else if (kv.second.is<sol::table>()) {
                        sol::table item_tbl = kv.second.as<sol::table>();
                        std::string text = item_tbl.get_or<std::string>("text", "");
                        picker_.items.push_back(text);
                    }
                }
            }
            picker_.filtered_items = picker_.items;

            // Store callbacks
            sol::optional<sol::protected_function> on_select =
                opts.get<sol::optional<sol::protected_function>>("on_select");
            if (on_select) picker_.on_select = makeFn(std::move(*on_select));

            sol::optional<sol::protected_function> on_cancel =
                opts.get<sol::optional<sol::protected_function>>("on_cancel");
            if (on_cancel) picker_.on_cancel = makeFn(std::move(*on_cancel));

            sol::optional<sol::protected_function> on_filter =
                opts.get<sol::optional<sol::protected_function>>("on_filter");
            if (on_filter) picker_.on_filter = makeFn(std::move(*on_filter));

            sol::optional<sol::protected_function> format =
                opts.get<sol::optional<sol::protected_function>>("format");
            if (format) picker_.format_fn = makeFn(std::move(*format));
        });

    // terminal.picker.input(opts)
    picker.set_function("input",
        [this](sol::table opts) {
            if (picker_.open) cancelPicker();

            picker_ = PickerState{};
            picker_.id = next_id_++;
            picker_.open = true;
            picker_.type = "input";
            picker_.title = opts.get_or<std::string>("title", "");
            picker_.prompt = opts.get_or<std::string>("prompt", "");
            picker_.input_text = opts.get_or<std::string>("default", "");

            sol::optional<sol::protected_function> on_confirm =
                opts.get<sol::optional<sol::protected_function>>("on_confirm");
            if (on_confirm) picker_.on_confirm = makeFn(std::move(*on_confirm));

            sol::optional<sol::protected_function> on_cancel =
                opts.get<sol::optional<sol::protected_function>>("on_cancel");
            if (on_cancel) picker_.on_cancel = makeFn(std::move(*on_cancel));
        });

    // terminal.picker.confirm(opts)
    picker.set_function("confirm",
        [this](sol::table opts) {
            if (picker_.open) cancelPicker();

            picker_ = PickerState{};
            picker_.id = next_id_++;
            picker_.open = true;
            picker_.type = "confirm";
            picker_.title = opts.get_or<std::string>("title", "");
            picker_.message = opts.get_or<std::string>("message", "");

            sol::optional<sol::protected_function> on_yes =
                opts.get<sol::optional<sol::protected_function>>("on_yes");
            if (on_yes) picker_.on_yes = makeFn(std::move(*on_yes));

            sol::optional<sol::protected_function> on_no =
                opts.get<sol::optional<sol::protected_function>>("on_no");
            if (on_no) picker_.on_no = makeFn(std::move(*on_no));
        });
}

// ---------------------------------------------------------------------------
void LuaPickerModule::clearCallbacks() {
    if (picker_.open) {
        picker_.open = false;
        if (picker_.type == "list" && picker_.on_cancel) {
            auto fn = asFn(picker_.on_cancel);
            auto r = (*fn)();
            (void)r;
        }
    }
    picker_ = PickerState{};
}

} // namespace termcore
