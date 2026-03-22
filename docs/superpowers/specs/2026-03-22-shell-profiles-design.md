# Shell Profile System Design

## Goal

OS별 설치된 쉘을 자동 감지하고, Windows Terminal 스타일의 프로필 시스템을 도입하여 사용자가 쉘 + 외형(테마, 폰트, 커서 스타일)을 묶어 관리할 수 있게 한다. 전 플랫폼(Windows, macOS, Linux)에서 동일한 UX를 제공한다.

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  ProfileManager                  │
│  ┌──────────────┐  ┌────────────────────────┐   │
│  │ ShellDetector│  │ Profile Storage (Lua)   │   │
│  │ (런타임 감지) │  │ (사용자 커스텀/오버라이드)│   │
│  └──────┬───────┘  └───────────┬────────────┘   │
│         │    merge & dedupe    │                 │
│         └──────────┬───────────┘                 │
│                    ▼                             │
│          std::vector<Profile>                    │
│          (감지 + 커스텀 합산 목록)                 │
└────────────────────┬────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
   Settings UI   Tab Bar ▼    Keyboard
   (프로필 카드)  (드롭다운)  (Mod+Shift+1~9)
```

### Core Principles

1. **자동 감지된 프로필은 파일에 저장하지 않음** — 매 실행 시 동적 생성
2. **사용자 수정/추가 프로필만 `config.lua`에 저장** — `terminal.profile({...})` 형태
3. **프로필 = 쉘 + 외형** — 쉘 경로, 테마, 폰트, 커서 스타일을 묶는 단위
4. **전 플랫폼 동일 UX** — 탭 바 드롭다운, 단축키, 설정 화면 모두 공통 구현

## Data Structures

### Profile (core)

```cpp
struct Profile {
    std::string id;           // 고유 ID ("cmd", "powershell", "wsl-ubuntu", "custom-ssh")
    std::string name;         // 표시 이름 ("PowerShell", "Ubuntu (WSL)")
    std::string command;      // 실행 명령 ("pwsh.exe", "wsl")
    std::vector<std::string> args;  // 추가 인자 ({"-d", "Ubuntu"})
    std::string working_dir;  // 시작 디렉토리 (비어있으면 홈)
    std::string icon;         // 아이콘 식별자 ("powershell", "ubuntu", "bash")

    // 외형 오버라이드 (optional — nullopt = 전역 설정 상속)
    std::optional<std::string> theme;
    std::optional<std::string> font_family;
    std::optional<float> font_size;
    std::optional<std::string> cursor_style;

    bool is_default = false;  // 기본 프로필 여부
    bool hidden = false;      // 목록에서 숨김
    bool auto_detected = false; // 자동 감지된 프로필 (파일 저장 안 함)
};
```

`std::optional`을 사용하여 "설정되지 않음 = 전역 상속"을 명확히 표현한다.

### Override Resolution

프로필의 외형 필드를 적용할 때 `resolveProfileConfig()` 헬퍼를 사용:

```cpp
/// 프로필 오버라이드를 전역 Config에 적용한 결과를 반환
Config resolveProfileConfig(const Config& global, const Profile& profile);
```

로직: 프로필의 optional 필드가 `has_value()`이면 해당 값 사용, 아니면 전역 Config 값 유지.

### ProfileManager (core)

```cpp
class ProfileManager {
public:
    /// 자동 감지 + 사용자 프로필 병합된 전체 목록
    const std::vector<Profile>& allProfiles() const;

    /// 숨기지 않은 프로필만 (순서: 감지 → 사용자 커스텀, 각 그룹 내 감지/등록 순서 유지)
    std::vector<const Profile*> visibleProfiles() const;

    /// 기본 프로필 반환. 항상 유효한 프로필을 반환 (fallback 보장).
    const Profile& defaultProfile() const;

    /// ID로 검색. 없으면 nullptr.
    const Profile* findProfile(const std::string& id) const;

    /// 사용자 프로필 추가/수정
    void setProfile(const Profile& profile);

