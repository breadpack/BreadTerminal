// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_storage_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_storage_module.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace termcore {

using json = nlohmann::json;

struct StorageNamespace::Impl {
    json data = json::object();
};

// ---------------------------------------------------------------------------
// sol::object <-> nlohmann::json conversion (storage-local copy)
// ---------------------------------------------------------------------------

static json solToJson(const sol::object& obj, int depth = 0) {
    if (depth > 50) return nullptr;

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
        // Array: sequential integer keys starting at 1.
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
            // Verify keys are 1..count
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
// LuaStorageHandle — usertype exposed to Lua
// ---------------------------------------------------------------------------

struct LuaStorageHandle {
    std::shared_ptr<StorageNamespace> ns;
    LuaStorageModule* module;

    void set(const std::string& key, sol::object value) {
        ns->impl->data[key] = solToJson(value);
        ns->dirty = true;
    }

    sol::object get(sol::this_state ts, const std::string& key,
                    sol::optional<sol::object> default_val) {
        sol::state_view lua(ts);
        auto it = ns->impl->data.find(key);
        if (it == ns->impl->data.end() || it->is_null()) {
            if (default_val) return *default_val;
            return sol::nil;
        }
        return jsonToSol(lua, *it);
    }

    void delete_key(const std::string& key) {
        ns->impl->data.erase(key);
        ns->dirty = true;
    }

    bool has(const std::string& key) {
        return ns->impl->data.contains(key);
    }

    sol::table keys(sol::this_state ts) {
        sol::state_view lua(ts);
        sol::table result = lua.create_table();
        int idx = 1;
        for (auto& [k, v] : ns->impl->data.items()) {
            result[idx++] = k;
        }
        return result;
    }

    void clear() {
        ns->impl->data = json::object();
        ns->dirty = true;
    }

    void save();
};

// ---------------------------------------------------------------------------
// LuaStorageModule implementation
// ---------------------------------------------------------------------------

LuaStorageModule::LuaStorageModule() = default;

void LuaStorageModule::setDataDirectory(const std::string& path) {
    data_dir_ = path;
}

std::shared_ptr<StorageNamespace> LuaStorageModule::getOrCreate(
    const std::string& name) {
    auto it = namespaces_.find(name);
    if (it != namespaces_.end()) return it->second;

    auto ns = std::make_shared<StorageNamespace>();
    ns->name = name;
    ns->impl = std::make_shared<StorageNamespace::Impl>();

    if (!data_dir_.empty()) {
        namespace fs = std::filesystem;
        fs::create_directories(data_dir_);
        ns->file_path = (fs::path(data_dir_) / (name + ".json")).string();
        loadFromDisk(*ns);
    }

    namespaces_[name] = ns;
    return ns;
}

void LuaStorageModule::loadFromDisk(StorageNamespace& ns) {
    if (ns.file_path.empty()) return;
    namespace fs = std::filesystem;
    if (!fs::exists(ns.file_path)) return;

    std::ifstream ifs(ns.file_path);
    if (!ifs.is_open()) return;

    try {
        ns.impl->data = json::parse(ifs);
        if (!ns.impl->data.is_object()) {
            ns.impl->data = json::object();
        }
    } catch (...) {
        ns.impl->data = json::object();
    }
}

void LuaStorageModule::saveToDisk(const StorageNamespace& ns) {
    if (ns.file_path.empty()) return;

    namespace fs = std::filesystem;
    fs::create_directories(fs::path(ns.file_path).parent_path());

    // Limit storage file size to 5 MB per namespace
    std::string serialized = ns.impl->data.dump(2);
    constexpr size_t kMaxStorageSize = 5 * 1024 * 1024;
    if (serialized.size() > kMaxStorageSize) {
        // Don't write oversized data — log and skip
        return;
    }

    std::ofstream ofs(ns.file_path);
    if (ofs.is_open()) {
        ofs << serialized;
    }
}

void LuaStorageModule::saveAll() {
    for (auto& [name, ns] : namespaces_) {
        if (ns->dirty) {
            saveToDisk(*ns);
            ns->dirty = false;
        }
    }
}

void LuaStorageHandle::save() {
    if (module && ns->dirty) {
        module->saveToDisk(*ns);
        ns->dirty = false;
    }
}

void LuaStorageModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    // Register LuaStorageHandle usertype
    lua.new_usertype<LuaStorageHandle>("__bt_StorageHandle",
        "set", &LuaStorageHandle::set,
        "get", &LuaStorageHandle::get,
        "delete", &LuaStorageHandle::delete_key,
        "has", &LuaStorageHandle::has,
        "keys", &LuaStorageHandle::keys,
        "clear", &LuaStorageHandle::clear,
        "save", &LuaStorageHandle::save);

    auto storage = terminal.create_named("storage");

    storage.set_function("open",
        [this](const std::string& name) -> LuaStorageHandle {
            // Validate namespace name: only alphanumeric, underscore, hyphen
            for (char ch : name) {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
                    throw std::runtime_error(
                        "storage.open: invalid namespace name '" + name +
                        "' (only alphanumeric, underscore, hyphen allowed)");
                }
            }
            if (name.empty() || name.size() > 64) {
                throw std::runtime_error(
                    "storage.open: namespace name must be 1-64 characters");
            }
            auto ns = getOrCreate(name);
            return LuaStorageHandle{ns, this};
        });
}

void LuaStorageModule::clearCallbacks() {
    saveAll();
    namespaces_.clear();
}

} // namespace termcore
