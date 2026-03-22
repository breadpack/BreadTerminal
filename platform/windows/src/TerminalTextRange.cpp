#if defined(_WIN32)

#include "TerminalAccessibility.h"

// UIA error code not in UIAutomationCore.h
#ifndef UIA_E_INVALIDOPERATION
#define UIA_E_INVALIDOPERATION 0x80131509L
#endif

// ===========================================================================
// TerminalTextRangeProvider
// ===========================================================================

TerminalTextRangeProvider::TerminalTextRangeProvider(
    TerminalAccessibilityProvider* owner,
    int startRow, int startCol,
    int endRow, int endCol)
    : owner_(owner)
    , startRow_(startRow), startCol_(startCol)
    , endRow_(endRow), endCol_(endCol) {
    if (owner_) owner_->AddRef();
}

TerminalTextRangeProvider::~TerminalTextRangeProvider() {
    if (owner_) owner_->Release();
}

void TerminalTextRangeProvider::clampToSnapshot(
    const TerminalAccessibilityProvider::ScreenSnapshot& snap) {
    if (snap.rows <= 0) {
        startRow_ = startCol_ = endRow_ = endCol_ = 0;
        return;
    }
    auto clampRow = [&](int r) {
        return (r < 0) ? 0 : (r >= snap.rows ? snap.rows - 1 : r);
    };
    auto clampCol = [&](int c) {
        return (c < 0) ? 0 : (c > snap.cols ? snap.cols : c);
    };
    startRow_ = clampRow(startRow_);
    startCol_ = clampCol(startCol_);
    endRow_ = clampRow(endRow_);
    endCol_ = clampCol(endCol_);
}

// --- IUnknown ---

ULONG STDMETHODCALLTYPE TerminalTextRangeProvider::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE TerminalTextRangeProvider::Release() {
    ULONG count = --refCount_;
    if (count == 0) delete this;
    return count;
}

