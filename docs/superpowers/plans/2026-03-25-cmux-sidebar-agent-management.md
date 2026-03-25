# cmux-style 사이드바 및 Agent 관리 기능 구현 플랜

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** BreadTerminal에 cmux-style 사이드바, 에이전트 상태 표시, Notification Ring, Claude Code hook 연동, tmux 호환 CLI를 추가한다.

**Architecture:** Core에 SidebarModel과 AgentTreeTracker 데이터 모델을 추가하고, 기존 bread CLI를 확장하여 hook-event와 tmux 호환 모드를 지원한다. Windows 플랫폼에서 D3D11 렌더링 패스를 추가/수정하여 사이드바와 Notification Ring을 그린다.

**Tech Stack:** C++20, CMake, D3D11, nlohmann/json, Named Pipes (Windows)

**Spec:** `docs/superpowers/specs/2026-03-25-cmux-sidebar-agent-management-design.md`

---

## File Map

### Core 신규 파일
| 파일 | 책임 |
|------|------|
| `core/include/termcore/agent_tree_tracker.h` | Subagent 계층 트리 데이터 모델 |
| `core/src/agent_tree_tracker.cpp` | AgentTreeTracker 구현 |
| `core/include/termcore/hook_bridge.h` | Claude Code hook 이벤트 → 내부 시스템 브릿지 |
| `core/src/hook_bridge.cpp` | HookBridge 구현 |
| `core/include/termcore/sidebar_model.h` | 사이드바 데이터 집약 모델 |
| `core/src/sidebar_model.cpp` | SidebarModel 구현 |

### Core 수정 파일
| 파일 | 변경 내용 |
|------|----------|
| `core/include/termcore/tab_controller.h` | TabInfo에 agent_state, progress_value 필드 추가 |
| `core/src/tab_controller.cpp` | tabBarInfo()에서 AgentTracker 데이터 매핑 |
| `core/include/termcore/pane_environment.h` | TMUX, TMUX_PROGRAM 환경변수 추가 |
| `core/include/termcore/config.h` | tmux_compat_enabled 옵션 추가 |
| `core/defaults/config.lua` | tmux_compat_enabled = true 기본값 |

### Platform/Windows 수정 파일
| 파일 | 변경 내용 |
|------|----------|
| `platform/windows/include/D3DTextRenderer.h` | TabInfo에 agent_state/progress, SidebarRenderInfo 추가 |
| `platform/windows/src/D3DCellBuilderOverlays.cpp` | Pass 8: 탭 상태 점/progress 바, Pass 9: ring 애니메이션 강화 |
| `platform/windows/src/TerminalWindowState.cpp` | updateTabBar() 확장, updateSidebar() 추가, 뷰포트 오프셋 |

### Platform/Windows 신규 파일
| 파일 | 책임 |
|------|------|
| `platform/windows/src/D3DCellBuilderSidebar.cpp` | Pass 10: 사이드바 렌더링 |

### CLI 수정 파일
| 파일 | 변경 내용 |
|------|----------|
| `tools/bread/arg_parser.h` | CommandType::TmuxCompat, LocalCmd::HookEvent 추가 |
| `tools/bread/arg_parser.cpp` | --tmux 모드 파싱, hook-event 서브커맨드 |
| `tools/bread/main.cpp` | TmuxCompat/HookEvent 분기 추가 |
| `tools/bread/hooks_installer.cpp` | subagent hook 이벤트 스크립트 추가 |
| `tools/bread/CMakeLists.txt` | tmux_compat.cpp 추가 |

### CLI 신규 파일
| 파일 | 책임 |
|------|------|
| `tools/bread/tmux_compat.h` | tmux CLI → JSON-RPC 변환 |
| `tools/bread/tmux_compat.cpp` | tmux 서브셋 파싱 구현 |

### 테스트 파일
| 파일 | 테스트 대상 |
|------|------------|
| `tests/test_agent_tree_tracker.cpp` | AgentTreeTracker 단위 테스트 |
| `tests/test_hook_bridge.cpp` | HookBridge JSON 파싱 테스트 |
| `tests/test_sidebar_model.cpp` | SidebarModel 데이터 수집 테스트 |
| `tests/test_tmux_compat.cpp` | tmux 명령어 파싱 → JSON-RPC 변환 |

---

## Chunk 1: Core 데이터 모델 (Agent 1 - 독립)

### Task 1: AgentTreeTracker

**Files:**
- Create: `core/include/termcore/agent_tree_tracker.h`
- Create: `core/src/agent_tree_tracker.cpp`
- Create: `tests/test_agent_tree_tracker.cpp`

- [ ] **Step 1: Write SubagentNode and AgentTreeTracker header**

```cpp
// core/include/termcore/agent_tree_tracker.h
#pragma once

#include "termcore/agent.h"
#include "termcore/mux.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

struct SubagentNode {
    std::string agent_id;
    std::string agent_type;
    std::string description;
    AgentState state = AgentState::Inactive;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point ended;
    std::vector<SubagentNode> children;
};

class AgentTreeTracker {
public:
    AgentTreeTracker() = default;

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

private:
    SubagentNode* findNode(std::vector<SubagentNode>& nodes, const std::string& id);
    const SubagentNode* findNodeConst(const std::vector<SubagentNode>& nodes,
                                       const std::string& id) const;

    std::unordered_map<PaneId, std::vector<SubagentNode>> pane_trees_;
    static const std::vector<SubagentNode> empty_vec_;
};

} // namespace termcore
```

- [ ] **Step 2: Write failing test for onAgentStart/rootAgents**

