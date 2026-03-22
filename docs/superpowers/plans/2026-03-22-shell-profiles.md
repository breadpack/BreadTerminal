# Shell Profile System Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** OS별 쉘 자동 감지 + Windows Terminal 스타일 프로필 시스템으로 쉘+외형을 묶어 관리

**Architecture:** ProfileManager가 ShellDetector(런타임 감지)와 사용자 프로필(Lua config)을 merge하여 통합 목록 제공. TabController가 `pendingProfileId_` 임시 멤버를 통해 프로필 정보를 Mux 콜백 체인에 전달. PtyFactory 시그니처를 Profile 기반으로 변경하여 프로필별 쉘+인자+작업디렉토리를 PTY에 전달. 세션 저장/복원에 profile_id 추가(version 2).

**Tech Stack:** C++17, GoogleTest, Sol3/Lua 5.4, nlohmann/json, CMake

**Spec:** `docs/superpowers/specs/2026-03-22-shell-profiles-design.md`

**Test binary:** `termcore_tests` (all test run commands use this name)

**Out of scope for this plan:** Tab Bar Dropdown UI rendering, Icon rendering (these are platform-specific rendering tasks that will be planned separately after core logic is in place).

---

## File Structure

| File | Purpose | Action |
|---|---|---|
| `core/include/termcore/profile.h` | Profile struct, ProfileManager, ShellDetector, resolveProfileConfig 선언 | Create |
| `core/src/profile_manager.cpp` | ProfileManager 구현 (merge, default, lookup, fallback) + resolveProfileConfig | Create |
| `core/src/shell_detector.cpp` | ShellDetector 공통 로직 + 플랫폼 디스패치 | Create |
| `core/src/shell_detector_windows.cpp` | Windows 감지 (cmd, PS, Git Bash, WSL) | Create |
| `core/src/shell_detector_unix.cpp` | macOS/Linux 감지 (/etc/shells) | Create |
| `core/include/termcore/config.h` | Config에 profiles, default_profile_id, hidden_profile_ids 추가 | Modify |
| `core/include/termcore/keybinding.h` | Action enum에 NewTabProfile1~9, ShowProfileDropdown 추가 | Modify |
| `core/src/keybinding.cpp` | parseAction에 새 액션 매핑 추가 | Modify |
| `core/src/keybinding_presets.cpp` | 프리셋에 프로필 관련 단축키 추가 | Modify |
| `core/include/termcore/tab_controller.h` | PtyFactory 시그니처 변경, pendingProfileId_, createTab/split에 profile_id | Modify |
| `core/src/tab_controller.cpp` | pendingProfileId_ 설정 후 Mux 호출, createPaneState에서 읽기 | Modify |
| `core/include/termcore/platform_host.h` | createPty 시그니처를 Profile 기반으로 변경 | Modify |
| `core/src/terminal_controller.cpp` | ProfileManager 통합, handleAction에 NewTabProfile 처리 | Modify |
| `core/include/termcore/terminal_controller.h` | ProfileManager 멤버 추가 | Modify |
| `platform/windows/include/TerminalWindowState.h` | createPty 선언 변경 | Modify |
| `platform/windows/src/TerminalWindowStateEvents.cpp` | createPty Profile 기반 구현 | Modify |
| `platform/macos/include/MacPlatformHost.h` | createPty 선언 변경 | Modify |
| `platform/macos/src/MacPlatformHost.mm` | createPty Profile 기반 구현 | Modify |
| `platform/linux/include/GtkPlatformHost.h` | createPty 선언 변경 | Modify |
| `platform/linux/src/GtkPlatformHost.cpp` | createPty Profile 기반 구현 | Modify |
| `core/include/termcore/session.h` | PaneSessionData에 profile_id, IPaneStateProvider에 getProfileId 추가 | Modify |
| `core/src/session_serializer.cpp` | profile_id 직렬화, version 2 | Modify |
| `core/src/session_deserializer.cpp` | profile_id 역직렬화, v1→v2 마이그레이션 | Modify |
| `core/src/lua_config.cpp` | terminal.profile(), default_profile(), hide_profile() API | Modify |
| `core/src/lua_config_writer.cpp` | 프로필 직렬화 | Modify |
| `core/src/settings_model.cpp` | Profiles 카테고리 추가 | Modify |
| `core/src/config_value_adapter.cpp` | default_profile_id string 접근자 | Modify |
| `core/src/termcore_api.cpp` | PtyFactory 시그니처 변경 반영 | Modify |
| `core/CMakeLists.txt` | 새 소스 파일 등록 | Modify |
| `tests/test_profile.cpp` | ProfileManager 단위 테스트 | Create |
| `tests/test_shell_detector.cpp` | ShellDetector 단위 테스트 | Create |
| `tests/test_session.cpp` | MockPaneProvider에 getProfileId 추가 | Modify |

---

## Chunk 1: Core Data Structures

### Task 1: Profile struct & resolveProfileConfig

**Files:**
- Create: `core/include/termcore/profile.h`
- Create: `core/src/profile_manager.cpp`
- Test: `tests/test_profile.cpp`

**Note on include strategy:** `profile.h`는 `config.h`를 include하지 않는다. `Config`는 forward declaration만 사용. `config.h`가 `profile.h`를 include한다 (Task 3에서). `resolveProfileConfig()`는 `profile.h`에 선언, `profile_manager.cpp`에서 양쪽 헤더 모두 include하여 구현.

- [ ] **Step 1: Write failing tests for Profile struct and resolveProfileConfig**

```cpp
// tests/test_profile.cpp
#include <gtest/gtest.h>
#include "termcore/profile.h"
#include "termcore/config.h"

using namespace termcore;

TEST(ProfileTest, DefaultConstruction) {
    Profile p;
    EXPECT_TRUE(p.id.empty());
    EXPECT_TRUE(p.name.empty());
    EXPECT_TRUE(p.command.empty());
    EXPECT_TRUE(p.args.empty());
    EXPECT_TRUE(p.working_dir.empty());
    EXPECT_TRUE(p.icon.empty());
    EXPECT_FALSE(p.is_default);
    EXPECT_FALSE(p.hidden);
    EXPECT_FALSE(p.auto_detected);
    EXPECT_FALSE(p.theme.has_value());
    EXPECT_FALSE(p.font_family.has_value());
    EXPECT_FALSE(p.font_size.has_value());
    EXPECT_FALSE(p.cursor_style.has_value());
}

TEST(ProfileTest, ResolveProfileConfig_NoOverrides) {
    Config global;
    global.font_family = "Consolas";
    global.font_size = 14.0f;
    global.theme = "Catppuccin Mocha";
    global.cursor_style = "block";
    global.scrollback_limit = 5000;

    Profile p;
    p.id = "cmd";

    Config resolved = resolveProfileConfig(global, p);
    EXPECT_EQ(resolved.font_family, "Consolas");
    EXPECT_EQ(resolved.font_size, 14.0f);
    EXPECT_EQ(resolved.theme, "Catppuccin Mocha");
    EXPECT_EQ(resolved.cursor_style, "block");
    // Non-overridable fields must be preserved
    EXPECT_EQ(resolved.scrollback_limit, 5000);
    EXPECT_EQ(resolved.background, global.background);
    EXPECT_EQ(resolved.foreground, global.foreground);
}

TEST(ProfileTest, ResolveProfileConfig_WithOverrides) {
    Config global;
    global.font_family = "Consolas";
    global.font_size = 14.0f;
    global.theme = "Catppuccin Mocha";
    global.cursor_style = "block";

    Profile p;
    p.id = "powershell";
    p.theme = "One Dark";
    p.font_family = "Cascadia Code";

    Config resolved = resolveProfileConfig(global, p);
    EXPECT_EQ(resolved.font_family, "Cascadia Code");
    EXPECT_EQ(resolved.font_size, 14.0f);
    EXPECT_EQ(resolved.theme, "One Dark");
    EXPECT_EQ(resolved.cursor_style, "block");
}

TEST(ProfileTest, ResolveProfileConfig_AllOverrides) {
    Config global;
    global.font_family = "Consolas";
    global.font_size = 14.0f;
    global.theme = "Catppuccin Mocha";
    global.cursor_style = "block";

    Profile p;
    p.theme = "Dracula";
    p.font_family = "JetBrains Mono";
    p.font_size = 16.0f;
    p.cursor_style = "bar";

    Config resolved = resolveProfileConfig(global, p);
    EXPECT_EQ(resolved.font_family, "JetBrains Mono");
    EXPECT_EQ(resolved.font_size, 16.0f);
    EXPECT_EQ(resolved.theme, "Dracula");
    EXPECT_EQ(resolved.cursor_style, "bar");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="ProfileTest.*"`
