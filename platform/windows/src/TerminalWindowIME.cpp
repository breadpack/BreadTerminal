#if defined(_WIN32)

#include <windows.h>
#include <imm.h>
#include <string>
#include <vector>
#include <functional>

#pragma comment(lib, "imm32.lib")

namespace termcore {

void handleImeStartComposition(HWND hwnd, int cursor_x, int cursor_y,
                               int cell_height) {
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;

    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = cursor_x;
    cf.ptCurrentPos.y = cursor_y;
    ImmSetCompositionWindow(imc, &cf);

    LOGFONTW lf = {};
    lf.lfHeight = cell_height;
    ImmSetCompositionFontW(imc, &lf);

    ImmReleaseContext(hwnd, imc);
}

std::string handleImeComposition(HWND hwnd, LPARAM lParam) {
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return {};

    std::string result;

    if (lParam & GCS_RESULTSTR) {
        LONG bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
        if (bytes > 0) {
            std::vector<wchar_t> wide(bytes / sizeof(wchar_t));
            ImmGetCompositionStringW(imc, GCS_RESULTSTR,
                                     wide.data(), bytes);

            int utf8_len = WideCharToMultiByte(
                CP_UTF8, 0,
                wide.data(), static_cast<int>(wide.size()),
                nullptr, 0, nullptr, nullptr);
            if (utf8_len > 0) {
                result.resize(utf8_len);
                WideCharToMultiByte(
                    CP_UTF8, 0,
                    wide.data(), static_cast<int>(wide.size()),
                    result.data(), utf8_len, nullptr, nullptr);
            }
        }
    }

    // GCS_COMPSTR is handled implicitly -- the composition window
    // displays the preedit string. We don't need to return it.

    ImmReleaseContext(hwnd, imc);
    return result;
}

void handleImeEndComposition(HWND hwnd) {
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;
    ImmReleaseContext(hwnd, imc);
}

void positionImeWindow(HWND hwnd, int x, int y, int height) {
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;

    CANDIDATEFORM cf = {};
    cf.dwIndex = 0;
    cf.dwStyle = CFS_CANDIDATEPOS;
    cf.ptCurrentPos.x = x;
    cf.ptCurrentPos.y = y + height;
    ImmSetCandidateWindow(imc, &cf);

    COMPOSITIONFORM comp = {};
    comp.dwStyle = CFS_POINT;
    comp.ptCurrentPos.x = x;
    comp.ptCurrentPos.y = y;
    ImmSetCompositionWindow(imc, &comp);

    ImmReleaseContext(hwnd, imc);
}

} // namespace termcore

#endif // _WIN32
