#!/bin/bash
# BreadTerminal notification hook for Claude Code
# Install: bread hooks install
bread notify send --title "Claude Code" --body "$CLAUDE_NOTIFICATION_MESSAGE"
