#include "termcore/config.h"

#include <algorithm>

namespace termcore {

namespace {

// clang-format off
static const Theme kBuiltinThemes[] = {
    // =========================================================================
    // Classic & Retro
    // =========================================================================
    {
        "Afterglow",
        0x212121, // background
        0xd0d0d0, // foreground
        0xd0d0d0, // cursor_color
        0x303030, // selection_background
        0xd0d0d0, // selection_foreground
        {
            0x151515, 0xac4142, 0x7e8e50, 0xe5b567,
            0x6c99bb, 0x9f4e85, 0x7dd6cf, 0xd0d0d0,
            0x505050, 0xac4142, 0x7e8e50, 0xe5b567,
            0x6c99bb, 0x9f4e85, 0x7dd6cf, 0xf5f5f5,
        },
    },
    {
        "Argonaut",
        0x0e1019, // background
        0xfffaf4, // foreground
        0xfffaf4, // cursor_color
        0x002a3a, // selection_background
        0xfffaf4, // selection_foreground
        {
            0x232323, 0xff000f, 0x8ce10b, 0xffb900,
            0x008df8, 0x6d43a6, 0x00d8eb, 0xffffff,
            0x444444, 0xff2740, 0xabe15b, 0xffd242,
            0x0092ff, 0x9a5feb, 0x67fff0, 0xffffff,
        },
    },
    {
        "Grass",
        0x13773d, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x1b4f2a, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0xc60001, 0x00c200, 0xc7c400,
            0x0225c7, 0xc7007b, 0x00c5c7, 0xc7c7c7,
            0x686868, 0xff6e67, 0x5ffa68, 0xfffc67,
            0x6871ff, 0xff77ff, 0x60fdff, 0xffffff,
        },
    },
    {
        "Homebrew",
        0x000000, // background
        0x00ff00, // foreground
        0x00ff00, // cursor_color
        0x003600, // selection_background
        0x00ff00, // selection_foreground
        {
            0x000000, 0x990000, 0x00a600, 0x999900,
            0x0000b2, 0xb200b2, 0x00a6b2, 0xbfbfbf,
            0x666666, 0xe50000, 0x00d900, 0xe5e500,
            0x0000ff, 0xe500e5, 0x00e5e5, 0xe5e5e5,
        },
    },
    {
        "Novel",
        0xdfdbc3, // background
        0x3b2322, // foreground
        0x3b2322, // cursor_color
        0xc8bfa6, // selection_background
        0x3b2322, // selection_foreground
        {
            0x000000, 0xcc0000, 0x009600, 0xd06b00,
            0x0000cc, 0xcc00cc, 0x0096cc, 0xa5a2a2,
            0x666666, 0xe50000, 0x00d900, 0xe5e500,
            0x0000ff, 0xe500e5, 0x00e5e5, 0xe5e5e5,
        },
    },
    {
        "Ocean",
        0x224fbc, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x1b3d8a, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0x990000, 0x00a600, 0x999900,
            0x0000b2, 0xb200b2, 0x00a6b2, 0xbfbfbf,
            0x666666, 0xe50000, 0x00d900, 0xe5e500,
            0x0000ff, 0xe500e5, 0x00e5e5, 0xe5e5e5,
        },
    },
    {
        "Pro",
        0x000000, // background
        0xf2f2f2, // foreground
        0x4d4d4d, // cursor_color
        0x414141, // selection_background
        0xf2f2f2, // selection_foreground
        {
            0x000000, 0x990000, 0x00a600, 0x999900,
            0x2009db, 0xb200b2, 0x00a6b2, 0xbfbfbf,
            0x666666, 0xe50000, 0x00d900, 0xe5e500,
            0x0000ff, 0xe500e5, 0x00e5e5, 0xe5e5e5,
        },
    },
    {
        "Red Sands",
        0x7a251e, // background
        0xd7c9a7, // foreground
        0xd7c9a7, // cursor_color
        0x5a1a15, // selection_background
        0xd7c9a7, // selection_foreground
        {
            0x000000, 0xff3f00, 0x00bb00, 0xe7b000,
            0x0072ff, 0xbb00bb, 0x00bbbb, 0xbbbbbb,
            0x555555, 0xbb0000, 0x00bb00, 0xe7b000,
            0x0072ae, 0xff55ff, 0x55ffff, 0xffffff,
        },
    },
    {
        "Tango Dark",
        0x000000, // background
        0xd3d7cf, // foreground
        0xd3d7cf, // cursor_color
        0x2e3436, // selection_background
        0xd3d7cf, // selection_foreground
        {
            0x2e3436, 0xcc0000, 0x4e9a06, 0xc4a000,
            0x3465a4, 0x75507b, 0x06989a, 0xd3d7cf,
            0x555753, 0xef2929, 0x8ae234, 0xfce94f,
            0x729fcf, 0xad7fa8, 0x34e2e2, 0xeeeeec,
        },
    },
    {
        "Tango Light",
        0xffffff, // background
        0x2e3436, // foreground
        0x2e3436, // cursor_color
        0xd3d7cf, // selection_background
        0x2e3436, // selection_foreground
        {
            0x2e3436, 0xcc0000, 0x4e9a06, 0xc4a000,
            0x3465a4, 0x75507b, 0x06989a, 0xd3d7cf,
            0x555753, 0xef2929, 0x8ae234, 0xfce94f,
            0x729fcf, 0xad7fa8, 0x34e2e2, 0xeeeeec,
        },
    },
    {
        "Ubuntu",
        0x300a24, // background
        0xeeeeec, // foreground
        0xeeeeec, // cursor_color
        0x49224a, // selection_background
        0xeeeeec, // selection_foreground
        {
            0x2e3436, 0xcc0000, 0x4e9a06, 0xc4a000,
            0x3465a4, 0x75507b, 0x06989a, 0xd3d7cf,
            0x555753, 0xef2929, 0x8ae234, 0xfce94f,
            0x729fcf, 0xad7fa8, 0x34e2e2, 0xeeeeec,
        },
    },
    // =========================================================================
    // Popular Dark Themes
    // =========================================================================
    {
        "Aura Dark",
        0x15141b, // background
        0xedecee, // foreground
        0xa277ff, // cursor_color
        0x29263c, // selection_background
        0xedecee, // selection_foreground
        {
            0x110f18, 0xff6767, 0x61ffca, 0xffca85,
            0xa277ff, 0xa277ff, 0x61ffca, 0xedecee,
            0x4d4d4d, 0xff6767, 0x61ffca, 0xffca85,
            0xa277ff, 0xa277ff, 0x61ffca, 0xedecee,
        },
    },
    {
        "Ayu Dark",
        0x0a0e14, // background
        0xb3b1ad, // foreground
        0xe6b450, // cursor_color
        0x273747, // selection_background
        0xb3b1ad, // selection_foreground
        {
            0x01060e, 0xea6c73, 0x91b362, 0xf9af4f,
            0x53bdfa, 0xfae994, 0x90e1c6, 0xc7c7c7,
            0x686868, 0xf07178, 0xc2d94c, 0xffb454,
            0x59c2ff, 0xffee99, 0x95e6cb, 0xffffff,
        },
    },
    {
        "Ayu Mirage",
        0x1f2430, // background
        0xcbccc6, // foreground
        0xffcc66, // cursor_color
        0x34455a, // selection_background
        0xcbccc6, // selection_foreground
        {
            0x191e2a, 0xed8274, 0xa6cc70, 0xfad07b,
            0x6dcbfa, 0xcfbafa, 0x90e1c6, 0xc7c7c7,
            0x686868, 0xf28779, 0xbae67e, 0xffd580,
            0x73d0ff, 0xd4bfff, 0x95e6cb, 0xffffff,
        },
    },
    {
        "Bamboo Dark",
        0x252623, // background
        0xe0dcc7, // foreground
        0xe0dcc7, // cursor_color
        0x3b3d37, // selection_background
        0xe0dcc7, // selection_foreground
        {
            0x1c1e1b, 0xe68183, 0xa7c080, 0xdbbc7f,
            0x7fbbb3, 0xd699b6, 0x83c092, 0xe0dcc7,
            0x5c6152, 0xe68183, 0xa7c080, 0xdbbc7f,
            0x7fbbb3, 0xd699b6, 0x83c092, 0xe0dcc7,
        },
    },
    {
        "Bluloco Dark",
        0x282c34, // background
        0xabb2bf, // foreground
        0xffcc00, // cursor_color
        0x3d4455, // selection_background
        0xabb2bf, // selection_foreground
        {
            0x41444d, 0xfc2f52, 0x25a45c, 0xff936a,
            0x3476ff, 0x7a82da, 0x4fa5a5, 0xabb2bf,
            0x6b7089, 0xff6480, 0x3fc56b, 0xf9c859,
            0x10b1fe, 0xff78f8, 0x5fb9bc, 0xffffff,
        },
    },
    {
        "Catppuccin Frappe",
        0x303446, // background
        0xc6d0f5, // foreground
        0xf2d5cf, // cursor_color
        0x51576d, // selection_background
        0xc6d0f5, // selection_foreground
        {
            0x51576d, 0xe78284, 0xa6d189, 0xe5c890,
            0x8caaee, 0xf4b8e4, 0x81c8be, 0xb5bfe2,
            0x626880, 0xe78284, 0xa6d189, 0xe5c890,
            0x8caaee, 0xf4b8e4, 0x81c8be, 0xa5adce,
        },
    },
    {
        "Catppuccin Macchiato",
        0x24273a, // background
        0xcad3f5, // foreground
        0xf4dbd6, // cursor_color
        0x494d64, // selection_background
        0xcad3f5, // selection_foreground
        {
            0x494d64, 0xed8796, 0xa6da95, 0xeed49f,
            0x8aadf4, 0xf5bde6, 0x8bd5ca, 0xb8c0e0,
            0x5b6078, 0xed8796, 0xa6da95, 0xeed49f,
            0x8aadf4, 0xf5bde6, 0x8bd5ca, 0xa5adcb,
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
        "Cobalt2",
        0x132738, // background
        0xffffff, // foreground
        0xf0cc09, // cursor_color
        0x18354f, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0xff0000, 0x38de21, 0xffe50a,
            0x1460d2, 0xff005d, 0x00bbbb, 0xbbbbbb,
            0x555555, 0xf40e17, 0x3bd01d, 0xedc809,
            0x5555ff, 0xff55ff, 0x6ae3fa, 0xffffff,
        },
    },
    {
        "Cyberdream",
        0x16181a, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x3c4048, // selection_background
        0xffffff, // selection_foreground
        {
            0x16181a, 0xff6e5e, 0x5eff6c, 0xf1ff5e,
            0x5ea1ff, 0xbd5eff, 0x5ef1ff, 0xffffff,
            0x3c4048, 0xff6e5e, 0x5eff6c, 0xf1ff5e,
            0x5ea1ff, 0xbd5eff, 0x5ef1ff, 0xffffff,
        },
    },
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
        "Duskfox",
        0x232136, // background
        0xe0def4, // foreground
        0xe0def4, // cursor_color
        0x433c59, // selection_background
        0xe0def4, // selection_foreground
        {
            0x393552, 0xeb6f92, 0xa3be8c, 0xf6c177,
            0x569fba, 0xc4a7e7, 0x9ccfd8, 0xe0def4,
            0x6e6a86, 0xeb6f92, 0xa3be8c, 0xf6c177,
            0x569fba, 0xc4a7e7, 0x9ccfd8, 0xe0def4,
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
        "Fairy Floss",
        0x5a5475, // background
        0xf8f8f2, // foreground
        0xf8f8f2, // cursor_color
        0x716799, // selection_background
        0xf8f8f2, // selection_foreground
        {
            0x040303, 0xf92672, 0xc2ffdf, 0xe6c000,
            0xc2ffdf, 0xffb8d1, 0xc5a3ff, 0xf8f8f2,
            0x6090cb, 0xff857f, 0xc2ffdf, 0xfff352,
            0xc2ffdf, 0xffb8d1, 0xc5a3ff, 0xf8f8f2,
        },
    },
    {
        "GitHub Dark",
        0x24292e, // background
        0xd1d5da, // foreground
        0xd1d5da, // cursor_color
        0x3a3f47, // selection_background
        0xd1d5da, // selection_foreground
        {
            0x586069, 0xea4a5a, 0x34d058, 0xffea7f,
            0x2188ff, 0xb392f0, 0x39c5cf, 0xd1d5da,
            0x959da5, 0xf97583, 0x85e89d, 0xffea7f,
            0x79b8ff, 0xb392f0, 0x56d4dd, 0xfafbfc,
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
        "Horizon Dark",
        0x1c1e26, // background
        0xe0e0e0, // foreground
        0xe0e0e0, // cursor_color
        0x2e303e, // selection_background
        0xe0e0e0, // selection_foreground
        {
            0x16161c, 0xe95678, 0x29d398, 0xfab795,
            0x26bbd9, 0xee64ac, 0x59e1e3, 0xd5d8da,
            0x232530, 0xec6a88, 0x3fdaa4, 0xfbc3a7,
            0x3fc4de, 0xf075b5, 0x6be4e6, 0xe3e6ee,
        },
    },
    {
        "Iceberg Dark",
        0x161821, // background
        0xc6c8d1, // foreground
        0xc6c8d1, // cursor_color
        0x1e2132, // selection_background
        0xc6c8d1, // selection_foreground
        {
            0x161821, 0xe27878, 0xb4be82, 0xe2a478,
            0x84a0c6, 0xa093c7, 0x89b8c2, 0xc6c8d1,
            0x6b7089, 0xe98989, 0xc0ca8e, 0xe9b189,
            0x91acd1, 0xada0d3, 0x95c4ce, 0xd2d4de,
        },
    },
    {
        "Kanagawa",
        0x1f1f28, // background
        0xdcd7ba, // foreground
        0xdcd7ba, // cursor_color
        0x2d4f67, // selection_background
        0xdcd7ba, // selection_foreground
        {
            0x16161d, 0xc34043, 0x76946a, 0xc0a36e,
            0x7e9cd8, 0x957fb8, 0x6a9589, 0xc8c093,
            0x727169, 0xe82424, 0x98bb6c, 0xe6c384,
            0x7fb4ca, 0x938aa9, 0x7aa89f, 0xdcd7ba,
        },
    },
    {
        "Laserwave",
        0x27212e, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x463465, // selection_background
        0xffffff, // selection_foreground
        {
            0x27212e, 0xf92672, 0x72f1b8, 0xfede5d,
            0x40b4c4, 0xff7edb, 0xb6f292, 0xffffff,
            0x6b6079, 0xf92672, 0x72f1b8, 0xfede5d,
            0x40b4c4, 0xff7edb, 0xb6f292, 0xffffff,
        },
    },
    {
        "Material Dark",
        0x263238, // background
        0xeeffff, // foreground
        0xffcc00, // cursor_color
        0x3c4d56, // selection_background
        0xeeffff, // selection_foreground
        {
            0x546e7a, 0xff5370, 0xc3e88d, 0xffcb6b,
            0x82aaff, 0xc792ea, 0x89ddff, 0xeeffff,
            0x546e7a, 0xff5370, 0xc3e88d, 0xffcb6b,
            0x82aaff, 0xc792ea, 0x89ddff, 0xeeffff,
        },
    },
    {
        "Material Oceanic",
        0x0f111a, // background
        0x8f93a2, // foreground
        0xffcc00, // cursor_color
        0x1f2233, // selection_background
        0x8f93a2, // selection_foreground
        {
            0x464b5d, 0xff5370, 0xc3e88d, 0xffcb6b,
            0x82aaff, 0xc792ea, 0x89ddff, 0x8f93a2,
            0x717cb4, 0xff5370, 0xc3e88d, 0xffcb6b,
            0x82aaff, 0xc792ea, 0x89ddff, 0xffffff,
        },
    },
    {
        "Melange Dark",
        0x292522, // background
        0xece1d7, // foreground
        0xece1d7, // cursor_color
        0x403a36, // selection_background
        0xece1d7, // selection_foreground
        {
            0x34302c, 0xbd8183, 0x78997a, 0xebc06d,
            0x86a3a3, 0xb380b0, 0x729893, 0xc1a78e,
            0x867462, 0xd47766, 0x85b695, 0xebc06d,
            0x86a3a3, 0xb380b0, 0x729893, 0xece1d7,
        },
    },
    {
        "Mellow",
        0x161617, // background
        0xc9c7cd, // foreground
        0xc9c7cd, // cursor_color
        0x28282a, // selection_background
        0xc9c7cd, // selection_foreground
        {
            0x1e1e20, 0xf5a191, 0xb1e3ad, 0xf0c6a8,
            0xa3b8ef, 0xecaad6, 0xb2daf4, 0xc9c7cd,
            0x35353a, 0xf5a191, 0xb1e3ad, 0xf0c6a8,
            0xa3b8ef, 0xecaad6, 0xb2daf4, 0xc9c7cd,
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
    {
        "Moonfly",
        0x080808, // background
        0xb2b2b2, // foreground
        0x9e9e9e, // cursor_color
        0xb2ceee, // selection_background
        0x080808, // selection_foreground
        {
            0x323437, 0xff5454, 0x8cc85f, 0xe3c78a,
            0x80a0ff, 0xd183e8, 0x79dac8, 0xc6c6c6,
            0x949494, 0xff5189, 0x36c692, 0xbfbf97,
            0x74b2ff, 0xae81ff, 0x85dc85, 0xe4e4e4,
        },
    },
    {
        "Night Owl",
        0x011627, // background
        0xd6deeb, // foreground
        0x80a4c2, // cursor_color
        0x1d3b53, // selection_background
        0xd6deeb, // selection_foreground
        {
            0x011627, 0xef5350, 0x22da6e, 0xaddb67,
            0x82aaff, 0xc792ea, 0x21c7a8, 0xd6deeb,
            0x637777, 0xef5350, 0x22da6e, 0xffeb95,
            0x82aaff, 0xc792ea, 0x7fdbca, 0xffffff,
        },
    },
    {
        "Nightfox",
        0x192330, // background
        0xcdcecf, // foreground
        0xcdcecf, // cursor_color
        0x283648, // selection_background
        0xcdcecf, // selection_foreground
        {
            0x393b44, 0xc94f6d, 0x81b29a, 0xdbc074,
            0x719cd6, 0x9d79d6, 0x63cdcf, 0xdfdfe0,
            0x575860, 0xd16983, 0x8ebaa4, 0xe0c989,
            0x86abdc, 0xbaa1e2, 0x7ad4d6, 0xe4e4e5,
        },
    },
    {
        "Noctis",
        0x292d36, // background
        0xb2cacd, // foreground
        0xb2cacd, // cursor_color
        0x3b4048, // selection_background
        0xb2cacd, // selection_foreground
        {
            0x1b2932, 0xc94922, 0x85b654, 0xf2a766,
            0x5e97c4, 0xb07ec8, 0x40bfb0, 0xb2cacd,
            0x4d6470, 0xd57250, 0xa0c975, 0xf4bf86,
            0x7db1de, 0xc59ad7, 0x5ed4c6, 0xd4e8ea,
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
        "Nordfox",
        0x2e3440, // background
        0xcdcecf, // foreground
        0xcdcecf, // cursor_color
        0x3e4a5b, // selection_background
        0xcdcecf, // selection_foreground
        {
            0x3b4252, 0xbf616a, 0xa3be8c, 0xebcb8b,
            0x81a1c1, 0xb48ead, 0x88c0d0, 0xc9d1d9,
            0x4c566a, 0xd06f79, 0xb1d196, 0xf0d399,
            0x8cafd2, 0xc895bf, 0x93ccdc, 0xe5e9f0,
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
        "Oxocarbon Dark",
        0x161616, // background
        0xf2f4f8, // foreground
        0xf2f4f8, // cursor_color
        0x353535, // selection_background
        0xf2f4f8, // selection_foreground
        {
            0x262626, 0xee5396, 0x42be65, 0xffe97b,
            0x33b1ff, 0xbe95ff, 0x3ddbd9, 0xdde1e6,
            0x393939, 0xee5396, 0x42be65, 0xffe97b,
            0x33b1ff, 0xbe95ff, 0x3ddbd9, 0xf2f4f8,
        },
    },
    {
        "Palenight",
        0x292d3e, // background
        0xa6accd, // foreground
        0xffcc00, // cursor_color
        0x3c435e, // selection_background
        0xa6accd, // selection_foreground
        {
            0x292d3e, 0xff5370, 0xc3e88d, 0xffcb6b,
            0x82aaff, 0xc792ea, 0x89ddff, 0xa6accd,
            0x676e95, 0xff5370, 0xc3e88d, 0xffcb6b,
            0x82aaff, 0xc792ea, 0x89ddff, 0xffffff,
        },
    },
    {
        "Pencil Dark",
        0x212121, // background
        0xf1f1f1, // foreground
        0xf1f1f1, // cursor_color
        0x424242, // selection_background
        0xf1f1f1, // selection_foreground
        {
            0x212121, 0xc30771, 0x10a778, 0xa89c14,
            0x008ec4, 0x523c79, 0x20a5ba, 0xd9d9d9,
            0x424242, 0xfb007a, 0x5fd7af, 0xf3e430,
            0x20bbfc, 0x6855de, 0x4fb8cc, 0xf1f1f1,
        },
    },
    {
        "Poimandres",
        0x1b1e28, // background
        0xa6accd, // foreground
        0xa6accd, // cursor_color
        0x303340, // selection_background
        0xa6accd, // selection_foreground
        {
            0x1b1e28, 0xd0679d, 0x5de4c7, 0xfffac2,
            0x89ddff, 0xfcc5e9, 0x89ddff, 0xa6accd,
            0x506477, 0xd0679d, 0x5de4c7, 0xfffac2,
            0x89ddff, 0xfcc5e9, 0xadd7ff, 0xe4f0fb,
        },
    },
    {
        "Rose Pine",
        0x191724, // background
        0xe0def4, // foreground
        0x555169, // cursor_color
        0x2a283e, // selection_background
        0xe0def4, // selection_foreground
        {
            0x26233a, 0xeb6f92, 0x31748f, 0xf6c177,
            0x9ccfd8, 0xc4a7e7, 0xebbcba, 0xe0def4,
            0x6e6a86, 0xeb6f92, 0x31748f, 0xf6c177,
            0x9ccfd8, 0xc4a7e7, 0xebbcba, 0xe0def4,
        },
    },
    {
        "Rose Pine Moon",
        0x232136, // background
        0xe0def4, // foreground
        0x56526e, // cursor_color
        0x393552, // selection_background
        0xe0def4, // selection_foreground
        {
            0x393552, 0xeb6f92, 0x3e8fb0, 0xf6c177,
            0x9ccfd8, 0xc4a7e7, 0xea9a97, 0xe0def4,
            0x6e6a86, 0xeb6f92, 0x3e8fb0, 0xf6c177,
            0x9ccfd8, 0xc4a7e7, 0xea9a97, 0xe0def4,
        },
    },
    {
        "Shades of Purple",
        0x1e1e3f, // background
        0xffffff, // foreground
        0xfad000, // cursor_color
        0x2d2b55, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0xff628c, 0xa5ff90, 0xfad000,
            0x9d8bff, 0xff628c, 0x80ffea, 0xffffff,
            0x575656, 0xff628c, 0xa5ff90, 0xfad000,
            0x9d8bff, 0xff628c, 0x80ffea, 0xffffff,
        },
    },
    {
        "Snazzy",
        0x282a36, // background
        0xeff0eb, // foreground
        0x97979b, // cursor_color
        0x3e404a, // selection_background
        0xeff0eb, // selection_foreground
        {
            0x282a36, 0xff5c57, 0x5af78e, 0xf3f99d,
            0x57c7ff, 0xff6ac1, 0x9aedfe, 0xf1f1f0,
            0x686868, 0xff5c57, 0x5af78e, 0xf3f99d,
            0x57c7ff, 0xff6ac1, 0x9aedfe, 0xf1f1f0,
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
        "Sonokai",
        0x2c2e34, // background
        0xe2e2e3, // foreground
        0xe2e2e3, // cursor_color
        0x3b3e48, // selection_background
        0xe2e2e3, // selection_foreground
        {
            0x181819, 0xfc5d7c, 0x9ed072, 0xe7c664,
            0x76cce0, 0xb39df3, 0xf39660, 0xe2e2e3,
            0x7f8490, 0xfc5d7c, 0x9ed072, 0xe7c664,
            0x76cce0, 0xb39df3, 0xf39660, 0xe2e2e3,
        },
    },
    {
        "Spaceduck",
        0x0f111b, // background
        0xecf0c1, // foreground
        0xecf0c1, // cursor_color
        0x1b1c36, // selection_background
        0xecf0c1, // selection_foreground
        {
            0x000000, 0xe33400, 0x5ccc96, 0xf2ce00,
            0x7a5ccc, 0xce6f8f, 0x00a3cc, 0x686f9a,
            0x686f9a, 0xe33400, 0x5ccc96, 0xf2ce00,
            0x7a5ccc, 0xce6f8f, 0x00a3cc, 0xf0f1ce,
        },
    },
    {
        "Synthwave '84",
        0x2b213a, // background
        0xf0e3ff, // foreground
        0xf0e3ff, // cursor_color
        0x463465, // selection_background
        0xf0e3ff, // selection_foreground
        {
            0x2b213a, 0xfe4450, 0x72f1b8, 0xfede5d,
            0x03edf9, 0xff7edb, 0x03edf9, 0xf0e3ff,
            0x614d85, 0xfe4450, 0x72f1b8, 0xfede5d,
            0x03edf9, 0xff7edb, 0x03edf9, 0xffffff,
        },
    },
    {
        "Tender",
        0x282828, // background
        0xeeeeee, // foreground
        0xeeeeee, // cursor_color
        0x4d4d4d, // selection_background
        0xeeeeee, // selection_foreground
        {
            0x282828, 0xf43753, 0xc9d05c, 0xffc24b,
            0xb3deef, 0xd3b987, 0x73cef4, 0xeeeeee,
            0x4c4c4c, 0xf43753, 0xc9d05c, 0xffc24b,
            0xb3deef, 0xd3b987, 0x73cef4, 0xfeffff,
        },
    },
    {
        "Tokyo Night",
        0x1a1b26, // background
        0xa9b1d6, // foreground
        0xc0caf5, // cursor_color
        0x283457, // selection_background
        0xa9b1d6, // selection_foreground
        {
            0x15161e, 0xf7768e, 0x9ece6a, 0xe0af68,
            0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xa9b1d6,
            0x414868, 0xf7768e, 0x9ece6a, 0xe0af68,
            0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xc0caf5,
        },
    },
    {
        "Tokyo Night Moon",
        0x222436, // background
        0xc8d3f5, // foreground
        0xc8d3f5, // cursor_color
        0x2f334d, // selection_background
        0xc8d3f5, // selection_foreground
        {
            0x1b1d2b, 0xff757f, 0xc3e88d, 0xffc777,
            0x82aaff, 0xfca7ea, 0x86e1fc, 0xc8d3f5,
            0x444a73, 0xff757f, 0xc3e88d, 0xffc777,
            0x82aaff, 0xfca7ea, 0x86e1fc, 0xc8d3f5,
        },
    },
    {
        "Tokyo Night Storm",
        0x24283b, // background
        0xa9b1d6, // foreground
        0xc0caf5, // cursor_color
        0x2f3549, // selection_background
        0xa9b1d6, // selection_foreground
        {
            0x1d202f, 0xf7768e, 0x9ece6a, 0xe0af68,
            0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xa9b1d6,
            0x414868, 0xf7768e, 0x9ece6a, 0xe0af68,
            0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xc0caf5,
        },
    },
    {
        "Vesper",
        0x101010, // background
        0xb9b9b9, // foreground
        0xffc799, // cursor_color
        0x232323, // selection_background
        0xb9b9b9, // selection_foreground
        {
            0x101010, 0xde6e6e, 0x6bab6b, 0xd2a964,
            0x60a1d5, 0xc583b6, 0x6bb5b5, 0xb9b9b9,
            0x505050, 0xef8e8e, 0x8bcf8b, 0xe2c984,
            0x80c1f5, 0xe5a3d6, 0x8bd5d5, 0xd9d9d9,
        },
    },
    {
        "Vitesse Dark",
        0x121212, // background
        0xdbd7ca, // foreground
        0xdbd7ca, // cursor_color
        0x262626, // selection_background
        0xdbd7ca, // selection_foreground
        {
            0x121212, 0xd11b22, 0x4d9375, 0xd4976c,
            0x6394bf, 0xd48bb6, 0x5eaab5, 0xdbd7ca,
            0x393a34, 0xdb4c4c, 0x80a665, 0xe6cc77,
            0x6394bf, 0xd48bb6, 0x5eaab5, 0xdbd7ca,
        },
    },
    {
        "Tomorrow Night",
        0x1d1f21, // background
        0xc5c8c6, // foreground
        0xc5c8c6, // cursor_color
        0x373b41, // selection_background
        0xc5c8c6, // selection_foreground
        {
            0x1d1f21, 0xcc6666, 0xb5bd68, 0xf0c674,
            0x81a2be, 0xb294bb, 0x8abeb7, 0xc5c8c6,
            0x969896, 0xcc6666, 0xb5bd68, 0xf0c674,
            0x81a2be, 0xb294bb, 0x8abeb7, 0xffffff,
        },
    },
    {
        "Tomorrow Night Bright",
        0x000000, // background
        0xeaeaea, // foreground
        0xeaeaea, // cursor_color
        0x424242, // selection_background
        0xeaeaea, // selection_foreground
        {
            0x000000, 0xd54e53, 0xb9ca4a, 0xe7c547,
            0x7aa6da, 0xc397d8, 0x70c0b1, 0xeaeaea,
            0x666666, 0xff3334, 0x9ec400, 0xe7c547,
            0x7aa6da, 0xb77ee0, 0x54ced6, 0xffffff,
        },
    },
    {
        "Tomorrow Night Eighties",
        0x2d2d2d, // background
        0xcccccc, // foreground
        0xcccccc, // cursor_color
        0x515151, // selection_background
        0xcccccc, // selection_foreground
        {
            0x2d2d2d, 0xf2777a, 0x99cc99, 0xffcc66,
            0x6699cc, 0xcc99cc, 0x66cccc, 0xcccccc,
            0x999999, 0xf2777a, 0x99cc99, 0xffcc66,
            0x6699cc, 0xcc99cc, 0x66cccc, 0xffffff,
        },
    },
    {
        "Terminal Basic",
        0x000000, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x525252, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0x990000, 0x00a600, 0x999900,
            0x0000b2, 0xb200b2, 0x00a6b2, 0xbfbfbf,
            0x666666, 0xe50000, 0x00d900, 0xe5e500,
            0x0000ff, 0xe500e5, 0x00e5e5, 0xe5e5e5,
        },
    },
    {
        "Warp",
        0x242424, // background
        0xe0e0e0, // foreground
        0xe0e0e0, // cursor_color
        0x383838, // selection_background
        0xe0e0e0, // selection_foreground
        {
            0x000000, 0xd81e00, 0x5ea702, 0xcfae00,
            0x427ab3, 0x89658e, 0x00a7aa, 0xdbded8,
            0x686a66, 0xf54235, 0x99e343, 0xfdeb61,
            0x84b0d8, 0xbc94b7, 0x37e6e8, 0xf1f1f0,
        },
    },
    {
        "Zenburn",
        0x3f3f3f, // background
        0xdcdccc, // foreground
        0xdcdccc, // cursor_color
        0x4f4f4f, // selection_background
        0xdcdccc, // selection_foreground
        {
            0x3f3f3f, 0xcc9393, 0x7f9f7f, 0xe3ceab,
            0x7cb8bb, 0xdc8cc3, 0x93e0e3, 0xdcdccc,
            0x636363, 0xdca3a3, 0xbfebbf, 0xf0dfaf,
            0x8cd0d3, 0xdc8cc3, 0x93e0e3, 0xffffff,
        },
    },
    // =========================================================================
    // Light Themes
    // =========================================================================
    {
        "Alabaster Light",
        0xf7f7f7, // background
        0x434343, // foreground
        0x434343, // cursor_color
        0xd3d2d3, // selection_background
        0x434343, // selection_foreground
        {
            0x000000, 0xaa3731, 0x448c27, 0xcb9000,
            0x325cc0, 0x7a3e9d, 0x0083b2, 0xbbbbbb,
            0x777777, 0xf05050, 0x60cb00, 0xffbc5d,
            0x007acc, 0xe64ce6, 0x00aacb, 0xffffff,
        },
    },
    {
        "Alabaster Dark",
        0x0e1415, // background
        0xc7c4b9, // foreground
        0xc7c4b9, // cursor_color
        0x293334, // selection_background
        0xc7c4b9, // selection_foreground
        {
            0x000000, 0xd2322d, 0x6abf40, 0xcd9731,
            0x007acc, 0x9b3596, 0x00a7aa, 0xc7c4b9,
            0x5e6f6f, 0xf05050, 0x7bc94e, 0xffbc5d,
            0x4499da, 0xe64ce6, 0x00bdbd, 0xffffff,
        },
    },
    {
        "Ayu Light",
        0xfafafa, // background
        0x575f66, // foreground
        0xff6a00, // cursor_color
        0xf0eee4, // selection_background
        0x575f66, // selection_foreground
        {
            0x000000, 0xff3333, 0x86b300, 0xf29718,
            0x41a6d9, 0xf07178, 0x4dbf99, 0xffffff,
            0x323232, 0xff6565, 0xb8e532, 0xffc94a,
            0x73d8ff, 0xff8f9e, 0x7ff1cb, 0xffffff,
        },
    },
    {
        "Bamboo Light",
        0xfaf3e0, // background
        0x3a3226, // foreground
        0x3a3226, // cursor_color
        0xe9dfc8, // selection_background
        0x3a3226, // selection_foreground
        {
            0x4c4436, 0xc75646, 0x6e9b3a, 0xcb8b00,
            0x3f7e97, 0xa76b82, 0x3e907f, 0xf0e4ca,
            0x7d7460, 0xe3614c, 0x86b554, 0xe5a82b,
            0x579fbd, 0xc28da3, 0x57b5a0, 0xfaf3e0,
        },
    },
    {
        "Base16 Default Dark",
        0x181818, // background
        0xd8d8d8, // foreground
        0xd8d8d8, // cursor_color
        0x383838, // selection_background
        0xd8d8d8, // selection_foreground
        {
            0x181818, 0xab4642, 0xa1b56c, 0xf7ca88,
            0x7cafc2, 0xba8baf, 0x86c1b9, 0xd8d8d8,
            0x585858, 0xab4642, 0xa1b56c, 0xf7ca88,
            0x7cafc2, 0xba8baf, 0x86c1b9, 0xf8f8f8,
        },
    },
    {
        "Base16 Default Light",
        0xf8f8f8, // background
        0x383838, // foreground
        0x383838, // cursor_color
        0xd8d8d8, // selection_background
        0x383838, // selection_foreground
        {
            0x181818, 0xab4642, 0xa1b56c, 0xf7ca88,
            0x7cafc2, 0xba8baf, 0x86c1b9, 0xd8d8d8,
            0x585858, 0xab4642, 0xa1b56c, 0xf7ca88,
            0x7cafc2, 0xba8baf, 0x86c1b9, 0xf8f8f8,
        },
    },
    {
        "Bluloco Light",
        0xf9f9f9, // background
        0x383a42, // foreground
        0xffcc00, // cursor_color
        0xd8dae4, // selection_background
        0x383a42, // selection_foreground
        {
            0x373a41, 0xd52753, 0x23974a, 0xdf631c,
            0x275fe4, 0x823ff1, 0x27618d, 0xababab,
            0x676b79, 0xff6480, 0x3cbc66, 0xc5a332,
            0x0099e1, 0xce33c0, 0x6d93bb, 0xd3d3d3,
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
        "Everforest Light",
        0xfdf6e3, // background
        0x5c6a72, // foreground
        0x5c6a72, // cursor_color
        0xe6e2cc, // selection_background
        0x5c6a72, // selection_foreground
        {
            0x5c6a72, 0xf85552, 0x8da101, 0xdfa000,
            0x3a94c5, 0xdf69ba, 0x35a77c, 0xdfddc8,
            0x829181, 0xf85552, 0x8da101, 0xdfa000,
            0x3a94c5, 0xdf69ba, 0x35a77c, 0xe8e5d0,
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
        "Horizon Light",
        0xfdf0ed, // background
        0x403c3d, // foreground
        0x403c3d, // cursor_color
        0xf3e6e1, // selection_background
        0x403c3d, // selection_foreground
        {
            0x16161c, 0xda103f, 0x1eb980, 0xf6661e,
            0x26bbd9, 0xee64ac, 0x1d8991, 0xfdf0ed,
            0x403c3d, 0xf43e5c, 0x07da8c, 0xf77d26,
            0x3fc4de, 0xf075b5, 0x24a1af, 0xfdf0ed,
        },
    },
    {
        "Iceberg Light",
        0xe8e9ec, // background
        0x33374c, // foreground
        0x33374c, // cursor_color
        0xc9cdd7, // selection_background
        0x33374c, // selection_foreground
        {
            0xdcdfe7, 0xcc517a, 0x668e3d, 0xc57339,
            0x2d539e, 0x7759b4, 0x3f83a6, 0x33374c,
            0x8389a3, 0xcc3768, 0x598030, 0xb6662d,
            0x22478e, 0x6845ad, 0x327698, 0x262a3f,
        },
    },
    {
        "Material Light",
        0xfafafa, // background
        0x546e7a, // foreground
        0x272727, // cursor_color
        0xe7e8ec, // selection_background
        0x546e7a, // selection_foreground
        {
            0x546e7a, 0xff5370, 0x91b859, 0xffb62c,
            0x6182b8, 0x7c4dff, 0x39adb5, 0xeee4d3,
            0x8796b0, 0xff5370, 0x91b859, 0xffb62c,
            0x6182b8, 0x7c4dff, 0x39adb5, 0xffffff,
        },
    },
    {
        "Melange Light",
        0xf1f1f1, // background
        0x54433a, // foreground
        0x54433a, // cursor_color
        0xe0d8d1, // selection_background
        0x54433a, // selection_foreground
        {
            0x54433a, 0xa6333f, 0x6a8a3a, 0xbd8e37,
            0x3f7e97, 0xa6599a, 0x3e907f, 0xf1f1f1,
            0x867462, 0xc64f58, 0x7da254, 0xd8a44c,
            0x5799b7, 0xc575b5, 0x57b5a0, 0xf1f1f1,
        },
    },
    {
        "Modus Operandi",
        0xffffff, // background
        0x000000, // foreground
        0x000000, // cursor_color
        0xbfefff, // selection_background
        0x000000, // selection_foreground
        {
            0x000000, 0xa60000, 0x006800, 0x6f5500,
            0x0031a9, 0x721045, 0x005e8b, 0xbfbfbf,
            0x595959, 0x972500, 0x316500, 0x884900,
            0x2544bb, 0x8f0075, 0x30517f, 0xffffff,
        },
    },
    {
        "Modus Vivendi",
        0x000000, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x3f3f68, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0xff8059, 0x44bc44, 0xd0bc00,
            0x2fafff, 0xfeacd0, 0x00d3d0, 0xbfbfbf,
            0x595959, 0xef8b50, 0x70b900, 0xc0c530,
            0x79a8ff, 0xb6a0ff, 0x6ae4b9, 0xffffff,
        },
    },
    {
        "One Light",
        0xfafafa, // background
        0x383a42, // foreground
        0x526eff, // cursor_color
        0xe5e5e6, // selection_background
        0x383a42, // selection_foreground
        {
            0x383a42, 0xe45649, 0x50a14f, 0xc18401,
            0x0184bc, 0xa626a4, 0x0997b3, 0xfafafa,
            0x4f525e, 0xe06c75, 0x98c379, 0xe5c07b,
            0x61afef, 0xc678dd, 0x56b6c2, 0xffffff,
        },
    },
    {
        "Oxocarbon Light",
        0xffffff, // background
        0x161616, // foreground
        0x161616, // cursor_color
        0xe0e0e0, // selection_background
        0x161616, // selection_foreground
        {
            0x161616, 0xee5396, 0x42be65, 0x8a3ffc,
            0x0f62fe, 0xbe95ff, 0x08bdba, 0xdde1e6,
            0x393939, 0xee5396, 0x42be65, 0x8a3ffc,
            0x0f62fe, 0xbe95ff, 0x08bdba, 0xffffff,
        },
    },
    {
        "PaperColor Light",
        0xeeeeee, // background
        0x444444, // foreground
        0x444444, // cursor_color
        0xd0d0d0, // selection_background
        0x444444, // selection_foreground
        {
            0xeeeeee, 0xaf0000, 0x008700, 0x5f8700,
            0x0087af, 0x878787, 0x005f87, 0x444444,
            0xbcbcbc, 0xd70000, 0xd70087, 0x8700af,
            0xd75f00, 0xd75f00, 0x005faf, 0x005f87,
        },
    },
    {
        "PaperColor Dark",
        0x1c1c1c, // background
        0xd0d0d0, // foreground
        0xd0d0d0, // cursor_color
        0x3a3a3a, // selection_background
        0xd0d0d0, // selection_foreground
        {
            0x1c1c1c, 0xaf005f, 0x5faf00, 0xd7af5f,
            0x5fafd7, 0x808080, 0xd7875f, 0xd0d0d0,
            0x585858, 0x5faf5f, 0xafd700, 0xaf87d7,
            0xffaf00, 0xff5faf, 0x00afaf, 0x5f8787,
        },
    },
    {
        "Pencil Light",
        0xf1f1f1, // background
        0x424242, // foreground
        0x424242, // cursor_color
        0xd5d5d5, // selection_background
        0x424242, // selection_foreground
        {
            0x212121, 0xc30771, 0x10a778, 0xa89c14,
            0x008ec4, 0x523c79, 0x20a5ba, 0xd9d9d9,
            0x424242, 0xfb007a, 0x5fd7af, 0xf3e430,
            0x20bbfc, 0x6855de, 0x4fb8cc, 0xf1f1f1,
        },
    },
    {
        "Rose Pine Dawn",
        0xfaf4ed, // background
        0x575279, // foreground
        0x9893a5, // cursor_color
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
        "Tokyo Night Light",
        0xe1e2e7, // background
        0x3760bf, // foreground
        0x3760bf, // cursor_color
        0xc4c8da, // selection_background
        0x3760bf, // selection_foreground
        {
            0x0f0f14, 0x8c4351, 0x33635c, 0x8f5e15,
            0x34548a, 0x5a3e8e, 0x0f4b6e, 0x3760bf,
            0x6172b0, 0x8c4351, 0x33635c, 0x8f5e15,
            0x34548a, 0x5a3e8e, 0x0f4b6e, 0x3760bf,
        },
    },
    {
        "Vitesse Light",
        0xffffff, // background
        0x393a34, // foreground
        0x393a34, // cursor_color
        0xebebeb, // selection_background
        0x393a34, // selection_foreground
        {
            0x393a34, 0xab5959, 0x59a694, 0xb58451,
            0x6394bf, 0xd48bb6, 0x5eaab5, 0xffffff,
            0x999999, 0xab5959, 0x2e8f63, 0xa88328,
            0x6394bf, 0xd48bb6, 0x5eaab5, 0xffffff,
        },
    },
    {
        "Atom One Light",
        0xf8f8f8, // background
        0x2a2b32, // foreground
        0x526eff, // cursor_color
        0xe5e5e6, // selection_background
        0x2a2b32, // selection_foreground
        {
            0x000000, 0xde3d35, 0x3e953a, 0xd2b67b,
            0x2f5af3, 0xa00095, 0x3e953a, 0xbbbbbb,
            0x555555, 0xf2777a, 0x55d44e, 0xe5c07b,
            0x6699cc, 0xa959a8, 0x66cccc, 0xffffff,
        },
    },
    {
        "Kanagawa Dragon",
        0x181616, // background
        0xc5c9c5, // foreground
        0xc8c093, // cursor_color
        0x2d4f67, // selection_background
        0xc5c9c5, // selection_foreground
        {
            0x0d0c0c, 0xc4746e, 0x87a987, 0xc4b28a,
            0x8ba4b0, 0xa292a3, 0x8ea4a2, 0xc8c093,
            0x625e5a, 0xe46876, 0x6a9589, 0xe6c384,
            0x7fb4ca, 0x938aa9, 0x7aa89f, 0xc5c9c5,
        },
    },
    {
        "Tomorrow Light",
        0xffffff, // background
        0x4d4d4c, // foreground
        0x4d4d4c, // cursor_color
        0xd6d6d6, // selection_background
        0x4d4d4c, // selection_foreground
        {
            0x000000, 0xc82829, 0x718c00, 0xeab700,
            0x4271ae, 0x8959a8, 0x3e999f, 0xffffff,
            0x8e908c, 0xc82829, 0x718c00, 0xeab700,
            0x4271ae, 0x8959a8, 0x3e999f, 0xffffff,
        },
    },
    {
        "Zenbones",
        0xf0edec, // background
        0x2c363c, // foreground
        0x2c363c, // cursor_color
        0xdad4d0, // selection_background
        0x2c363c, // selection_foreground
        {
            0x2c363c, 0xa8334c, 0x4f6c31, 0x944927,
            0x286486, 0x88507d, 0x3b8992, 0xb4bdc3,
            0x617580, 0xc4546a, 0x6d924a, 0xb36a3b,
            0x417fa0, 0xa46b94, 0x569fa2, 0xcdd1d5,
        },
    },
    // =========================================================================
    // High Contrast / Accessibility
    // =========================================================================
    {
        "High Contrast Dark",
        0x000000, // background
        0xffffff, // foreground
        0xffffff, // cursor_color
        0x444444, // selection_background
        0xffffff, // selection_foreground
        {
            0x000000, 0xff0000, 0x00ff00, 0xffff00,
            0x0066ff, 0xff00ff, 0x00ffff, 0xffffff,
            0x808080, 0xff3333, 0x33ff33, 0xffff33,
            0x3399ff, 0xff33ff, 0x33ffff, 0xffffff,
        },
    },
    {
        "High Contrast Light",
        0xffffff, // background
        0x000000, // foreground
        0x000000, // cursor_color
        0xcccccc, // selection_background
        0x000000, // selection_foreground
        {
            0x000000, 0xcc0000, 0x008800, 0xaa5500,
            0x0000cc, 0xcc00cc, 0x008888, 0xaaaaaa,
            0x555555, 0xee0000, 0x00cc00, 0xcc8800,
            0x0000ee, 0xee00ee, 0x00aaaa, 0xffffff,
        },
    },
    {
        "High Contrast Yellow",
        0x000000, // background
        0xffff00, // foreground
        0xffff00, // cursor_color
        0x333300, // selection_background
        0xffff00, // selection_foreground
        {
            0x000000, 0xff0000, 0x00ff00, 0xffff00,
            0x0066ff, 0xff00ff, 0x00ffff, 0xffffff,
            0x808080, 0xff3333, 0x33ff33, 0xffff33,
            0x3399ff, 0xff33ff, 0x33ffff, 0xffffff,
        },
    },
    {
        "High Contrast Green",
        0x000000, // background
        0x00ff00, // foreground
        0x00ff00, // cursor_color
        0x003300, // selection_background
        0x00ff00, // selection_foreground
        {
            0x000000, 0xff0000, 0x00ff00, 0xffff00,
            0x0066ff, 0xff00ff, 0x00ffff, 0xffffff,
            0x808080, 0xff3333, 0x33ff33, 0xffff33,
            0x3399ff, 0xff33ff, 0x33ffff, 0xffffff,
        },
    },
};
// clang-format on

} // namespace

const Theme* getBuiltinTheme(const std::string& name) {
    for (const auto& t : kBuiltinThemes) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

std::vector<std::string> listBuiltinThemes() {
    std::vector<std::string> names;
    for (const auto& t : kBuiltinThemes) {
        names.push_back(t.name);
    }
    return names;
}

} // namespace termcore
