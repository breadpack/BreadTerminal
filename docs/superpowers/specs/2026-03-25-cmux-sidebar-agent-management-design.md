# cmux-style 사이드바 및 Agent 관리 기능 설계

## 개요

BreadTerminal에 cmux에서 영감을 받은 AI 에이전트 관리 기능을 추가한다. 좌측 세로 사이드바, 탭 바 에이전트 상태 표시, Notification Ring 애니메이션, `bread` CLI 확장, Claude Code Agent Team tmux 호환 백엔드, 그리고 Claude Code hook 연동을 통한 subagent 트리 뷰를 구현한다.

## 결정 사항

| 항목 | 결정 |
|------|------|
| 구현 범위 | 우선순위 1~6 전부 |
| 사이드바 방식 | 상단 탭 바 유지 + 좌측 사이드바 토글 추가 |
| Subagent 감지 | Claude Code hooks 연동 + 자동 hook 설치 편의 옵션 |
| CLI 구현 | 기존 `tools/bread/` C++ CLI 확장 (CMake 통합, 코드 재사용) |
| Agent Team 통합 | tmux 프로토콜 서브셋 구현 |
| 플랫폼 범위 | Core 크로스플랫폼 + Windows 렌더링 우선 |

## 아키텍처

```
┌─ Core (크로스플랫폼) ─────────────────────────────────┐
│  SidebarModel          - 사이드바 데이터 모델           │
│  AgentTreeTracker      - subagent 계층 트리 추적        │
│  HookBridge            - Claude Code hook 연동 로직      │
│  (기존) AgentTracker, Mux, NotificationStore            │
│  (기존) TmuxIntegration - tmux control mode 파싱        │
│  (기존) PaneEnvironment - 자식 프로세스 환경변수         │
└─────────────────────────────────────────────────────────┘
         │
┌─ Platform/Windows ──────────────────────────────────┐
│  D3DCellBuilderSidebar  - 사이드바 렌더링 (새 Pass)   │
│  D3DCellBuilderOverlays - 탭 바 확장 + Notif Ring     │
│  TerminalWindowState    - 사이드바 토글/뷰포트 조정    │
└──────────────────────────────────────────────────────┘
         │
┌─ CLI (기존 tools/bread/ 확장) ──────────────────────┐
│  --tmux 모드 추가    - tmux 호환 서브셋               │
│  hook-event 명령 추가 - hook → Named Pipe 브릿지      │
│  (기존) hooks install, identify, capabilities 등      │
└──────────────────────────────────────────────────────┘
```

## 기존 코드 활용 계획

### 이미 존재하는 인프라

| 기존 코드 | 위치 | 이번 설계에서의 역할 |
|-----------|------|---------------------|
| `AgentTracker` | `core/include/termcore/agent.h` | 에이전트 상태 데이터 소스 |
| `NotificationStore` | `core/include/termcore/notification.h` | 알림/unread 데이터 소스 |
| `CommandDispatcher` | `core/include/termcore/socket/command_dispatcher.h` | PaneStatus, PaneProgress, AttentionCallback |
| `BorderSegment` | `platform/windows/include/D3DTextRenderer.h` | ring_intensity, ring_color 필드 활용 |
| `PaneEnvironment` | `core/include/termcore/pane_environment.h` | TMUX 환경변수 추가 대상 |
| `TmuxIntegration` | `core/include/termcore/tmux_integration.h` | tmux control mode 파싱 (별도 관심사) |
| `bread CLI` | `tools/bread/` | hook-event, --tmux 모드 확장 대상 |
| `hooks_installer` | `tools/bread/hooks_installer.h` | hook 설치 기능 확장 |
| `Config` | `core/include/termcore/config.h` | sidebar_visible, sidebar_width 필드 |

### TmuxIntegration vs tmux 호환 모드 관계

