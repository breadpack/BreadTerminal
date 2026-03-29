// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_timer_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_timer_module.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace termcore {

struct TimerEntry {
    uint64_t id;
    uint64_t interval_ms;
    std::shared_ptr<sol::protected_function> callback;
    bool repeat;
    bool cancelled;
    uint64_t next_fire_time;
};

struct LuaTimerModule::Impl {
    std::vector<TimerEntry> timers;
    uint64_t next_id = 1;
    void* luaPtr = nullptr;

    static constexpr size_t kMaxTimers = 1024;

    uint64_t addTimer(uint64_t interval_ms,
                      sol::protected_function fn,
                      bool repeat,
                      uint64_t now_ms) {
        // Clean up cancelled timers before checking limit
        removeExpired();
        if (timers.size() >= kMaxTimers) {
            throw std::runtime_error("timer limit exceeded (max " +
                std::to_string(kMaxTimers) + ")");
        }
        uint64_t id = next_id++;
        TimerEntry entry;
        entry.id = id;
        entry.interval_ms = interval_ms;
        entry.callback = std::make_shared<sol::protected_function>(std::move(fn));
        entry.repeat = repeat;
        entry.cancelled = false;
        entry.next_fire_time = now_ms + interval_ms;
        timers.push_back(std::move(entry));
        return id;
    }

    void cancelTimer(uint64_t id) {
        for (auto& t : timers) {
            if (t.id == id) {
                t.cancelled = true;
                break;
            }
        }
    }

    void removeExpired() {
        timers.erase(
            std::remove_if(timers.begin(), timers.end(),
                           [](const TimerEntry& t) { return t.cancelled; }),
            timers.end());
    }
};

LuaTimerModule::LuaTimerModule() : impl_(std::make_unique<Impl>()) {}
LuaTimerModule::~LuaTimerModule() = default;

void LuaTimerModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    impl_->luaPtr = luaState;

    auto timer = terminal.create_named("timer");

    // terminal.timer.once(ms, callback) -> timer_id
    timer.set_function("once",
        [this](uint64_t ms, sol::protected_function fn) -> uint64_t {
            // Use 0 as current time; actual time comes from tick()
            // The timer will fire on the first tick where now_ms >= next_fire_time
            return impl_->addTimer(ms, std::move(fn), false, 0);
        });

    // terminal.timer.every(ms, callback) -> timer_id
    timer.set_function("every",
        [this](uint64_t ms, sol::protected_function fn) -> uint64_t {
            return impl_->addTimer(ms, std::move(fn), true, 0);
        });

    // terminal.timer.cancel(id)
    timer.set_function("cancel",
        [this](uint64_t id) {
            impl_->cancelTimer(id);
        });

    // terminal.timer.sleep(ms) -> returns a table that async module can await
    timer.set_function("sleep",
        [this, &lua](uint64_t ms) -> sol::table {
            sol::table awaitable = lua.create_table();
            awaitable["__timer_sleep"] = true;
            awaitable["ms"] = ms;
            awaitable["__timer_module"] = static_cast<void*>(this);
            return awaitable;
        });

    // terminal.timer.debounce(ms, callback) -> debounced_function
    // Returns a C++ callable that resets a one-shot timer on each invocation.
    // The returned function captures impl_ raw pointer, which is safe because
    // clearCallbacks() clears all timers (and thus all debounced closures)
    // before the module is destroyed.
    timer.set_function("debounce",
        [this](uint64_t ms, sol::protected_function fn) -> std::function<void()> {
            auto shared_fn = std::make_shared<sol::protected_function>(std::move(fn));
            auto active_id = std::make_shared<uint64_t>(0);
            auto* impl = impl_.get();
            auto delay = ms;

            return [impl, shared_fn, active_id, delay]() {
                // Cancel previous timer if any
                if (*active_id != 0) {
                    impl->cancelTimer(*active_id);
                }
                *active_id = impl->addTimer(delay, *shared_fn, false, 0);
            };
        });
}

int LuaTimerModule::tick(uint64_t now_ms) {
    int fired = 0;

    // Process timers - iterate by index since callbacks could add timers
    size_t count = impl_->timers.size();
    for (size_t i = 0; i < count; ++i) {
        auto& t = impl_->timers[i];
        if (t.cancelled) continue;
        if (now_ms >= t.next_fire_time) {
            auto result = (*t.callback)();
            (void)result;
            ++fired;

            if (t.repeat) {
                t.next_fire_time = now_ms + t.interval_ms;
            } else {
                t.cancelled = true;
            }
        }
    }

    impl_->removeExpired();
    return fired;
}

bool LuaTimerModule::hasPendingTimers() const {
    for (const auto& t : impl_->timers) {
        if (!t.cancelled) return true;
    }
    return false;
}

size_t LuaTimerModule::activeTimerCount() const {
    size_t count = 0;
    for (const auto& t : impl_->timers) {
        if (!t.cancelled) ++count;
    }
    return count;
}

void LuaTimerModule::clearCallbacks() {
    impl_->timers.clear();
    impl_->luaPtr = nullptr;
}

} // namespace termcore
