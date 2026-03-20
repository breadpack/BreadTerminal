#!/bin/bash
# BreadTerminal post-tool hook for Claude Code
# Install: bread hooks install
bread notify send --title "Tool: $CLAUDE_TOOL_NAME" --body "Completed" --urgency low
