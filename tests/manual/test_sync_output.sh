#!/bin/bash
# Test: Synchronized Output (DEC mode 2026)
# Expected: No flickering between BSU and ESU

echo "=== Synchronized Output Test ==="
echo "You should see a clean box appear without any flickering."
echo "Press Enter to start..."
read

# BSU - Begin Synchronized Update
printf '\033[?2026h'

# Clear and draw a box
printf '\033[2J\033[H'
printf '\033[1;34m'
printf '┌──────────────────────────────────┐\n'
printf '│                                  │\n'
printf '│   Synchronized Output Test       │\n'
printf '│                                  │\n'
printf '│   If you see this without        │\n'
printf '│   any flicker, DEC 2026 works!   │\n'
printf '│                                  │\n'
printf '└──────────────────────────────────┘\n'
printf '\033[0m'

# ESU - End Synchronized Update
printf '\033[?2026l'

echo ""
echo "=== Rapid Redraw Stress Test ==="
echo "Press Enter to start 50 rapid redraws..."
read

for i in $(seq 1 50); do
    printf '\033[?2026h'
    printf '\033[2J\033[H'
    printf "Frame $i / 50\n"
    printf '┌──────────────────────────────────┐\n'
    for row in $(seq 1 10); do
        printf "│ Row %-2d  Data: %-18s │\n" "$row" "$(head -c 18 /dev/urandom | base64 | head -c 18)"
    done
    printf '└──────────────────────────────────┘\n'
    printf '\033[?2026l'
done

echo ""
echo "Done. If frames appeared cleanly without tearing, sync output works."
