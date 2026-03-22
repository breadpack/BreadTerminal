#ifndef TERMCORE_KITTY_GRAPHICS_H
#define TERMCORE_KITTY_GRAPHICS_H
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

struct KittyImage {
    uint32_t id = 0;
    int width = 0, height = 0;
    int format = 32; // 24=RGB, 32=RGBA, 100=PNG
    std::vector<uint8_t> data; // Raw pixel data (decoded)
    bool complete = false;
};

struct KittyPlacement {
    uint32_t image_id = 0;
    uint32_t placement_id = 0;
    int x = 0, y = 0; // Cell position
    int src_x = 0, src_y = 0; // Source crop
    int src_w = 0, src_h = 0;
    int cols = 0, rows = 0; // Display size in cells
    int z_index = 0;
};

/// Manages Kitty graphics images and placements
class KittyGraphicsManager {
public:
    KittyGraphicsManager() = default;

    /// Process a Kitty graphics command (parsed from APC sequence)
    /// control: the key=value part, payload: base64 data
    /// Returns response string to send back to PTY (or empty)
    std::string processCommand(const std::string& control, const std::string& payload);

    /// Get stored image by ID
    const KittyImage* getImage(uint32_t id) const;

    /// Get all placements
    const std::vector<KittyPlacement>& placements() const { return placements_; }

    /// Get image count
    size_t imageCount() const { return images_.size(); }

    /// Clear all images and placements
    void clear();

    /// Delete images/placements by criteria
    void deleteByImageId(uint32_t id);

    /// Add a pre-built image (used by iTerm2 protocol integration).
    /// Returns the assigned image ID.
    uint32_t addImage(KittyImage image);

    /// Add a placement directly (used by iTerm2 protocol integration).
    void addPlacement(const KittyPlacement& placement);

private:
    struct PendingTransmit {
        KittyImage image;
        std::string accumulated_payload;
    };

    std::unordered_map<std::string, std::string> parseControl(const std::string& control);
    std::vector<uint8_t> base64Decode(const std::string& input);
    void handleTransmit(const std::unordered_map<std::string, std::string>& params,
                        const std::string& payload, bool display);
    void handleDisplay(const std::unordered_map<std::string, std::string>& params);
    void handleDelete(const std::unordered_map<std::string, std::string>& params);

    std::unordered_map<uint32_t, KittyImage> images_;
    std::vector<KittyPlacement> placements_;
    PendingTransmit pending_;
    uint32_t next_id_ = 1;
};

} // namespace termcore
#endif
