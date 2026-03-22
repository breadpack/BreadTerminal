#if defined(__linux__)

#include "FontInstaller.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

namespace termcore {

// ---------------------------------------------------------------------------
// ZIP helpers (same approach as Windows FontInstaller)
// ---------------------------------------------------------------------------

static uint16_t readU16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t readU32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static bool isFontFile(const std::string& name) {
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".ttf" || ext == ".otf";
}

static std::string filenameOnly(const std::string& path) {
    auto pos = path.find_last_of('/');
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

static size_t findEOCD(const std::vector<uint8_t>& data) {
    if (data.size() < 22) return SIZE_MAX;
    size_t searchStart = data.size() < 65557 ? 0 : data.size() - 65557;
    for (size_t i = data.size() - 22; i >= searchStart; --i) {
        if (readU32(&data[i]) == 0x06054b50) return i;
        if (i == 0) break;
    }
    return SIZE_MAX;
}

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

    size_t eocd = findEOCD(data);
    if (eocd == SIZE_MAX) return extracted;

    uint32_t cdOffset = readU32(&data[eocd + 16]);
    uint16_t cdEntries = readU16(&data[eocd + 10]);

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
        pos += 46 + nameLen + extraLen + commentLen;

        if (!isFontFile(entryName) || uncompSize == 0) continue;

        if (localOffset + 30 > fileSize) continue;
        if (readU32(&data[localOffset]) != 0x04034b50) continue;

        uint16_t localNameLen  = readU16(&data[localOffset + 26]);
        uint16_t localExtraLen = readU16(&data[localOffset + 28]);
        size_t dataStart = localOffset + 30 + localNameLen + localExtraLen;

        if (dataStart + compSize > fileSize) continue;

        std::string outName = filenameOnly(entryName);
        std::string outPath = destDir + "/" + outName;
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

// ---------------------------------------------------------------------------
// getUserFontDir — ~/.local/share/fonts/
// ---------------------------------------------------------------------------

static std::string getUserFontDir() {
    const char* home = getenv("HOME");
    if (!home) return {};
    std::string dir = std::string(home) + "/.local/share/fonts";
    // Create directories recursively
    std::string partial = std::string(home) + "/.local";
    mkdir(partial.c_str(), 0755);
    partial += "/share";
    mkdir(partial.c_str(), 0755);
    partial += "/fonts";
    mkdir(partial.c_str(), 0755);
    return dir;
}

// ---------------------------------------------------------------------------
// refreshFontCache — run fc-cache -f
// ---------------------------------------------------------------------------

static void refreshFontCache() {
    // Run fc-cache in the background; don't block the UI thread
    int ret = system("fc-cache -f >/dev/null 2>&1");
    (void)ret;
}

// ---------------------------------------------------------------------------
// installFontFromUrl
// ---------------------------------------------------------------------------

bool installFontFromUrl(
    const std::string& url,
    const std::string& fontName,
    std::function<void(const std::string& status)> progressCb)
{
    auto notify = [&](const std::string& msg) {
        if (progressCb) progressCb(msg);
        fprintf(stderr, "FontInstaller: %s\n", msg.c_str());
    };

    notify("Downloading " + fontName + "...");

    // Download using curl (universally available on Linux)
    std::string tmpZip = "/tmp/breadterm_font_" +
        std::to_string(getpid()) + ".zip";

    std::string cmd = "curl -fsSL -o '" + tmpZip + "' '" + url + "'";
    int ret = system(cmd.c_str());
    if (ret != 0) {
        notify("Download failed for " + fontName);
        unlink(tmpZip.c_str());
        return false;
    }

    std::string fontDir = getUserFontDir();
    if (fontDir.empty()) {
        notify("Cannot determine font directory");
        unlink(tmpZip.c_str());
        return false;
    }

    auto fontFiles = extractFontsFromZip(tmpZip, fontDir);
    unlink(tmpZip.c_str());

    if (fontFiles.empty()) {
        notify("No font files found in archive");
        return false;
    }

    notify("Installed " + std::to_string(fontFiles.size()) +
           " font files, refreshing cache...");
    refreshFontCache();
    notify(fontName + " installed successfully");
    return true;
}

// ---------------------------------------------------------------------------
// uninstallFont
// ---------------------------------------------------------------------------

bool uninstallFont(const std::string& fontName) {
    std::string fontDir = getUserFontDir();
    if (fontDir.empty()) return false;

    // Build lowercase compact name for matching
    std::string compactName;
    for (char c : fontName) {
        if (c != ' ')
            compactName += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    DIR* dir = opendir(fontDir.c_str());
    if (!dir) return false;

    int removed = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        // Check extension
        if (name.size() < 4) continue;
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".ttf" && ext != ".otf") continue;

        // Check if filename matches font name
        std::string lowerFile;
        for (char c : name) {
            if (c != ' ' && c != '-' && c != '_')
                lowerFile += static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
        if (lowerFile.find(compactName) == std::string::npos) continue;

        std::string fullPath = fontDir + "/" + name;
        if (unlink(fullPath.c_str()) == 0) {
            removed++;
        }
    }
    closedir(dir);

    if (removed > 0) {
        refreshFontCache();
    }
    return removed > 0;
}

} // namespace termcore

#endif // __linux__
