#include "termcore/config.h"

#include <algorithm>

namespace termcore {

namespace {

// clang-format off
static const Theme kBuiltinThemes[] = {
    {
        "Dracula",
        0x282a36, // background
        0xf8f8f2, // foreground
        0xf8f8f2, // cursor_color
        0x44475a, // selection_background
        0xf8f8f2, // selection_foreground
        {
            0x21222c, 0xff5555, 0x50fa7b, 0xf1fa8c,
            0xbd93f9, 0xff79c6, 0x8be9fd, 0xf8f8f2,
            0x6272a4, 0xff6e6e, 0x69ff94, 0xffffa5,
            0xd6acff, 0xff92df, 0xa4ffff, 0xffffff,
        },
    },
    {
        "One Dark",
        0x282c34, // background
        0xabb2bf, // foreground
        0x528bff, // cursor_color
        0x3e4451, // selection_background
        0xabb2bf, // selection_foreground
        {
            0x282c34, 0xe06c75, 0x98c379, 0xe5c07b,
            0x61afef, 0xc678dd, 0x56b6c2, 0xabb2bf,
            0x545862, 0xe06c75, 0x98c379, 0xe5c07b,
            0x61afef, 0xc678dd, 0x56b6c2, 0xc8ccd4,
        },
    },
    {
        "Catppuccin Mocha",
        0x1e1e2e, // background
        0xcdd6f4, // foreground
        0xf5e0dc, // cursor_color
        0x585b70, // selection_background
        0xcdd6f4, // selection_foreground
        {
            0x45475a, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
            0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xbac2de,
            0x585b70, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
            0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xa6adc8,
        },
    },
    {
        "Solarized Dark",
        0x002b36, // background
        0x839496, // foreground
        0x93a1a1, // cursor_color
        0x073642, // selection_background
        0x93a1a1, // selection_foreground
        {
            0x073642, 0xdc322f, 0x859900, 0xb58900,
            0x268bd2, 0xd33682, 0x2aa198, 0xeee8d5,
            0x002b36, 0xcb4b16, 0x586e75, 0x657b83,
            0x839496, 0x6c71c4, 0x93a1a1, 0xfdf6e3,
        },
    },
    {
        "Nord",
        0x2e3440, // background
        0xd8dee9, // foreground
        0xd8dee9, // cursor_color
        0x434c5e, // selection_background
        0xd8dee9, // selection_foreground
        {
            0x3b4252, 0xbf616a, 0xa3be8c, 0xebcb8b,
            0x81a1c1, 0xb48ead, 0x88c0d0, 0xe5e9f0,
            0x4c566a, 0xbf616a, 0xa3be8c, 0xebcb8b,
            0x81a1c1, 0xb48ead, 0x8fbcbb, 0xeceff4,
        },
    },
    {
        "Gruvbox Dark",
        0x282828, // background
        0xebdbb2, // foreground
        0xebdbb2, // cursor_color
        0x504945, // selection_background
        0xebdbb2, // selection_foreground
        {
            0x282828, 0xcc241d, 0x98971a, 0xd79921,
            0x458588, 0xb16286, 0x689d6a, 0xa89984,
            0x928374, 0xfb4934, 0xb8bb26, 0xfabd2f,
            0x83a598, 0xd3869b, 0x8ec07c, 0xebdbb2,
        },
    },
    // --- Light themes ---
    {
        "Solarized Light",
        0xfdf6e3, // background
        0x657b83, // foreground
        0x586e75, // cursor_color
        0xeee8d5, // selection_background
        0x586e75, // selection_foreground
        {
            0x073642, 0xdc322f, 0x859900, 0xb58900,
            0x268bd2, 0xd33682, 0x2aa198, 0xeee8d5,
            0x002b36, 0xcb4b16, 0x586e75, 0x657b83,
            0x839496, 0x6c71c4, 0x93a1a1, 0xfdf6e3,
        },
    },
    {
        "Catppuccin Latte",
        0xeff1f5, // background
        0x4c4f69, // foreground
        0xdc8a78, // cursor_color
        0xacb0be, // selection_background
        0x4c4f69, // selection_foreground
        {
            0x5c5f77, 0xd20f39, 0x40a02b, 0xdf8e1d,
            0x1e66f5, 0xea76cb, 0x179299, 0xbcc0cc,
            0x6c6f85, 0xd20f39, 0x40a02b, 0xdf8e1d,
            0x1e66f5, 0xea76cb, 0x179299, 0xacb0be,
        },
    },
    {
        "GitHub Light",
        0xffffff, // background
        0x24292e, // foreground
        0x044289, // cursor_color
        0xc8c8fa, // selection_background
        0x24292e, // selection_foreground
        {
            0x24292e, 0xcf222e, 0x116329, 0x4d2d00,
            0x0550ae, 0x8250df, 0x1b7c83, 0x6e7781,
            0x57606a, 0xa40e26, 0x1a7f37, 0x633c01,
            0x0969da, 0x6639ba, 0x3192aa, 0x8c959f,
        },
    },
    {
        "Gruvbox Light",
        0xfbf1c7, // background
        0x3c3836, // foreground
        0x3c3836, // cursor_color
        0xd5c4a1, // selection_background
        0x3c3836, // selection_foreground
        {
            0xfbf1c7, 0xcc241d, 0x98971a, 0xd79921,
            0x458588, 0xb16286, 0x689d6a, 0x7c6f64,
            0x928374, 0x9d0006, 0x79740e, 0xb57614,
            0x076678, 0x8f3f71, 0x427b58, 0x3c3836,
        },
    },
};
// clang-format on

} // namespace

const Theme* getBuiltinTheme(const std::string& name) {
    for (const auto& theme : kBuiltinThemes) {
        if (theme.name == name) {
            return &theme;
        }
    }
    return nullptr;
}

std::vector<std::string> listBuiltinThemes() {
    std::vector<std::string> names;
    names.reserve(std::size(kBuiltinThemes));
    for (const auto& theme : kBuiltinThemes) {
        names.push_back(theme.name);
    }
    return names;
}

} // namespace termcore
