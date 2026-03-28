# Threading Design & Synchronization

Reference for BreadTerminal's multi-threaded architecture, lock protocols, and invariants.

## 1. Architecture Overview

```
+------------------+       +------------------+       +------------------+
|   Main Thread    |       |  Render Thread   |       | RasterQueue      |
|                  |       |                  |       | Worker           |
| - Win32 message  |       | - WaitForSingle  |       |                  |
|   loop (WndProc) |       |   Object loop    |       | - Glyph          |
| - PTY polling    |       | - Snapshot capture|       |   rasterization  |
| - Input drain    |       | - HarfBuzz shape |       |   (DirectWrite)  |
| - Controller     |       | - D3D submit     |       |                  |
|   mutations      |       | - SwapChain      |       |                  |
|                  |       |   Present        |       |                  |
+--------+---------+       +--------+---------+       +------------------+
         |                          |
         |   SRWLOCK renderLock_    |
         +--------------------------+
         |
+--------+---------+       +------------------+       +------------------+
| SocketServer     |       | SessionAutoSave  |       | PTY I/O          |
|                  |       |                  |       | (OS pipe,        |
| - Accept thread  |       | - Timer thread   |       |  read by main    |
| - Per-client     |       | - Periodic save  |       |  thread via      |
|   detached       |       |   with mutex +   |       |  pollPty)        |
|   threads        |       |   condvar wake   |       |                  |
+------------------+       +------------------+       +------------------+
```

### Thread ownership

| Thread | Owns / Mutates |
|--------|---------------|
| Main thread | `TerminalController`, `Screen`, `Selection`, input queue consumer side, PTY read |
| Render thread | `D3DTextRenderer`, `ScreenSnapshot` (local copy), D3D device context, `SwapChain::Present` |
| RasterQueue worker | `IFontRasterizer` calls (thread-safe by contract), result queue producer side |
| SocketServer accept | Listening socket; spawns per-client threads |
| SocketServer client | Per-connection read/write; dispatches through `dispatch_mutex_` |
| SessionAutoSave | Timer loop; calls `StateProvider` callback under its own mutex |

## 2. SRWLOCK Protocol

**Lock:** `TerminalWindowState::renderLock_` (a Windows Slim Reader/Writer Lock).

### What it protects

All shared state between the main thread and render thread:
- `TerminalController` and its owned `Screen` (cell data, cursor, colors)
- `FontCollection`, `GlyphCache`, `GlyphAtlas` (font resources)
- `D3DTextRenderer` state setters (`setTabBar`, `setSelection`, etc.)
- UI flags: `needsRender`, `inLiveResize`, `cursorBlinkOn`, `imeCompositionText`, `showResizeOverlay`

### Shared (read) lock holders

**Render thread** acquires shared lock in `renderThreadFunc()`:
1. `captureRenderSnapshot()` -- reads controller/screen state
2. `pushRendererState()` -- pushes controller state to renderer setters
3. `screenCopy.captureFrom(*screen)` -- deep-copies cell data
4. `screen->clearDirty()` -- resets dirty flags

The lock is released **before** HarfBuzz shaping (`prepareFrame`) and GPU submission (`submitFrame`).

**WM_IME_STARTCOMPOSITION** briefly acquires shared lock to read cursor position.

### Exclusive (write) lock holders

**Main thread** acquires exclusive lock via `withWriteLock()` for:
- `pollPty()` + `drainInputQueue()` -- the main poll cycle in the message loop
- `WM_SETFOCUS` / `WM_KILLFOCUS` -- focus state changes
- `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE` -- resize state
- `WM_MOUSEWHEEL` -- scroll events
- `WM_PAINT` -- marks content dirty
- `WM_IME_COMPOSITION` / `WM_IME_ENDCOMPOSITION` -- IME input
- `WM_COMMAND` (search edit) -- search query changes
- `WM_TIMER` (resize overlay hide)

**`onFontChanged()`** manually acquires exclusive lock (not via `withWriteLock`) to replace font resources:
```
AcquireSRWLockExclusive(&renderLock_);
// detach renderer from old font resources
// clear cache, recreate atlas
// re-attach renderer
ReleaseSRWLockExclusive(&renderLock_);
signalInvalidate();
```

### Lock ordering rules

1. **No nested locks.** SRWLOCK is not recursive. A thread holding `renderLock_` must never call any path that re-acquires it.
2. **No shared-to-exclusive upgrade.** SRWLOCK does not support this; attempting it deadlocks.
3. **`renderLock_` is the only cross-thread lock** between main and render. Other mutexes (`RasterQueue::request_mutex_`, `SocketServer::dispatch_mutex_`, `SessionAutoSave::mutex_`) are independent and do not interact with `renderLock_`.

