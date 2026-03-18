#ifndef TERMCORE_LUA_ENGINE_H
#define TERMCORE_LUA_ENGINE_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

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

    bool loadPlugin(const std::string& path);
    bool loadString(const std::string& code);
    void fireEvent(LuaEvent event, const std::string& data = "");
    void registerFunction(const std::string& name,
                          std::function<std::string(const std::string&)> fn);
    const std::string& lastError() const { return last_error_; }
    bool isValid() const;
    const std::vector<std::string>& loadedPlugins() const {
        return loaded_plugins_;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string last_error_;
    std::vector<std::string> loaded_plugins_;
};

} // namespace termcore
#endif
