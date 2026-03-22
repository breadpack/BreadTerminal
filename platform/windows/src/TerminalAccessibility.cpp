#if defined(_WIN32)

#include "TerminalAccessibility.h"
#include "termcore/screen.h"
#include "termcore/selection_manager.h"

#pragma comment(lib, "uiautomationcore.lib")

// ---------------------------------------------------------------------------
// UIA property/event/pattern IDs (stable ABI values from UIAutomationClient.h)
// We define them here to avoid header conflicts with windowsapp.lib
// ---------------------------------------------------------------------------
static constexpr PROPERTYID kUIA_ControlTypePropertyId          = 30003;
static constexpr PROPERTYID kUIA_NamePropertyId                 = 30005;
static constexpr PROPERTYID kUIA_AutomationIdPropertyId         = 30011;
static constexpr PROPERTYID kUIA_IsControlElementPropertyId     = 30016;
static constexpr PROPERTYID kUIA_IsContentElementPropertyId     = 30017;
static constexpr PROPERTYID kUIA_HasKeyboardFocusPropertyId     = 30008;
static constexpr PROPERTYID kUIA_IsKeyboardFocusablePropertyId  = 30009;
static constexpr PROPERTYID kUIA_LocalizedControlTypePropertyId = 30004;
static constexpr PROPERTYID kUIA_IsTextPatternAvailablePropertyId = 30040;
static constexpr PROPERTYID kUIA_LiveSettingPropertyId          = 30135;

static constexpr long kUIA_DocumentControlTypeId = 50030;

static constexpr PATTERNID kUIA_TextPatternId = 10014;

static constexpr EVENTID kUIA_Text_TextChangedEventId       = 20015;
static constexpr EVENTID kUIA_AutomationFocusChangedEventId = 20005;

// LiveSetting polite
static constexpr int kLiveSetting_Polite = 1;

// ---------------------------------------------------------------------------
// Helper: convert UTF-32 codepoint to wstring
// ---------------------------------------------------------------------------
static std::wstring codepointToWide(char32_t cp) {
    if (cp <= 0xFFFF) {
        return std::wstring(1, static_cast<wchar_t>(cp));
    }
    // Surrogate pair
    cp -= 0x10000;
    wchar_t high = static_cast<wchar_t>((cp >> 10) + 0xD800);
    wchar_t low = static_cast<wchar_t>((cp & 0x3FF) + 0xDC00);
    return {high, low};
}

// ===========================================================================
// TerminalAccessibilityProvider
// ===========================================================================

TerminalAccessibilityProvider::TerminalAccessibilityProvider(HWND hwnd)
    : hwnd_(hwnd) {}

TerminalAccessibilityProvider::~TerminalAccessibilityProvider() = default;

void TerminalAccessibilityProvider::setScreen(termcore::Screen* screen) {
    std::lock_guard<std::mutex> lock(screenMutex_);
    screen_ = screen;
}

void TerminalAccessibilityProvider::setSelection(
    const termcore::SelectionManager* sel) {
    std::lock_guard<std::mutex> lock(screenMutex_);
    selection_ = sel;
}

void TerminalAccessibilityProvider::setCellSize(float cellW, float cellH) {
    std::lock_guard<std::mutex> lock(screenMutex_);
    cellW_ = cellW;
    cellH_ = cellH;
}

TerminalAccessibilityProvider::ScreenSnapshot
TerminalAccessibilityProvider::takeSnapshot() const {
    ScreenSnapshot snap;
    std::lock_guard<std::mutex> lock(screenMutex_);
    if (!screen_) return snap;

    snap.rows = screen_->rows();
    snap.cols = screen_->cols();
    snap.cursorRow = screen_->cursorRow();
    snap.cursorCol = screen_->cursorCol();

    snap.lines.resize(snap.rows);
    for (int r = 0; r < snap.rows; ++r) {
        std::wstring line;
        line.reserve(snap.cols);
        for (int c = 0; c < snap.cols; ++c) {
            const auto& cell = screen_->cellAt(r, c);
            if (cell.codepoint == 0 || cell.codepoint == ' ') {
                line += L' ';
            } else {
                line += codepointToWide(cell.codepoint);
            }
            // Skip trailing half of wide characters
            if (cell.width > 1) {
                c += (cell.width - 1);
            }
        }
        // Trim trailing spaces
        auto end = line.find_last_not_of(L' ');
        if (end != std::wstring::npos) {
            line.resize(end + 1);
        } else {
            line.clear();
        }
        snap.lines[r] = std::move(line);
    }

    if (selection_) {
        snap.hasSelection = selection_->hasSelection();
        if (snap.hasSelection) {
            snap.selStartRow = selection_->start().row;
            snap.selStartCol = selection_->start().col;
            snap.selEndRow = selection_->end().row;
            snap.selEndCol = selection_->end().col;
        }
    }

    return snap;
}

