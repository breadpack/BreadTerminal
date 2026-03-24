#!/bin/bash
# Test: Erase commands with dirty flags
# Expected: No leftover artifacts after erase operations

echo "=== Erase Display Test ==="
echo "Press Enter to test ED (Erase Display)..."
read

# Fill screen with '#' characters
for i in $(seq 1 20); do
    printf '########################################\n'
done

sleep 0.5

# ED 2 - Erase entire display (should leave NO '#' artifacts)
printf '\033[2J\033[H'
echo "Screen should be completely clean above this line."
echo "If you see any '#' artifacts, dirty flags are broken."

echo ""
echo "Press Enter to test EL (Erase Line)..."
read

# Write a line, then erase it
printf '\033[10;1HTHIS LINE SHOULD BE ERASED'
sleep 0.3
printf '\033[10;1H\033[2K'
printf '\033[12;1H'
echo "Line 10 should be blank. If 'THIS LINE SHOULD BE ERASED' is visible, EL dirty flag is broken."

echo ""
echo "Press Enter to test ECH (Erase Characters)..."
read

printf '\033[15;1HABCDEFGHIJKLMNOPQRSTUVWXYZ'
sleep 0.3
# Erase 10 characters starting from column 5
printf '\033[15;5H\033[10X'
printf '\033[17;1H'
echo "Line 15 should show 'ABCD' then blank then 'OPQRSTUVWXYZ'."

echo ""
echo "Press Enter to test ICH/DCH (Insert/Delete Characters)..."
read

printf '\033[20;1HABCDEFGHIJ'
sleep 0.3
# Insert 3 characters at column 4
printf '\033[20;4H\033[3@'
printf '\033[22;1H'
echo "Line 20 should show 'ABC   DEFGHIJ' (3 blanks inserted at position 4)."

echo ""
echo "All erase tests complete."
