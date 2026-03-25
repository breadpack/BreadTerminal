# Completion System & SGR Dim Support Design

## Overview

두 가지 기능을 구현한다:
1. **SGR 2 (dim/faint) 속성 지원** — 쉘의 예측 텍스트가 흐리게 렌더링되도록
2. **터미널 레벨 자동완성 시스템** — fish-style ghost text + Lua provider 확장 아키텍처

## Part 1: SGR 2 (Dim/Faint) 지원

### 문제

- `CellAttribute` enum에 `AttrDim` 플래그가 없음
- `Screen::handleSGR()`에서 `p == 2` 케이스 누락 (bold=1 → italic=3으로 건너뜀)
- SGR 22 (bold off)가 dim도 함께 해제해야 하지만 bold만 해제
- 렌더러에서 dim 셀의 밝기를 줄이는 로직 없음
- 결과: PSReadLine 등이 SGR 2로 보내는 예측 텍스트가 일반 텍스트와 동일하게 보임

### 변경사항

#### term_cell.h
```cpp
enum CellAttribute : uint16_t {
    AttrBold          = 1,
    AttrItalic        = 2,
    AttrUnderline     = 4,
    AttrBlink         = 8,
    AttrInverse       = 16,
    AttrHidden        = 32,
    AttrStrikethrough = 64,
    AttrDim           = 128,   // 새로 추가
};
```

#### screen_csi.cpp — handleSGR()
- `p == 2`: `pen_.attributes |= AttrDim;` 추가
- `p == 22`: `pen_.attributes &= ~(AttrBold | AttrDim);` (SGR 22는 bold와 dim 모두 해제)

#### Bold + Dim 동시 사용
ECMA-48 표준에 따라 AttrBold와 AttrDim은 독립적으로 설정 가능하다. 둘 다 설정된 경우 dim이 적용되어 밝기가 줄어든 bold 텍스트가 된다.

#### SGR 0 (reset)
기존 `pen_ = Pen{}` 코드가 모든 attributes를 0으로 초기화하므로 AttrDim도 자동으로 해제된다. 추가 작업 불필요.

#### 모든 플랫폼 렌더러에 적용
dim 렌더링은 3개 플랫폼 모두에 적용한다:
- `platform/windows/src/D3DCellBuilder.cpp` (D3D11)
- `platform/linux/src/GLCellBuilder.cpp` (OpenGL)
- `platform/macos/src/MetalCellBuilder.mm` (Metal)

Pass 2 (glyph 렌더링)에서 fg_color 적용 후, AttrDim 체크:
```cpp
// fg_color 설정 이후, inverse/selection 처리 이후에 적용
if (cell.attributes & AttrDim) {
    inst.fg_color[0] *= 0.5f;
    inst.fg_color[1] *= 0.5f;
    inst.fg_color[2] *= 0.5f;
}
```
dim은 foreground color에만 영향을 미친다. background는 변경하지 않는다.

## Part 2: 자동완성 시스템

### 아키텍처

```
┌──────────────────────────────────────────┐
│           CompletionManager              │
│  - 현재 입력 텍스트 추적 (prompt 상태)     │
│  - Provider 목록 관리 (우선순위 정렬)      │
│  - ghost text 결정                       │
├──────────────────────────────────────────┤
│           Provider Interface             │
│  getSuggestion(input, cwd) -> string     │
├───────────────┬──────────────────────────┤
│   Built-in    │     Lua Providers        │
│   History     │  register_provider()     │
│   Provider    │  (동기/비동기)            │
│               │  향후 LLM 플러그인 등     │
└───────────────┴──────────────────────────┘
```

### 소유권 및 생명주기

- **CompletionManager**는 `TerminalController`가 소유 (멤버 변수)
- **HistoryProvider**는 CompletionManager가 소유
- 히스토리는 **탭 간 공유** (TerminalController가 단일 인스턴스 보유)
- CompletionManager는 TerminalController 생성 시 초기화, 소멸 시 함께 해제