    /// 기본 프로필 지정
    void setDefaultProfile(const std::string& id);

    /// 프로필 숨기기
    void hideProfile(const std::string& id);

    /// 자동 감지 실행 (시작 시 + 설정 리로드 시)
    void detectShells();

private:
    std::vector<Profile> detected_;   // 자동 감지
    std::vector<Profile> user_;       // 사용자 정의/오버라이드
    std::vector<Profile> merged_;     // 합산 결과
    std::string default_id_;
    Profile fallback_;                // 항상 존재하는 fallback 프로필

    void rebuildMerged();
    void ensureFallback();
};
```

### Fallback Profile

`defaultProfile()`은 항상 유효한 참조를 반환한다:

1. `default_id_`에 해당하는 프로필이 있으면 → 반환
2. `merged_`의 첫 번째 프로필이 있으면 → 반환
3. 둘 다 없으면 → `fallback_` 반환 (Windows: `cmd.exe`, Unix: `$SHELL` 또는 `/bin/sh`)

`fallback_`은 생성자에서 플랫폼별로 초기화된다.

### Merge Semantics

`rebuildMerged()`의 병합 규칙:

1. `detected_` 프로필을 먼저 `merged_`에 복사
2. `user_` 프로필을 순회:
   - **동일 `id`가 `merged_`에 있으면**: 필드 단위 오버라이드 (user 프로필에서 비어있지 않은 필드만 덮어씀). `auto_detected`는 `false`로 변경.
   - **동일 `id`가 없으면**: 새 프로필로 `merged_` 끝에 추가
3. `hidden_profile_ids`에 있는 프로필은 `hidden = true` 설정

### Profile Ordering

`visibleProfiles()`의 순서:
1. 자동 감지 프로필 (감지 순서 유지 — Windows: cmd, PS5, PS7, Git Bash, WSL 순)
2. 사용자 커스텀 프로필 (등록 순서 유지)

`Mod+Shift+1~9` 단축키는 이 `visibleProfiles()` 순서의 인덱스에 매핑된다.
범위를 벗어난 인덱스(예: 프로필 3개인데 `Mod+Shift+5`)는 무시(no-op).

### ShellDetector (core, platform-specific impl)

```cpp
class ShellDetector {
public:
    /// OS별 설치된 쉘 목록 반환
    static std::vector<Profile> detect();

private:
    // Windows
    static std::vector<Profile> detectWindows();
    static std::vector<Profile> detectWslDistros();

    // macOS / Linux
    static std::vector<Profile> detectUnix();
    static std::vector<Profile> parseEtcShells();
};
```

## OS-Specific Shell Detection

### Windows

| Shell | Detection Method | Profile ID |
|---|---|---|
| `cmd.exe` | `GetSystemDirectory()` + `\cmd.exe` | `cmd` |
| PowerShell 5 | `%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe` 존재 확인 | `powershell` |
| PowerShell 7+ | `where pwsh.exe` 또는 `Program Files\PowerShell\7\pwsh.exe` | `pwsh` |
| Git Bash | `Program Files\Git\bin\bash.exe` 존재 확인 | `git-bash` |
| WSL 배포판 | `wsl --list --quiet` 파싱 → 각 배포판별 프로필 | `wsl-<name>` |

**WSL 감지 세부사항:**
- `wsl --list --quiet` 실행 (UTF-16LE 출력)
- 각 줄을 배포판 이름으로 파싱
- 프로필: `command = "wsl"`, `args = {"-d", "<name>"}`
- WSL 미설치 시 조용히 건너뜀 (프로세스 실행 실패 = 0개 결과)
- **타임아웃: 3초.** 초과 시 WSL 프로필 0개로 처리
- **비동기 실행:** `detectShells()`는 백그라운드 스레드에서 실행. WSL 감지 완료 후 메인 스레드에서 `rebuildMerged()` 호출

### macOS

| Shell | Detection Method | Profile ID |
|---|---|---|
| zsh, bash, fish, sh, etc. | `/etc/shells` 파싱 | 바이너리 이름 (e.g., `zsh`) |
| nushell, etc. | `which nushell` 확인 (하드코딩 후보 목록) | `nushell` |

### Linux

| Shell | Detection Method | Profile ID |
|---|---|---|
| bash, zsh, fish, sh, etc. | `/etc/shells` 파싱 | 바이너리 이름 |
| flatpak/snap 쉘 | `/etc/shells`에 포함된 경우 자동 감지 | 경로 기반 |

## Call Chain: Profile → PTY Spawn

### Modified Interfaces

현재 PTY 생성 체인:

```
Action::NewTab → handleAction() → TabController::createTab()
  → PtyFactory(rows, cols) → IPlatformHost::createPty(shell, rows, cols)
    → Pty::spawn(command, args, cwd, rows, cols)