- **TmuxIntegration** (기존): BreadTerminal이 tmux의 control mode 프로토콜을 *파싱*하는 클라이언트 역할. 원격 tmux 세션 연결에 사용.
- **tmux 호환 모드** (신규): `bread --tmux` CLI가 tmux 명령어 인터페이스를 *에뮬레이션*. Claude Code가 보내는 tmux 명령을 JSON-RPC로 변환. 서로 다른 관심사.

## 컴포넌트 상세 설계

### 1. SidebarModel (core/include/termcore/sidebar_model.h)

사이드바에 표시할 데이터를 집약하는 모델. 렌더러 독립적.

```cpp
struct SidebarEntry {
    PaneId pane_id;
    std::string title;
    std::string icon;
    AgentState agent_state;
    std::string git_branch;
    std::string cwd;
    std::vector<PaneStatus> pills;
    PaneProgress progress;
    bool has_unread;
    float attention_intensity;   // 0.0-1.0
    std::vector<SubagentNode> subagents;
    bool expanded = true;        // subagent 트리 접기/펼치기
};

class SidebarModel {
public:
    explicit SidebarModel(AgentTreeTracker& tree_tracker);

    // 이벤트 기반 업데이트 (mux 구조 변경, 에이전트 상태 변경, 알림 추가 시 호출)
    void update(const AgentTracker&, const NotificationStore&,
                const TabController&, const CommandDispatcher&);
    const std::vector<SidebarEntry>& entries() const;
    void setExpanded(PaneId, bool);
};
```

**데이터 수집 흐름:**
- `AgentTracker` → agent_state, icon, agent type name
- `NotificationStore` → has_unread, (새 알림 시 attention_intensity 설정)
- `TabController` → title, icon_name, process_name
- `TabController::Screen` → cwd (Screen의 working directory 추적, OSC 7)
- git branch → Shell Integration의 git status 데이터 또는 cwd 기반 `git rev-parse`
- `CommandDispatcher` → pills, progress (PaneStatus, PaneProgress)
- `AgentTreeTracker` → subagents 트리

**업데이트 빈도:** 이벤트 구동. `AgentTracker::setStateCallback`, `NotificationStore::setCallback`, `Mux::setOnChanged`에 연결. 매 프레임 폴링하지 않음.

**스레드 안전:** `SidebarModel::update()`는 main thread에서만 호출. `CommandDispatcher`의 `agent_meta_mutex_`는 내부적으로 보호됨. `getPaneStatuses()`, `getPaneProgress()` 호출은 이미 thread-safe.

### 2. 탭 바 확장 (platform/windows)

**양쪽 TabInfo 모두 수정:**

core `TabController::TabInfo` (tab_controller.h):
```cpp
struct TabInfo {
    // ... 기존 필드 (title, icon_name, process_name, active, has_unread, needs_attention) ...
    AgentState agent_state = AgentState::Inactive;  // 추가
    float progress_value = -1.0f;                   // 추가 (-1 = 숨김)
};
```

platform `D3DTextRenderer::TabInfo` (D3DTextRenderer.h):
```cpp
struct TabInfo {
    // ... 기존 필드 ...
    int agent_state = 0;          // 추가 (AgentState as int, core 타입 의존 방지)
    float progress_value = -1.0f; // 추가
};
```

**데이터 전달:** `TerminalWindowState::updateTabBar()`에서 core TabInfo → renderer TabInfo 매핑.

**렌더링 변경 (D3DCellBuilderOverlays.cpp Pass 8):**
- 탭 아이콘 옆에 8x8px 상태 색상 점 추가
  - Inactive: 없음, Running/Thinking: 노랑, ToolUse: 주황, Waiting: 파랑, Error: 빨강, Idle: 초록
- 탭 하단에 2px 높이 progress 바 추가 (progress_value >= 0일 때만)
- 기존 `has_unread` 점 표시와 겹치지 않도록 위치 조정

### 3. Notification Ring 애니메이션

기존 `BorderSegment`의 `ring_intensity`, `ring_color` 필드를 활용.