### 스레딩 모델

모든 CompletionManager 호출은 **메인 스레드에서만** 발생한다:
- 입력 변경 → Screen::onPrint() → 메인 스레드
- 키 이벤트 → InputHandler → 메인 스레드
- Lua 콜백 → LuaEngine → 메인 스레드
- 비동기 provider의 결과도 `postToMainThread()`를 통해 메인 스레드에서 setSuggestion() 호출

별도의 mutex는 불필요하다.

### 핵심 구성요소

#### 1. CompletionManager (core/include/termcore/completion_manager.h)

```cpp
class CompletionManager {
public:
    struct Provider {
        std::string name;
        int priority;  // 높을수록 우선. 동일 우선순위 시 먼저 등록된 것 우선.
        std::function<std::string(const std::string& input,
                                   const std::string& cwd)> getSuggestion;
    };

    void registerProvider(Provider provider);
    void removeProvider(const std::string& name);

    // 입력 변경 시 호출 — 최적의 suggestion을 결정
    void onInputChanged(const std::string& currentInput,
                        const std::string& cwd);

    // 비동기 provider가 결과를 보고 (메인 스레드에서 호출)
    void setSuggestion(const std::string& providerName,
                       const std::string& suggestion);

    // 현재 ghost text 반환 (렌더러가 사용)
    const std::string& ghostText() const;

    // ghost text가 있는지
    bool hasGhostText() const;

    // ghost text 수락
    std::string acceptFull();       // 전체 수락 (→)
    std::string acceptWord();       // 단어 수락 (Ctrl+→)

    void clear();

    // 활성화 상태
    void setEnabled(bool enabled);
    bool isEnabled() const;

private:
    std::vector<Provider> providers_;  // priority 내림차순 정렬
    std::string currentInput_;
    std::string ghostText_;           // 현재 표시할 ghost text (input 이후 부분만)
    std::string ghostProviderName_;
    bool enabled_ = true;
};
```

#### 2. HistoryProvider (core/include/termcore/history_provider.h)

```cpp
class HistoryProvider {
public:
    void addEntry(const std::string& command);
    std::string suggest(const std::string& prefix) const;

private:
    // 최신 순 저장 (index 0 = 가장 최근)
    // 중복 시 기존 항목 제거 후 앞에 추가
    std::vector<std::string> history_;
    static constexpr size_t kMaxHistory = 1000;
};
```

- prefix 매칭 시 **index 0부터 순회** (최신 우선), 첫 매칭 반환
- `addEntry()`: 동일 명령어가 이미 있으면 제거 후 index 0에 삽입
- 빈 문자열, 공백만 있는 명령어는 추가하지 않음

#### 3. Ghost Text 렌더링

**데이터 전달 경로:**
```
CompletionManager (core)
    ↓ ghostText(), ghostRow(), ghostCol()
TerminalController (core)
    ↓ activeScreen() + completion 정보 노출
TerminalWindowState (platform) — renderFrame() 시점
    ↓ renderer에 ghost text 정보 설정
D3DTextRenderer::Impl (platform)
    ↓ buildCellBuffer() 내에서 ghost text 렌더링
```

플랫폼 레이어가 렌더링 전에 CompletionManager에서 ghost text를 읽어 renderer에 전달한다. 기존 패턴(selection, searchHighlights, urlHighlights 등)과 동일한 방식.

**Renderer Impl에 추가할 상태:**
```cpp
// D3DTextRendererImpl.h
struct GhostText {
    std::string text;
    int row = -1;       // ghost text가 표시될 행
    int col = -1;       // ghost text 시작 열 (커서 위치)
};
GhostText ghostText;
```