HRESULT STDMETHODCALLTYPE TerminalTextRangeProvider::QueryInterface(
    REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;

    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(ITextRangeProvider)) {
        *ppv = static_cast<ITextRangeProvider*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

// --- ITextRangeProvider ---

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::Clone(ITextRangeProvider** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = new TerminalTextRangeProvider(
        owner_, startRow_, startCol_, endRow_, endCol_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::Compare(
    ITextRangeProvider* range, BOOL* pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = FALSE;

    auto* other = dynamic_cast<TerminalTextRangeProvider*>(range);
    if (other) {
        *pRetVal = (startRow_ == other->startRow_ &&
                    startCol_ == other->startCol_ &&
                    endRow_ == other->endRow_ &&
                    endCol_ == other->endCol_) ? TRUE : FALSE;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::CompareEndpoints(
    TextPatternRangeEndpoint endpoint,
    ITextRangeProvider* targetRange,
    TextPatternRangeEndpoint targetEndpoint,
    int* pRetVal) {
    if (!pRetVal) return E_POINTER;

    auto* other = dynamic_cast<TerminalTextRangeProvider*>(targetRange);
    if (!other) return E_INVALIDARG;

    int thisRow, thisCol, otherRow, otherCol;
    if (endpoint == TextPatternRangeEndpoint_Start) {
        thisRow = startRow_; thisCol = startCol_;
    } else {
        thisRow = endRow_; thisCol = endCol_;
    }
    if (targetEndpoint == TextPatternRangeEndpoint_Start) {
        otherRow = other->startRow_; otherCol = other->startCol_;
    } else {
        otherRow = other->endRow_; otherCol = other->endCol_;
    }

    if (thisRow < otherRow) *pRetVal = -1;
    else if (thisRow > otherRow) *pRetVal = 1;
    else if (thisCol < otherCol) *pRetVal = -1;
    else if (thisCol > otherCol) *pRetVal = 1;
    else *pRetVal = 0;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::ExpandToEnclosingUnit(TextUnit unit) {
    auto snap = owner_->takeSnapshot();
    clampToSnapshot(snap);

    switch (unit) {
    case TextUnit_Character:
        endRow_ = startRow_;
        endCol_ = startCol_ + 1;
        if (endCol_ > snap.cols) endCol_ = snap.cols;
        break;

    case TextUnit_Word: {
        // Expand to word boundaries: find whitespace boundaries on current line
        if (startRow_ < static_cast<int>(snap.lines.size())) {
            const auto& line = snap.lines[startRow_];
            int len = static_cast<int>(line.size());

            // Find start of word
            int ws = startCol_;
            while (ws > 0 && ws < len && !iswspace(line[ws])) --ws;
            if (ws < len && iswspace(line[ws])) ++ws;
            startCol_ = ws;

            // Find end of word
            int we = startCol_;
            while (we < len && !iswspace(line[we])) ++we;
            endRow_ = startRow_;
            endCol_ = we;
        }
        break;
    }

    case TextUnit_Line:
    case TextUnit_Paragraph:
        startCol_ = 0;
        endRow_ = startRow_;
        endCol_ = snap.cols;
        break;

    case TextUnit_Format:
        // Treat entire line as single format unit
        startCol_ = 0;
        endRow_ = startRow_;
        endCol_ = snap.cols;
        break;

    case TextUnit_Page:
    case TextUnit_Document:
        startRow_ = 0;
        startCol_ = 0;
        endRow_ = snap.rows > 0 ? snap.rows - 1 : 0;
        endCol_ = snap.cols;
        break;

    default:
        break;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::FindAttribute(
    TEXTATTRIBUTEID /*attributeId*/, VARIANT /*val*/, BOOL /*backward*/,
    ITextRangeProvider** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;  // Not supported yet
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::FindText(
    BSTR text, BOOL backward, BOOL ignoreCase,
    ITextRangeProvider** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!text) return E_INVALIDARG;

    auto snap = owner_->takeSnapshot();
    std::wstring needle(text);
    if (needle.empty()) return S_OK;

    // Build full document text with line positions
    std::wstring doc;
    std::vector<std::pair<int, int>> charMap; // (row, col) for each char

    for (int r = 0; r < snap.rows; ++r) {
        const auto& line = (r < static_cast<int>(snap.lines.size()))
                           ? snap.lines[r] : std::wstring();
        for (int c = 0; c < static_cast<int>(line.size()); ++c) {
            doc += line[c];
            charMap.push_back({r, c});
        }
        // Add newline between rows
        if (r < snap.rows - 1) {
            doc += L'\n';
            charMap.push_back({r, static_cast<int>(line.size())});
        }
    }

    if (ignoreCase) {
        for (auto& ch : doc) ch = towlower(ch);
        for (auto& ch : needle) ch = towlower(ch);
    }

    size_t pos;
    if (backward) {
        pos = doc.rfind(needle);
    } else {
        pos = doc.find(needle);
    }

    if (pos != std::wstring::npos && pos < charMap.size()) {
        size_t endPos = pos + needle.size() - 1;
        if (endPos >= charMap.size()) endPos = charMap.size() - 1;

        *pRetVal = new TerminalTextRangeProvider(
            owner_,
            charMap[pos].first, charMap[pos].second,
            charMap[endPos].first, charMap[endPos].second + 1);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::GetAttributeValue(
    TEXTATTRIBUTEID /*attributeId*/, VARIANT* pRetVal) {
    if (!pRetVal) return E_POINTER;
    VariantInit(pRetVal);
    // Return "not supported" for attribute queries
    pRetVal->vt = VT_UNKNOWN;
    HRESULT hr = UiaGetReservedNotSupportedValue(&pRetVal->punkVal);
    return hr;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::GetBoundingRectangles(SAFEARRAY** pRetVal) {
    if (!pRetVal) return E_POINTER;

    float cw = owner_->cellWidth();
    float ch = owner_->cellHeight();
    HWND hwnd = owner_->hwnd();

    // Get client-to-screen offset
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);

    // One bounding rect per row in the range
    int numRows = endRow_ - startRow_ + 1;
    if (numRows <= 0) {
        *pRetVal = SafeArrayCreateVector(VT_R8, 0, 0);
        return S_OK;
    }

    *pRetVal = SafeArrayCreateVector(VT_R8, 0, numRows * 4);
    if (!*pRetVal) return E_OUTOFMEMORY;

    LONG idx = 0;
    for (int r = startRow_; r <= endRow_; ++r) {
        int sc = (r == startRow_) ? startCol_ : 0;
        int ec = (r == endRow_) ? endCol_ : 80; // approximate

        double left = origin.x + sc * cw;
        double top = origin.y + r * ch;
        double width = (ec - sc) * cw;
        double height = ch;

        SafeArrayPutElement(*pRetVal, &idx, &left); ++idx;
        SafeArrayPutElement(*pRetVal, &idx, &top); ++idx;
        SafeArrayPutElement(*pRetVal, &idx, &width); ++idx;
        SafeArrayPutElement(*pRetVal, &idx, &height); ++idx;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::GetEnclosingElement(
    IRawElementProviderSimple** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = owner_;
    owner_->AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::GetText(int maxLength, BSTR* pRetVal) {
    if (!pRetVal) return E_POINTER;

    auto snap = owner_->takeSnapshot();
    clampToSnapshot(snap);

    std::wstring result;

    for (int r = startRow_; r <= endRow_ && r < snap.rows; ++r) {
        const auto& line = (r < static_cast<int>(snap.lines.size()))
                           ? snap.lines[r] : std::wstring();

        int colStart = (r == startRow_) ? startCol_ : 0;
        int colEnd = (r == endRow_) ? endCol_ : static_cast<int>(line.size());

        if (colStart < static_cast<int>(line.size())) {
            int actualEnd = (colEnd < static_cast<int>(line.size()))
                            ? colEnd : static_cast<int>(line.size());
            if (actualEnd > colStart) {
                result += line.substr(colStart, actualEnd - colStart);
            }
        }

        // Add newline between rows (not after last)
        if (r < endRow_) {
            result += L'\n';
        }
    }

    if (maxLength >= 0 && static_cast<int>(result.size()) > maxLength) {
        result.resize(maxLength);
    }

    *pRetVal = SysAllocStringLen(result.c_str(),
                                 static_cast<UINT>(result.size()));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::Move(TextUnit unit, int count, int* pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    if (count == 0) return S_OK;

    auto snap = owner_->takeSnapshot();
    clampToSnapshot(snap);

    int moved = 0;

    switch (unit) {
    case TextUnit_Character: {
        int dir = (count > 0) ? 1 : -1;
        int remaining = (count > 0) ? count : -count;
        while (remaining > 0) {
            int newCol = startCol_ + dir;
            int newRow = startRow_;
            if (newCol >= snap.cols) {
                if (newRow + 1 >= snap.rows) break;
                newRow++;
                newCol = 0;
            } else if (newCol < 0) {
                if (newRow <= 0) break;
                newRow--;
                newCol = snap.cols - 1;
            }
            startRow_ = newRow;
            startCol_ = newCol;
            moved += dir;
            --remaining;
        }
        endRow_ = startRow_;
        endCol_ = startCol_ + 1;
        if (endCol_ > snap.cols) endCol_ = snap.cols;
        break;
    }

    case TextUnit_Line:
    case TextUnit_Paragraph: {
        int dir = (count > 0) ? 1 : -1;
        int remaining = (count > 0) ? count : -count;
        while (remaining > 0) {
            int newRow = startRow_ + dir;
            if (newRow < 0 || newRow >= snap.rows) break;
            startRow_ = newRow;
            moved += dir;
            --remaining;
        }
        startCol_ = 0;
        endRow_ = startRow_;
        endCol_ = snap.cols;
        break;
    }

    case TextUnit_Word: {
        // Simplified: move by word on current line, then wrap to next line
        int dir = (count > 0) ? 1 : -1;
        int remaining = (count > 0) ? count : -count;
        while (remaining > 0) {
            if (startRow_ < static_cast<int>(snap.lines.size())) {
                const auto& line = snap.lines[startRow_];
                int len = static_cast<int>(line.size());
                if (dir > 0) {
                    // Skip current word, then skip whitespace
                    int c = startCol_;
                    while (c < len && !iswspace(line[c])) ++c;
                    while (c < len && iswspace(line[c])) ++c;
                    if (c >= len) {
                        if (startRow_ + 1 >= snap.rows) break;
                        startRow_++;
                        startCol_ = 0;
                    } else {
                        startCol_ = c;
                    }
                } else {
                    int c = startCol_ - 1;
                    if (c < 0) {
                        if (startRow_ <= 0) break;
                        startRow_--;
                        startCol_ = static_cast<int>(
                            snap.lines[startRow_].size());
                    } else {
                        while (c > 0 && iswspace(line[c])) --c;
                        while (c > 0 && !iswspace(line[c - 1])) --c;
                        startCol_ = c;
                    }
                }
            }
            moved += dir;
            --remaining;
        }
        endRow_ = startRow_;
        endCol_ = startCol_;
        // Expand to word
        ExpandToEnclosingUnit(TextUnit_Word);
        break;
    }

    case TextUnit_Page:
    case TextUnit_Document:
        if (count > 0) {
            startRow_ = snap.rows > 0 ? snap.rows - 1 : 0;
            startCol_ = 0;
            endRow_ = startRow_;
            endCol_ = snap.cols;
            moved = 1;
        } else {
            startRow_ = 0;
            startCol_ = 0;
            endRow_ = 0;
            endCol_ = snap.cols;
            moved = -1;
        }
        break;

    default:
        break;
    }

    *pRetVal = moved;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::MoveEndpointByUnit(
    TextPatternRangeEndpoint endpoint,
    TextUnit unit, int count, int* pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    if (count == 0) return S_OK;

    auto snap = owner_->takeSnapshot();
    clampToSnapshot(snap);

    int& row = (endpoint == TextPatternRangeEndpoint_Start)
               ? startRow_ : endRow_;
    int& col = (endpoint == TextPatternRangeEndpoint_Start)
               ? startCol_ : endCol_;
    int moved = 0;
    int dir = (count > 0) ? 1 : -1;
    int remaining = (count > 0) ? count : -count;

    switch (unit) {
    case TextUnit_Character:
        while (remaining > 0) {
            int newCol = col + dir;
            int newRow = row;
            if (newCol > snap.cols) {
                if (newRow + 1 >= snap.rows) break;
                newRow++;
                newCol = 0;
            } else if (newCol < 0) {
                if (newRow <= 0) break;
                newRow--;
                newCol = snap.cols;
            }
            row = newRow;
            col = newCol;
            moved += dir;
            --remaining;
        }
        break;

    case TextUnit_Line:
    case TextUnit_Paragraph:
        while (remaining > 0) {
            int newRow = row + dir;
            if (newRow < 0 || newRow >= snap.rows) break;
            row = newRow;
            moved += dir;
            --remaining;
        }
        if (endpoint == TextPatternRangeEndpoint_Start) {
            col = 0;
        } else {
            col = snap.cols;
        }
        break;

    case TextUnit_Page:
    case TextUnit_Document:
        if (dir > 0) {
            row = snap.rows > 0 ? snap.rows - 1 : 0;
            col = snap.cols;
        } else {
            row = 0;
            col = 0;
        }
        moved = dir;
        break;

    default:
        break;
    }

    // Ensure start <= end
    if (startRow_ > endRow_ ||
        (startRow_ == endRow_ && startCol_ > endCol_)) {
        if (endpoint == TextPatternRangeEndpoint_Start) {
            endRow_ = startRow_;
            endCol_ = startCol_;
        } else {
            startRow_ = endRow_;
            startCol_ = endCol_;
        }
    }

    *pRetVal = moved;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::MoveEndpointByRange(
    TextPatternRangeEndpoint endpoint,
    ITextRangeProvider* targetRange,
    TextPatternRangeEndpoint targetEndpoint) {
    auto* other = dynamic_cast<TerminalTextRangeProvider*>(targetRange);
    if (!other) return E_INVALIDARG;

    int targetRow, targetCol;
    if (targetEndpoint == TextPatternRangeEndpoint_Start) {
        targetRow = other->startRow_;
        targetCol = other->startCol_;
    } else {
        targetRow = other->endRow_;
        targetCol = other->endCol_;
    }

    if (endpoint == TextPatternRangeEndpoint_Start) {
        startRow_ = targetRow;
        startCol_ = targetCol;
        // Ensure start <= end
        if (startRow_ > endRow_ ||
            (startRow_ == endRow_ && startCol_ > endCol_)) {
            endRow_ = startRow_;
            endCol_ = startCol_;
        }
    } else {
        endRow_ = targetRow;
        endCol_ = targetCol;
        if (startRow_ > endRow_ ||
            (startRow_ == endRow_ && startCol_ > endCol_)) {
            startRow_ = endRow_;
            startCol_ = endCol_;
        }
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TerminalTextRangeProvider::Select() {
    // Selection is managed by the terminal, not by UIA
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TerminalTextRangeProvider::AddToSelection() {
    return UIA_E_INVALIDOPERATION;
}

HRESULT STDMETHODCALLTYPE TerminalTextRangeProvider::RemoveFromSelection() {
    return UIA_E_INVALIDOPERATION;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::ScrollIntoView(BOOL /*alignToTop*/) {
    // Viewport scrolling not implemented via UIA
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
TerminalTextRangeProvider::GetChildren(SAFEARRAY** pRetVal) {
    if (!pRetVal) return E_POINTER;
    *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return S_OK;
}

#endif // _WIN32