```cpp
// tests/test_agent_tree_tracker.cpp
#include "termcore/agent_tree_tracker.h"
#include <gtest/gtest.h>

TEST(AgentTreeTracker, StartsEmpty) {
    termcore::AgentTreeTracker tracker;
    ASSERT_TRUE(tracker.rootAgents(1).empty());
    ASSERT_EQ(tracker.activeCount(1), 0u);
}

TEST(AgentTreeTracker, OnAgentStartAddsRoot) {
    termcore::AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "Explore", "Find files");

    auto& roots = tracker.rootAgents(1);
    ASSERT_EQ(roots.size(), 1u);
    ASSERT_EQ(roots[0].agent_id, "agent-1");
    ASSERT_EQ(roots[0].agent_type, "Explore");
    ASSERT_EQ(roots[0].description, "Find files");
    ASSERT_EQ(roots[0].state, termcore::AgentState::Starting);
}

TEST(AgentTreeTracker, OnAgentStartAddsChild) {
    termcore::AgentTreeTracker tracker;
    tracker.onAgentStart(1, "parent", "TDD", "Run tests");
    tracker.onAgentStart(1, "child", "Explore", "Find test files", "parent");

    auto& roots = tracker.rootAgents(1);
    ASSERT_EQ(roots.size(), 1u);
    ASSERT_EQ(roots[0].children.size(), 1u);
    ASSERT_EQ(roots[0].children[0].agent_id, "child");
}

TEST(AgentTreeTracker, OnAgentStopUpdatesState) {
    termcore::AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "Explore", "desc");
    tracker.onAgentStop(1, "agent-1", termcore::AgentState::Exited);

    auto* node = tracker.findAgent("agent-1");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->state, termcore::AgentState::Exited);
}

TEST(AgentTreeTracker, ActiveCountIgnoresExited) {
    termcore::AgentTreeTracker tracker;
    tracker.onAgentStart(1, "a1", "Explore", "d1");
    tracker.onAgentStart(1, "a2", "TDD", "d2");
    tracker.onAgentStop(1, "a1", termcore::AgentState::Exited);

    ASSERT_EQ(tracker.activeCount(1), 1u);
}

TEST(AgentTreeTracker, ClearForPane) {
    termcore::AgentTreeTracker tracker;
    tracker.onAgentStart(1, "a1", "Explore", "d1");
    tracker.onAgentStart(2, "a2", "TDD", "d2");
    tracker.clearForPane(1);

    ASSERT_TRUE(tracker.rootAgents(1).empty());
    ASSERT_EQ(tracker.rootAgents(2).size(), 1u);
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target tests && ctest -R AgentTreeTracker -V`
Expected: FAIL — `agent_tree_tracker.cpp` 미존재

- [ ] **Step 4: Write AgentTreeTracker implementation**

```cpp
// core/src/agent_tree_tracker.cpp
#include "termcore/agent_tree_tracker.h"
#include <algorithm>

namespace termcore {

const std::vector<SubagentNode> AgentTreeTracker::empty_vec_;

void AgentTreeTracker::onAgentStart(PaneId pane, const std::string& agent_id,
                                     const std::string& type, const std::string& desc,
                                     const std::string& parent_id) {
    SubagentNode node;
    node.agent_id = agent_id;
    node.agent_type = type;
    node.description = desc;
    node.state = AgentState::Starting;
    node.started = std::chrono::steady_clock::now();

    auto& trees = pane_trees_[pane];
    if (parent_id.empty()) {
        trees.push_back(std::move(node));
    } else {
        auto* parent = findNode(trees, parent_id);
        if (parent) {
            parent->children.push_back(std::move(node));
        } else {
            trees.push_back(std::move(node));
        }
    }
}

void AgentTreeTracker::onAgentStop(PaneId pane, const std::string& agent_id,
                                    AgentState final_state) {
    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return;
    auto* node = findNode(it->second, agent_id);
    if (node) {
        node->state = final_state;
        node->ended = std::chrono::steady_clock::now();
    }
}

void AgentTreeTracker::onAgentStateChange(PaneId pane, const std::string& agent_id,
                                           AgentState state) {
    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return;
    auto* node = findNode(it->second, agent_id);
    if (node) {
        node->state = state;
    }
}

const std::vector<SubagentNode>& AgentTreeTracker::rootAgents(PaneId pane) const {
    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return empty_vec_;
    return it->second;
}

const SubagentNode* AgentTreeTracker::findAgent(const std::string& agent_id) const {
    for (auto& [pane, trees] : pane_trees_) {
        auto* node = findNodeConst(trees, agent_id);
        if (node) return node;
    }
    return nullptr;
}

size_t AgentTreeTracker::activeCount(PaneId pane) const {
    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return 0;
    size_t count = 0;
    std::function<void(const std::vector<SubagentNode>&)> walk =
        [&](const std::vector<SubagentNode>& nodes) {
            for (auto& n : nodes) {
                if (n.state != AgentState::Exited && n.state != AgentState::Inactive)
                    ++count;
                walk(n.children);
            }
        };
    walk(it->second);
    return count;
}

void AgentTreeTracker::sweepCompleted(std::chrono::seconds max_age) {
    auto now = std::chrono::steady_clock::now();
    for (auto& [pane, trees] : pane_trees_) {
        std::function<void(std::vector<SubagentNode>&)> sweep =
            [&](std::vector<SubagentNode>& nodes) {
                for (auto& n : nodes) sweep(n.children);  // children first
                nodes.erase(
                    std::remove_if(nodes.begin(), nodes.end(),
                        [&](const SubagentNode& n) {
                            return n.state == AgentState::Exited &&
                                   n.children.empty() &&
                                   (now - n.ended) > max_age;
                        }),
                    nodes.end());
            };
        sweep(trees);
    }
}

void AgentTreeTracker::clearForPane(PaneId pane) {
    pane_trees_.erase(pane);
}

SubagentNode* AgentTreeTracker::findNode(std::vector<SubagentNode>& nodes,
                                          const std::string& id) {
    for (auto& n : nodes) {
        if (n.agent_id == id) return &n;
        auto* child = findNode(n.children, id);
        if (child) return child;
    }
    return nullptr;
}

const SubagentNode* AgentTreeTracker::findNodeConst(const std::vector<SubagentNode>& nodes,
                                                     const std::string& id) const {
    for (auto& n : nodes) {
        if (n.agent_id == id) return &n;
        auto* child = findNodeConst(n.children, id);
        if (child) return child;
    }
    return nullptr;
}

} // namespace termcore
```