**렌더링 방식:**
- Pass 2 (glyph) 이후에 Ghost Text Pass 추가
- 커서 위치(row, col)부터 ghost text의 각 문자를 개별 glyph로 렌더링
- fg_color: 기본 foreground의 35% 밝기 (dim보다 더 흐리게)
- bg_color: 투명 (배경 quad 없음, glyph만)
- 커서 행만 대상 (멀티라인 미지원)
- 해당 위치에 이미 실제 셀 내용이 있으면 ghost text를 표시하지 않음

#### 4. 입력 추적

**프롬프트 입력 영역 감지:**

OSC 133;B 수신 시 `prompt_state_`가 `PromptState::Input`이 되고, 이 시점의 커서 위치(row, col)를 `input_start_row_`, `input_start_col_`로 저장한다.

```cpp
// screen.h에 추가
int input_start_row_ = -1;  // OSC 133;B 시점의 커서 행
int input_start_col_ = -1;  // OSC 133;B 시점의 커서 열
```

**현재 입력 텍스트 추출:**
`Screen`에 `currentInputText()` 메서드를 추가:
```cpp
std::string Screen::currentInputText() const {
    if (prompt_state_ != PromptState::Input) return "";
    if (input_start_row_ < 0) return "";
    // input_start_row_, input_start_col_ 부터 cursor_.row, cursor_.col 까지의 텍스트를 읽어 반환
    // 멀티행 입력(행 연속)도 처리
}
```

**입력 변경 감지:**
`Screen::onPrint()` 에서 `prompt_state_ == PromptState::Input`일 때 dirty flag를 설정하고, TerminalController의 렌더 사이클에서 CompletionManager에 입력 변경을 알린다.

```cpp
// TerminalController render cycle에서:
if (screen->promptState() == PromptState::Input) {
    std::string input = screen->currentInputText();
    completionManager_.onInputChanged(input, screen->workingDirectory());
}
```

Shell integration이 비활성인 경우 (`prompt_state_`가 항상 `None`) 자동완성은 자동으로 비활성화된다.

#### 5. 키 바인딩 & 입력 가로채기

**InputHandler에 CompletionManager 의존성 추가:**
```cpp
// InputHandler::Deps에 추가
std::function<CompletionManager*()> getCompletionManager;
```

**가로채기 위치:**
`InputHandler::onKeyEvent()` 내에서 keybinding 조회 **이전**에 ghost text 관련 키를 처리한다.

**조건 및 동작:**

| 키 | 조건 | 동작 |
|---|------|------|
| `→` (Right Arrow) | ghost text 있음 AND 커서가 입력 끝에 위치 | ghost text 전체를 PTY로 전송, CompletionManager clear |
| `Ctrl+→` | ghost text 있음 AND 커서가 입력 끝에 위치 | ghost text에서 다음 단어까지를 PTY로 전송 |
| `Escape` | ghost text 있음 AND 검색/복사 모드가 아닐 때 | CompletionManager clear만 수행 (PTY 전송 없음) |
| 기타 | - | 정상 처리, ghost text는 다음 렌더 사이클에서 자동 갱신 |

**"커서가 입력 끝에 위치" 판단:**
`screen->cursorCol()` 이후에 공백이 아닌 문자가 없는 경우. 또는 `screen->cursorCol()` == `currentInputText().length() + input_start_col_` 으로 판단.

커서가 입력 중간에 있으면 → 키는 일반적인 커서 이동으로 동작한다.

**acceptWord() 단어 경계 정의:**
fish shell과 동일하게 `/`, `.`, `-`, `_`, 공백을 단어 경계로 사용한다. 현재 위치부터 다음 경계까지의 텍스트를 수락한다.

#### 6. 히스토리 수집

**명령어 캡처 시점:** OSC 133;C (명령 시작 = 사용자가 Enter를 누름)

**캡처 방법:**
OSC 133;C 수신 시 `input_start_row_`, `input_start_col_`부터 현재 커서 직전 행까지의 텍스트를 `currentInputText()`로 읽어 HistoryProvider에 추가한다.