**추가 로직:**
- `NotificationStore` 콜백에서 새 알림 시 해당 pane의 `ring_intensity = 1.0` 설정
- 프레임마다 `ring_intensity -= deltaTime * decay_rate` (약 3초에 걸쳐 감쇠)
- `AgentTracker` 상태 변경 시 색상 매핑:
  - Waiting/NeedsInput → `#3B82F6` (파랑)
  - Error → `#EF4444` (빨강)
  - Exited(성공) → `#22C55E` (초록)
- 펄싱: `ring_intensity > 0`일 때 `0.5 + 0.5 * sin(time * 4.0)` 곱하여 글로우 펄싱

**렌더링 변경 (D3DCellBuilderOverlays.cpp Pass 9):**
- 기존 `needs_attention` 글로우를 `ring_intensity > 0`일 때도 적용
- 글로우 두께를 intensity에 비례하여 2~6px로 변화

### 4. AgentTreeTracker (core/include/termcore/agent_tree_tracker.h)

Claude Code subagent 계층을 트리로 추적.

```cpp
struct SubagentNode {
    std::string agent_id;
    std::string agent_type;     // "Explore", "TDD", "code-reviewer" 등
    std::string description;
    AgentState state;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point ended;  // state==Exited일 때
    std::vector<SubagentNode> children;
};

class AgentTreeTracker {
public:
    void onAgentStart(PaneId pane, const std::string& agent_id,
                      const std::string& type, const std::string& desc,
                      const std::string& parent_id = "");
    void onAgentStop(PaneId pane, const std::string& agent_id,
                     AgentState final_state);
    void onAgentStateChange(PaneId pane, const std::string& agent_id,
                            AgentState state);

    const std::vector<SubagentNode>& rootAgents(PaneId pane) const;
    const SubagentNode* findAgent(const std::string& agent_id) const;
    size_t activeCount(PaneId pane) const;

    void sweepCompleted(std::chrono::seconds max_age = std::chrono::seconds(300));
    void clearForPane(PaneId pane);
};
```

**데이터 소스:** `HookBridge`를 통해 Claude Code hook 이벤트 수신.

### 5. HookBridge (core/include/termcore/hook_bridge.h)

Claude Code hook과 BreadTerminal 간의 브릿지.

```cpp
class HookBridge {
public:
    HookBridge(AgentTreeTracker& tree, AgentTracker& tracker,
               NotificationStore& notifications);

    // Named Pipe로 수신한 hook 이벤트 처리
    // 알 수 없는 필드는 무시 (forward compatibility)
    void processHookEvent(const nlohmann::json& event);

    // hook 설치/제거 — 기존 tools/bread/hooks_installer 확장
    static bool installHooks(const std::string& bread_cli_path);
    static bool uninstallHooks();
    static bool isInstalled();
};
```

**hook 이벤트 JSON 형식 (Claude Code → bread CLI → Named Pipe):**
```json
{
  "event": "SubagentStop",
  "agent_id": "abc123",
  "agent_type": "Explore",
  "description": "Find test files",
  "state": "completed",
  "parent_agent_id": "root"
}
```

**에러 처리:**
- Named Pipe 서버 미실행 시: bread CLI가 stderr에 경고 출력 후 exit 0 (hook 체인 중단 방지)
- 잘못된 JSON: 로그 남기고 무시
- 알 수 없는 event 타입: 무시 (forward compatibility)
- 설치된 bread 경로 변경 감지: `bread hooks install` 시 기존 설정 덮어쓰기

**자동 설치 흐름:**
1. `bread hooks install` 실행 (기존 `hooks_installer.h` 확장)
2. `~/.claude/settings.json`의 hooks 배열에 BreadTerminal hook 스크립트 등록
3. hook 스크립트: 이벤트 발생 시 `bread hook-event --json '{...}'` 호출
4. `bread` CLI가 Named Pipe로 BreadTerminal에 전달

### 6. bread CLI 확장 (기존 tools/bread/)

기존 CLI 프레임워크에 두 가지 기능 추가.