- [ ] **Step 5: Add to CMakeLists.txt**

Modify `core/CMakeLists.txt`: add `src/agent_tree_tracker.cpp` to the source list.

- [ ] **Step 6: Run tests to verify they pass**

Run: tunit-test-runner 에이전트 사용
Expected: 5 tests PASS

- [ ] **Step 7: Commit**

```bash
git add core/include/termcore/agent_tree_tracker.h core/src/agent_tree_tracker.cpp tests/test_agent_tree_tracker.cpp core/CMakeLists.txt
git commit -m "feat: add AgentTreeTracker for subagent hierarchy tracking"
```

### Task 2: SidebarModel

**Files:**
- Create: `core/include/termcore/sidebar_model.h`
- Create: `core/src/sidebar_model.cpp`
- Create: `tests/test_sidebar_model.cpp`

- [ ] **Step 1: Write SidebarModel header**

```cpp
// core/include/termcore/sidebar_model.h
#pragma once

#include "termcore/agent.h"
#include "termcore/agent_tree_tracker.h"
#include "termcore/mux.h"
#include "termcore/notification.h"
#include "termcore/socket/command_dispatcher.h"
#include "termcore/tab_controller.h"

#include <string>
#include <vector>

namespace termcore {

struct SidebarEntry {
    PaneId pane_id = kInvalidPane;
    std::string title;
    std::string icon;
    AgentState agent_state = AgentState::Inactive;
    std::string git_branch;
    std::string cwd;
    std::vector<PaneStatus> pills;
    PaneProgress progress;
    bool has_unread = false;
    float attention_intensity = 0.0f;
    std::vector<SubagentNode> subagents;
    bool expanded = true;
};

class SidebarModel {
public:
    explicit SidebarModel(const AgentTreeTracker& tree_tracker);

    void update(const AgentTracker& agents,
                const NotificationStore& notifications,
                const TabController& tabs,
                const CommandDispatcher& dispatcher);

    const std::vector<SidebarEntry>& entries() const { return entries_; }
    void setExpanded(PaneId pane, bool expanded);
    bool isExpanded(PaneId pane) const;

private:
    const AgentTreeTracker& tree_tracker_;
    std::vector<SidebarEntry> entries_;
    std::unordered_map<PaneId, bool> expanded_state_;
};

} // namespace termcore
```

- [ ] **Step 2: Write failing tests**

```cpp
// tests/test_sidebar_model.cpp
// Test that SidebarModel collects data from AgentTracker and TabController.
// Tests cover: empty state, agent state mapping, unread detection, expansion toggle.
```

- [ ] **Step 3: Write SidebarModel implementation**

`core/src/sidebar_model.cpp`: `update()` iterates active workspace panes, pulls data from each subsystem, populates `entries_`.

- [ ] **Step 4: Add to CMakeLists.txt and run tests**

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/sidebar_model.h core/src/sidebar_model.cpp tests/test_sidebar_model.cpp core/CMakeLists.txt
git commit -m "feat: add SidebarModel for sidebar data aggregation"
```

---

## Chunk 2: 탭 바 확장 + Notification Ring (Agent 2 - 독립)

> **Note:** 탭 바(Pass 8)와 Notification Ring(Pass 9)은 같은 파일(`D3DCellBuilderOverlays.cpp`, `TerminalWindowState.cpp`)을 수정하므로 한 에이전트가 순차 처리한다.

### Task 3: Core TabInfo 확장

**Files:**
- Modify: `core/include/termcore/tab_controller.h:63-70`
- Modify: `core/src/tab_controller.cpp` (tabBarInfo 함수)

- [ ] **Step 1: Add agent_state and progress_value to core TabInfo**

```cpp
// core/include/termcore/tab_controller.h — TabInfo struct에 추가
    AgentState agent_state = AgentState::Inactive;
    float progress_value = -1.0f;  // -1 = hidden
```

- [ ] **Step 2: Add setAgentTracker setter to TabController**

TabController가 AgentTracker에 접근해야 하므로 setter 추가:
```cpp
// tab_controller.h에 추가
void setAgentTracker(const AgentTracker* tracker) { agent_tracker_ = tracker; }
// private:
const AgentTracker* agent_tracker_ = nullptr;
```

- [ ] **Step 3: Update tabBarInfo() to populate new fields**

`tab_controller.cpp`의 `tabBarInfo()` 에서 각 pane에 대해:
```cpp
if (agent_tracker_) {
    auto* agent = agent_tracker_->getAgent(pane_id);
    if (agent) {
        info.agent_state = agent->state;
    }
}
// progress_value는 외부에서 TerminalWindowState가 CommandDispatcher에서 가져와서 설정
```

- [ ] **Step 4: Commit**

```bash
git add core/include/termcore/tab_controller.h core/src/tab_controller.cpp
git commit -m "feat: add agent_state and progress_value to core TabInfo"
```

### Task 4: Platform TabInfo 확장 및 렌더링

**Files:**
- Modify: `platform/windows/include/D3DTextRenderer.h:116-123`
- Modify: `platform/windows/src/D3DCellBuilderOverlays.cpp` (Pass 8)
- Modify: `platform/windows/src/TerminalWindowState.cpp` (updateTabBar)

- [ ] **Step 1: Add fields to D3DTextRenderer::TabInfo**

```cpp
// D3DTextRenderer.h TabInfo에 추가
    int agent_state = 0;          // AgentState as int
    float progress_value = -1.0f; // -1 = hidden