Expected: FAIL — `profile.h` not found

- [ ] **Step 3: Implement Profile struct and resolveProfileConfig**

```cpp
// core/include/termcore/profile.h
#ifndef TERMCORE_PROFILE_H
#define TERMCORE_PROFILE_H

#include <optional>
#include <string>
#include <vector>

namespace termcore {

struct Config;  // forward declaration — defined in config.h

struct Profile {
    std::string id;
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::string working_dir;
    std::string icon;

    std::optional<std::string> theme;
    std::optional<std::string> font_family;
    std::optional<float> font_size;
    std::optional<std::string> cursor_style;

    bool is_default = false;
    bool hidden = false;
    bool auto_detected = false;
};

/// Apply profile appearance overrides on top of global Config.
Config resolveProfileConfig(const Config& global, const Profile& profile);

} // namespace termcore
#endif
```

```cpp
// core/src/profile_manager.cpp
#include "termcore/profile.h"
#include "termcore/config.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <string>

namespace termcore {

Config resolveProfileConfig(const Config& global, const Profile& profile) {
    Config resolved = global;  // copy all fields
    if (profile.theme.has_value())        resolved.theme = *profile.theme;
    if (profile.font_family.has_value())  resolved.font_family = *profile.font_family;
    if (profile.font_size.has_value())    resolved.font_size = *profile.font_size;
    if (profile.cursor_style.has_value()) resolved.cursor_style = *profile.cursor_style;
    return resolved;
}

} // namespace termcore
```

- [ ] **Step 4: Add new files to CMakeLists.txt**

In `core/CMakeLists.txt`, add to TERMCORE_SOURCES (after `src/config_value_adapter.cpp`):
```cmake
    src/profile_manager.cpp
```

In `tests/CMakeLists.txt`, add `test_profile.cpp` to the `termcore_tests` sources list.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="ProfileTest.*"`
Expected: 4 tests PASS

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/profile.h core/src/profile_manager.cpp tests/test_profile.cpp core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(profile): add Profile struct and resolveProfileConfig"
```

---

### Task 2: ProfileManager core (merge, default, lookup, fallback)

**Files:**
- Modify: `core/include/termcore/profile.h`
- Modify: `core/src/profile_manager.cpp`
- Test: `tests/test_profile.cpp`

- [ ] **Step 1: Write failing tests for ProfileManager**

Append to `tests/test_profile.cpp`:

```cpp
class ProfileManagerTest : public ::testing::Test {
protected:
    ProfileManager mgr;
};

TEST_F(ProfileManagerTest, StartsEmpty) {
    EXPECT_TRUE(mgr.allProfiles().empty());
}

TEST_F(ProfileManagerTest, DefaultProfileFallback) {
    const Profile& def = mgr.defaultProfile();
    EXPECT_FALSE(def.command.empty());
#if defined(_WIN32)
    EXPECT_NE(def.command.find("cmd"), std::string::npos);
#endif
}

TEST_F(ProfileManagerTest, SetProfileAdds) {
    Profile p;
    p.id = "test-shell";
    p.name = "Test Shell";
    p.command = "/usr/bin/test";
    mgr.setProfile(p);
    EXPECT_EQ(mgr.allProfiles().size(), 1u);
    auto* found = mgr.findProfile("test-shell");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "Test Shell");
}

TEST_F(ProfileManagerTest, SetProfileUpdatesExisting) {
    Profile p1; p1.id = "my-shell"; p1.name = "v1"; p1.command = "/bin/sh";
    mgr.setProfile(p1);
    Profile p2; p2.id = "my-shell"; p2.name = "v2"; p2.command = "/bin/bash";
    mgr.setProfile(p2);
    EXPECT_EQ(mgr.allProfiles().size(), 1u);
    EXPECT_EQ(mgr.findProfile("my-shell")->command, "/bin/bash");
}

TEST_F(ProfileManagerTest, SetDefaultProfile) {
    Profile p1; p1.id = "a"; p1.name = "A"; p1.command = "/a"; mgr.setProfile(p1);
    Profile p2; p2.id = "b"; p2.name = "B"; p2.command = "/b"; mgr.setProfile(p2);
    mgr.setDefaultProfile("b");
    EXPECT_EQ(mgr.defaultProfile().id, "b");
}

// defaultProfile falls back to first merged when no default_id set
TEST_F(ProfileManagerTest, DefaultProfileFirstMerged) {
    Profile p1; p1.id = "first"; p1.name = "First"; p1.command = "/first"; mgr.setProfile(p1);
    Profile p2; p2.id = "second"; p2.name = "Second"; p2.command = "/second"; mgr.setProfile(p2);
    // No setDefaultProfile called
    EXPECT_EQ(mgr.defaultProfile().id, "first");
}

TEST_F(ProfileManagerTest, SetDefaultProfileNonexistent) {
    mgr.setDefaultProfile("nonexistent");
    const Profile& def = mgr.defaultProfile();
    EXPECT_FALSE(def.command.empty());
}

TEST_F(ProfileManagerTest, FindProfileUnknown) {
    EXPECT_EQ(mgr.findProfile("nope"), nullptr);
}

TEST_F(ProfileManagerTest, HideProfile) {
    Profile p; p.id = "hide-me"; p.name = "Hidden"; p.command = "/hidden";
    mgr.setProfile(p);
    mgr.hideProfile("hide-me");
    for (auto* vp : mgr.visibleProfiles()) {
        EXPECT_NE(vp->id, "hide-me");
    }
}

TEST_F(ProfileManagerTest, VisibleProfilesExcludesHidden) {
    Profile p1; p1.id = "a"; p1.name = "A"; p1.command = "/a"; mgr.setProfile(p1);
    Profile p2; p2.id = "b"; p2.name = "B"; p2.command = "/b"; mgr.setProfile(p2);
    Profile p3; p3.id = "c"; p3.name = "C"; p3.command = "/c"; mgr.setProfile(p3);
    mgr.hideProfile("b");
    auto visible = mgr.visibleProfiles();
    EXPECT_EQ(visible.size(), 2u);
    EXPECT_EQ(visible[0]->id, "a");
    EXPECT_EQ(visible[1]->id, "c");
}

TEST_F(ProfileManagerTest, MergeDetectedAndUser) {
    std::vector<Profile> detected;
    Profile d1; d1.id = "cmd"; d1.name = "cmd.exe"; d1.command = "cmd.exe"; d1.auto_detected = true;
    Profile d2; d2.id = "ps"; d2.name = "PowerShell"; d2.command = "powershell.exe"; d2.auto_detected = true;
    detected.push_back(d1);
    detected.push_back(d2);
    mgr.setDetectedProfiles(std::move(detected));

    Profile user_override; user_override.id = "ps"; user_override.theme = "One Dark";
    mgr.setProfile(user_override);

    Profile custom; custom.id = "my-ssh"; custom.name = "SSH"; custom.command = "ssh";
    mgr.setProfile(custom);

    auto all = mgr.allProfiles();
    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].id, "cmd");
    EXPECT_EQ(all[1].id, "ps");
    EXPECT_EQ(all[2].id, "my-ssh");

    auto* ps = mgr.findProfile("ps");
    ASSERT_NE(ps, nullptr);
    EXPECT_EQ(ps->name, "PowerShell");
    EXPECT_TRUE(ps->theme.has_value());
    EXPECT_EQ(*ps->theme, "One Dark");
    EXPECT_FALSE(ps->auto_detected);
}

TEST_F(ProfileManagerTest, FieldLevelMerge) {
    std::vector<Profile> detected;
    Profile d; d.id = "bash"; d.name = "Bash"; d.command = "/bin/bash"; d.icon = "bash"; d.auto_detected = true;
    detected.push_back(d);
    mgr.setDetectedProfiles(std::move(detected));

    Profile user; user.id = "bash"; user.font_family = "Fira Code";
    // user.name is empty → don't override detected name
    mgr.setProfile(user);

    auto* merged = mgr.findProfile("bash");
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->name, "Bash");
    EXPECT_EQ(merged->command, "/bin/bash");
    EXPECT_EQ(merged->icon, "bash");
    EXPECT_TRUE(merged->font_family.has_value());
    EXPECT_EQ(*merged->font_family, "Fira Code");
}

// User can override a detected profile's command
TEST_F(ProfileManagerTest, FieldLevelMerge_CommandOverride) {
    std::vector<Profile> detected;
    Profile d; d.id = "ps"; d.name = "PowerShell"; d.command = "powershell.exe"; d.auto_detected = true;
    detected.push_back(d);
    mgr.setDetectedProfiles(std::move(detected));

    Profile user; user.id = "ps"; user.command = "pwsh.exe";
    mgr.setProfile(user);

    auto* merged = mgr.findProfile("ps");
    EXPECT_EQ(merged->command, "pwsh.exe");
    EXPECT_EQ(merged->name, "PowerShell");  // preserved from detected
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="ProfileManager*"`
Expected: FAIL — ProfileManager not defined