```

변경 후:

```
Action::NewTab → handleAction(profile_id) → TabController::createTab(rows, cols, profile_id)
  → PtyFactory(profile, rows, cols) → IPlatformHost::createPty(profile, rows, cols)
    → Pty::spawn(profile.command, profile.args, profile.working_dir, rows, cols)
```

### Interface Changes

**PtyFactory 시그니처 변경:**

```cpp
// Before:
using PtyFactory = std::function<std::unique_ptr<Pty>(int rows, int cols)>;

// After:
using PtyFactory = std::function<std::unique_ptr<Pty>(const Profile& profile, int rows, int cols)>;
```

**IPlatformHost::createPty 시그니처 변경:**

```cpp
// Before:
virtual std::unique_ptr<Pty> createPty(const std::string& shell, int rows, int cols) = 0;

// After:
virtual std::unique_ptr<Pty> createPty(const Profile& profile, int rows, int cols) = 0;
```

각 플랫폼 구현체 변경:
- `TerminalWindowState::createPty()` (Windows)
- `MacPlatformHost::createPty()` (macOS)
- `GtkPlatformHost::createPty()` (Linux)

모두 `profile.command`, `profile.args`, `profile.working_dir`를 `Pty::spawn()`에 전달.

**TabController 변경:**

```cpp
// 새 오버로드 추가:
PaneId createTab(int rows, int cols, const std::string& profile_id = "");
PaneId splitPane(..., const std::string& profile_id = "");
```

`profile_id`가 비어있으면 기본 프로필 사용.

**TerminalController::handleAction 변경:**

```cpp
case Action::NewTab:
    tabCtrl_->createTab(rows, cols);  // 기본 프로필
    break;
case Action::NewTabProfile1 ... Action::NewTabProfile9: {
    int idx = static_cast<int>(action) - static_cast<int>(Action::NewTabProfile1);
    auto profiles = profileMgr_->visibleProfiles();
    if (idx < (int)profiles.size()) {
        tabCtrl_->createTab(rows, cols, profiles[idx]->id);
    }
    // 범위 밖이면 무시 (no-op)
    break;
}
```

## Lua API

### 프로필 추가/오버라이드

```lua
-- 커스텀 프로필 추가
terminal.profile({
    id = "my-ssh",
    name = "Production Server",
    command = "ssh",
    args = { "user@prod.example.com" },
    icon = "ssh",
    theme = "Dracula",
})

-- 자동 감지된 프로필의 외형만 오버라이드
terminal.profile({
    id = "powershell",        -- 자동 감지 ID와 매칭
    theme = "One Dark",
    font_family = "Cascadia Code",
})

-- 기본 프로필 지정
terminal.default_profile("powershell")

-- 프로필 숨기기
terminal.hide_profile("cmd")
```

### Config 구조체 변경

```cpp
struct Config {
    // ... 기존 필드 ...
    std::string shell;  // 유지 (하위 호환). 프로필 시스템이 활성화되면 무시됨.