```

- [ ] **Step 2: Map core TabInfo → renderer TabInfo in updateTabBar()**

`TerminalWindowState.cpp`의 `updateTabBar()` 에서:
```cpp
ti.agent_state = static_cast<int>(tabs[i].agent_state);
ti.progress_value = tabs[i].progress_value;
```

- [ ] **Step 3: Render agent state dot in Pass 8**

`D3DCellBuilderOverlays.cpp` Pass 8에서, 각 탭의 아이콘 위치 옆에 8x8px 색상 점 추가:
```cpp
// agent state → color mapping
uint32_t stateColor = 0;
switch (tab.agent_state) {
    case 3: /* Running */  stateColor = 0xEAB308; break; // yellow
    case 4: /* Thinking */ stateColor = 0xEAB308; break; // yellow
    case 5: /* ToolUse */  stateColor = 0xF97316; break; // orange
    case 6: /* Waiting */  stateColor = 0x3B82F6; break; // blue
    case 7: /* Error */    stateColor = 0xEF4444; break; // red
    case 2: /* Idle */     stateColor = 0x22C55E; break; // green
    default: break; // no dot
}
if (stateColor != 0) {
    // emit 8x8 tint cell at (iconX + iconW + 4, centerY - 4)
}
```

- [ ] **Step 4: Render progress bar at tab bottom in Pass 8**

탭 하단에 2px progress 바:
```cpp
if (tab.progress_value >= 0.0f) {
    float barW = tabW * tab.progress_value;
    // emit tint cell: x=tabX, y=tabBottom-2, w=barW, h=2, color=accent
}
```

- [ ] **Step 5: Commit**

```bash
git add platform/windows/include/D3DTextRenderer.h platform/windows/src/D3DCellBuilderOverlays.cpp platform/windows/src/TerminalWindowState.cpp
git commit -m "feat: render agent state dot and progress bar in tab bar"
```

### Task 5: Notification Ring 로직 + 렌더링

**Files:**
- Modify: `platform/windows/include/TerminalWindowState.h` (PaneRingState 추가)
- Modify: `platform/windows/src/TerminalWindowState.cpp` (ring 트리거, 감쇠, border 반영)
- Modify: `platform/windows/src/D3DCellBuilderOverlays.cpp` (Pass 9 글로우 확장)

- [ ] **Step 1: Add PaneRingState to TerminalWindowState.h**

```cpp
// platform/windows/include/TerminalWindowState.h — private 섹션에 추가
struct PaneRingState {
    float intensity = 0.0f;
    uint32_t color = 0x007acc;
    std::chrono::steady_clock::time_point triggered;
};
std::unordered_map<uint32_t, PaneRingState> pane_ring_states_;
```

- [ ] **Step 2: Wire NotificationStore callback to trigger ring**

`TerminalWindowState` 초기화에서 `NotificationStore::setCallback()` 등록:
```cpp
notifications_.setCallback([this](const termcore::Notification& n) {
    auto& ring = pane_ring_states_[n.pane_id];
    ring.intensity = 1.0f;
    ring.color = (n.urgency == termcore::NotificationUrgency::Critical)
                 ? 0xEF4444 : 0x3B82F6;
    ring.triggered = std::chrono::steady_clock::now();
});
```

- [ ] **Step 3: Wire AgentTracker callback to trigger ring with state color**

```cpp
agent_tracker_.setStateCallback([this](uint32_t pane_id, const termcore::AgentInfo& info) {
    auto& ring = pane_ring_states_[pane_id];
    switch (info.state) {
        case termcore::AgentState::Waiting:
        case termcore::AgentState::NeedsInput:
            ring.intensity = 1.0f; ring.color = 0x3B82F6; break;
        case termcore::AgentState::Error:
            ring.intensity = 1.0f; ring.color = 0xEF4444; break;
        case termcore::AgentState::Exited:
            ring.intensity = 0.8f; ring.color = 0x22C55E; break;
        default: return;
    }
    ring.triggered = std::chrono::steady_clock::now();
});
```

- [ ] **Step 4: Add decay logic in render frame update**

프레임 루프(render 직전)에서:
```cpp
float dt = deltaTime; // seconds since last frame
for (auto& [pane, ring] : pane_ring_states_) {
    ring.intensity -= dt * 0.33f; // ~3 second decay
    if (ring.intensity < 0.0f) ring.intensity = 0.0f;
}
```

BorderSegment 빌드 시 ring 데이터 반영:
```cpp
seg.ring_intensity = pane_ring_states_[pane_id].intensity;
seg.ring_color = pane_ring_states_[pane_id].color;
```

- [ ] **Step 5: Enhance Pass 9 glow rendering**

`D3DCellBuilderOverlays.cpp` Pass 9에서 기존 `needs_attention` 글로우를 확장:
```cpp
if (seg.ring_intensity > 0.0f) {
    float pulse = 0.5f + 0.5f * sinf(time_sec * 4.0f);
    float alpha = seg.ring_intensity * pulse;
    float thickness = 2.0f + 4.0f * seg.ring_intensity; // 2-6px
    // emit glow cells with ring_color at given alpha and thickness
}
```

- [ ] **Step 6: Build and visually verify**

알림 발생 시 pane 경계선에 펄싱 글로우 애니메이션 확인, 탭 바 상태 점과 progress 바 확인.

- [ ] **Step 7: Commit**

```bash
git add platform/windows/src/D3DCellBuilderOverlays.cpp platform/windows/src/TerminalWindowState.cpp platform/windows/include/TerminalWindowState.h
git commit -m "feat: add notification ring pulsing animation on pane borders"
```

---

## Chunk 3: bread CLI 확장 (Agent 3 - 독립)

### Task 6: hook-event 서브커맨드 추가

**Files:**
- Modify: `tools/bread/arg_parser.h`
- Modify: `tools/bread/arg_parser.cpp`
- Modify: `tools/bread/main.cpp`

- [ ] **Step 1: Parse "hook-event" subcommand as RemoteRPC in parseArgs()**

`hook-event`는 서버로 JSON-RPC 전송하므로 `CommandType::RemoteRPC`로 처리. `LocalCmd` enum 변경 불필요.

`arg_parser.cpp`에서 "hook-event" 서브커맨드 인식:
```cpp
if (subcommand == "hook-event") {
    args.type = CommandType::RemoteRPC;
    args.method = "hook.event";
    // --json 인자에서 JSON 파싱
    auto json_idx = findFlag(argc, argv, "--json");
    if (json_idx >= 0 && json_idx + 1 < argc) {
        args.params = nlohmann::json::parse(argv[json_idx + 1],
                                             nullptr, false);
        if (args.params.is_discarded()) {
            args.valid = false;
            args.error = "Invalid JSON in --json argument";
            return args;
        }
    }
    args.valid = true;
    return args;
}
```

- [ ] **Step 2: Add graceful failure for hook-event when server is down**

`main.cpp`에서 `hook.event` 메서드의 서버 연결 실패 시 stderr 경고 + exit 0 (hook 체인 중단 방지):
```cpp
if (!client.connect(args.socket_path, args.timeout_ms)) {
    if (args.method == "hook.event") {
        std::cerr << "Warning: BreadTerminal not running, hook event skipped\n";
        return 0;  // Don't break hook chain
    }
    std::cerr << "Error: " << client.lastError() << "\n";
    return 1;
}
```

- [ ] **Step 3: Test with manual invocation**

```bash
bread hook-event --json '{"event":"SubagentStop","agent_id":"test"}'
```
서버 미실행 시: `Warning: BreadTerminal not running, hook event skipped`, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add tools/bread/arg_parser.h tools/bread/arg_parser.cpp tools/bread/main.cpp
git commit -m "feat: add hook-event subcommand to bread CLI"
```