**기존 파일 수정:**

`tools/bread/arg_parser.h` — `CommandType`에 `TmuxCompat` 추가, `LocalCmd`에 `HookEvent` 추가:
```cpp
enum class CommandType { RpcCommand, LocalCommand, TmuxCompat };
enum class LocalCmd { None, HooksInstall, Identify, Capabilities, GetText, HookEvent };
```

`tools/bread/main.cpp` — tmux 모드 분기 및 hook-event 처리 추가.

**새 파일:**

`tools/bread/tmux_compat.h/.cpp` — tmux CLI 인터페이스 → JSON-RPC 변환.

```cpp
namespace bread {
    // "bread --tmux split-window -h" → JSON-RPC pane.split 요청
    ParsedArgs parseTmuxArgs(int argc, char* argv[]);
}
```

**tmux 호환 서브셋:**

| Claude Code 호출 | JSON-RPC 변환 |
|-----------------|--------------|
| `split-window -h` | `pane.split {direction: "horizontal"}` |
| `split-window -v` | `pane.split {direction: "vertical"}` |
| `send-keys -t %N "text" Enter` | `pane.sendKeys {pane_id: N, keys: "text\n"}` |
| `select-pane -t %N` | `pane.focus {pane_id: N}` |
| `list-panes` | `pane.list {}` |
| `kill-pane -t %N` | `pane.close {pane_id: N}` |
| `display-message -p '#{pane_id}'` | `query.activePane {}` |
| (미지원 명령) | stderr에 지원 명령 목록 출력, exit 1 |

**환경변수 설정 (PaneEnvironment 확장):**

`core/include/termcore/pane_environment.h`의 `toEnvVars()`에 추가:
```cpp
// Agent Team tmux 호환 모드 활성화
vars.emplace_back("TMUX", "bread//" + std::to_string(pane_id));
vars.emplace_back("TMUX_PROGRAM", bread_cli_path + " --tmux");
```

Config에 `tmux_compat_enabled` 옵션 추가 (기본 true). false로 설정하면 TMUX 환경변수를 주입하지 않음 (실제 tmux와 충돌 방지).

### 7. 사이드바 렌더링 (platform/windows)

**렌더링용 데이터 구조 (D3DTextRenderer.h):**

core `SidebarEntry`를 직접 참조하지 않고, 플랫폼측 flat 구조 사용:

```cpp
struct SidebarRenderEntry {
    uint32_t pane_id;
    std::string title;
    std::string subtitle;          // "main • PR #42" 등 미리 포맷팅된 문자열
    std::string status_text;       // "Thinking... (8s)" 등
    int agent_state;               // AgentState as int
    float progress_value;          // -1 = hidden, 0.0-1.0
    std::string progress_label;
    bool has_unread;
    bool active;
    float attention_intensity;
    // subagent 트리 (flat list with indent level)
    struct SubagentRenderEntry {
        std::string name;          // "Explore"
        std::string status;        // "[Running]"
        int state;                 // AgentState as int
        int indent_level;          // 0 = root agent's child, 1 = grandchild
    };
    std::vector<SubagentRenderEntry> subagents;
    bool subagents_expanded;
};

struct SidebarRenderInfo {
    bool visible = false;
    int width = 220;               // config sidebar_width 기본값과 일치
    std::vector<SidebarRenderEntry> entries;
    int hovered_entry = -1;
    int hovered_subagent = -1;
    int scroll_offset = 0;
    uint32_t bg_color;
    uint32_t fg_color;
    uint32_t accent_color;
    uint32_t separator_color;
};
```

**D3DCellBuilderSidebar.cpp (새 파일, Pass 10):**

