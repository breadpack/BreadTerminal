// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_json_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_json_module.h"

#include <nlohmann/json.hpp>
#include <string>

namespace termcore {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// sol::object <-> nlohmann::json conversion (json-module-local copy)
// ---------------------------------------------------------------------------

static json solToJson(const sol::object& obj, int depth = 0) {
    if (depth > 50) {
        throw std::runtime_error("json.encode: depth limit exceeded (circular reference?)");
    }

    switch (obj.get_type()) {
    case sol::type::string:
        return obj.as<std::string>();
    case sol::type::number: {
        double d = obj.as<double>();
        if (d == static_cast<double>(static_cast<int64_t>(d))) {
            return static_cast<int64_t>(d);
        }
        return d;
    }
    case sol::type::boolean:
        return obj.as<bool>();
    case sol::type::table: {
        sol::table t = obj;
        // Determine if array or object.
        bool is_array = true;
        std::size_t count = 0;
        for (auto& [k, v] : t) {
            ++count;
            if (k.get_type() != sol::type::number) {
                is_array = false;
                break;
            }
        }
        if (is_array && count > 0) {
            for (std::size_t i = 1; i <= count; ++i) {
                sol::object v = t[static_cast<int>(i)];
                if (!v.valid() || v.get_type() == sol::type::none) {
                    is_array = false;
                    break;
                }
            }
        }
        if (count == 0) is_array = false;

        if (is_array) {
            json arr = json::array();
            for (std::size_t i = 1; i <= count; ++i) {
                arr.push_back(solToJson(t[static_cast<int>(i)], depth + 1));
            }
            return arr;
        } else {
            json obj_json = json::object();
            for (auto& [k, v] : t) {
                std::string key;
                if (k.get_type() == sol::type::string) {
                    key = k.as<std::string>();
                } else if (k.get_type() == sol::type::number) {
                    key = std::to_string(k.as<int>());
                } else {
                    continue;
                }
                obj_json[key] = solToJson(v, depth + 1);
            }
            return obj_json;
        }
    }
    default:
        return nullptr;
    }
}

static sol::object jsonToSol(sol::state_view lua, const json& j) {
    switch (j.type()) {
    case json::value_t::string:
        return sol::make_object(lua, j.get<std::string>());
    case json::value_t::number_integer:
        return sol::make_object(lua, j.get<int64_t>());
    case json::value_t::number_unsigned:
        return sol::make_object(lua, j.get<uint64_t>());
    case json::value_t::number_float:
        return sol::make_object(lua, j.get<double>());
    case json::value_t::boolean:
        return sol::make_object(lua, j.get<bool>());
    case json::value_t::array: {
        sol::table t = lua.create_table(static_cast<int>(j.size()), 0);
        int idx = 1;
        for (auto& elem : j) {
            t[idx++] = jsonToSol(lua, elem);
        }
        return t;
    }
    case json::value_t::object: {
        sol::table t = lua.create_table(0, static_cast<int>(j.size()));
        for (auto& [k, v] : j.items()) {
            t[k] = jsonToSol(lua, v);
        }
        return t;
    }
    default:
        return sol::nil;
    }
}

// ---------------------------------------------------------------------------
// LuaJsonModule
// ---------------------------------------------------------------------------

void LuaJsonModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto json_table = terminal.create_named("json");

    // terminal.json.encode(value [, options])
    json_table.set_function("encode",
        [](sol::object value, sol::optional<sol::table> options) -> std::string {
            json j = solToJson(value);

            bool pretty = false;
            int indent = 4;

            if (options) {
                sol::optional<bool> p = (*options)["pretty"];
                if (p) pretty = *p;
                sol::optional<int> i = (*options)["indent"];
                if (i) indent = *i;
            }

            if (pretty) {
                return j.dump(indent);
            }
            return j.dump();
        });

    // terminal.json.decode(str)
    json_table.set_function("decode",
        [&lua](const std::string& str) -> sol::object {
            // Limit input size to 10 MB to prevent memory exhaustion
            constexpr size_t kMaxJsonSize = 10 * 1024 * 1024;
            if (str.size() > kMaxJsonSize) {
                throw std::runtime_error("json.decode: input too large (max 10 MB)");
            }
            try {
                json j = json::parse(str);
                return jsonToSol(lua, j);
            } catch (const json::parse_error& e) {
                throw std::runtime_error(std::string("json.decode: ") + e.what());
            }
        });
}

} // namespace termcore