### Task 7: tmux 호환 모드 추가

**Files:**
- Create: `tools/bread/tmux_compat.h`
- Create: `tools/bread/tmux_compat.cpp`
- Create: `tests/test_tmux_compat.cpp`
- Modify: `tools/bread/arg_parser.h`
- Modify: `tools/bread/arg_parser.cpp`
- Modify: `tools/bread/main.cpp`
- Modify: `tools/bread/CMakeLists.txt`

- [ ] **Step 1: Write tmux_compat header**

```cpp
// tools/bread/tmux_compat.h
#pragma once

#include "arg_parser.h"
#include <string>
#include <vector>

namespace bread {

/// Parse tmux-style arguments and convert to BreadTerminal JSON-RPC ParsedArgs.
/// argv[0] should be the tmux subcommand (e.g., "split-window").
/// Returns ParsedArgs with method and params set for JSON-RPC dispatch.
ParsedArgs parseTmuxArgs(int argc, char* argv[]);

/// List of supported tmux commands for error messages.
std::string supportedTmuxCommands();

} // namespace bread
```

- [ ] **Step 2: Write failing tests for tmux command parsing**

```cpp
// tests/test_tmux_compat.cpp
// Test: "split-window -h" → method="pane.split", params={direction:"horizontal"}
// Test: "send-keys -t %3 hello Enter" → method="pane.sendKeys", params={pane_id:3, keys:"hello\n"}
// Test: "select-pane -t %5" → method="pane.focus", params={pane_id:5}
// Test: "list-panes" → method="pane.list", params={}
// Test: "kill-pane -t %2" → method="pane.close", params={pane_id:2}
// Test: "display-message -p '#{pane_id}'" → method="query.activePane", params={}
// Test: unsupported command → valid=false, error contains supported list
```

- [ ] **Step 3: Implement parseTmuxArgs**

`tmux_compat.cpp`: 각 tmux 서브커맨드를 switch/map으로 분기, 플래그 파싱 후 JSON-RPC method/params 설정.

핵심 매핑:
- `split-window -h` → `pane.split {direction: "horizontal"}`
- `split-window -v` → `pane.split {direction: "vertical"}`
- `send-keys -t %N "text" Enter` → `pane.sendKeys {pane_id: N, keys: "text\n"}`
  - `Enter`/`C-c`/`C-d` 등 특수 키 변환
- `select-pane -t %N` → `pane.focus {pane_id: N}`
- `list-panes` → `pane.list {}`
- `kill-pane -t %N` → `pane.close {pane_id: N}`
- `display-message -p '#{pane_id}'` → `query.activePane {}`
- 기타 → `valid=false`, error에 지원 명령 목록

- [ ] **Step 4: Add --tmux flag detection in parseArgs()**

`CommandType` enum 변경 불필요 — `parseTmuxArgs()`가 `CommandType::RemoteRPC`로 설정된 `ParsedArgs`를 반환.

`arg_parser.cpp`의 `parseArgs()` 초반에 `--tmux` 감지:
```cpp
if (argc >= 2 && std::string(argv[1]) == "--tmux") {
    return parseTmuxArgs(argc - 2, argv + 2);
}
```

`main.cpp` 변경 불필요 — tmux 모드 결과는 기존 RemoteRPC 분기로 처리됨.

- [ ] **Step 6: Update CMakeLists.txt**

```cmake
add_executable(bread
    main.cpp
    cli_client.cpp
    arg_parser.cpp
    output_formatter.cpp
    hooks_installer.cpp
    local_commands.cpp
    tmux_compat.cpp    # NEW
)
```

- [ ] **Step 7: Run tests**

Expected: 7 tmux parsing tests PASS

- [ ] **Step 8: Commit**

```bash
git add tools/bread/tmux_compat.h tools/bread/tmux_compat.cpp tools/bread/arg_parser.h tools/bread/arg_parser.cpp tools/bread/main.cpp tools/bread/CMakeLists.txt tests/test_tmux_compat.cpp
git commit -m "feat: add tmux compatibility mode to bread CLI for Agent Team support"
```

---