```
┌─ Sidebar (width: 220px) ──────────────────┐
│                                            │
│  ▶ Pane 1: Claude Code              🟢   │
│    main • PR #42                          │
│    ██████████░░ 60%  Compiling...         │
│    ⚒ build: passing  🧪 test: 3/10       │
│    ├─ Explore           [Running]  🟡     │
│    ├─ TDD               [Done]     🟢     │
│    └─ Code Review       [Waiting]  🔵     │
│                                            │
│  ▶ Pane 2: Codex                    🟡   │
│    feat/auth                              │
│    Thinking... (8s)                       │
│                                            │
│  ▶ Pane 3: shell                    ⚪   │
│    ~/project/src                          │
│                                            │
└────────────────────────────────────────────┘
```

**레이아웃 규칙:**
- 사이드바 너비: config `sidebar_width` (기본 220px, 기존 defaults/config.lua와 일치)
- 엔트리 높이: 가변 (기본 정보 3줄 + subagent당 1줄)
- 접기/펼치기: ▶/▼ 클릭으로 subagent 트리 토글
- 활성 pane 강조: 배경색 약간 밝게
- 스크롤: pane이 많을 때 세로 스크롤
- 사이드바 가장자리 드래그로 너비 조절 가능

**사이드바 토글:**
- 단축키: `Ctrl+Shift+B` (config로 변경 가능, Ctrl+B는 tmux prefix와 충돌 방지)
- Config: `sidebar_visible = true/false`

**뷰포트 조정:**
- `sidebar_visible=true`일 때, 모든 기존 렌더링 패스(셀 그리드, 탭 바, pane 경계선, 검색 하이라이트)의 뷰포트 origin이 우측으로 `sidebar_width`px 이동
- `TerminalWindowState`에서 `gridOffsetX` 계산에 사이드바 너비 반영
- 터미널 그리드의 열 수는 사이드바 너비만큼 줄어듦 (PTY에 resize 전달)

**키보드 네비게이션:**
- `Ctrl+Shift+B`로 사이드바 포커스 토글
- 사이드바 포커스 시: ↑/↓로 엔트리 이동, Enter로 해당 pane 포커스, Space로 subagent 트리 접기/펼치기

## 병렬 작업 분할

| 에이전트 | 작업 | 범위 | 의존성 |
|---------|------|------|--------|
| Agent 1 | SidebarModel + AgentTreeTracker | core 신규 파일 | 없음 |
| Agent 2 | 탭 바 확장 렌더링 | core TabInfo + platform D3D | 없음 |
| Agent 3 | Notification Ring 애니메이션 | platform D3D | 없음 |
| Agent 4 | bread CLI 확장 (hook-event + tmux 모드) | tools/bread/ 확장 | 없음 |
| Agent 5 | HookBridge + PaneEnvironment TMUX 환경변수 | core 신규 + 기존 수정 | 없음 |
| Agent 6 | 사이드바 렌더링 + 뷰포트 조정 | platform D3D 신규 | Agent 1 |

Agent 1~5: 동시 시작 가능 (독립적)
Agent 6: Agent 1(SidebarModel) 완료 후 시작 (데이터 구조 의존)

## 테스트 전략

- Core 컴포넌트: 단위 테스트 (SidebarModel, AgentTreeTracker, HookBridge JSON 파싱)
- CLI tmux 호환: 단위 테스트 (tmux 명령어 파싱 → JSON-RPC 변환 검증)
- CLI 미지원 명령 에러: 명확한 에러 메시지 + 지원 명령 목록 출력 검증
- 렌더링: 수동 테스트 (시각적 확인)
- Agent Team 통합: Claude Code와 실제 연동 테스트

## 리스크 및 완화

| 리스크 | 완화 |
|--------|------|
| TMUX 환경변수가 다른 도구에 영향 | `tmux_compat_enabled` config 옵션으로 비활성화 가능 |
| Claude Code hooks API 변경 | HookBridge가 알 수 없는 필드 무시, 스키마 버전 체크 |
| Named Pipe 동시 접속 부하 (hook 이벤트 빈발) | 기존 connect-send-close 모델 유지, 필요시 배칭 추가 |
| 미지원 tmux 명령 호출 | 명확한 에러 메시지로 지원 명령 안내 |
