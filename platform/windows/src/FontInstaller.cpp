#if defined(_WIN32)

#include "FontInstaller.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <urlmon.h>
#include <shlwapi.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shlwapi.lib")

// Minimal ZIP reading for font extraction (no external dependency).
// ZIP local file header: signature 0x04034b50, then fixed fields.
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace termcore {

// --- Minimal ZIP extraction (local file headers only) ---

struct ZipLocalHeader {
    uint16_t version;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t nameLen;
    uint16_t extraLen;
};

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

/// Extract stored (uncompressed) font files from a ZIP.
/// Returns list of extracted file paths.
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

    size_t offset = 0;
    while (offset + 30 <= fileSize) {
        // Check local file header signature
        uint32_t sig = readU32(&data[offset]);
        if (sig != 0x04034b50) break;

        ZipLocalHeader hdr;
        hdr.version = readU16(&data[offset + 4]);
        hdr.flags = readU16(&data[offset + 6]);
        hdr.compression = readU16(&data[offset + 8]);
        hdr.compressedSize = readU32(&data[offset + 18]);
        hdr.uncompressedSize = readU32(&data[offset + 22]);
        hdr.nameLen = readU16(&data[offset + 26]);
        hdr.extraLen = readU16(&data[offset + 28]);

        size_t nameStart = offset + 30;
        size_t dataStart = nameStart + hdr.nameLen + hdr.extraLen;

        if (nameStart + hdr.nameLen > fileSize) break;

        std::string entryName(reinterpret_cast<const char*>(&data[nameStart]),
                              hdr.nameLen);

        // Only extract stored (compression=0) font files
        if (hdr.compression == 0 && hdr.uncompressedSize > 0 &&
            isFontFile(entryName)) {
            if (dataStart + hdr.uncompressedSize <= fileSize) {
                std::string outName = filenameOnly(entryName);
                std::string outPath = destDir + "\\" + outName;

                std::ofstream out(outPath, std::ios::binary);
                if (out) {
                    out.write(reinterpret_cast<const char*>(&data[dataStart]),
                              hdr.uncompressedSize);
                    out.close();
                    extracted.push_back(outPath);
                }
            }
        }

        // Advance to next entry
        offset = dataStart + hdr.compressedSize;
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
static bool registerFont(const std::string& fontPath) {
    // Convert to wide
    int sz = MultiByteToWideChar(CP_UTF8, 0, fontPath.c_str(), -1, nullptr, 0);
    std::wstring wide(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, fontPath.c_str(), -1, wide.data(), sz);

    // Add font resource for current session
    int result = AddFontResourceExW(wide.c_str(), FR_PRIVATE, nullptr);
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

    // Notify other applications
    SendMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    return true;
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

    // Create temp file for download
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);

    std::wstring tempZip = std::wstring(tempDir) + L"breadterm_font.zip";

    // Convert URL to wide
    int urlSz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wideUrl(urlSz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wideUrl.data(), urlSz);

    // Download
    HRESULT hr = URLDownloadToFileW(nullptr, wideUrl.c_str(),
                                     tempZip.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        notify("Download failed (HRESULT: " + std::to_string(hr) + ")");
        return false;
    }

    notify("Extracting fonts...");

    // Convert temp zip path to UTF-8
    int zipSz = WideCharToMultiByte(CP_UTF8, 0, tempZip.c_str(), -1,
                                     nullptr, 0, nullptr, nullptr);
    std::string zipPathUtf8(zipSz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, tempZip.c_str(), -1,
                        zipPathUtf8.data(), zipSz, nullptr, nullptr);

    // Get user font directory
    std::string fontDir = getUserFontDir();
    if (fontDir.empty()) {
        notify("Cannot find user font directory");
        DeleteFileW(tempZip.c_str());
        return false;
    }

    // Extract font files
    auto fontFiles = extractFontsFromZip(zipPathUtf8, fontDir);

    // Clean up temp ZIP
    DeleteFileW(tempZip.c_str());

    if (fontFiles.empty()) {
        notify("No font files found in archive (only uncompressed ZIPs supported, trying shell extract...)");

        // Fallback: Use Windows Shell to extract (handles deflate compression)
        // Create a temp extraction directory
        std::wstring extractDir = std::wstring(tempDir) + L"breadterm_fonts\\";
        CreateDirectoryW(extractDir.c_str(), nullptr);

        // Re-download since we deleted the zip
        hr = URLDownloadToFileW(nullptr, wideUrl.c_str(),
                                 tempZip.c_str(), 0, nullptr);
        if (FAILED(hr)) {
            notify("Re-download failed");
            return false;
        }

        // Use PowerShell to extract
        std::wstring psCmd = L"powershell -NoProfile -Command \"Expand-Archive -Force '"
            + tempZip + L"' '" + extractDir + L"'\"";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (CreateProcessW(nullptr, const_cast<wchar_t*>(psCmd.c_str()),
                           nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 30000); // 30 second timeout
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        // Scan extracted directory for font files
        WIN32_FIND_DATAW fd;
        std::wstring searchPattern = extractDir + L"*";
        // Recursive search
        std::vector<std::wstring> dirs = { extractDir };
        while (!dirs.empty()) {
            std::wstring dir = dirs.back();
            dirs.pop_back();
            HANDLE hFind = FindFirstFileW((dir + L"*").c_str(), &fd);
            if (hFind == INVALID_HANDLE_VALUE) continue;
            do {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                std::wstring full = dir + name;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    dirs.push_back(full + L"\\");
                } else {
                    // Check if it's a font file
                    std::wstring ext = name.size() >= 4
                        ? name.substr(name.size() - 4) : L"";
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    if (ext == L".ttf" || ext == L".otf") {
                        // Copy to user font dir
                        int fnSz = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1,
                                                       nullptr, 0, nullptr, nullptr);
                        std::string fnUtf8(fnSz - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1,
                                            fnUtf8.data(), fnSz, nullptr, nullptr);

                        std::wstring destW;
                        {
                            int dSz = MultiByteToWideChar(CP_UTF8, 0, fontDir.c_str(),
                                                           -1, nullptr, 0);
                            destW.resize(dSz - 1);
                            MultiByteToWideChar(CP_UTF8, 0, fontDir.c_str(), -1,
                                                destW.data(), dSz);
                        }
                        destW += L"\\" + name;
                        CopyFileW(full.c_str(), destW.c_str(), FALSE);
                        fontFiles.push_back(fontDir + "\\" + fnUtf8);
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }

        // Clean up temp files
        DeleteFileW(tempZip.c_str());
        // Clean up extract dir (best effort)
        std::wstring rmCmd = L"cmd /c rmdir /s /q \"" + extractDir + L"\"";
        STARTUPINFOW si2 = { sizeof(si2) };
        PROCESS_INFORMATION pi2 = {};
        si2.dwFlags = STARTF_USESHOWWINDOW;
        si2.wShowWindow = SW_HIDE;
        if (CreateProcessW(nullptr, const_cast<wchar_t*>(rmCmd.c_str()),
                           nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &si2, &pi2)) {
            WaitForSingleObject(pi2.hProcess, 5000);
            CloseHandle(pi2.hProcess);
            CloseHandle(pi2.hThread);
        }
    }

    if (fontFiles.empty()) {
        notify("No .ttf/.otf files found in archive");
        return false;
    }

    // Register each font
    int installed = 0;
    for (const auto& ff : fontFiles) {
        if (registerFont(ff)) {
            installed++;
            notify("Installed: " + filenameOnly(ff));
        }
    }

    if (installed > 0) {
        notify(fontName + " installed successfully (" +
               std::to_string(installed) + " files)");
        return true;
    }

    notify("Failed to register fonts");
    return false;
}

} // namespace termcore

#endif
