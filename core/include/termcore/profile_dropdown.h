#ifndef TERMCORE_PROFILE_DROPDOWN_H
#define TERMCORE_PROFILE_DROPDOWN_H

#include <string>
#include <vector>

namespace termcore {

/// A single entry in the profile dropdown overlay.
struct ProfileDropdownItem {
    std::string id;
    std::string name;
    std::string icon;
};

/// Simple dropdown overlay for selecting a profile when creating a new tab.
/// Follows the same open/close/select pattern as CommandPalette.
class ProfileDropdown {
public:
    void open(std::vector<ProfileDropdownItem> items);
    void close();
    bool isOpen() const { return open_; }

    void selectNext();
    void selectPrev();
    int selectedIndex() const { return selectedIndex_; }

    /// Returns the profile ID of the currently selected item, or empty string.
    std::string selectedProfileId() const;

    const std::vector<ProfileDropdownItem>& items() const { return items_; }

    static constexpr int kMaxVisibleItems = 9;

private:
    bool open_ = false;
    int selectedIndex_ = 0;
    std::vector<ProfileDropdownItem> items_;
};

} // namespace termcore

#endif // TERMCORE_PROFILE_DROPDOWN_H