## Chunk 4: HookBridge + PaneEnvironment (Agent 4 - 독립)

### Task 8: HookBridge

**Files:**
- Create: `core/include/termcore/hook_bridge.h`
- Create: `core/src/hook_bridge.cpp`
- Create: `tests/test_hook_bridge.cpp`
- Modify: `tools/bread/hooks_installer.cpp` (subagent hook 스크립트 추가)
- Modify: `core/CMakeLists.txt` (hook_bridge.cpp 추가)

- [ ] **Step 1: Write HookBridge header**

```cpp
// core/include/termcore/hook_bridge.h
#pragma once

#include "termcore/agent.h"
#include "termcore/agent_tree_tracker.h"
#include "termcore/notification.h"

#include <nlohmann/json.hpp>
#include <string>

namespace termcore {

class HookBridge {
public:
    HookBridge(AgentTreeTracker& tree, AgentTracker& tracker,
               NotificationStore& notifications);

    /// Process a hook event received via Named Pipe.
    /// Unknown fields are silently ignored for forward compatibility.
    void processHookEvent(const nlohmann::json& event);

private:
    void handleSubagentStart(const nlohmann::json& event);
    void handleSubagentStop(const nlohmann::json& event);
    void handleNotification(const nlohmann::json& event);
    void handleStateChange(const nlohmann::json& event);

    AgentTreeTracker& tree_;
    AgentTracker& tracker_;
    NotificationStore& notifications_;
};

} // namespace termcore
```

- [ ] **Step 2: Write failing tests**

```cpp
// tests/test_hook_bridge.cpp
#include <gtest/gtest.h>
#include "termcore/hook_bridge.h"

class HookBridgeTest : public ::testing::Test {
protected:
    termcore::AgentTreeTracker tree;
    termcore::AgentTracker tracker;
    termcore::NotificationStore notifications;
    termcore::HookBridge bridge{tree, tracker, notifications};
};

TEST_F(HookBridgeTest, SubagentStartCreatesNode) {
    nlohmann::json event = {
        {"event", "SubagentStart"},
        {"agent_id", "a1"}, {"agent_type", "Explore"},
        {"description", "Find files"}, {"pane_id", 1}
    };
    bridge.processHookEvent(event);
    ASSERT_EQ(tree.rootAgents(1).size(), 1u);
    ASSERT_EQ(tree.rootAgents(1)[0].agent_type, "Explore");
}

TEST_F(HookBridgeTest, SubagentStopUpdatesState) {
    bridge.processHookEvent({{"event","SubagentStart"},{"agent_id","a1"},
        {"agent_type","TDD"},{"description","d"},{"pane_id",1}});
    bridge.processHookEvent({{"event","SubagentStop"},{"agent_id","a1"},
        {"pane_id",1},{"state","completed"}});
    auto* node = tree.findAgent("a1");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->state, termcore::AgentState::Exited);
}

TEST_F(HookBridgeTest, NotificationAddsEntry) {
    bridge.processHookEvent({{"event","Notification"},{"pane_id",1},
        {"title","Done"},{"body","Task completed"},{"urgency","normal"}});
    ASSERT_EQ(notifications.count(), 1u);
}

TEST_F(HookBridgeTest, UnknownEventIgnored) {
    bridge.processHookEvent({{"event","FutureEvent"},{"data","x"}});
    // no crash, no side effects
    ASSERT_EQ(tree.rootAgents(1).empty(), true);
    ASSERT_EQ(notifications.count(), 0u);
}

TEST_F(HookBridgeTest, MissingFieldsGraceful) {
    bridge.processHookEvent({{"event","SubagentStart"}});  // missing agent_id
    // should not crash, node not created
    ASSERT_TRUE(tree.rootAgents(0).empty());
}
```

- [ ] **Step 3: Implement HookBridge**

```cpp
// core/src/hook_bridge.cpp
#include "termcore/hook_bridge.h"

namespace termcore {

HookBridge::HookBridge(AgentTreeTracker& tree, AgentTracker& tracker,
                       NotificationStore& notifications)
    : tree_(tree), tracker_(tracker), notifications_(notifications) {}

void HookBridge::processHookEvent(const nlohmann::json& event) {
    auto it = event.find("event");
    if (it == event.end() || !it->is_string()) return;

    const std::string& type = it->get_ref<const std::string&>();
    if (type == "SubagentStart")       handleSubagentStart(event);
    else if (type == "SubagentStop")   handleSubagentStop(event);
    else if (type == "Notification")   handleNotification(event);
    else if (type == "StateChange")    handleStateChange(event);
    // Unknown events silently ignored for forward compatibility
}

void HookBridge::handleSubagentStart(const nlohmann::json& e) {
    auto id = e.value("agent_id", "");
    if (id.empty()) return;
    auto pane = e.value("pane_id", 0u);
    auto type = e.value("agent_type", "Unknown");
    auto desc = e.value("description", "");
    auto parent = e.value("parent_agent_id", "");
    tree_.onAgentStart(pane, id, type, desc, parent);
}

void HookBridge::handleSubagentStop(const nlohmann::json& e) {
    auto id = e.value("agent_id", "");
    if (id.empty()) return;
    auto pane = e.value("pane_id", 0u);
    tree_.onAgentStop(pane, id, AgentState::Exited);
}

void HookBridge::handleNotification(const nlohmann::json& e) {
    auto pane = e.value("pane_id", 0u);
    auto title = e.value("title", "");
    auto body = e.value("body", "");
    auto urgency_str = e.value("urgency", "normal");
    auto urgency = (urgency_str == "critical") ? NotificationUrgency::Critical
                 : (urgency_str == "low")      ? NotificationUrgency::Low
                                               : NotificationUrgency::Normal;
    notifications_.add(pane, NotificationSource::Agent, urgency, title, body);
}

void HookBridge::handleStateChange(const nlohmann::json& e) {
    auto id = e.value("agent_id", "");
    if (id.empty()) return;
    auto pane = e.value("pane_id", 0u);
    auto state_str = e.value("state", "");
    auto state = AgentTracker::stringToState(state_str);
    tree_.onAgentStateChange(pane, id, state);
}

} // namespace termcore
```

