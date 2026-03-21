#if defined(_WIN32)

#include "FontInstaller.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <urlmon.h>
#include <shlwapi.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shlwapi.lib")

// ZIP reading with deflate support via zlib.
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <zlib.h>

namespace termcore {

// --- ZIP extraction with central directory parsing ---

static uint16_t readU16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t readU32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static bool isFontFile(const std::string& name) {
    auto len = name.size();
    if (len < 4) return false;
    std::string ext = name.substr(len - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".ttf" || ext == ".otf";
}

static std::string filenameOnly(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return pos != std::string::npos ? path.substr(pos + 1) : path;
}

static bool zlibInflate(const uint8_t* src, size_t srcLen,
                        std::vector<uint8_t>& dest, size_t destLen) {
    dest.resize(destLen);
    z_stream strm = {};
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
    strm.next_in = const_cast<Bytef*>(src);
    strm.avail_in = static_cast<uInt>(srcLen);
    strm.next_out = dest.data();
    strm.avail_out = static_cast<uInt>(destLen);
    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    return ret == Z_STREAM_END;
}

/// Find the End of Central Directory record in a ZIP file.
static size_t findEOCD(const std::vector<uint8_t>& data) {
    // EOCD signature: 0x06054b50, search backwards from end
    if (data.size() < 22) return SIZE_MAX;
    size_t searchStart = data.size() < 65557 ? 0 : data.size() - 65557;
    for (size_t i = data.size() - 22; i >= searchStart; --i) {
        if (readU32(&data[i]) == 0x06054b50) return i;
        if (i == 0) break;
    }
    return SIZE_MAX;
}

/// Extract font files from a ZIP using central directory (handles data descriptors).
static std::vector<std::string> extractFontsFromZip(
    const std::string& zipPath,
    const std::string& destDir)
{
    std::vector<std::string> extracted;

    std::ifstream f(zipPath, std::ios::binary | std::ios::ate);
    if (!f) return extracted;

    size_t fileSize = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(fileSize);
    f.read(reinterpret_cast<char*>(data.data()), fileSize);
    f.close();

    // Find EOCD to locate central directory
    size_t eocd = findEOCD(data);
    if (eocd == SIZE_MAX) return extracted;

    uint32_t cdOffset = readU32(&data[eocd + 16]);
    uint16_t cdEntries = readU16(&data[eocd + 10]);

    // Parse central directory entries
    size_t pos = cdOffset;
    for (int e = 0; e < cdEntries && pos + 46 <= fileSize; ++e) {
        if (readU32(&data[pos]) != 0x02014b50) break;

        uint16_t compression = readU16(&data[pos + 10]);
        uint32_t compSize    = readU32(&data[pos + 20]);
        uint32_t uncompSize  = readU32(&data[pos + 24]);
        uint16_t nameLen     = readU16(&data[pos + 28]);
        uint16_t extraLen    = readU16(&data[pos + 30]);
        uint16_t commentLen  = readU16(&data[pos + 32]);
        uint32_t localOffset = readU32(&data[pos + 42]);

        if (pos + 46 + nameLen > fileSize) break;

        std::string entryName(reinterpret_cast<const char*>(&data[pos + 46]),
                              nameLen);

        // Advance to next central directory entry
        pos += 46 + nameLen + extraLen + commentLen;

        if (!isFontFile(entryName) || uncompSize == 0) continue;

        // Read local file header to find actual data start
        if (localOffset + 30 > fileSize) continue;
        if (readU32(&data[localOffset]) != 0x04034b50) continue;

        uint16_t localNameLen  = readU16(&data[localOffset + 26]);
        uint16_t localExtraLen = readU16(&data[localOffset + 28]);
        size_t dataStart = localOffset + 30 + localNameLen + localExtraLen;

        if (dataStart + compSize > fileSize) continue;

        std::string outName = filenameOnly(entryName);
        std::string outPath = destDir + "\\" + outName;
        bool written = false;

        if (compression == 0) {
            std::ofstream out(outPath, std::ios::binary);
            if (out) {
                out.write(reinterpret_cast<const char*>(&data[dataStart]),
                          uncompSize);
                written = true;
            }
        } else if (compression == 8) {
            std::vector<uint8_t> decompressed;
            if (zlibInflate(&data[dataStart], compSize,
                            decompressed, uncompSize)) {
                std::ofstream out(outPath, std::ios::binary);
                if (out) {
                    out.write(reinterpret_cast<const char*>(decompressed.data()),
                              decompressed.size());
                    written = true;
                }
            }
        }

        if (written) extracted.push_back(outPath);
    }

    return extracted;
}

/// Get per-user font directory, creating if needed.
static std::string getUserFontDir() {
    wchar_t* localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                        &localAppData))) {
        std::wstring dir = std::wstring(localAppData) +
                           L"\\Microsoft\\Windows\\Fonts";
        CoTaskMemFree(localAppData);

        CreateDirectoryW(dir.c_str(), nullptr);

        int sz = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1,
                                     nullptr, 0, nullptr, nullptr);
        std::string result(sz - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1,
                            result.data(), sz, nullptr, nullptr);
        return result;
    }
    return {};
}