```cpp
// Screen::handleOscShellIntegration() 내 OSC 133;C 처리 시:
if (prompt_state_ == PromptState::Input) {
    std::string cmd = currentInputText();
    // TerminalController로 콜백하여 historyProvider에 추가
}
```

**멀티행 명령어:** `input_start_row_`부터 현재 커서 행까지 순회하며 텍스트를 연결한다. 각 행 사이에 줄바꿈 문자를 삽입한다.

### Lua Plugin API

#### terminal.completion 모듈 (LuaCompletionModule)

ILuaModule 인터페이스를 구현하며, `terminal.completion` 서브테이블에 바인딩한다.

```lua
-- Provider 등록
terminal.completion.register_provider(name, options)
-- options:
--   priority: number (기본 50, 동일 우선순위 시 먼저 등록된 것 우선)
--   on_input: function(context) -> string or nil
--     context.text: 현재 입력 텍스트
--     context.cwd: 현재 작업 디렉토리
--   async: boolean (기본 false)
--     true인 경우 on_input의 반환값은 무시되고,
--     set_suggestion()으로 결과를 비동기 보고해야 함

-- 비동기 결과 설정 (메인 스레드에서 호출)
terminal.completion.set_suggestion(provider_name, text)

-- Provider 제거
terminal.completion.remove_provider(name)

-- 자동완성 활성화/비활성화
terminal.completion.set_enabled(bool)
```

#### 사용 예시: LLM 플러그인 (향후, 비동기 HTTP 모듈 필요)

```lua
-- 주의: 이 예시는 향후 HTTP 모듈이 구현된 후에 동작한다.
-- 현재는 동기 provider만 실용적으로 사용 가능.
terminal.completion.register_provider("llm", {
    priority = 10,
    async = true,
    on_input = function(context)
        if #context.text < 3 then return nil end
        -- 비동기 HTTP 호출 (향후 구현될 API)
        http.post("https://api.example.com/suggest", {
            input = context.text,
            cwd = context.cwd
        }, function(response)
            terminal.completion.set_suggestion("llm", response.suggestion)
        end)
    end
})
```

### 우선순위 체계

- Built-in HistoryProvider: priority = 100 (가장 높음, 동기)
- Lua provider 기본: priority = 50, 사용자 설정 가능
- 동일 우선순위: 먼저 등록된 provider 우선
- 높은 우선순위 provider가 결과를 반환하면 낮은 provider는 호출하지 않음
- 비동기 provider의 경우: 동기 provider 결과가 이미 있으면 비동기 결과가 와도 무시

### 파일 구조

```
core/
  include/termcore/
    completion_manager.h    — CompletionManager 클래스
    history_provider.h      — HistoryProvider 클래스
  src/
    completion_manager.cpp  — CompletionManager 구현
    history_provider.cpp    — HistoryProvider 구현
    lua_bindings/
      lua_completion_module.h   — Lua 바인딩 모듈 헤더
      lua_completion_module.cpp — terminal.completion.* API
```

### 구현 범위 (이번 작업)

1. SGR 2 (dim/faint) 지원 — 3개 플랫폼 모두
2. CompletionManager + HistoryProvider (C++ core)
3. Screen에 input_start_row_/col_ 추가 및 currentInputText() 구현
4. Ghost text 렌더링 (3개 플랫폼 렌더러 확장)
5. 키 바인딩 (→, Ctrl+→ 수락, Escape 해제)
6. InputHandler에 CompletionManager 의존성 추가
7. Lua completion API (register_provider, set_suggestion, remove_provider, set_enabled)
8. OSC 133;C 연동 히스토리 수집

### 구현 범위 제외

- 세션 간 히스토리 영속성 (파일 저장/로드)
- 멀티라인 ghost text
- 비동기 HTTP 모듈 (Lua 플러그인이 LLM을 호출하려면 별도 구현 필요)
- Tab 키 연동 (기존 쉘 Tab completion과 충돌 방지)