- [ ] **Step 3: Add ProfileManager declaration to profile.h**

Add to `core/include/termcore/profile.h` after `resolveProfileConfig`:

```cpp
class ProfileManager {
public:
    ProfileManager();

    const std::vector<Profile>& allProfiles() const;
    std::vector<const Profile*> visibleProfiles() const;
    const Profile& defaultProfile() const;
    const Profile* findProfile(const std::string& id) const;

    void setProfile(const Profile& profile);
    void setDefaultProfile(const std::string& id);
    void hideProfile(const std::string& id);
    void setDetectedProfiles(std::vector<Profile> detected);

private:
    std::vector<Profile> detected_;
    std::vector<Profile> user_;
    std::vector<Profile> merged_;
    std::string default_id_;
    std::vector<std::string> hidden_ids_;
    Profile fallback_;

    void rebuildMerged();
    void ensureFallback();
};
```

- [ ] **Step 4: Implement ProfileManager in profile_manager.cpp**

Add after `resolveProfileConfig` in `core/src/profile_manager.cpp`:

```cpp
ProfileManager::ProfileManager() {
    ensureFallback();
}

void ProfileManager::ensureFallback() {
    fallback_.id = "__fallback__";
#if defined(_WIN32)
    char sys[MAX_PATH] = {};
    GetSystemDirectoryA(sys, MAX_PATH);
    fallback_.command = std::string(sys) + "\\cmd.exe";
    fallback_.name = "cmd.exe";
    fallback_.icon = "cmd";
#else
    const char* sh = std::getenv("SHELL");
    fallback_.command = (sh && sh[0]) ? sh : "/bin/sh";
    std::string cmd = fallback_.command;
    auto pos = cmd.rfind('/');
    fallback_.name = (pos != std::string::npos) ? cmd.substr(pos + 1) : cmd;
    fallback_.icon = fallback_.name;
#endif
}

const std::vector<Profile>& ProfileManager::allProfiles() const { return merged_; }

std::vector<const Profile*> ProfileManager::visibleProfiles() const {
    std::vector<const Profile*> result;
    for (const auto& p : merged_) {
        if (!p.hidden) result.push_back(&p);
    }
    return result;
}

const Profile& ProfileManager::defaultProfile() const {
    if (!default_id_.empty()) {
        for (const auto& p : merged_) {
            if (p.id == default_id_) return p;
        }
    }
    if (!merged_.empty()) return merged_[0];
    return fallback_;
}

const Profile* ProfileManager::findProfile(const std::string& id) const {
    for (const auto& p : merged_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void ProfileManager::setProfile(const Profile& profile) {
    for (auto& p : user_) {
        if (p.id == profile.id) {
            p = profile;
            rebuildMerged();
            return;
        }
    }
    user_.push_back(profile);
    rebuildMerged();
}

void ProfileManager::setDefaultProfile(const std::string& id) { default_id_ = id; }

void ProfileManager::hideProfile(const std::string& id) {
    if (std::find(hidden_ids_.begin(), hidden_ids_.end(), id) == hidden_ids_.end()) {
        hidden_ids_.push_back(id);
    }
    rebuildMerged();
}

void ProfileManager::setDetectedProfiles(std::vector<Profile> detected) {
    detected_ = std::move(detected);
    rebuildMerged();
}

void ProfileManager::rebuildMerged() {
    merged_.clear();
    merged_ = detected_;

    for (const auto& user : user_) {
        bool found = false;
        for (auto& m : merged_) {
            if (m.id == user.id) {
                // Field-level override: non-empty user fields override
                if (!user.name.empty())       m.name = user.name;
                if (!user.command.empty())     m.command = user.command;
                if (!user.args.empty())        m.args = user.args;
                if (!user.working_dir.empty()) m.working_dir = user.working_dir;
                if (!user.icon.empty())        m.icon = user.icon;
                if (user.theme.has_value())       m.theme = user.theme;
                if (user.font_family.has_value()) m.font_family = user.font_family;
                if (user.font_size.has_value())   m.font_size = user.font_size;
                if (user.cursor_style.has_value()) m.cursor_style = user.cursor_style;
                m.auto_detected = false;
                found = true;
                break;
            }
        }
        if (!found) merged_.push_back(user);
    }

    for (auto& m : merged_) {
        m.hidden = std::find(hidden_ids_.begin(), hidden_ids_.end(), m.id) != hidden_ids_.end();
    }
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="ProfileManager*"`
Expected: All ProfileManager tests PASS

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/profile.h core/src/profile_manager.cpp tests/test_profile.cpp
git commit -m "feat(profile): implement ProfileManager with merge, fallback, hide"
```

---

### Task 3: Config struct extensions

**Files:**
- Modify: `core/include/termcore/config.h`
- Modify: `core/src/config_value_adapter.cpp`

- [ ] **Step 1: Add profile.h include and profile fields to Config**

In `core/include/termcore/config.h`, add include after line 8 (`#include <vector>`):
```cpp
#include "termcore/profile.h"
```

Add after line 80 (`std::vector<KeyBinding> keybindings;`):
```cpp
    // Profiles (user-defined only — auto-detected are runtime)
    std::vector<Profile> profiles;
    std::string default_profile_id;
    std::vector<std::string> hidden_profile_ids;
```

No circular dependency: `profile.h` does NOT include `config.h` (uses forward declaration of `Config`). `config.h` includes `profile.h`. This is the correct direction.

- [ ] **Step 2: Update config_value_adapter.cpp**

In `core/src/config_value_adapter.cpp`:

In `getConfigString()`, add:
```cpp
if (key == "default_profile_id") return cfg.default_profile_id;
```

In `setConfigString()`, add:
```cpp
else if (key == "default_profile_id") cfg.default_profile_id = val;
```