/// Register a font file with Windows (per-user, persistent across sessions).
/// Does NOT broadcast WM_FONTCHANGE — caller should do that once after batch.
static bool registerFont(const std::string& fontPath) {
    // Convert to wide
    int sz = MultiByteToWideChar(CP_UTF8, 0, fontPath.c_str(), -1, nullptr, 0);
    std::wstring wide(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, fontPath.c_str(), -1, wide.data(), sz);

    // Add font resource for current session (0 = system-wide, not FR_PRIVATE)
    int result = AddFontResourceExW(wide.c_str(), 0, nullptr);
    if (result == 0) return false;

    // Also register permanently via registry (per-user)
    std::wstring fontFileName = wide;
    auto pos = fontFileName.find_last_of(L'\\');
    if (pos != std::wstring::npos) fontFileName = fontFileName.substr(pos + 1);

    HKEY hKey = nullptr;
    LONG err = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        0, KEY_SET_VALUE, &hKey);
    if (err == ERROR_SUCCESS && hKey) {
        RegSetValueExW(hKey, fontFileName.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(wide.c_str()),
                       static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    return true;
}

static std::wstring toWideStr(const std::string& utf8) {
    if (utf8.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), sz);
    return w;
}

static std::string toUtf8Str(const std::wstring& wide) {
    if (wide.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                  nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}

bool installFontFromUrl(
    const std::string& url,
    const std::string& fontName,
    std::function<void(const std::string& status)> progressCb)
{
    auto notify = [&](const std::string& msg) {
        if (progressCb) progressCb(msg);
        OutputDebugStringA(("FontInstaller: " + msg + "\n").c_str());
    };

    notify("Downloading " + fontName + "...");

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring tempZip = std::wstring(tempDir) + L"breadterm_font.zip";
    std::wstring wideUrl = toWideStr(url);

    // Clear URL cache to ensure fresh download
    DeleteUrlCacheEntryW(wideUrl.c_str());

    HRESULT hr = URLDownloadToFileW(nullptr, wideUrl.c_str(),
                                     tempZip.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        notify("Download failed (HRESULT: 0x" + std::to_string(hr) + ")");
        return false;
    }

    std::string fontDir = getUserFontDir();
    if (fontDir.empty()) {
        DeleteFileW(tempZip.c_str());
        return false;
    }

    auto fontFiles = extractFontsFromZip(toUtf8Str(tempZip), fontDir);
    DeleteFileW(tempZip.c_str());

    if (fontFiles.empty()) {
        notify("No font files found in archive");
        return false;
    }

    int installed = 0;
    for (const auto& ff : fontFiles) {
        if (registerFont(ff)) installed++;
    }

    if (installed > 0) {
        PostMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
        notify(fontName + " installed (" + std::to_string(installed) + " files)");
        return true;
    }

    notify("Failed to register fonts");
    return false;
}

bool uninstallFont(const std::string& fontName) {
    auto log = [](const std::string& msg) {
        FILE* f = fopen("C:\\Users\\milen\\font_uninstall.log", "a");
        if (f) { fprintf(f, "%s\n", msg.c_str()); fclose(f); }
    };
    log("Uninstalling: " + fontName);

    std::string fontDir = getUserFontDir();
    if (fontDir.empty()) { log("No font dir"); return false; }

    std::wstring fontDirW = toWideStr(fontDir);
    std::wstring searchPattern = fontDirW + L"\\*";

    // Build lowercase match prefix from font name (e.g. "Hack" → "hack")
    std::string lowerName = fontName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    // Remove spaces for matching (e.g. "JetBrains Mono" → "jetbrainsmono")
    std::string compactName;
    for (char c : lowerName) {
        if (c != ' ') compactName += c;
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    int removed = 0;
    HKEY hKey = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER,
                  L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                  0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);

    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        // Check extension
        std::wstring ext = name.size() >= 4 ? name.substr(name.size() - 4) : L"";
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext != L".ttf" && ext != L".otf") continue;

        // Check if filename matches font name (case-insensitive, ignoring spaces/hyphens)
        std::string nameUtf8 = toUtf8Str(name);
        std::string lowerFile;
        for (char c : nameUtf8) {
            if (c != ' ' && c != '-' && c != '_')
                lowerFile += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lowerFile.find(compactName) == std::string::npos) continue;

        std::wstring fullPath = fontDirW + L"\\" + name;
        log("  Match: " + toUtf8Str(fullPath));

        // Remove font resource from current session
        RemoveFontResourceExW(fullPath.c_str(), 0, nullptr);

        // Delete registry entry
        if (hKey) RegDeleteValueW(hKey, name.c_str());

        // Delete file
        BOOL delOk = DeleteFileW(fullPath.c_str());
        log("  Delete: " + std::string(delOk ? "OK" : "FAIL"));
        removed++;
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    if (hKey) RegCloseKey(hKey);

    log("Removed " + std::to_string(removed) + " files");
    if (removed > 0) {
        PostMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    }
    return removed > 0;
}

} // namespace termcore

#endif