### Why SRWLOCK (not recursive mutex)

- SRWLOCK supports reader/writer semantics: the render thread holds shared lock during snapshot capture, while the main thread only needs exclusive lock for mutations. This allows concurrent reads when the main thread is idle.
- Non-recursive by design -- prevents accidental re-entrant locking which would mask ownership bugs.
- Zero-overhead initialization (`SRWLOCK_INIT`), no kernel object allocation.

## 3. Lock-Free Input Queue

**Type:** `InputRingBuffer` -- a fixed-capacity SPSC (Single Producer, Single Consumer) ring buffer.

### SPSC contract

| Role | Thread | Operation |
|------|--------|-----------|
| Producer | WndProc (main message dispatch) | `enqueueKeyDown()`, `enqueueChar()` -- calls `push()` |
| Consumer | Main thread (inside `withWriteLock`) | `drainInputQueue()` -- calls `pop()` in loop |

Both producer and consumer run on the main thread but at different phases of the message loop: WndProc runs during `DispatchMessageW`, while drain runs after all messages are processed.

### Memory ordering

| Operation | Ordering | Rationale |
|-----------|----------|-----------|
| `write_` load in `push()` | `relaxed` | Only the producer reads/writes `write_` |
| `read_` load in `push()` (full check) | `acquire` | Synchronizes with consumer's release store |
| `write_` store in `push()` | `release` | Makes written `buf_[w]` visible to consumer |
| `read_` load in `pop()` | `relaxed` | Only the consumer reads/writes `read_` |
| `write_` load in `pop()` (empty check) | `acquire` | Synchronizes with producer's release store |
| `read_` store in `pop()` | `release` | Makes slot available for reuse |

### Overflow behavior

When the ring buffer is full (256 entries), `push()` returns `false` and the event is **silently dropped**. The return value is currently ignored by `enqueueKeyDown()` and `enqueueChar()`.

> **Recommendation:** Log dropped events in debug builds to detect capacity issues. 256 slots is generous for human typing but could overflow under programmatic input injection.

### Fast path for character input

`enqueueChar()` has a fast path: when no modal UI (command palette, copy mode) is active, it bypasses the ring buffer entirely and writes directly to the PTY pipe via `controller->sendPtyData()`. This is safe because `sendPtyData` only writes to the OS pipe and does not mutate `Screen` state. The queue is only used for key-down events and characters that need locked processing.

## 4. Render Thread Lifecycle

### Init

```cpp
void initRenderThread() {
    invalidateEvent_ = CreateEventW(..., FALSE, ...);  // auto-reset
    renderPausedEvent_ = CreateEventW(..., TRUE, ...);  // manual-reset
    renderRunning_ = true;
    renderThread_ = std::thread([this]() { renderThreadFunc(); });
}
```

### Run loop

```
while (renderRunning_) {
    WaitForSingleObject(invalidateEvent_, 500ms);  // wake on signal or timeout

    AcquireSRWLockShared(renderLock_);
        captureRenderSnapshot()      // Phase 1: read controller state
        pushRendererState()          // push to renderer setters
        screenCopy.captureFrom()     // deep-copy cells (dirty rows only)
        screen->clearDirty()
    ReleaseSRWLockShared(renderLock_)

    renderer->prepareFrame(screenCopy)  // Phase 2: HarfBuzz shaping (NO LOCK)
    renderer->submitFrame()             // Phase 3: GPU draw calls (NO LOCK)
    swapChain->Present()
}
```

- **Signal wake (WAIT_OBJECT_0):** content changed, calls `markContentDirty()`
- **Timeout wake (500ms):** cursor blink toggle only

### Resize handling

On `WM_SIZE`, the render thread is **stopped and restarted** rather than paused:
1. `stopRenderThread()` -- sets `renderRunning_ = false`, signals event, joins thread
2. `resizeSwapChain()` -- resizes D3D resources (no contention)
3. `initRenderThread()` + `signalInvalidate()` -- restarts

### Stop

```cpp
void stopRenderThread() {
    renderRunning_ = false;           // atomic store
    SetEvent(invalidateEvent_);       // wake the thread
    renderThread_.join();             // wait for exit
    CloseHandle(invalidateEvent_);
    CloseHandle(renderPausedEvent_);
}
```

## 5. ScreenSnapshot Design

**File:** `platform/windows/include/ScreenSnapshot.h`

### Two-phase rendering

| Phase | Lock held | Work |
|-------|-----------|------|
| Phase 1: Capture | Shared | Copy cell data from `Screen` into `ScreenSnapshot` |
| Phase 2: Shape | None | HarfBuzz text shaping on the copied data |
| Phase 3: Submit | None | D3D buffer upload and draw calls |