    // Profiles (사용자 정의만 — 자동 감지는 런타임)
    std::vector<Profile> profiles;
    std::string default_profile_id;
    std::vector<std::string> hidden_profile_ids;
};
```

### Config.shell 하위 호환성

- `Config::shell`은 기존 설정 파일과의 호환성을 위해 유지
- 프로필이 1개 이상 존재하면 `Config::shell`은 무시됨
- 프로필이 0개이고 `Config::shell`이 설정되어 있으면 → 해당 쉘로 fallback 프로필 자동 생성
- 이를 통해 기존 `terminal.config({ shell = "pwsh.exe" })` 사용자도 마이그레이션 없이 동작

### Lua 직렬화 (config 저장)

`lua_config_writer.cpp`에서 사용자 프로필을 `terminal.profile({...})` 호출로 직렬화:

```lua
-- Auto-generated profile overrides
terminal.profile({
    id = "powershell",
    theme = "One Dark",
    font_family = "Cascadia Code",
})

terminal.default_profile("powershell")
terminal.hide_profile("cmd")
```

## UI Integration

### Tab Bar Dropdown (전 플랫폼 공통)

새 탭 `+` 버튼 옆에 `▼` 화살표 추가:

```
┌──────────┐ ┌──────────┐ ┌──────────┐  ┌──┬──┐
│  bash    │ │ node     │ │  ssh     │  │ +│▼ │
└──────────┘ └──────────┘ └──────────┘  └──┴──┘
                                            │
                                ┌───────────┴───────────┐
                                │ ● PowerShell         │
                                │   Git Bash           │
                                │   Ubuntu (WSL)       │
                                │   cmd                │
                                │ ──────────────────── │
                                │   Production Server  │
                                │ ──────────────────── │
                                │   Settings...        │
                                └───────────────────────┘
```

- `●` = 기본 프로필 표시
- 구분선: 자동 감지 / 사용자 커스텀 / 설정 링크
- `+` 클릭 = 기본 프로필로 새 탭
- `▼` 클릭 = 드롭다운 표시

### Keyboard Shortcuts

| 단축키 | 동작 |
|---|---|
| `Mod+T` | 기본 프로필로 새 탭 (기존 동작 유지) |
| `Mod+Shift+1~9` | visible 프로필 인덱스 1~9로 새 탭. 범위 밖이면 no-op. |

### Settings UI

**사이드바에 Profiles 카테고리 추가:**

```
📂 General
📂 Appearance
📂 Font
📂 Keyboard
📂 Profiles        ← 새 카테고리
   └ All Profiles  (카드 그리드)
📂 Clipboard
```

**프로필 카드:**
```
┌─────────────────────────────────┐
│ 🔷 PowerShell                  │
│ pwsh.exe                       │
│ Theme: One Dark | Font: default│
│ [Default ●] [Hide] [Edit]     │
└─────────────────────────────────┘
```

- 카드 클릭 → 프로필 편집 (이름, 쉘 경로, 테마/폰트/커서 오버라이드)
- `+ Add Profile` 버튼 → 커스텀 프로필 추가
- Default 토글 → 기본 프로필 지정

### Icon Rendering

V1에서 아이콘은 텍스트 기반 식별자로 관리:
- 내장 아이콘 맵: `{"cmd": ">_", "powershell": "PS", "bash": "$", "zsh": "%", "fish": "><>", "wsl-*": "🐧", "ssh": "→"}`
- 설정 UI의 프로필 카드에서 아이콘 텍스트를 표시
- V2에서 SVG/텍스처 아이콘으로 확장 가능

## Session Save/Restore

### PaneSessionData 변경

```cpp
struct PaneSessionData {
    // ... 기존 필드 ...
    std::string profile_id;  // 새 필드: 이 패인의 프로필 ID
};
```

### Session Version

- `SessionData::version`을 `1` → `2`로 올림
- 역직렬화 시 version 1 (profile_id 없음)은 빈 문자열로 처리 → 기본 프로필 fallback
- version 2에서는 `profile_id` 필드를 읽어 해당 프로필로 복원

세션 복원 시:
1. `profile_id`로 프로필 검색
2. 찾으면 해당 프로필의 쉘로 재생성
3. 못 찾으면 (쉘이 삭제된 경우) 기본 프로필로 fallback

## Override Priority

```
전역 Config (기본값)
    ↓ 오버라이드
