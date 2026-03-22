#ifndef BREAD_TERMINAL_ACCESSIBILITY_H
#define BREAD_TERMINAL_ACCESSIBILITY_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace termcore {
class Screen;
class SelectionManager;
} // namespace termcore

// Forward declaration
class TerminalTextRangeProvider;

/// UI Automation provider for BreadTerminal.
/// Implements IRawElementProviderSimple, IRawElementProviderFragment, and
/// ITextProvider so screen readers can access terminal content.
class TerminalAccessibilityProvider
    : public IRawElementProviderSimple
    , public IRawElementProviderFragment
    , public ITextProvider {
public:
    TerminalAccessibilityProvider(HWND hwnd);
    ~TerminalAccessibilityProvider();

    // Bind the screen and selection objects (called from main thread)
    void setScreen(termcore::Screen* screen);
    void setSelection(const termcore::SelectionManager* sel);
    void setCellSize(float cellW, float cellH);

    // Event firing (call from main thread after PTY read)
    void notifyTextChanged();
    void notifyCursorMoved();

    // Thread-safe snapshot helpers used by text range providers
    struct ScreenSnapshot {
        std::vector<std::wstring> lines;
        int cursorRow = 0;
        int cursorCol = 0;
        int rows = 0;
        int cols = 0;
        bool hasSelection = false;
        int selStartRow = 0;
        int selStartCol = 0;
        int selEndRow = 0;
        int selEndCol = 0;
    };
    ScreenSnapshot takeSnapshot() const;

    HWND hwnd() const { return hwnd_; }
    float cellWidth() const { return cellW_; }
    float cellHeight() const { return cellH_; }

    // --- IUnknown ---
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;

    // --- IRawElementProviderSimple ---
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(
        ProviderOptions* pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(
        PATTERNID patternId, IUnknown** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(
        PROPERTYID propertyId, VARIANT* pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** pRetVal) override;

    // --- IRawElementProviderFragment ---
    HRESULT STDMETHODCALLTYPE Navigate(
        NavigateDirection direction,
        IRawElementProviderFragment** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(
        UiaRect* pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(
        SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(
        IRawElementProviderFragmentRoot** pRetVal) override;

    // --- ITextProvider ---
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE RangeFromChild(
        IRawElementProviderSimple* childElement,
        ITextRangeProvider** pRetVal) override;
    HRESULT STDMETHODCALLTYPE RangeFromPoint(
        UiaPoint point, ITextRangeProvider** pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_DocumentRange(
        ITextRangeProvider** pRetVal) override;
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(
        SupportedTextSelection* pRetVal) override;

private:
    HWND hwnd_;
    std::atomic<ULONG> refCount_{1};

    mutable std::mutex screenMutex_;
    termcore::Screen* screen_ = nullptr;
    const termcore::SelectionManager* selection_ = nullptr;
    float cellW_ = 8.0f;
    float cellH_ = 16.0f;

    // Event throttling
    std::chrono::steady_clock::time_point lastTextChangedEvent_;
    std::chrono::steady_clock::time_point lastCursorEvent_;
    static constexpr auto kMinEventInterval = std::chrono::milliseconds(100);
};

/// ITextRangeProvider for a contiguous range of terminal text.
class TerminalTextRangeProvider : public ITextRangeProvider {
public:
    TerminalTextRangeProvider(
        TerminalAccessibilityProvider* owner,
        int startRow, int startCol,
        int endRow, int endCol);
    ~TerminalTextRangeProvider();

    // --- IUnknown ---
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;

    // --- ITextRangeProvider ---
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** pRetVal) override;
    HRESULT STDMETHODCALLTYPE Compare(
        ITextRangeProvider* range, BOOL* pRetVal) override;
    HRESULT STDMETHODCALLTYPE CompareEndpoints(
        TextPatternRangeEndpoint endpoint,
        ITextRangeProvider* targetRange,
        TextPatternRangeEndpoint targetEndpoint,
        int* pRetVal) override;
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(
        TextUnit unit) override;
    HRESULT STDMETHODCALLTYPE FindAttribute(
        TEXTATTRIBUTEID attributeId, VARIANT val, BOOL backward,
        ITextRangeProvider** pRetVal) override;
    HRESULT STDMETHODCALLTYPE FindText(
        BSTR text, BOOL backward, BOOL ignoreCase,
        ITextRangeProvider** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetAttributeValue(
        TEXTATTRIBUTEID attributeId, VARIANT* pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(
        SAFEARRAY** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(
        IRawElementProviderSimple** pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetText(int maxLength,
        BSTR* pRetVal) override;
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count,
        int* pRetVal) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(
        TextPatternRangeEndpoint endpoint,
        TextUnit unit, int count, int* pRetVal) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(
        TextPatternRangeEndpoint endpoint,
        ITextRangeProvider* targetRange,
        TextPatternRangeEndpoint targetEndpoint) override;
    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL alignToTop) override;
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** pRetVal) override;

    // Accessors for CompareEndpoints
    int startRow() const { return startRow_; }
    int startCol() const { return startCol_; }
    int endRow() const { return endRow_; }
    int endCol() const { return endCol_; }

private:
    TerminalAccessibilityProvider* owner_;
    std::atomic<ULONG> refCount_{1};
    int startRow_, startCol_;
    int endRow_, endCol_;

    void clampToSnapshot(const TerminalAccessibilityProvider::ScreenSnapshot& snap);
};

#endif // _WIN32
#endif // BREAD_TERMINAL_ACCESSIBILITY_H