### Dirty row optimization

`ScreenSnapshot::captureFrom()` performs **incremental copies**:
- If grid dimensions changed or screen is not dirty: full copy of all cells
- Otherwise: only copies rows where `screen.isRowDirty(row)` is true

On a 250x80 terminal (20,000 cells), typical PTY output changes 1-2 rows per frame, reducing the lock-held copy from ~20K cells to ~500 cells.

### Why this improves latency

Before this design, the main thread could not acquire the exclusive lock until the render thread finished HarfBuzz shaping (10-50ms for complex scripts). Now the shared lock is held only during the fast cell copy (~0.1ms), so the main thread's input processing is rarely blocked.

## 6. Known Risks & Invariants

### Font change requires exclusive lock

`onFontChanged()` manually acquires the exclusive `renderLock_` to safely replace font resources (`GlyphCache`, `GlyphAtlas`). The renderer is first detached from old resources, then re-attached to new ones. This prevents the render thread from referencing freed font data.

### Mouse events run without lock

`WM_LBUTTONDOWN`, `WM_MOUSEMOVE`, `WM_LBUTTONUP`, and `WM_LBUTTONDBLCLK` call mouse handlers **without acquiring `renderLock_`**. The comment in `WM_MOUSEMOVE` notes:

> "Selection/hover state is simple coordinate data -- a one-frame stale read by the render thread is visually imperceptible."

**Risk:** If selection state involves non-atomic multi-field updates (start row/col + end row/col), the render thread could read a partially updated selection. This is considered acceptable for visual fidelity but is technically a data race.

### No recursive SRWLOCK

SRWLOCK does not support recursive acquisition. Any callback invoked while the lock is held must not attempt to re-acquire it. This applies to:
- `IPlatformHost` callbacks called from `TerminalController` during `pollPty()` (already under exclusive lock)
- `invalidate()` only calls `signalInvalidate()`, which sets an event handle (safe)
- `onFontChanged()` acquires the lock itself, so it must **not** be called from a path already holding the lock

### InputRingBuffer overflow silently drops events

As noted in section 3, `push()` returns false on overflow but callers ignore it. Under extreme input rates (e.g., paste flood), keystrokes could be lost without any diagnostic output.

### WM_SIZE stops the render thread

The current resize strategy stops and restarts the render thread on every `WM_SIZE`. During a drag-resize, this means rapid thread create/join cycles. This works but is heavier than a pause/resume pattern.

## 7. Background Workers

### RasterQueue

**File:** `core/include/termcore/font/raster_queue.h`

- **Pattern:** Producer-consumer with mutex + condition variable
- **Producer:** Render thread enqueues `RasterRequest` on glyph cache miss
- **Consumer:** Single worker thread calls `IFontRasterizer` to rasterize glyphs
- **Synchronization:**
  - `request_mutex_` + `request_cv_`: protects request queue, wakes worker
  - `result_mutex_`: protects result vector
  - `pending_keys_` (unordered_set): deduplicates in-flight requests
- **Lifecycle:** `start(fn)` spawns worker, `stop()` sets `running_ = false` and joins

### SessionAutoSave

**File:** `core/include/termcore/session_autosave.h`

- **Pattern:** Timer thread with periodic wake via condition variable
- **Thread:** Single timer thread runs `timerLoop()`
- **Synchronization:** `mutex_` + `cv_` for timed wait (default 30s interval)
- **Callback:** `StateProvider` is invoked on the timer thread to snapshot session state
- **Lifecycle:** `start(config, provider)` spawns thread, `stop()` sets `running_ = false`, notifies cv, joins
- **Writes:** Atomic file writes (write `.tmp`, rename) to prevent corruption

### SocketServer

**File:** `core/include/termcore/socket/socket_server.h`

- **Pattern:** Accept thread + per-client detached threads
- **Accept thread:** Loops on `transport_->acceptClient()`, spawns a detached `std::thread` per connection
- **Client threads:** Read JSON-RPC lines, dispatch commands through `dispatch_mutex_`
- **Synchronization:**
  - `dispatch_mutex_`: serializes all command dispatch calls across client threads
  - `auth_mutex_`: protects auth token reads/writes
  - `queue_mutex_`: protects main-thread dispatch queue (`pending_`)
- **Main thread integration:** `drainMainThreadQueue()` is called periodically from the main thread to execute queued callbacks
- **Lifecycle:** `start()` begins accept loop, `stop()` sets `running_ = false`, shuts down transport, joins accept thread. Client threads are detached and exit when they detect `running_ == false` or connection closes.