- [ ] **Step 4: Add hook_bridge.cpp to core/CMakeLists.txt**

- [ ] **Step 5: Extend hooks_installer.cpp for subagent events**

기존 `installHooks()` 확장: `~/.claude/hooks/` 디렉토리에 추가 스크립트:
- `subagent-stop.sh`: `bread hook-event --json '{"event":"SubagentStop","agent_id":"$CLAUDE_AGENT_ID","agent_type":"$CLAUDE_AGENT_TYPE","pane_id":"$BREADTERMINAL_PANE_ID"}'`
- `subagent-start.sh`: `bread hook-event --json '{"event":"SubagentStart","agent_id":"$CLAUDE_AGENT_ID","agent_type":"$CLAUDE_AGENT_TYPE","description":"$CLAUDE_AGENT_DESCRIPTION","pane_id":"$BREADTERMINAL_PANE_ID"}'`

`~/.claude/settings.json`의 hooks에 SubagentStop, SubagentStart 이벤트 등록.

- [ ] **Step 6: Register hook.event handler in CommandDispatcher**

`CommandDispatcher::dispatch()`에 `"hook.event"` 메서드 추가 → `HookBridge::processHookEvent()` 호출.

- [ ] **Step 7: Run tests**

Run: `cmake --build build --target tests && ctest -R HookBridge -V`
Expected: 5 tests PASS

- [ ] **Step 8: Commit**

```bash
git add core/include/termcore/hook_bridge.h core/src/hook_bridge.cpp tests/test_hook_bridge.cpp tools/bread/hooks_installer.cpp core/CMakeLists.txt
git commit -m "feat: add HookBridge for Claude Code hook event processing"
```

### Task 9: PaneEnvironment TMUX 환경변수

**Files:**
- Modify: `core/include/termcore/pane_environment.h`
- Modify: `core/include/termcore/config.h`
- Modify: `core/defaults/config.lua`

- [ ] **Step 1: Add tmux_compat_enabled to Config**

```cpp
// config.h — Config struct에 추가 (sidebar_visible 근처)
bool tmux_compat_enabled = true;
```

```lua
-- config.lua에 추가 (sidebar_visible 근처)
tmux_compat_enabled = true,
```

참고: `sidebar_visible`, `sidebar_width`는 이미 Config에 존재함 (config.h:86-88). 새로 추가하지 말 것.

- [ ] **Step 2: Add TMUX environment variables to PaneEnvironment**

```cpp
// pane_environment.h — 필드 추가 + toEnvVars() 수정
struct PaneEnvironment {
    std::string socket_path;
    WorkspaceId workspace_id = kInvalidWorkspace;
    TabId tab_id = kInvalidTab;
    PaneId pane_id = kInvalidPane;
    bool tmux_compat_enabled = true;   // NEW
    std::string bread_cli_path;        // NEW: bread 실행파일 경로

    std::vector<std::pair<std::string, std::string>> toEnvVars() const {
        std::vector<std::pair<std::string, std::string>> vars;
        vars.reserve(8);

        vars.emplace_back("BREADTERMINAL", "1");
        vars.emplace_back("TERM_PROGRAM", "BreadTerminal");

        if (!socket_path.empty())
            vars.emplace_back("BREADTERMINAL_SOCKET", socket_path);
        if (workspace_id != kInvalidWorkspace)
            vars.emplace_back("BREADTERMINAL_WORKSPACE_ID", std::to_string(workspace_id));
        if (tab_id != kInvalidTab)
            vars.emplace_back("BREADTERMINAL_TAB_ID", std::to_string(tab_id));
        if (pane_id != kInvalidPane)
            vars.emplace_back("BREADTERMINAL_PANE_ID", std::to_string(pane_id));

        // TMUX compatibility for Claude Code Agent Team
        if (tmux_compat_enabled && !bread_cli_path.empty()) {
            vars.emplace_back("TMUX", "bread//" + std::to_string(pane_id));
            vars.emplace_back("TMUX_PROGRAM", bread_cli_path + " --tmux");
        }

        return vars;
    }
};
```

- [ ] **Step 3: Populate bread_cli_path at pane creation**

`bread_cli_path`는 pane 생성 시 설정해야 함. `TabController` 또는 `TerminalWindowState`에서 PaneEnvironment를 구성할 때:

```cpp
// bread 실행파일 경로 결정 (실행 중인 BreadTerminal 경로 기준)
// Windows: GetModuleFileName() → 같은 디렉토리의 bread.exe
// Unix: /proc/self/exe → 같은 디렉토리의 bread
std::string resolveBreadCliPath(); // platform-specific helper

// PaneEnvironment 구성 시:
env.bread_cli_path = resolveBreadCliPath();
env.tmux_compat_enabled = config.tmux_compat_enabled;
```

이 helper 함수는 `platform/windows/src/TerminalWindowState.cpp` (또는 해당 플랫폼 파일)에서 구현.

- [ ] **Step 4: Commit**

```bash
git add core/include/termcore/pane_environment.h core/include/termcore/config.h core/defaults/config.lua
git commit -m "feat: add TMUX compat env vars to PaneEnvironment for Agent Team support"
```

---

## Chunk 5: 사이드바 렌더링 (Agent 5 - Agent 1 완료 후)

### Task 10: SidebarRenderInfo 구조체

**Files:**
- Modify: `platform/windows/include/D3DTextRenderer.h`

- [ ] **Step 1: Add SidebarRenderInfo to D3DTextRenderer.h**