// --- Event notifications ---

void TerminalAccessibilityProvider::notifyTextChanged() {
    auto now = std::chrono::steady_clock::now();
    if (now - lastTextChangedEvent_ < kMinEventInterval) return;
    lastTextChangedEvent_ = now;

    if (UiaClientsAreListening()) {
        UiaRaiseAutomationEvent(
            static_cast<IRawElementProviderSimple*>(this),
            kUIA_Text_TextChangedEventId);
    }
}

void TerminalAccessibilityProvider::notifyCursorMoved() {
    auto now = std::chrono::steady_clock::now();
    if (now - lastCursorEvent_ < kMinEventInterval) return;
    lastCursorEvent_ = now;

    if (UiaClientsAreListening()) {
        UiaRaiseAutomationEvent(
            static_cast<IRawElementProviderSimple*>(this),
            kUIA_AutomationFocusChangedEventId);
    }
}

// --- IUnknown ---

ULONG STDMETHODCALLTYPE TerminalAccessibilityProvider::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE TerminalAccessibilityProvider::Release() {
    ULONG count = --refCount_;
    if (count == 0) delete this;
    return count;
}

HRESULT STDMETHODCALLTYPE TerminalAccessibilityProvider::QueryInterface(
    REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;

    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IRawElementProviderSimple)) {
        *ppv = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == __uuidof(IRawElementProviderFragment)) {
        *ppv = static_cast<IRawElementProviderFragment*>(this);
    } else if (riid == __uuidof(ITextProvider)) {
        *ppv = static_cast<ITextProvider*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

// --- IRawElementProviderSimple ---

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::get_ProviderOptions(ProviderOptions* pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = ProviderOptions_ServerSideProvider |
               ProviderOptions_UseComThreading;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::GetPatternProvider(
    PATTERNID patternId, IUnknown** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;

    if (patternId == kUIA_TextPatternId) {
        *pRetVal = static_cast<ITextProvider*>(this);
        AddRef();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::GetPropertyValue(
    PROPERTYID propertyId, VARIANT* pRetVal) {
    if (!pRetVal) return E_POINTER;
    VariantInit(pRetVal);

    if (propertyId == kUIA_ControlTypePropertyId) {
        pRetVal->vt = VT_I4;
        pRetVal->lVal = kUIA_DocumentControlTypeId;
    } else if (propertyId == kUIA_NamePropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"Terminal");
    } else if (propertyId == kUIA_AutomationIdPropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"BreadTerminalContent");
    } else if (propertyId == kUIA_IsTextPatternAvailablePropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == kUIA_IsControlElementPropertyId ||
               propertyId == kUIA_IsContentElementPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == kUIA_IsKeyboardFocusablePropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
    } else if (propertyId == kUIA_HasKeyboardFocusPropertyId) {
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = (GetFocus() == hwnd_) ? VARIANT_TRUE
                                                  : VARIANT_FALSE;
    } else if (propertyId == kUIA_LocalizedControlTypePropertyId) {
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"terminal");
    } else if (propertyId == kUIA_LiveSettingPropertyId) {
        pRetVal->vt = VT_I4;
        pRetVal->lVal = kLiveSetting_Polite;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::get_HostRawElementProvider(
    IRawElementProviderSimple** pRetVal) {
    if (!pRetVal) return E_POINTER;
    return UiaHostProviderFromHwnd(hwnd_, pRetVal);
}

// --- IRawElementProviderFragment ---

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::Navigate(
    NavigateDirection /*direction*/,
    IRawElementProviderFragment** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::GetRuntimeId(SAFEARRAY** pRetVal) {
    if (!pRetVal) return E_POINTER;

    int runtimeId[] = {UiaAppendRuntimeId, 1};
    *pRetVal = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!*pRetVal) return E_OUTOFMEMORY;

    for (LONG i = 0; i < 2; ++i) {
        SafeArrayPutElement(*pRetVal, &i, &runtimeId[i]);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::get_BoundingRectangle(UiaRect* pRetVal) {
    if (!pRetVal) return E_POINTER;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&rc), 2);

    pRetVal->left = static_cast<double>(rc.left);
    pRetVal->top = static_cast<double>(rc.top);
    pRetVal->width = static_cast<double>(rc.right - rc.left);
    pRetVal->height = static_cast<double>(rc.bottom - rc.top);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::GetEmbeddedFragmentRoots(
    SAFEARRAY** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TerminalAccessibilityProvider::SetFocus() {
    ::SetFocus(hwnd_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::get_FragmentRoot(
    IRawElementProviderFragmentRoot** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;
}

// --- ITextProvider ---

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::GetSelection(SAFEARRAY** pRetVal) {
    if (!pRetVal) return E_POINTER;

    auto snap = takeSnapshot();

    if (snap.hasSelection) {
        auto* range = new TerminalTextRangeProvider(
            this,
            snap.selStartRow, snap.selStartCol,
            snap.selEndRow, snap.selEndCol);

        *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
        if (!*pRetVal) { range->Release(); return E_OUTOFMEMORY; }
        LONG idx = 0;
        ITextRangeProvider* pRange = range;
        SafeArrayPutElement(*pRetVal, &idx, pRange);
    } else {
        // Return degenerate range at cursor
        auto* range = new TerminalTextRangeProvider(
            this,
            snap.cursorRow, snap.cursorCol,
            snap.cursorRow, snap.cursorCol);

        *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
        if (!*pRetVal) { range->Release(); return E_OUTOFMEMORY; }
        LONG idx = 0;
        ITextRangeProvider* pRange = range;
        SafeArrayPutElement(*pRetVal, &idx, pRange);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::GetVisibleRanges(SAFEARRAY** pRetVal) {
    if (!pRetVal) return E_POINTER;

    auto snap = takeSnapshot();
    if (snap.rows <= 0) {
        *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return S_OK;
    }

    *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, snap.rows);
    if (!*pRetVal) return E_OUTOFMEMORY;

    for (int r = 0; r < snap.rows; ++r) {
        int endCol = snap.cols;
        if (r < static_cast<int>(snap.lines.size())) {
            endCol = static_cast<int>(snap.lines[r].size());
        }
        auto* range = new TerminalTextRangeProvider(
            this, r, 0, r, endCol);
        LONG idx = static_cast<LONG>(r);
        ITextRangeProvider* pRange = range;
        SafeArrayPutElement(*pRetVal, &idx, pRange);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::RangeFromChild(
    IRawElementProviderSimple* /*childElement*/,
    ITextRangeProvider** pRetVal) {
    if (!pRetVal) return E_POINTER;
    return get_DocumentRange(pRetVal);
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::RangeFromPoint(
    UiaPoint point, ITextRangeProvider** pRetVal) {
    if (!pRetVal) return E_POINTER;

    POINT screenPt = {static_cast<LONG>(point.x),
                      static_cast<LONG>(point.y)};
    ScreenToClient(hwnd_, &screenPt);

    float cw, ch;
    {
        std::lock_guard<std::mutex> lock(screenMutex_);
        cw = cellW_;
        ch = cellH_;
    }

    int row = static_cast<int>(screenPt.y / ch);
    int col = static_cast<int>(screenPt.x / cw);

    auto snap = takeSnapshot();
    if (row < 0) row = 0;
    if (row >= snap.rows) row = snap.rows > 0 ? snap.rows - 1 : 0;
    if (col < 0) col = 0;
    if (col >= snap.cols) col = snap.cols > 0 ? snap.cols - 1 : 0;

    *pRetVal = new TerminalTextRangeProvider(this, row, col, row, col);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::get_DocumentRange(
    ITextRangeProvider** pRetVal) {
    if (!pRetVal) return E_POINTER;

    auto snap = takeSnapshot();
    int lastRow = snap.rows > 0 ? snap.rows - 1 : 0;
    int lastCol = snap.cols;

    *pRetVal = new TerminalTextRangeProvider(this, 0, 0, lastRow, lastCol);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalAccessibilityProvider::get_SupportedTextSelection(
    SupportedTextSelection* pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = SupportedTextSelection_Single;
    return S_OK;
}

#endif // _WIN32
