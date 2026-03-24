#!/bin/bash
# Test: DA1/DA2 response
# Expected: VT220 level response

echo "=== Device Attributes Test ==="
echo ""

# Query DA1 (Primary Device Attributes)
echo "Querying DA1 (Primary Device Attributes)..."
echo "Expected: ESC[?62;22c (VT220 + ANSI color)"
echo -n "Response: "

# Send DA1 query and capture response
response=""
# Use stty to capture raw response
old_settings=$(stty -g)
stty raw -echo min 0 time 10
printf '\033[c'
response=$(dd bs=32 count=1 2>/dev/null)
stty "$old_settings"

# Print response as hex for debugging
echo "$response" | cat -v
echo ""

# Query DA2 (Secondary Device Attributes)
echo "Querying DA2 (Secondary Device Attributes)..."
echo "Expected: ESC[>65;1;0c (VT500 family, v1)"
echo -n "Response: "

old_settings=$(stty -g)
stty raw -echo min 0 time 10
printf '\033[>c'
response=$(dd bs=32 count=1 2>/dev/null)
stty "$old_settings"

echo "$response" | cat -v
echo ""

# Query XTVERSION
echo "Querying XTVERSION..."
echo "Expected: DCS>|BreadTerminal 0.1 ST"
echo -n "Response: "

old_settings=$(stty -g)
stty raw -echo min 0 time 10
printf '\033[>q'
response=$(dd bs=64 count=1 2>/dev/null)
stty "$old_settings"

echo "$response" | cat -v
echo ""

# Query DECRQM for mode 2026
echo "Querying DECRQM for Synchronized Output (mode 2026)..."
echo "Expected: ESC[?2026;2\$y (mode supported, currently reset)"
echo -n "Response: "

old_settings=$(stty -g)
stty raw -echo min 0 time 10
printf '\033[?2026$p'
response=$(dd bs=32 count=1 2>/dev/null)
stty "$old_settings"

echo "$response" | cat -v
echo ""

echo "=== All DA tests complete ==="