```cpp
struct SidebarSubagentEntry {
    std::string name;
    std::string status;     // "[Running]", "[Done]" etc.
    int state = 0;          // AgentState as int
    int indent_level = 0;
};

struct SidebarRenderEntry {
    uint32_t pane_id = 0;
    std::string title;
    std::string subtitle;        // "main • PR #42"
    std::string status_text;     // "Thinking... (8s)"
    int agent_state = 0;
    float progress_value = -1.0f;
    std::string progress_label;
    bool has_unread = false;
    bool active = false;
    float attention_intensity = 0.0f;
    std::vector<SidebarSubagentEntry> subagents;
    bool subagents_expanded = true;
};

struct SidebarRenderInfo {
    bool visible = false;
    int width = 220;
    std::vector<SidebarRenderEntry> entries;
    int hovered_entry = -1;
    int hovered_subagent = -1;
    int scroll_offset = 0;
    uint32_t bg_color = 0x1a1a1a;
    uint32_t fg_color = 0xcccccc;
    uint32_t accent_color = 0x007acc;
    uint32_t separator_color = 0x333333;
};
```

- [ ] **Step 2: Add setSidebar() method and member to D3DTextRenderer**

```cpp
void setSidebar(const SidebarRenderInfo& info);
```

- [ ] **Step 3: Commit**

```bash
git add platform/windows/include/D3DTextRenderer.h
git commit -m "feat: add SidebarRenderInfo structs to D3DTextRenderer"
```

### Task 11: 사이드바 렌더링 구현

**Files:**
- Create: `platform/windows/src/D3DCellBuilderSidebar.cpp`
- Modify: `platform/windows/src/D3DTextRenderer.cpp` (Pass 10 호출)
- Modify: `platform/windows/src/TerminalWindowState.cpp` (updateSidebar, 뷰포트)

- [ ] **Step 1: Create D3DCellBuilderSidebar.cpp**

Pass 10에서 사이드바 렌더링:
- 사이드바 배경 (전체 높이, sidebar_width)
- 각 SidebarRenderEntry:
  - 타이틀 + 상태 색상 점
  - 서브타이틀 (git branch 등)
  - Progress 바 (있으면)
  - Status pills
  - Subagent 트리 (expanded일 때)
- 구분선 (entries 사이)
- 스크롤 위치 반영

- [ ] **Step 2: Add updateSidebar() to TerminalWindowState**

`SidebarModel::entries()` → `SidebarRenderInfo` 매핑:
```cpp
void TerminalWindowState::updateSidebar() {
    SidebarRenderInfo info;
    info.visible = config.sidebar_visible;
    info.width = config.sidebar_width;
    // sidebar_model_.entries() → SidebarRenderEntry 변환
    for (auto& entry : sidebar_model_.entries()) {
        SidebarRenderEntry re;
        re.pane_id = entry.pane_id;
        re.title = entry.title;
        re.agent_state = static_cast<int>(entry.agent_state);
        // ... 나머지 필드 매핑
        info.entries.push_back(std::move(re));
    }
    renderer->setSidebar(info);
}
```

- [ ] **Step 3: Adjust viewport offset for sidebar**

`TerminalWindowState`에서 사이드바 visible일 때:
```cpp
float gridOffsetX = sidebar_visible ? sidebar_width : 0;
```
기존 모든 패스의 X 좌표에 `gridOffsetX` 반영. PTY에 resize 전달 (열 수 감소).

- [ ] **Step 4: Add sidebar toggle keybinding**

`Ctrl+Shift+B` → `config.sidebar_visible` 토글 → 리사이즈 트리거.

- [ ] **Step 5: Add mouse interaction**

- 사이드바 엔트리 클릭 → 해당 pane 포커스
- ▶/▼ 클릭 → subagent 트리 토글
- 사이드바 우측 가장자리 드래그 → 너비 조절
- 마우스 휠 → 스크롤

- [ ] **Step 6: Add keyboard navigation**

- 사이드바 포커스 시: ↑/↓로 엔트리 이동, Enter로 pane 포커스, Space로 접기/펼치기

- [ ] **Step 7: Add D3DCellBuilderSidebar.cpp to platform/windows/CMakeLists.txt**

- [ ] **Step 8: Build and visually verify**

Ctrl+Shift+B로 사이드바 토글, 에이전트 상태 표시, subagent 트리 확인.

- [ ] **Step 9: Commit**

```bash
git add platform/windows/src/D3DCellBuilderSidebar.cpp platform/windows/src/D3DTextRenderer.cpp platform/windows/src/TerminalWindowState.cpp platform/windows/include/TerminalWindowState.h platform/windows/CMakeLists.txt
git commit -m "feat: implement sidebar rendering with agent status and subagent tree"
```

---

## Dependency Graph

```
Task 1 (AgentTreeTracker) ──────┐
Task 2 (SidebarModel) ──────────┤──→ Task 10, 11 (사이드바 렌더링)
Task 3-5 (탭 바 + Notif Ring)   │    (Agent 5)
Task 6-7 (CLI 확장)             │
Task 8-9 (HookBridge + Env)     │
                                │
독립: Tasks 1-9 (Agents 1-4)    │
의존: Tasks 10-11 ← Task 1,2 ──┘
```

## 에이전트 할당

| 에이전트 | 태스크 | worktree 격리 | Chunk |
|---------|--------|---------------|-------|
| Agent 1 | Task 1 (AgentTreeTracker) + Task 2 (SidebarModel) | Yes | Chunk 1 |
| Agent 2 | Task 3-4 (탭 바 확장) + Task 5 (Notification Ring) | Yes | Chunk 2 |
| Agent 3 | Task 6 (hook-event) + Task 7 (tmux compat) | Yes | Chunk 3 |
| Agent 4 | Task 8 (HookBridge) + Task 9 (PaneEnvironment) | Yes | Chunk 4-5 |
| Agent 5 | Task 10 (SidebarRenderInfo) + Task 11 (사이드바 렌더링) | Yes, after Agent 1 | Chunk 6 |
