// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_storage_module.h
#pragma once

#include "termcore/lua_module.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace termcore {

struct StorageNamespace {
    std::string name;
    std::string file_path;
    // Data stored as opaque JSON; actual nlohmann::json lives in .cpp
    // to avoid nlohmann/json.hpp in the header.
    struct Impl;
    std::shared_ptr<Impl> impl;
    bool dirty = false;
};

class LuaStorageModule : public ILuaModule {
public:
    LuaStorageModule();

    std::string_view moduleName() const override { return "storage"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::FileSystem;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Set the data directory path (called by host).
    void setDataDirectory(const std::string& path);

    // Save all dirty namespaces to disk.
    void saveAll();

private:
    std::string data_dir_;
    std::unordered_map<std::string, std::shared_ptr<StorageNamespace>> namespaces_;

    std::shared_ptr<StorageNamespace> getOrCreate(const std::string& name);
    void loadFromDisk(StorageNamespace& ns);

public:
    // Public so LuaStorageHandle can call it from the .cpp
    void saveToDisk(const StorageNamespace& ns);
};

} // namespace termcore
