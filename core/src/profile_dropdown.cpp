#include "termcore/profile_dropdown.h"

#include <algorithm>

namespace termcore {

void ProfileDropdown::open(std::vector<ProfileDropdownItem> items) {
    items_ = std::move(items);
    selectedIndex_ = 0;
    open_ = true;
}

void ProfileDropdown::close() {
    open_ = false;
    items_.clear();
    selectedIndex_ = 0;
}

void ProfileDropdown::selectNext() {
    if (!items_.empty()) {
        selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(items_.size());
    }
}

void ProfileDropdown::selectPrev() {
    if (!items_.empty()) {
        selectedIndex_ = (selectedIndex_ - 1 + static_cast<int>(items_.size()))
                         % static_cast<int>(items_.size());
    }
}

std::string ProfileDropdown::selectedProfileId() const {
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(items_.size())) {
        return items_[selectedIndex_].id;
    }
    return {};
}

} // namespace termcore
