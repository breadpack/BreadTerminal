#include "termcore/kitty_graphics.h"
#include "termcore/base64.h"
#include "termcore/iterm_image.h"
#include <algorithm>
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
    return termcore::base64Decode(input);
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
        // Respond to query: terminal supports graphics protocol
        auto i_it = params.find("i");
        uint32_t image_id = i_it != params.end() ? safeStou(i_it->second) : 0;
        return "i=" + std::to_string(image_id) + ";OK";
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
        pending_.start_time = std::chrono::steady_clock::now();

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

        // Validate dimensions early (width * height * 4 bytes per pixel)
        if (pending_.image.width > 0 && pending_.image.height > 0) {
            size_t estimated = static_cast<size_t>(pending_.image.width)
                             * static_cast<size_t>(pending_.image.height) * 4;
            if (estimated > kMaxSingleImageSize) {
                pending_.accumulated_payload.clear();
                pending_.image = KittyImage{};
                return;
            }
        }
    }

    // Pending transfer timeout check
    auto elapsed = std::chrono::steady_clock::now() - pending_.start_time;
    if (elapsed > kPendingTimeout) {
        pending_.accumulated_payload.clear();
        pending_.image = KittyImage{};
        return;
    }

    // Accumulate payload and check pending size limit
    pending_.accumulated_payload += payload;
    if (pending_.accumulated_payload.size() > kMaxPendingPayload) {
        pending_.accumulated_payload.clear();
        pending_.image = KittyImage{};
        return;
    }

    if (more_chunks) {
        // More data coming, don't finalize yet
        return;
    }

    // Final chunk: decode and store
    pending_.image.data = base64Decode(pending_.accumulated_payload);
    pending_.image.complete = true;

    // Decode PNG format (f=100) to RGBA
    if (pending_.image.format == 100 && !pending_.image.data.empty()) {
        int w = 0, h = 0;
        std::vector<uint8_t> rgba_pixels;
        if (decodeImageData(pending_.image.data, w, h, rgba_pixels)) {
            pending_.image.data = std::move(rgba_pixels);
            pending_.image.width = w;
            pending_.image.height = h;
            pending_.image.format = 32; // Now RGBA
        }
    }

    // Check decoded data size against single image limit
    if (pending_.image.data.size() > kMaxSingleImageSize) {
        pending_.accumulated_payload.clear();
        pending_.image = KittyImage{};
        return;
    }

    uint32_t image_id = pending_.image.id;

    // If replacing an existing image, subtract its old memory
    auto existing = images_.find(image_id);
    if (existing != images_.end()) {
        total_image_memory_ -= existing->second.data.size();
        // Remove from LRU (will be re-added)
        lru_order_.remove(image_id);
    }

    size_t new_size = pending_.image.data.size();
    images_[image_id] = std::move(pending_.image);
    total_image_memory_ += new_size;
    lru_order_.push_back(image_id);

    // Evict old images if over limits
    evictIfNeeded();

    // Reset pending state
    pending_.accumulated_payload.clear();
    pending_.image = KittyImage{};

    // If transmit+display, also create a placement
    if (display) {
        KittyPlacement placement;
        placement.image_id = image_id;
        placement.col = cursor_col_;
        placement.absolute_row = cursor_absolute_row_;

        auto p_it = params.find("p");
        if (p_it != params.end()) {
            placement.placement_id = safeStou(p_it->second);
        }

        // Kitty protocol x/y params are source crop offsets
        auto x_it = params.find("x");
        if (x_it != params.end()) {
            placement.src_x = safeStoi(x_it->second);
        }

        auto y_it = params.find("y");
        if (y_it != params.end()) {
            placement.src_y = safeStoi(y_it->second);
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
    placement.col = cursor_col_;
    placement.absolute_row = cursor_absolute_row_;

    auto i_it = params.find("i");
    if (i_it != params.end()) {
        placement.image_id = safeStou(i_it->second);
    }

    auto p_it = params.find("p");
    if (p_it != params.end()) {
        placement.placement_id = safeStou(p_it->second);
    }

    // Kitty protocol x/y params are source crop offsets
    auto x_it = params.find("x");
    if (x_it != params.end()) {
        placement.src_x = safeStoi(x_it->second);
    }

    auto y_it = params.find("y");
    if (y_it != params.end()) {
        placement.src_y = safeStoi(y_it->second);
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

    // Touch LRU on display access
    if (placement.image_id != 0) {
        touchLru(placement.image_id);
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
    lru_order_.clear();
    total_image_memory_ = 0;
}

void KittyGraphicsManager::deleteByImageId(uint32_t id) {
    removeImage(id);
    placements_.erase(
        std::remove_if(placements_.begin(), placements_.end(),
                        [id](const KittyPlacement& p) { return p.image_id == id; }),
        placements_.end());
}

void KittyGraphicsManager::removeImage(uint32_t id) {
    auto it = images_.find(id);
    if (it != images_.end()) {
        total_image_memory_ -= it->second.data.size();
        images_.erase(it);
        lru_order_.remove(id);
    }
}

void KittyGraphicsManager::evictIfNeeded() {
    // Evict by count limit
    while (images_.size() > kMaxImageCount && !lru_order_.empty()) {
        uint32_t oldest = lru_order_.front();
        lru_order_.pop_front();
        auto it = images_.find(oldest);
        if (it != images_.end()) {
            total_image_memory_ -= it->second.data.size();
            images_.erase(it);
        }
        // Also remove associated placements
        placements_.erase(
            std::remove_if(placements_.begin(), placements_.end(),
                            [oldest](const KittyPlacement& p) { return p.image_id == oldest; }),
            placements_.end());
    }

    // Evict by memory limit
    while (total_image_memory_ > kMaxTotalImageMemory && !lru_order_.empty()) {
        uint32_t oldest = lru_order_.front();
        lru_order_.pop_front();
        auto it = images_.find(oldest);
        if (it != images_.end()) {
            total_image_memory_ -= it->second.data.size();
            images_.erase(it);
        }
        placements_.erase(
            std::remove_if(placements_.begin(), placements_.end(),
                            [oldest](const KittyPlacement& p) { return p.image_id == oldest; }),
            placements_.end());
    }
}

void KittyGraphicsManager::touchLru(uint32_t id) {
    lru_order_.remove(id);
    lru_order_.push_back(id);
}

uint32_t KittyGraphicsManager::addImage(KittyImage image) {
    uint32_t id = image.id;
    if (id == 0) {
        id = next_id_++;
    }
    image.id = id;
    images_[id] = std::move(image);
    return id;
}

void KittyGraphicsManager::addPlacement(const KittyPlacement& placement) {
    placements_.push_back(placement);
}

void KittyGraphicsManager::setCursorPosition(int col, int64_t absolute_row) {
    cursor_col_ = col;
    cursor_absolute_row_ = absolute_row;
}

void KittyGraphicsManager::evictPlacementsBefore(int64_t oldest_absolute_row) {
    placements_.erase(
        std::remove_if(placements_.begin(), placements_.end(),
                        [oldest_absolute_row](const KittyPlacement& p) {
                            return p.absolute_row < oldest_absolute_row;
                        }),
        placements_.end());
}

} // namespace termcore
