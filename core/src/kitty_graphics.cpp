#include "termcore/kitty_graphics.h"
#include <algorithm>
#include <array>
#include <sstream>

namespace termcore {

namespace {

// Helper to safely parse an integer from a string, returning default_val on failure.
int safeStoi(const std::string& s, int default_val = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return default_val;
    }
}

uint32_t safeStou(const std::string& s, uint32_t default_val = 0) {
    try {
        return static_cast<uint32_t>(std::stoul(s));
    } catch (...) {
        return default_val;
    }
}

} // anonymous namespace

std::unordered_map<std::string, std::string>
KittyGraphicsManager::parseControl(const std::string& control) {
    std::unordered_map<std::string, std::string> params;
    if (control.empty()) return params;

    size_t start = 0;
    while (start < control.size()) {
        size_t comma = control.find(',', start);
        if (comma == std::string::npos) comma = control.size();

        std::string token = control.substr(start, comma - start);
        size_t eq = token.find('=');
        if (eq != std::string::npos) {
            std::string key = token.substr(0, eq);
            std::string value = token.substr(eq + 1);
            params[key] = value;
        }
        start = comma + 1;
    }
    return params;
}

std::vector<uint8_t> KittyGraphicsManager::base64Decode(const std::string& input) {
    // Standard base64 decoding table
    static const std::array<int, 256> decode_table = []() {
        std::array<int, 256> t;
        t.fill(-1);
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; chars[i]; ++i) {
            t[static_cast<unsigned char>(chars[i])] = i;
        }
        return t;
    }();

    std::vector<uint8_t> result;
    result.reserve(input.size() * 3 / 4);

    int val = 0;
    int bits = 0;
    for (unsigned char c : input) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int d = decode_table[c];
        if (d < 0) continue; // skip invalid characters

        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
        }
    }
    return result;
}

std::string KittyGraphicsManager::processCommand(
    const std::string& control, const std::string& payload) {

    auto params = parseControl(control);

    std::string action = "t"; // default action is transmit
    auto it = params.find("a");
    if (it != params.end()) {
        action = it->second;
    }

    if (action == "t") {
        handleTransmit(params, payload, false);
    } else if (action == "T") {
        handleTransmit(params, payload, true);
    } else if (action == "p") {
        handleDisplay(params);
    } else if (action == "d") {
        handleDelete(params);
    } else if (action == "q") {
        // Query: respond with OK
        return "\033_Gok\033\\";
    }

    return "";
}

void KittyGraphicsManager::handleTransmit(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& payload, bool display) {

    // Check if this is a chunked transfer
    auto m_it = params.find("m");
    bool more_chunks = (m_it != params.end() && m_it->second == "1");

    // If this is the first chunk of a new transfer, initialize pending
    if (pending_.accumulated_payload.empty()) {
        pending_.image = KittyImage{};

        auto id_it = params.find("i");
        if (id_it != params.end()) {
            pending_.image.id = safeStou(id_it->second);
        }
        if (pending_.image.id == 0) {
            pending_.image.id = next_id_++;
        }

        auto s_it = params.find("s");
        if (s_it != params.end()) {
            pending_.image.width = safeStoi(s_it->second);
        }

        auto v_it = params.find("v");
        if (v_it != params.end()) {
            pending_.image.height = safeStoi(v_it->second);
        }

        auto f_it = params.find("f");
        if (f_it != params.end()) {
            pending_.image.format = safeStoi(f_it->second, 32);
        }
    }

    // Accumulate payload
    pending_.accumulated_payload += payload;

    if (more_chunks) {
        // More data coming, don't finalize yet
        return;
    }

    // Final chunk: decode and store
    pending_.image.data = base64Decode(pending_.accumulated_payload);
    pending_.image.complete = true;

    uint32_t image_id = pending_.image.id;
    images_[image_id] = std::move(pending_.image);

    // Reset pending state
    pending_.accumulated_payload.clear();
    pending_.image = KittyImage{};

    // If transmit+display, also create a placement
    if (display) {
        KittyPlacement placement;
        placement.image_id = image_id;

        auto p_it = params.find("p");
        if (p_it != params.end()) {
            placement.placement_id = safeStou(p_it->second);
        }

        auto x_it = params.find("x");
        if (x_it != params.end()) {
            placement.x = safeStoi(x_it->second);
        }

        auto y_it = params.find("y");
        if (y_it != params.end()) {
            placement.y = safeStoi(y_it->second);
        }

        auto z_it = params.find("z");
        if (z_it != params.end()) {
            placement.z_index = safeStoi(z_it->second);
        }

        auto c_it = params.find("c");
        if (c_it != params.end()) {
            placement.cols = safeStoi(c_it->second);
        }

        auto r_it = params.find("r");
        if (r_it != params.end()) {
            placement.rows = safeStoi(r_it->second);
        }

        placements_.push_back(placement);
    }
}

void KittyGraphicsManager::handleDisplay(
    const std::unordered_map<std::string, std::string>& params) {

    KittyPlacement placement;

    auto i_it = params.find("i");
    if (i_it != params.end()) {
        placement.image_id = safeStou(i_it->second);
    }

    auto p_it = params.find("p");
    if (p_it != params.end()) {
        placement.placement_id = safeStou(p_it->second);
    }

    auto x_it = params.find("x");
    if (x_it != params.end()) {
        placement.x = safeStoi(x_it->second);
    }

    auto y_it = params.find("y");
    if (y_it != params.end()) {
        placement.y = safeStoi(y_it->second);
    }

    auto z_it = params.find("z");
    if (z_it != params.end()) {
        placement.z_index = safeStoi(z_it->second);
    }

    auto c_it = params.find("c");
    if (c_it != params.end()) {
        placement.cols = safeStoi(c_it->second);
    }

    auto r_it = params.find("r");
    if (r_it != params.end()) {
        placement.rows = safeStoi(r_it->second);
    }

    placements_.push_back(placement);
}

void KittyGraphicsManager::handleDelete(
    const std::unordered_map<std::string, std::string>& params) {

    auto d_it = params.find("d");
    if (d_it != params.end()) {
        std::string what = d_it->second;
        if (what == "a" || what == "A") {
            // Delete all
            clear();
            return;
        }
        if (what == "i" || what == "I") {
            auto i_it = params.find("i");
            if (i_it != params.end()) {
                deleteByImageId(safeStou(i_it->second));
            }
            return;
        }
    }

    // Default: if image ID is given, delete that image
    auto i_it = params.find("i");
    if (i_it != params.end()) {
        deleteByImageId(safeStou(i_it->second));
    }
}

const KittyImage* KittyGraphicsManager::getImage(uint32_t id) const {
    auto it = images_.find(id);
    if (it == images_.end()) return nullptr;
    return &it->second;
}

void KittyGraphicsManager::clear() {
    images_.clear();
    placements_.clear();
}

void KittyGraphicsManager::deleteByImageId(uint32_t id) {
    images_.erase(id);
    placements_.erase(
        std::remove_if(placements_.begin(), placements_.end(),
                        [id](const KittyPlacement& p) { return p.image_id == id; }),
        placements_.end());
}

} // namespace termcore
