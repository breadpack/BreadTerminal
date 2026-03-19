#!/usr/bin/env python3
"""
Generate theme_index.json from a directory of Ghostty theme files.

Usage:
    python3 generate_theme_index.py <ghostty_themes_dir> [-o output.json]

Each Ghostty theme file contains key=value lines like:
    background = 282a36
    foreground = f8f8f2
    palette = 0=#21222c
    palette = 1=#ff5555
    ...
"""

import argparse
import json
import os
import sys


def parse_color(value: str) -> str:
    """Strip '#' prefix and return 6-digit hex color string."""
    value = value.strip()
    if value.startswith('#'):
        value = value[1:]
    return value.lower()


def parse_ghostty_theme(filepath: str) -> dict | None:
    """Parse a Ghostty theme file and return a theme dict."""
    name = os.path.splitext(os.path.basename(filepath))[0]
    background = None
    foreground = None
    palette = ['000000'] * 16

    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue

                if '=' not in line:
                    continue

                key, _, value = line.partition('=')
                key = key.strip()
                value = value.strip()

                if key == 'background':
                    background = parse_color(value)
                elif key == 'foreground':
                    foreground = parse_color(value)
                elif key == 'palette':
                    # Format: N=#RRGGBB or N=RRGGBB
                    if '=' in value:
                        idx_str, _, color = value.partition('=')
                        try:
                            idx = int(idx_str.strip())
                            if 0 <= idx < 16:
                                palette[idx] = parse_color(color)
                        except ValueError:
                            pass
    except (OSError, UnicodeDecodeError):
        return None

    if background is None and foreground is None:
        return None

    return {
        'name': name,
        'background': background or '000000',
        'foreground': foreground or 'ffffff',
        'palette': palette,
        'source_url': '',
    }


def main():
    parser = argparse.ArgumentParser(
        description='Generate theme_index.json from Ghostty theme files'
    )
    parser.add_argument('themes_dir', help='Directory containing Ghostty theme files')
    parser.add_argument('-o', '--output', default='theme_index.json',
                        help='Output JSON file (default: theme_index.json)')
    parser.add_argument('--base-url', default='',
                        help='Base URL prefix for source_url field')
    args = parser.parse_args()

    if not os.path.isdir(args.themes_dir):
        print(f"Error: {args.themes_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    themes = []
    for filename in sorted(os.listdir(args.themes_dir)):
        filepath = os.path.join(args.themes_dir, filename)
        if not os.path.isfile(filepath):
            continue

        theme = parse_ghostty_theme(filepath)
        if theme:
            if args.base_url:
                theme['source_url'] = f"{args.base_url.rstrip('/')}/{filename}"
            themes.append(theme)

    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(themes, f, indent=2)

    print(f"Generated {len(themes)} themes -> {args.output}")


if __name__ == '__main__':
    main()
