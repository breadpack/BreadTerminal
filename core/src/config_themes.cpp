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
    {
        "Tokyo Night",
        0x1a1b26, // background
        0xa9b1d6, // foreground
        0xc0caf5, // cursor_color
        0x33467c, // selection_background
        0xc0caf5, // selection_foreground
        {
            0x15161e, 0xf7768e, 0x9ece6a, 0xe0af68,
            0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xa9b1d6,
            0x414868, 0xf7768e, 0x9ece6a, 0xe0af68,
            0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xc0caf5,
        },
    },
    {
        "Rose Pine",
        0x191724, // background
        0xe0def4, // foreground
        0x56526e, // cursor_color
        0x2a2837, // selection_background
        0xe0def4, // selection_foreground
        {
            0x26233a, 0xeb6f92, 0x31748f, 0xf6c177,
            0x9ccfd8, 0xc4a7e7, 0xebbcba, 0xe0def4,
            0x6e6a86, 0xeb6f92, 0x31748f, 0xf6c177,
            0x9ccfd8, 0xc4a7e7, 0xebbcba, 0xe0def4,
        },
    },
    {
        "Everforest Dark",
        0x2d353b, // background
        0xd3c6aa, // foreground
        0xd3c6aa, // cursor_color
        0x543a48, // selection_background
        0xd3c6aa, // selection_foreground
        {
            0x343f44, 0xe67e80, 0xa7c080, 0xdbbc7f,
            0x7fbbb3, 0xd699b6, 0x83c092, 0xd3c6aa,
            0x475258, 0xe67e80, 0xa7c080, 0xdbbc7f,
            0x7fbbb3, 0xd699b6, 0x83c092, 0xd3c6aa,
        },
    },
    {
        "Kanagawa",
        0x1f1f28, // background
        0xdcd7ba, // foreground
        0xc8c093, // cursor_color
        0x2d4f67, // selection_background
        0xc8c093, // selection_foreground
        {
            0x16161d, 0xc34043, 0x76946a, 0xc0a36e,
            0x7e9cd8, 0x957fb8, 0x6a9589, 0xc8c093,
            0x727169, 0xe82424, 0x98bb6c, 0xe6c384,
            0x7fb4ca, 0x938aa9, 0x7aa89f, 0xdcd7ba,
        },
    },
    {
        "Monokai Pro",
        0x2d2a2e, // background
        0xfcfcfa, // foreground
        0xfcfcfa, // cursor_color
        0x403e41, // selection_background
        0xfcfcfa, // selection_foreground
        {
            0x403e41, 0xff6188, 0xa9dc76, 0xffd866,
            0xfc9867, 0xab9df2, 0x78dce8, 0xfcfcfa,
            0x727072, 0xff6188, 0xa9dc76, 0xffd866,
            0xfc9867, 0xab9df2, 0x78dce8, 0xfcfcfa,
        },
    },
    // --- High Contrast Accessibility Themes ---
    {
        "High Contrast Dark",
        0x000000, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x264f78, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0xff0000, 0x00ff00, 0xffff00,
            0x0080ff, 0xff00ff, 0x00ffff, 0xffffff,
            0x808080, 0xff5555, 0x55ff55, 0xffff55,
            0x5599ff, 0xff55ff, 0x55ffff, 0xffffff,
        },
    },
    {
        "High Contrast Yellow on Black",
        0x000000, // background
        0xffff00, // foreground
        0xffff00, // cursor_color
        0x444400, // selection_background
        0xffff00, // selection_foreground
        {
            0x000000, 0xff0000, 0x00ff00, 0xffff00,
            0x0080ff, 0xff00ff, 0x00ffff, 0xffff00,
            0x808000, 0xff5555, 0x55ff55, 0xffff55,
            0x5599ff, 0xff55ff, 0x55ffff, 0xffffff,
        },
    },
    {
        "High Contrast Green on Black",
        0x000000, // background
        0x00ff00, // foreground
        0x00ff00, // cursor_color
        0x004400, // selection_background
        0x00ff00, // selection_foreground
        {
            0x000000, 0xff0000, 0x00ff00, 0xffff00,
            0x0080ff, 0xff00ff, 0x00ffff, 0x00ff00,
            0x008000, 0xff5555, 0x55ff55, 0xffff55,
            0x5599ff, 0xff55ff, 0x55ffff, 0xffffff,
        },
    },
    // --- Light themes ---
    {
        "High Contrast Light",
        0xffffff, // background
        0x000000, // foreground
        0x000000, // cursor_color
        0xadd6ff, // selection_background
        0x000000, // selection_foreground
        {
            0x000000, 0xcd0000, 0x00aa00, 0xaa5500,
            0x0000cd, 0xaa00aa, 0x00aaaa, 0xe5e5e5,
            0x666666, 0xff0000, 0x00ff00, 0xffaa00,
            0x0000ff, 0xff00ff, 0x00ffff, 0xffffff,
        },
    },
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
    {
        "One Light",
        0xfafafa, // background
        0x383a42, // foreground
        0x526fff, // cursor_color
        0xe5e5e6, // selection_background
        0x383a42, // selection_foreground
        {
            0x383a42, 0xe45649, 0x50a14f, 0xc18401,
            0x4078f2, 0xa626a4, 0x0184bc, 0xa0a1a7,
            0x696c77, 0xe45649, 0x50a14f, 0xc18401,
            0x4078f2, 0xa626a4, 0x0184bc, 0xfafafa,
        },
    },
    {
        "Tokyo Night Light",
        0xd5d6db, // background
        0x343b59, // foreground
        0x343b59, // cursor_color
        0x99a7df, // selection_background
        0x343b59, // selection_foreground
        {
            0x0f0f14, 0x8c4351, 0x33635c, 0x8f5e15,
            0x34548a, 0x5a4a78, 0x0f4b6e, 0x343b59,
            0x9699a3, 0x8c4351, 0x33635c, 0x8f5e15,
            0x34548a, 0x5a4a78, 0x0f4b6e, 0xd5d6db,
        },
    },
    {
        "Rose Pine Dawn",
        0xfaf4ed, // background
        0x575279, // foreground
        0x56526e, // cursor_color
        0xdfdad9, // selection_background
        0x575279, // selection_foreground
        {
            0xf2e9e1, 0xb4637a, 0x286983, 0xea9d34,
            0x56949f, 0x907aa9, 0xd7827e, 0x575279,
            0x9893a5, 0xb4637a, 0x286983, 0xea9d34,
            0x56949f, 0x907aa9, 0xd7827e, 0x575279,
        },
    },
    {
        "Everforest Light",
        0xfdf6e3, // background
        0x5c6a72, // foreground
        0x5c6a72, // cursor_color
        0xe6e2cc, // selection_background
        0x5c6a72, // selection_foreground
        {
            0x5c6a72, 0xf85552, 0x8da101, 0xdfa000,
            0x3a94c5, 0xdf69ba, 0x35a77c, 0xdfddc8,
            0x939f91, 0xf85552, 0x8da101, 0xdfa000,
            0x3a94c5, 0xdf69ba, 0x35a77c, 0xfdf6e3,
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