- [ ] **Step 3: Build and verify no regressions**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests`
Expected: All existing tests pass

- [ ] **Step 4: Commit**

```bash
git add core/include/termcore/config.h core/src/config_value_adapter.cpp
git commit -m "feat(config): add profile fields to Config struct"
```

---

## Chunk 2: Shell Detection

### Task 4: ShellDetector — common interface and tests

**Files:**
- Modify: `core/include/termcore/profile.h`
- Create: `core/src/shell_detector.cpp`
- Create: `tests/test_shell_detector.cpp`

- [ ] **Step 1: Add ShellDetector to profile.h**

```cpp
class ShellDetector {
public:
    static std::vector<Profile> detect();
};
```

- [ ] **Step 2: Write failing tests**

```cpp
// tests/test_shell_detector.cpp
#include <gtest/gtest.h>
#include "termcore/profile.h"
#include <set>

using namespace termcore;

TEST(ShellDetectorTest, DetectsAtLeastOneShell) {
    auto profiles = ShellDetector::detect();
    ASSERT_GE(profiles.size(), 1u);
    for (const auto& p : profiles) {
        EXPECT_TRUE(p.auto_detected);
        EXPECT_FALSE(p.id.empty());
        EXPECT_FALSE(p.command.empty());
        EXPECT_FALSE(p.name.empty());
    }
}

#if defined(_WIN32)
TEST(ShellDetectorTest, WindowsDetectsCmd) {
    auto profiles = ShellDetector::detect();
    bool found = false;
    for (const auto& p : profiles) {
        if (p.id == "cmd") { found = true; EXPECT_NE(p.command.find("cmd.exe"), std::string::npos); }
    }
    EXPECT_TRUE(found);
}
#else
TEST(ShellDetectorTest, UnixDetectsDefaultShell) {
    auto profiles = ShellDetector::detect();
    EXPECT_GE(profiles.size(), 1u);
}
#endif

TEST(ShellDetectorTest, NoDuplicateIds) {
    auto profiles = ShellDetector::detect();
    std::set<std::string> ids;
    for (const auto& p : profiles) {
        EXPECT_TRUE(ids.insert(p.id).second) << "Duplicate ID: " << p.id;
    }
}
```

- [ ] **Step 3: Implement shell_detector.cpp (dispatch)**

```cpp
// core/src/shell_detector.cpp
#include "termcore/profile.h"

namespace termcore {

#if defined(_WIN32)
std::vector<Profile> detectWindowsShells();
#else
std::vector<Profile> detectUnixShells();
#endif

std::vector<Profile> ShellDetector::detect() {
#if defined(_WIN32)
    return detectWindowsShells();
#else
    return detectUnixShells();
#endif
}

} // namespace termcore
```

- [ ] **Step 4: Add to CMakeLists.txt**

Add `src/shell_detector.cpp` to main TERMCORE_SOURCES.
Add `test_shell_detector.cpp` to tests.

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/profile.h core/src/shell_detector.cpp tests/test_shell_detector.cpp core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(profile): add ShellDetector interface and tests"
```

---

### Task 5: ShellDetector — Windows implementation

**Files:**
- Create: `core/src/shell_detector_windows.cpp`
- Modify: `core/CMakeLists.txt`

- [ ] **Step 1: Implement Windows shell detection**

```cpp
// core/src/shell_detector_windows.cpp
#if defined(_WIN32)

#include "termcore/profile.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

namespace termcore {

static Profile makeProfile(const std::string& id, const std::string& name,
                            const std::string& command, const std::string& icon,
                            const std::vector<std::string>& args = {}) {
    Profile p;
    p.id = id; p.name = name; p.command = command; p.icon = icon;
    p.args = args; p.auto_detected = true;
    return p;
}

static std::string getEnv(const char* name) {
    const char* val = std::getenv(name);
    return val ? val : "";
}

static std::string runCommand(const std::string& cmd, int timeout_ms = 3000) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return "";
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::string cmdline = cmd;
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(read_pipe); CloseHandle(write_pipe);
        return "";
    }
    CloseHandle(write_pipe);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(read_pipe);
        return "";
    }

    std::string output;
    char buf[4096];
    DWORD bytes_read = 0;
    while (ReadFile(read_pipe, buf, sizeof(buf), &bytes_read, nullptr) && bytes_read > 0) {
        output.append(buf, bytes_read);
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(read_pipe);
    return output;
}

static std::string utf16ToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), result.data(), size, nullptr, nullptr);
    return result;
}

static std::vector<Profile> detectWslDistros() {
    std::vector<Profile> result;
    std::string raw = runCommand("wsl --list --quiet", 3000);
    if (raw.empty()) return result;

    // WSL outputs UTF-16LE
    std::wstring wide;
    if (raw.size() >= 2) {
        const auto* data = reinterpret_cast<const wchar_t*>(raw.data());
        size_t wlen = raw.size() / sizeof(wchar_t);
        if (wlen > 0 && data[0] == 0xFEFF) wide.assign(data + 1, wlen - 1);
        else wide.assign(data, wlen);
    }
    std::string utf8 = utf16ToUtf8(wide);

    std::istringstream iss(utf8);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
               line.back() == ' ' || line.back() == '\0'))
            line.pop_back();
        if (line.empty()) continue;

        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        result.push_back(makeProfile("wsl-" + lower, line + " (WSL)", "wsl", "wsl", {"-d", line}));
    }
    return result;
}

std::vector<Profile> detectWindowsShells() {
    std::vector<Profile> profiles;
    std::string sysRoot = getEnv("SystemRoot");

    // 1. cmd.exe
    char sysDir[MAX_PATH] = {};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    profiles.push_back(makeProfile("cmd", "cmd.exe", std::string(sysDir) + "\\cmd.exe", "cmd"));

    // 2. PowerShell 5
    if (!sysRoot.empty()) {
        std::string ps5 = sysRoot + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (fs::exists(ps5)) {
            profiles.push_back(makeProfile("powershell", "Windows PowerShell", ps5, "powershell"));
        }
    }

    // 3. PowerShell 7+
    std::string progFiles = getEnv("ProgramFiles");
    if (!progFiles.empty()) {
        std::string pwsh = progFiles + "\\PowerShell\\7\\pwsh.exe";
        if (fs::exists(pwsh)) {
            profiles.push_back(makeProfile("pwsh", "PowerShell 7", pwsh, "powershell"));
        }
    }

    // 4. Git Bash
    if (!progFiles.empty()) {
        std::string gitBash = progFiles + "\\Git\\bin\\bash.exe";
        if (fs::exists(gitBash)) {
            profiles.push_back(makeProfile("git-bash", "Git Bash", gitBash, "bash"));
        }
    }

    // 5. WSL distros (3s timeout)
    auto wsl = detectWslDistros();
    profiles.insert(profiles.end(), std::make_move_iterator(wsl.begin()), std::make_move_iterator(wsl.end()));

    return profiles;
}

} // namespace termcore
#endif
```

- [ ] **Step 2: Add to CMakeLists.txt**

In `if(WIN32)` block, add `src/shell_detector_windows.cpp`.

- [ ] **Step 3: Build and run tests (Windows)**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="ShellDetector*"`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add core/src/shell_detector_windows.cpp core/CMakeLists.txt
git commit -m "feat(profile): Windows shell detection (cmd, PS, Git Bash, WSL)"
```

---

### Task 6: ShellDetector — Unix implementation

**Files:**
- Create: `core/src/shell_detector_unix.cpp`
- Modify: `core/CMakeLists.txt`

- [ ] **Step 1: Implement Unix shell detection**