프로필 설정 (theme, font_family, font_size, cursor_style)
    ↓ 상속
optional 필드가 nullopt이면 전역 설정 사용
```

## New Actions

```cpp
enum class Action : uint16_t {
    // ... 기존 ...
    NewTabProfile1, NewTabProfile2, NewTabProfile3,
    NewTabProfile4, NewTabProfile5, NewTabProfile6,
    NewTabProfile7, NewTabProfile8, NewTabProfile9,
    ShowProfileDropdown,
};
```

`NewTabProfile1~9`는 `visibleProfiles()` 인덱스 0~8에 매핑.
범위를 벗어난 인덱스는 no-op (무시).

## File Structure

| File | Purpose |
|---|---|
| `core/include/termcore/profile.h` | Profile struct, ProfileManager 선언 |
| `core/src/profile_manager.cpp` | ProfileManager 구현 (merge, default, lookup, fallback) |
| `core/src/shell_detector.cpp` | ShellDetector 공통 로직 + 플랫폼 디스패치 |
| `core/src/shell_detector_windows.cpp` | Windows 감지 (cmd, PS, Git Bash, WSL) |
| `core/src/shell_detector_unix.cpp` | macOS/Linux 감지 (/etc/shells, which) |
| `core/src/lua_config.cpp` | `terminal.profile()`, `terminal.default_profile()`, `terminal.hide_profile()` API 추가 |
| `core/src/lua_config_writer.cpp` | 프로필 직렬화 (`terminal.profile({...})` 출력) |
| `core/include/termcore/tab_controller.h` | PtyFactory 시그니처 변경, createTab profile_id 파라미터 |
| `core/src/tab_controller.cpp` | profile_id를 PtyFactory에 전달 |
| `core/src/terminal_controller.cpp` | ProfileManager 통합, handleAction에 NewTabProfile 처리 |
| `core/include/termcore/platform_host.h` | IPlatformHost::createPty 시그니처 변경 |
| `platform/windows/src/TerminalWindowStateEvents.cpp` | createPty Profile 기반 구현 |
| `platform/macos/src/MacPlatformHost.mm` | createPty Profile 기반 구현 |
| `platform/linux/src/GtkPlatformHost.cpp` | createPty Profile 기반 구현 |
| `core/src/session_serializer.cpp` | profile_id 직렬화 |
| `core/src/session_deserializer.cpp` | profile_id 역직렬화 + fallback, version 2 지원 |
| `core/src/keybinding.cpp` | NewTabProfile1~9, ShowProfileDropdown 액션 추가 |
| `core/src/settings_model.cpp` | Profiles 카테고리 추가 |
| `tests/test_profile.cpp` | ProfileManager 단위 테스트 |
| `tests/test_shell_detector.cpp` | ShellDetector 단위 테스트 |

## Testing Strategy

1. **ShellDetector 테스트:** 각 OS에서 기본 쉘 최소 1개 감지 확인, 빈 /etc/shells 처리
2. **ProfileManager 테스트:** merge/dedupe, default fallback, hide, field-level override
3. **ProfileManager 테스트 (edge cases):** 빈 프로필 목록, 존재하지 않는 ID로 default 지정, duplicate ID
4. **Lua API 테스트:** `terminal.profile()`, `terminal.default_profile()`, `terminal.hide_profile()`
5. **Session 테스트:** profile_id 직렬화/역직렬화, version 1→2 마이그레이션, fallback
6. **Keybinding 테스트:** NewTabProfile1~9 액션 매핑, 범위 밖 no-op
7. **Config 호환성 테스트:** `Config::shell`만 설정된 기존 config.lua가 정상 동작하는지

## Out of Scope (V1)

- 프로필별 키바인딩 프리셋 분리
- 프로필별 스크롤백 크기 분리
- 프로필 내보내기/가져오기
- 프로필 아이콘 커스터마이즈 (V1은 텍스트 기반 하드코딩 아이콘)
- 프로필 드래그 앤 드롭 순서 변경
