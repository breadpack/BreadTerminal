#ifndef TERMCORE_LUA_ENGINE_H
#define TERMCORE_LUA_ENGINE_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "termcore/result.h"
#include "termcore/lua_module.h"

namespace termcore {

enum class LuaEvent {
    OnBell,
    OnTitleChange,
    OnDirectoryChange,
    OnNotification,
    OnProcessExit,
    OnResize,
    OnKeyPress,
};

class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();
    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    Result<void> loadPlugin(const std::string& path);
    Result<void> loadString(const std::string& code);
    void fireEvent(LuaEvent event, const std::string& data = "");
    void registerFunction(const std::string& name,
                          std::function<std::string(const std::string&)> fn);
    const std::string& lastError() const { return last_error_; }
    bool isValid() const;
    const std::vector<std::string>& loadedPlugins() const {
        return loaded_plugins_;
    }

    void registerModule(std::shared_ptr<ILuaModule> module);
    void initializeModules();
    void initializeModules(const std::vector<PluginCapability>& capabilities);
    void clearAllModules();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string last_error_;
    std::vector<std::string> loaded_plugins_;
    std::vector<std::shared_ptr<ILuaModule>> modules_;
};

} // namespace termcore
#endif