```cpp
// core/src/shell_detector_unix.cpp
#if !defined(_WIN32)

#include "termcore/profile.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace termcore {

static Profile makeProfile(const std::string& id, const std::string& name,
                            const std::string& command, const std::string& icon) {
    Profile p;
    p.id = id; p.name = name; p.command = command; p.icon = icon;
    p.auto_detected = true;
    return p;
}

static std::string shellNameFromPath(const std::string& path) {
    auto pos = path.rfind('/');
    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
}

static std::string friendlyName(const std::string& bin) {
    if (bin == "bash") return "Bash";
    if (bin == "zsh") return "Zsh";
    if (bin == "fish") return "Fish";
    if (bin == "sh") return "sh";
    if (bin == "dash") return "Dash";
    if (bin == "tcsh") return "tcsh";
    if (bin == "csh") return "csh";
    if (bin == "ksh") return "KornShell";
    if (bin == "nu") return "Nushell";
    if (bin == "elvish") return "Elvish";
    if (bin == "xonsh") return "xonsh";
    if (bin == "pwsh") return "PowerShell";
    return bin;
}

std::vector<Profile> detectUnixShells() {
    std::vector<Profile> profiles;
    std::set<std::string> seen;

    // Parse /etc/shells
    std::ifstream ifs("/etc/shells");
    if (ifs) {
        std::string line;
        while (std::getline(ifs, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] != '/') continue;
            if (!fs::exists(line)) continue;
            std::string bin = shellNameFromPath(line);
            if (bin.empty() || !seen.insert(bin).second) continue;
            profiles.push_back(makeProfile(bin, friendlyName(bin), line, bin));
        }
    }

    // Fallback if /etc/shells yielded nothing
    if (profiles.empty()) {
        const char* sh = std::getenv("SHELL");
        if (sh && sh[0]) {
            std::string bin = shellNameFromPath(sh);
            profiles.push_back(makeProfile(bin, friendlyName(bin), sh, bin));
        } else {
            profiles.push_back(makeProfile("sh", "sh", "/bin/sh", "sh"));
        }
    }

    // Check for extra known shells
    const char* extras[] = {"nu", "elvish", "xonsh"};
    for (const char* name : extras) {
        if (seen.count(name)) continue;
        std::string paths[] = {
            std::string("/usr/local/bin/") + name,
            std::string("/usr/bin/") + name,
            std::string("/opt/homebrew/bin/") + name,
        };
        for (const auto& path : paths) {
            if (fs::exists(path)) {
                profiles.push_back(makeProfile(name, friendlyName(name), path, name));
                break;
            }
        }
    }

    return profiles;
}

} // namespace termcore
#endif
```

- [ ] **Step 2: Add to CMakeLists.txt**

In `if(UNIX)` block, add `src/shell_detector_unix.cpp`.

- [ ] **Step 3: Run tests**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="ShellDetector*"`
Expected: All PASS

- [ ] **Step 4: Commit**

```bash
git add core/src/shell_detector_unix.cpp core/CMakeLists.txt
git commit -m "feat(profile): Unix shell detection (/etc/shells, known shells)"
```

---

## Chunk 3: Integration (PtyFactory, TabController, Session, Actions)

### Task 7: New Action enum values

**Files:**
- Modify: `core/include/termcore/keybinding.h:22-55`
- Modify: `core/src/keybinding.cpp`
- Modify: `core/src/keybinding_presets.cpp`
- Test: `tests/test_keybinding.cpp`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_keybinding.cpp`:

```cpp
TEST(ActionParseTest, ProfileActions) {
    EXPECT_EQ(KeybindingManager::parseAction("new_tab_profile1"), Action::NewTabProfile1);
    EXPECT_EQ(KeybindingManager::parseAction("new_tab_profile9"), Action::NewTabProfile9);
    EXPECT_EQ(KeybindingManager::parseAction("show_profile_dropdown"), Action::ShowProfileDropdown);
}

TEST_F(KeybindingTest, PresetResolvesProfileShortcuts) {
    // All non-Default presets include commonGuiBindings which has Mod+Shift+1~9
    mgr.loadPreset(KeymapPreset::Ghostty);
    auto combo = KeybindingManager::parseCombo(std::string(PMS) + "+1");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTabProfile1);
    auto combo9 = KeybindingManager::parseCombo(std::string(PMS) + "+9");
    EXPECT_EQ(mgr.lookup(combo9), Action::NewTabProfile9);
}
```

- [ ] **Step 2: Add new Action values**

In `keybinding.h`, add before `Custom`:
```cpp
    NewTabProfile1, NewTabProfile2, NewTabProfile3,
    NewTabProfile4, NewTabProfile5, NewTabProfile6,
    NewTabProfile7, NewTabProfile8, NewTabProfile9,
    ShowProfileDropdown,
```

- [ ] **Step 3: Add parseAction mappings in keybinding.cpp**

**IMPORTANT:** `keybinding.cpp`의 `parseAction`은 `static const std::unordered_map<std::string, Action>` (`actionNameMap()`)을 사용한다. `if` 체인이 아님. 다음 엔트리들을 `actionNameMap()` 맵에 추가:

```cpp
// actionNameMap() 안의 map 초기화 리스트에 추가 ({"custom", Action::Custom} 직전):
{"new_tab_profile1", Action::NewTabProfile1},
{"new_tab_profile2", Action::NewTabProfile2},
{"new_tab_profile3", Action::NewTabProfile3},
{"new_tab_profile4", Action::NewTabProfile4},
{"new_tab_profile5", Action::NewTabProfile5},
{"new_tab_profile6", Action::NewTabProfile6},
{"new_tab_profile7", Action::NewTabProfile7},
{"new_tab_profile8", Action::NewTabProfile8},
{"new_tab_profile9", Action::NewTabProfile9},
{"show_profile_dropdown", Action::ShowProfileDropdown},
```

- [ ] **Step 4: Add profile shortcuts to keybinding_presets.cpp ONLY (not initDefaults)**

Add to `commonGuiBindings()` in `keybinding_presets.cpp`. **NOTE:** 이 파일에서 `kModS`는 `const char*`이므로 `mk()` 헬퍼를 사용해야 한다:
```cpp
{mk(kModS, "1"), "new_tab_profile1"},
{mk(kModS, "2"), "new_tab_profile2"},
// ... through 9 ...
{mk(kModS, "9"), "new_tab_profile9"},
```

**NOTE:** Do NOT add these to `initDefaults()` in `keybinding.cpp` — that would duplicate them since presets already include common GUI bindings. Only add to `commonGuiBindings()` in presets.

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="*Profile*:*Action*"`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/keybinding.h core/src/keybinding.cpp core/src/keybinding_presets.cpp tests/test_keybinding.cpp
git commit -m "feat(keybinding): add NewTabProfile1~9 and ShowProfileDropdown actions"
```

---

### Task 8: PtyFactory & IPlatformHost signature change + pendingProfileId

This is the most complex task. The Mux `PaneCreateCallback` has signature `(int rows, int cols)` and cannot carry `profile_id`. Solution: TabController stores `pendingProfileId_` before calling Mux, then reads it in `createPaneState()`.

**Files:**
- Modify: `core/include/termcore/tab_controller.h`
- Modify: `core/src/tab_controller.cpp`
- Modify: `core/include/termcore/platform_host.h`
- Modify: `core/src/terminal_controller.cpp`
- Modify: `platform/windows/include/TerminalWindowState.h`
- Modify: `platform/windows/src/TerminalWindowStateEvents.cpp`
- Modify: `platform/macos/include/MacPlatformHost.h`
- Modify: `platform/macos/src/MacPlatformHost.mm`
- Modify: `platform/linux/include/GtkPlatformHost.h`
- Modify: `platform/linux/src/GtkPlatformHost.cpp`
- Modify: `core/src/termcore_api.cpp`

- [ ] **Step 1: Change PtyFactory typedef and add pendingProfileId_ to TabController**

In `core/include/termcore/tab_controller.h`:

```cpp
// Add include:
#include "termcore/profile.h"

// Change PtyFactory (line 24):
// Before: using PtyFactory = std::function<std::unique_ptr<Pty>(int rows, int cols)>;
// After:
using PtyFactory = std::function<std::unique_ptr<Pty>(const Profile& profile, int rows, int cols)>;

// Change method signatures:
void createTab(int rows, int cols, const std::string& profile_id = "");
void splitRight(int rows, int cols, const std::string& profile_id = "");
void splitDown(int rows, int cols, const std::string& profile_id = "");

// Add to PaneState struct:
struct PaneState {
    PaneId id = kInvalidPane;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;
    std::string profile_id;  // which profile created this pane
};

// Add to private section:
ProfileManager* profileMgr_ = nullptr;
std::string pendingProfileId_;  // temporary state for Mux callback
// NOTE: pendingProfileId_ is only set/read on the main thread (UI thread).
// Mux callbacks are synchronous within createTab/split calls, so no data race.

// Add public setter:
void setProfileManager(ProfileManager* mgr) { profileMgr_ = mgr; }
```

- [ ] **Step 2: Update tab_controller.cpp — pendingProfileId pattern**

```cpp
void TabController::createTab(int rows, int cols, const std::string& profile_id) {
    pendingProfileId_ = profile_id;  // set before Mux callback fires
    mux_->createTab(wsId_, rows, cols);
    pendingProfileId_.clear();
    syncActivePointers();
}

void TabController::splitRight(int rows, int cols, const std::string& profile_id) {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    pendingProfileId_ = profile_id;
    mux_->splitPane(wsId_, tab->id, tab->active_pane, SplitDirection::Horizontal, rows, cols);
    pendingProfileId_.clear();
    syncActivePointers();
}

void TabController::splitDown(int rows, int cols, const std::string& profile_id) {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    pendingProfileId_ = profile_id;
    mux_->splitPane(wsId_, tab->id, tab->active_pane, SplitDirection::Vertical, rows, cols);
    pendingProfileId_.clear();
    syncActivePointers();
}

PaneId TabController::createPaneState(int rows, int cols) {
    PaneId id = nextPaneId_++;
    auto ps = std::make_unique<PaneState>();
    ps->id = id;
    ps->screen = std::make_unique<Screen>();
    ps->screen->resize(rows, cols);
    ps->parser = std::make_unique<VtParser>(*ps->screen);

    // Resolve profile from pendingProfileId_
    Profile profile;
    if (profileMgr_) {
        if (!pendingProfileId_.empty()) {
            auto* p = profileMgr_->findProfile(pendingProfileId_);
            profile = p ? *p : profileMgr_->defaultProfile();
        } else {
            profile = profileMgr_->defaultProfile();
        }
    } else {
        // Fallback when ProfileManager is not set (e.g., C API path, tests)
        // Use an empty profile — PtyFactory implementations should handle
        // empty command by falling back to system default shell
    }
    ps->profile_id = profile.id;

    if (ptyFactory_) {
        ps->pty = ptyFactory_(profile, rows, cols);
    }

    panes_[id] = std::move(ps);
    return id;
}
```

- [ ] **Step 3: Change IPlatformHost::createPty signature**

In `core/include/termcore/platform_host.h`:
```cpp
#include "termcore/profile.h"

// Change line 73-75:
virtual std::unique_ptr<Pty> createPty(const Profile& profile,
                                       int rows, int cols) = 0;
```

- [ ] **Step 4: Update TerminalController PtyFactory lambda**

In `core/src/terminal_controller.cpp` (line 49-52):
```cpp
PtyFactory factory = [this](const Profile& profile, int rows, int cols) -> std::unique_ptr<Pty> {
    if (!host_) return nullptr;
    return host_->createPty(profile, rows, cols);
};
```

- [ ] **Step 5: Update ALL platform createPty implementations**

**Windows** (`platform/windows/include/TerminalWindowState.h` and `TerminalWindowStateEvents.cpp`):
```cpp
// Header declaration:
std::unique_ptr<termcore::Pty> createPty(const termcore::Profile& profile, int rows, int cols) override;

// Implementation:
std::unique_ptr<termcore::Pty> TerminalWindowState::createPty(
        const termcore::Profile& profile, int rows, int cols) {
    auto pty = termcore::createPty();
    std::string working_dir = profile.working_dir;
    // Empty working_dir → let PTY use default (home dir)
    if (!pty->spawn(profile.command, profile.args, working_dir, rows, cols)) {
        OutputDebugStringW(L"BreadTerminal: failed to spawn shell for pane\n");
    }
    return pty;
}
```

**macOS** (`platform/macos/include/MacPlatformHost.h` and `MacPlatformHost.mm`):
```cpp
// Same pattern. If profile.working_dir is empty, fall back to $HOME (preserving existing behavior).
std::unique_ptr<termcore::Pty> createPty(const termcore::Profile& profile, int rows, int cols) override;
```

**Linux** (`platform/linux/include/GtkPlatformHost.h` and `GtkPlatformHost.cpp`):
```cpp
// Same pattern.
std::unique_ptr<termcore::Pty> createPty(const termcore::Profile& profile, int rows, int cols) override;
```

- [ ] **Step 6: Update termcore_api.cpp**

`core/src/termcore_api.cpp`의 `setPaneCallbacks` 람다 (line 69-72)는 `tc_pane_create`를 통해 pane을 생성한다. 이 C API 경로는 PtyFactory를 통하지 않고 직접 PTY를 생성하므로, PtyFactory 시그니처 변경의 직접적 영향은 없다. 하지만 `tc_pane_create` 내부에서 PTY를 생성할 때 `IPlatformHost::createPty`를 호출한다면, 해당 호출부를 default Profile로 변경해야 한다:

```cpp
// termcore_api.cpp에서 createPty를 호출하는 부분이 있다면:
// Before: host->createPty(shell, rows, cols)
// After:
termcore::Profile defaultProfile;
defaultProfile.command = shell;  // 기존 shell 변수 유지
auto pty = host->createPty(defaultProfile, rows, cols);
```

실제 `termcore_api.cpp`를 읽어서 `createPty` 호출 여부를 확인하고, 호출이 없으면 변경 불필요.

- [ ] **Step 7: Build and run all tests**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests`
Expected: All tests PASS

- [ ] **Step 8: Commit**

```bash
git add core/include/termcore/tab_controller.h core/include/termcore/platform_host.h \
  core/src/tab_controller.cpp core/src/terminal_controller.cpp core/src/termcore_api.cpp \
  platform/windows/include/TerminalWindowState.h platform/windows/src/TerminalWindowStateEvents.cpp \
  platform/macos/include/MacPlatformHost.h platform/macos/src/MacPlatformHost.mm \
  platform/linux/include/GtkPlatformHost.h platform/linux/src/GtkPlatformHost.cpp
git commit -m "refactor: change PtyFactory/createPty to use Profile, add pendingProfileId pattern"
```

---

### Task 9: TerminalController — ProfileManager integration

**Files:**
- Modify: `core/include/termcore/terminal_controller.h`
- Modify: `core/src/terminal_controller.cpp`

- [ ] **Step 1: Add ProfileManager member**

In `terminal_controller.h`:
```cpp
#include "termcore/profile.h"

// private:
std::unique_ptr<ProfileManager> profileMgr_;

