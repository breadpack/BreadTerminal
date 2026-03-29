// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_async_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_async_module.h"
#include "lua_timer_module.h"

extern "C" {
#include <lua.h>
}

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace termcore {

enum class CoroutineState {
    Ready,          // Ready to resume immediately
    WaitingTimer,   // Waiting for a timer to fire
    Finished,       // Completed or errored
};

struct CoroutineEntry {
    sol::thread thread;
    sol::coroutine coroutine;
    CoroutineState state = CoroutineState::Ready;
    uint64_t wake_time_ms = 0;      // When to wake from WaitingTimer
    uint64_t pending_sleep_ms = 0;   // Duration set by await(sleep())
};

struct LuaAsyncModule::Impl {
    std::vector<CoroutineEntry> coroutines;
    LuaTimerModule* timerModule = nullptr;
    void* luaPtr = nullptr;

    static constexpr size_t kMaxCoroutines = 256;

    void removeFinished() {
        coroutines.erase(
            std::remove_if(coroutines.begin(), coroutines.end(),
                           [](const CoroutineEntry& c) {
                               return c.state == CoroutineState::Finished;
                           }),
            coroutines.end());
    }
};

LuaAsyncModule::LuaAsyncModule() : impl_(std::make_unique<Impl>()) {}
LuaAsyncModule::~LuaAsyncModule() = default;

void LuaAsyncModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    impl_->luaPtr = luaState;

    // terminal.async(fn) - wraps fn in a coroutine and schedules it
    terminal.set_function("async",
        [this, &lua](sol::protected_function fn) {
            impl_->removeFinished();
            if (impl_->coroutines.size() >= Impl::kMaxCoroutines) {
                throw std::runtime_error("coroutine limit exceeded (max " +
                    std::to_string(Impl::kMaxCoroutines) + ")");
            }

            sol::thread th = sol::thread::create(lua);
            sol::coroutine co(th.state(), fn);

            CoroutineEntry entry;
            entry.thread = std::move(th);
            entry.coroutine = std::move(co);
            entry.state = CoroutineState::Ready;
            impl_->coroutines.push_back(std::move(entry));
        });

    // terminal.yield and terminal.await are Lua wrappers around coroutine.yield
    // They must be Lua functions (not C functions) to avoid yielding across C boundary
    lua.safe_script(R"(
        terminal.yield = function()
            coroutine.yield()
        end
        terminal.await = function(awaitable)
            if type(awaitable) == "table" and awaitable.__timer_sleep then
                -- Pass sleep duration as second yield value
                coroutine.yield("__timer_sleep", awaitable.ms)
            else
                coroutine.yield()
            end
        end
    )");
}

void LuaAsyncModule::tick(uint64_t now_ms) {
    for (auto& entry : impl_->coroutines) {
        if (entry.state == CoroutineState::Finished) continue;

        if (entry.state == CoroutineState::WaitingTimer) {
            // Check if the sleep timer has expired
            if (now_ms >= entry.wake_time_ms) {
                entry.state = CoroutineState::Ready;
            } else {
                continue;
            }
        }

        // Resume ready coroutines
        if (entry.state == CoroutineState::Ready) {
            // Use coroutine.status() to check if it's runnable.
            // Do NOT use thread.status() before first resume — sol2 reports
            // newly created threads (empty stack) as 'dead'.
            auto co_status = entry.coroutine.status();
            if (co_status != sol::call_status::runtime &&
                co_status != sol::call_status::gc &&
                co_status != sol::call_status::memory) {
                auto result = entry.coroutine();
                (void)result;

                // After resume, check thread status
                auto new_status = entry.thread.status();
                if (new_status == sol::thread_status::dead) {
                    entry.state = CoroutineState::Finished;
                } else {
                    // Check if the yield value indicates a sleep request
                    // coroutine.yield("__timer_sleep", ms) from terminal.await()
                    lua_State* L = entry.thread.lua_state();
                    int nvals = lua_gettop(L);
                    if (nvals >= 1 && lua_type(L, 1) == LUA_TSTRING) {
                        const char* val = lua_tostring(L, 1);
                        if (val && std::string(val) == "__timer_sleep" && nvals >= 2) {
                            uint64_t sleep_ms = static_cast<uint64_t>(
                                lua_tonumber(L, 2));
                            entry.state = CoroutineState::WaitingTimer;
                            entry.wake_time_ms = now_ms + sleep_ms;
                        }
                    }
                    // Otherwise stays Ready, will resume next tick
                }
            } else {
                entry.state = CoroutineState::Finished;
            }
        }
    }

    impl_->removeFinished();
}

void LuaAsyncModule::setTimerModule(LuaTimerModule* timer) {
    impl_->timerModule = timer;
}

bool LuaAsyncModule::hasPendingCoroutines() const {
    for (const auto& c : impl_->coroutines) {
        if (c.state != CoroutineState::Finished) return true;
    }
    return false;
}

size_t LuaAsyncModule::activeCoroutineCount() const {
    size_t count = 0;
    for (const auto& c : impl_->coroutines) {
        if (c.state != CoroutineState::Finished) ++count;
    }
    return count;
}

void LuaAsyncModule::clearCallbacks() {
    impl_->coroutines.clear();
    impl_->luaPtr = nullptr;
}

} // namespace termcore