// public:
ProfileManager* profileManager() { return profileMgr_.get(); }
```

- [ ] **Step 2: Initialize ProfileManager in constructor**

In constructor, after keybindings setup:
```cpp
profileMgr_ = std::make_unique<ProfileManager>();
for (const auto& p : config_.profiles) profileMgr_->setProfile(p);
if (!config_.default_profile_id.empty()) profileMgr_->setDefaultProfile(config_.default_profile_id);
for (const auto& id : config_.hidden_profile_ids) profileMgr_->hideProfile(id);
```

- [ ] **Step 3: Detect shells and set up ProfileManager in initTerminal()**

Before creating PtyFactory:
```cpp
// NOTE: Spec defines detectShells() as a ProfileManager member, but the plan
// intentionally separates ShellDetector (detection) from ProfileManager (storage).
// This improves testability — ShellDetector can be tested independently.
auto detected = ShellDetector::detect();
profileMgr_->setDetectedProfiles(std::move(detected));

// Config::shell backward compatibility
if (profileMgr_->allProfiles().empty() && !config_.shell.empty()) {
    Profile legacy;
    legacy.id = "__legacy_shell__";
    legacy.name = "Shell";
    legacy.command = config_.shell;
    legacy.icon = "shell";
    profileMgr_->setProfile(legacy);
    profileMgr_->setDefaultProfile(legacy.id);
}
```

After creating TabController:
```cpp
tabCtrl_->setProfileManager(profileMgr_.get());
```

- [ ] **Step 4: Handle NewTabProfile actions in handleAction()**

```cpp
case Action::NewTabProfile1: case Action::NewTabProfile2: case Action::NewTabProfile3:
case Action::NewTabProfile4: case Action::NewTabProfile5: case Action::NewTabProfile6:
case Action::NewTabProfile7: case Action::NewTabProfile8: case Action::NewTabProfile9: {
    if (tabCtrl_ && profileMgr_) {
        int idx = static_cast<int>(action) - static_cast<int>(Action::NewTabProfile1);
        auto visible = profileMgr_->visibleProfiles();
        if (idx >= 0 && idx < static_cast<int>(visible.size())) {
            tabCtrl_->createTab(termRows_, termCols_, visible[idx]->id);
            needsRender_ = true;
        }
    }
    break;
}
case Action::ShowProfileDropdown:
    // Platform-specific UI will handle this. No-op at controller level for now.
    break;
```

- [ ] **Step 5: Build and test**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/terminal_controller.h core/src/terminal_controller.cpp
git commit -m "feat(profile): integrate ProfileManager into TerminalController"
```

---

### Task 10: Session save/restore — profile_id support

**Files:**
- Modify: `core/include/termcore/session.h`
- Modify: `core/src/session_serializer.cpp`
- Modify: `core/src/session_deserializer.cpp`
- Modify: `tests/test_session.cpp` (MockPaneProvider)

- [ ] **Step 1: Add profile_id to PaneSessionData and IPaneStateProvider**

In `core/include/termcore/session.h`:

```cpp
struct PaneSessionData {
    // ... existing fields ...
    std::string profile_id;  // NEW
};

// In IPaneStateProvider, add:
virtual std::string getProfileId(uint32_t pane_id) const = 0;
```

Change default version:
```cpp
struct SessionData {
    int version = 2;  // changed from 1
    // ...
};
```

- [ ] **Step 2: Update session_serializer.cpp**

**IMPORTANT:** `capture()` 함수가 `data.version = 1`을 하드코딩한다 (line 127). 이를 `data.version = 2`로 변경해야 v2 세션이 실제로 저장된다:

```cpp
// session_serializer.cpp capture() 함수 안 (line 127):
// Before: data.version = 1;
// After:
data.version = 2;
```

In pane serialization, add after `pj["webview_url"]`:
```cpp
pj["profile_id"] = p.profile_id;
```

In `capture()`, add to pane data collection:
```cpp
pd.profile_id = provider.getProfileId(pane_id);
```

- [ ] **Step 3: Update session_deserializer.cpp**

Change version check:
```cpp
if (data.version != 1 && data.version != 2)
    return Error("unsupported session version: " + std::to_string(data.version));
```

In pane deserialization:
```cpp
pd.profile_id = pj.value("profile_id", "");  // empty for v1
```

- [ ] **Step 4: Update MockPaneProvider in tests/test_session.cpp**

Add `getProfileId` override:
```cpp
std::string getProfileId(uint32_t pane_id) const override { return ""; }
```

- [ ] **Step 5: Build and test**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/session.h core/src/session_serializer.cpp \
  core/src/session_deserializer.cpp tests/test_session.cpp
git commit -m "feat(session): add profile_id to PaneSessionData (version 2)"
```

---

## Chunk 4: Lua API & Settings UI

### Task 11: Lua API — terminal.profile(), default_profile(), hide_profile()

**Files:**
- Modify: `core/src/lua_config.cpp`
- Test: `tests/test_profile.cpp` (Lua round-trip test)

- [ ] **Step 1: Add Lua APIs in LuaConfigState::init()**

First, add `#include "termcore/profile.h"` to the includes in `core/src/lua_config.cpp` (needed for the `Profile` type).

In `core/src/lua_config.cpp`, inside `LuaConfigState::init()`:

```cpp
// terminal.profile({...})
terminal.set_function("profile", [this](sol::table t) {
    Profile p;
    p.id = getStr(t, "id", "");
    if (p.id.empty()) return;

    p.name = getStr(t, "name", "");
    p.command = getStr(t, "command", "");
    if (auto a = t["args"]; a.valid() && a.get_type() == sol::type::table) {
        sol::table args = a;
        for (auto& [k, v] : args) {
            if (v.is<std::string>()) p.args.push_back(v.as<std::string>());
        }
    }
    p.working_dir = getStr(t, "working_dir", "");
    p.icon = getStr(t, "icon", "");

    // Optional appearance (type-checked)
    if (auto v = t["theme"]; v.valid() && v.get_type() == sol::type::string)
        p.theme = v.get<std::string>();
    if (auto v = t["font_family"]; v.valid() && v.get_type() == sol::type::string)
        p.font_family = v.get<std::string>();
    if (auto v = t["font_size"]; v.valid() && v.get_type() == sol::type::number)
        p.font_size = v.get<float>();
    if (auto v = t["cursor_style"]; v.valid() && v.get_type() == sol::type::string)
        p.cursor_style = v.get<std::string>();

    config.profiles.push_back(p);
});

terminal.set_function("default_profile", [this](const std::string& id) {
    config.default_profile_id = id;
});

terminal.set_function("hide_profile", [this](const std::string& id) {
    config.hidden_profile_ids.push_back(id);
});
```

- [ ] **Step 2: Write Lua API tests**

Append to `tests/test_profile.cpp`:

```cpp
#if TERMCORE_HAS_LUA
#include "termcore/lua_config.h"

TEST(ProfileLuaTest, LoadProfileFromLua) {
    std::string code = R"(
        terminal.profile({
            id = "test-ssh",
            name = "Test SSH",
            command = "ssh",
            args = { "user@host" },
            icon = "ssh",
            theme = "Dracula",
            font_size = 16,
        })
        terminal.default_profile("test-ssh")
        terminal.hide_profile("cmd")
    )";
    auto result = loadConfigLuaString(code);
    ASSERT_TRUE(result.ok()) << result.error();

    const Config& cfg = luaConfig();
    ASSERT_EQ(cfg.profiles.size(), 1u);
    EXPECT_EQ(cfg.profiles[0].id, "test-ssh");
    EXPECT_EQ(cfg.profiles[0].name, "Test SSH");
    EXPECT_EQ(cfg.profiles[0].command, "ssh");
    ASSERT_EQ(cfg.profiles[0].args.size(), 1u);
    EXPECT_EQ(cfg.profiles[0].args[0], "user@host");
    EXPECT_TRUE(cfg.profiles[0].theme.has_value());
    EXPECT_EQ(*cfg.profiles[0].theme, "Dracula");
    EXPECT_TRUE(cfg.profiles[0].font_size.has_value());
    EXPECT_FLOAT_EQ(*cfg.profiles[0].font_size, 16.0f);

    EXPECT_EQ(cfg.default_profile_id, "test-ssh");
    ASSERT_EQ(cfg.hidden_profile_ids.size(), 1u);
    EXPECT_EQ(cfg.hidden_profile_ids[0], "cmd");
}
#endif
```

- [ ] **Step 3: Run tests**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="*ProfileLua*"`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add core/src/lua_config.cpp tests/test_profile.cpp
git commit -m "feat(lua): add terminal.profile(), default_profile(), hide_profile() APIs"
```

---

### Task 12: Lua config writer — profile serialization

**Files:**
- Modify: `core/src/lua_config_writer.cpp`

- [ ] **Step 1: Add profile serialization**

In `core/src/lua_config_writer.cpp`, add `#include "termcore/profile.h"` at top.

Before the keybindings section, add:

```cpp
// Profiles
if (!config.profiles.empty()) {
    o << "-- Profiles\n";
    for (const auto& p : config.profiles) {
        o << "terminal.profile({\n";
        o << "    id = " << escLua(p.id) << ",\n";
        if (!p.name.empty())    o << "    name = " << escLua(p.name) << ",\n";
        if (!p.command.empty()) o << "    command = " << escLua(p.command) << ",\n";
        if (!p.args.empty()) {
            o << "    args = { ";
            for (size_t i = 0; i < p.args.size(); ++i) {
                if (i > 0) o << ", ";
                o << escLua(p.args[i]);
            }
            o << " },\n";
        }
        if (!p.working_dir.empty()) o << "    working_dir = " << escLua(p.working_dir) << ",\n";
        if (!p.icon.empty())        o << "    icon = " << escLua(p.icon) << ",\n";
        if (p.theme.has_value())       o << "    theme = " << escLua(*p.theme) << ",\n";
        if (p.font_family.has_value()) o << "    font_family = " << escLua(*p.font_family) << ",\n";
        if (p.font_size.has_value())   o << "    font_size = " << *p.font_size << ",\n";
        if (p.cursor_style.has_value()) o << "    cursor_style = " << escLua(*p.cursor_style) << ",\n";
        o << "})\n";
    }
    o << "\n";
}

if (!config.default_profile_id.empty())
    o << "terminal.default_profile(" << escLua(config.default_profile_id) << ")\n\n";

for (const auto& id : config.hidden_profile_ids)
    o << "terminal.hide_profile(" << escLua(id) << ")\n";
if (!config.hidden_profile_ids.empty()) o << "\n";
```

- [ ] **Step 2: Write round-trip test**

Append to `tests/test_profile.cpp`:

```cpp
#if TERMCORE_HAS_LUA
TEST(ProfileLuaTest, ConfigWriterRoundTrip) {
    Config cfg;
    Profile p;
    p.id = "my-shell";
    p.name = "My Shell";
    p.command = "C:\\Program Files\\Git\\bin\\bash.exe";  // backslash test
    p.args = {"--login"};
    p.theme = "Dracula";
    p.font_size = 16.0f;
    cfg.profiles.push_back(p);
    cfg.default_profile_id = "my-shell";
    cfg.hidden_profile_ids.push_back("cmd");

    // Serialize
    std::string lua = serializeConfigLua(cfg);

    // Re-load
    auto result = loadConfigLuaString(lua);
    ASSERT_TRUE(result.ok()) << result.error();

    const Config& loaded = luaConfig();
    ASSERT_EQ(loaded.profiles.size(), 1u);
    EXPECT_EQ(loaded.profiles[0].id, "my-shell");
    EXPECT_EQ(loaded.profiles[0].command, "C:\\Program Files\\Git\\bin\\bash.exe");
    EXPECT_EQ(loaded.default_profile_id, "my-shell");
    ASSERT_EQ(loaded.hidden_profile_ids.size(), 1u);
    EXPECT_EQ(loaded.hidden_profile_ids[0], "cmd");
}
#endif
```

- [ ] **Step 3: Run tests**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests --gtest_filter="*ProfileLua*"`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add core/src/lua_config_writer.cpp tests/test_profile.cpp
git commit -m "feat(lua): serialize profiles in config writer with round-trip test"
```

---

### Task 13: Settings model — Profiles category

**Files:**
- Modify: `core/src/settings_model.cpp`

- [ ] **Step 1: Add Profiles category to buildCategories()**

Follow existing brace-init style:

```cpp
// Profiles
categories_.push_back({"profiles", "Profiles", "", SectionType::Settings, {}});
categories_.push_back({"profiles.all", "All Profiles", "profiles", SectionType::CardGrid, {}});
```

- [ ] **Step 2: Add default_profile_id to string accessors**

In `stringValue()`:
```cpp
if (key == "default_profile_id") return cfg.default_profile_id;
```

Also update `isStringKey()` (line 77-81) to include `"default_profile_id"`, or `markModified()` won't detect changes:
```cpp
static bool isStringKey(const std::string& key) {
    return key == "shell" || key == "cursor_style" ||
           key == "clipboard_paste_protection" || key == "font_family" ||
           key == "theme" || key == "keybinding_preset" ||
           key == "default_profile_id";
}
```

- [ ] **Step 2b: Verify config_value_adapter.cpp (already done in Task 3)**

`default_profile_id`는 Task 3 Step 2에서 이미 `config_value_adapter.cpp`의 getter/setter에 추가되었다. 여기서는 추가 코드 변경 불필요. 커밋 시 staging에 포함하여 feature 완결성을 확인한다.

- [ ] **Step 3: Build and test**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add core/src/settings_model.cpp core/src/config_value_adapter.cpp
git commit -m "feat(settings): add Profiles category and default_profile_id accessor"
```

---

### Task 14: Integration tests and final verification

**Files:**
- Modify: `tests/test_profile.cpp`
- All CMakeLists.txt files verified

- [ ] **Step 1: Write integration tests**

Append to `tests/test_profile.cpp`:

```cpp
TEST(ProfileIntegrationTest, ConfigBackwardCompat_ShellField) {
    // When no profiles but Config::shell is set,
    // ProfileManager + TerminalController should create a legacy fallback
    ProfileManager mgr;

    // Simulate what TerminalController does:
    std::string config_shell = "/usr/bin/zsh";
    if (mgr.allProfiles().empty() && !config_shell.empty()) {
        Profile legacy;
        legacy.id = "__legacy_shell__";
        legacy.name = "Shell";
        legacy.command = config_shell;
        mgr.setProfile(legacy);
        mgr.setDefaultProfile(legacy.id);
    }

    EXPECT_EQ(mgr.defaultProfile().command, "/usr/bin/zsh");
    EXPECT_EQ(mgr.defaultProfile().id, "__legacy_shell__");
}

TEST(ProfileIntegrationTest, ResolveProfileConfigEndToEnd) {
    Config global;
    global.font_family = "Consolas";
    global.font_size = 14.0f;
    global.theme = "Catppuccin Mocha";

    ProfileManager mgr;
    std::vector<Profile> detected;
    Profile d; d.id = "bash"; d.name = "Bash"; d.command = "/bin/bash"; d.auto_detected = true;
    detected.push_back(d);
    mgr.setDetectedProfiles(std::move(detected));

    Profile user; user.id = "bash"; user.theme = "Dracula";
    mgr.setProfile(user);

    auto* p = mgr.findProfile("bash");
    ASSERT_NE(p, nullptr);
    Config resolved = resolveProfileConfig(global, *p);
    EXPECT_EQ(resolved.theme, "Dracula");
    EXPECT_EQ(resolved.font_family, "Consolas");
}
```

- [ ] **Step 2: Run full test suite**

Run: `cmake --build build --target termcore_tests && ./build/tests/termcore_tests`
Expected: All tests PASS

- [ ] **Step 3: Full build verification**

Run: `cmake --build build`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add tests/test_profile.cpp
git commit -m "test(profile): add integration tests for profile system"
```
