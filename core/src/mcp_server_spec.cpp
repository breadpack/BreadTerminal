#include "termcore/mcp_server_spec.h"

namespace termcore {

// --- Workspace tools ---
std::vector<McpToolSpec> McpServerSpec::workspaceTools() {
    return {
        {
            "breadterminal_list_workspaces",
            "List all open workspaces in BreadTerminal.",
            R"({
  "type": "object",
  "properties": {},
  "additionalProperties": false
})"
        },
        {
            "breadterminal_create_workspace",
            "Create a new workspace with an optional name and working directory.",
            R"({
  "type": "object",
  "properties": {
    "name": {
      "type": "string",
      "description": "Display name for the workspace."
    },
    "cwd": {
      "type": "string",
      "description": "Initial working directory for the workspace."
    }
  },
  "additionalProperties": false
})"
        },
        {
            "breadterminal_select_workspace",
            "Switch to a workspace by ID or index.",
            R"({
  "type": "object",
  "properties": {
    "workspace_id": {
      "type": "string",
      "description": "UUID of the workspace to switch to."
    },
    "index": {
      "type": "integer",
      "description": "Zero-based index of the workspace."
    }
  },
  "additionalProperties": false
})"
        }
    };
}

// --- Pane tools ---
std::vector<McpToolSpec> McpServerSpec::paneTools() {
    return {
        {
            "breadterminal_split_pane",
            "Create a new pane by splitting an existing pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the pane to split. Uses focused pane if omitted."
    },
    "direction": {
      "type": "string",
      "enum": ["left", "right", "up", "down"],
      "description": "Direction to place the new pane relative to the source."
    },
    "cwd": {
      "type": "string",
      "description": "Working directory for the new pane's shell."
    }
  },
  "required": ["direction"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_send_text",
            "Send text input to a pane's PTY (as if the user typed it).",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the target pane. Uses focused pane if omitted."
    },
    "text": {
      "type": "string",
      "description": "Text to send to the pane's terminal."
    },
    "press_enter": {
      "type": "boolean",
      "description": "If true, append a newline after the text. Default: true."
    }
  },
  "required": ["text"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_send_key",
            "Send a special keystroke to a pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the target pane. Uses focused pane if omitted."
    },
    "key": {
      "type": "string",
      "description": "Key name (e.g. 'Enter', 'Tab', 'Escape', 'Ctrl-C', 'Up', 'Down')."
    }
  },
  "required": ["key"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_read_screen",
            "Capture terminal output from a pane (last N lines of visible content).",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the pane to read. Uses focused pane if omitted."
    },
    "lines": {
      "type": "integer",
      "description": "Number of lines to capture from the bottom. Default: all visible lines.",
      "minimum": 1,
      "maximum": 10000
    },
    "include_scrollback": {
      "type": "boolean",
      "description": "If true, also include scrollback buffer lines. Default: false."
    }
  },
  "additionalProperties": false
})"
        },
        {
            "breadterminal_focus_pane",
            "Set keyboard focus to a specific pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the pane to focus."
    }
  },
  "required": ["pane_id"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_close_pane",
            "Close a pane, terminating its shell process.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the pane to close. Uses focused pane if omitted."
    }
  },
  "additionalProperties": false
})"
        }
    };
}

// --- Agent orchestration tools ---
std::vector<McpToolSpec> McpServerSpec::agentTools() {
    return {
        {
            "breadterminal_launch_agents",
            "Launch N agent instances in new panes, each running a specified command.",
            R"({
  "type": "object",
  "properties": {
    "count": {
      "type": "integer",
      "description": "Number of agent instances to launch.",
      "minimum": 1,
      "maximum": 20
    },
    "command": {
      "type": "string",
      "description": "Shell command to run in each agent pane (e.g. 'claude --agent')."
    },
    "cwd": {
      "type": "string",
      "description": "Working directory for the agent panes."
    },
    "layout": {
      "type": "string",
      "enum": ["grid", "horizontal", "vertical"],
      "description": "Layout arrangement for the agent panes. Default: grid."
    }
  },
  "required": ["count", "command"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_orchestrate",
            "Assign tasks to multiple agent panes by sending text to each.",
            R"({
  "type": "object",
  "properties": {
    "assignments": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "pane_id": {
            "type": "string",
            "description": "UUID of the target agent pane."
          },
          "text": {
            "type": "string",
            "description": "Task text to send to the agent."
          }
        },
        "required": ["pane_id", "text"]
      },
      "description": "List of pane-task assignments."
    }
  },
  "required": ["assignments"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_read_all",
            "Read the last N lines of output from all panes at once.",
            R"({
  "type": "object",
  "properties": {
    "lines": {
      "type": "integer",
      "description": "Number of lines to read from each pane. Default: 20.",
      "minimum": 1,
      "maximum": 1000
    }
  },
  "additionalProperties": false
})"
        },
        {
            "breadterminal_get_idle",
            "Find panes that appear idle (shell prompt visible, no running command).",
            R"({
  "type": "object",
  "properties": {},
  "additionalProperties": false
})"
        },
        {
            "breadterminal_agent_set_status",
            "Set agent state with a custom label and icon on a pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." },
    "state": { "type": "string", "enum": ["inactive","starting","idle","running","thinking","tool_use","waiting","needs_input","error","exited"], "description": "Agent state." },
    "label": { "type": "string", "description": "Custom label text." },
    "icon": { "type": "string", "description": "Icon name or emoji." }
  },
  "required": ["pane_id", "state"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_agent_set_status_pills",
            "Set status pills (badges) on a pane for agent display.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." },
    "pills": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "text": { "type": "string" },
          "bg_color": { "type": "string" },
          "fg_color": { "type": "string" }
        },
        "required": ["text"]
      }
    }
  },
  "required": ["pane_id", "pills"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_agent_request_attention",
            "Trigger a border glow and notification on a pane to get the user's attention.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." },
    "urgency": { "type": "string", "enum": ["low","normal","critical"], "description": "Attention urgency level." },
    "message": { "type": "string", "description": "Message to display in notification." },
    "color": { "type": "string", "description": "Hex color for border glow (e.g. '#ff5500')." }
  },
  "required": ["pane_id"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_agent_clear_status",
            "Clear all agent UI elements (pills, progress, status) from a pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." }
  },
  "required": ["pane_id"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_agent_broadcast",
            "Send a message to all tracked agent panes.",
            R"({
  "type": "object",
  "properties": {
    "message": { "type": "string", "description": "Message to broadcast." }
  },
  "required": ["message"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_agent_list",
            "List all active agents with their states and pane IDs.",
            R"({
  "type": "object",
  "properties": {},
  "additionalProperties": false
})"
        }
    };
}

// --- Status tools ---
std::vector<McpToolSpec> McpServerSpec::statusTools() {
    return {
        {
            "breadterminal_set_progress",
            "Set a progress bar on a pane's tab or title bar.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the pane. Uses focused pane if omitted."
    },
    "progress": {
      "type": "number",
      "description": "Progress value from 0.0 to 1.0. Set to -1 to hide.",
      "minimum": -1,
      "maximum": 1
    },
    "label": {
      "type": "string",
      "description": "Optional text label shown alongside the progress bar."
    }
  },
  "required": ["progress"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_set_status",
            "Set a status pill (badge) on a pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the pane. Uses focused pane if omitted."
    },
    "text": {
      "type": "string",
      "description": "Status text to display in the pill."
    },
    "color": {
      "type": "string",
      "description": "Color name or hex value (e.g. 'green', '#ff5500')."
    }
  },
  "required": ["text"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_get_screen_content",
            "Read visible terminal content from a pane with optional row range.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." },
    "start_row": { "type": "integer", "description": "First row to read (0-based). Default: 0." },
    "end_row": { "type": "integer", "description": "Last row (exclusive). Default: all rows." }
  },
  "required": ["pane_id"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_get_selection",
            "Get the currently selected text from a pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." }
  },
  "required": ["pane_id"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_get_cursor_position",
            "Get the cursor row and column in a pane.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." }
  },
  "required": ["pane_id"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_list_panes",
            "List all panes across all workspaces with their agent status.",
            R"({
  "type": "object",
  "properties": {},
  "additionalProperties": false
})"
        },
        {
            "breadterminal_get_pane_info",
            "Get detailed information about a specific pane including agent state.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": { "type": "integer", "description": "Target pane ID." }
  },
  "required": ["pane_id"],
  "additionalProperties": false
})"
        }
    };
}

// --- Notification tools ---
std::vector<McpToolSpec> McpServerSpec::notificationTools() {
    return {
        {
            "breadterminal_notify",
            "Send a desktop or in-app notification.",
            R"({
  "type": "object",
  "properties": {
    "title": {
      "type": "string",
      "description": "Notification title."
    },
    "body": {
      "type": "string",
      "description": "Notification body text."
    },
    "urgency": {
      "type": "string",
      "enum": ["low", "normal", "critical"],
      "description": "Notification urgency level. Default: normal."
    }
  },
  "required": ["title", "body"],
  "additionalProperties": false
})"
        }
    };
}

// --- Browser tools ---
std::vector<McpToolSpec> McpServerSpec::browserTools() {
    return {
        {
            "breadterminal_browser_navigate",
            "Navigate the embedded browser pane to a URL.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the browser pane."
    },
    "url": {
      "type": "string",
      "description": "URL to navigate to."
    }
  },
  "required": ["url"],
  "additionalProperties": false
})"
        },
        {
            "breadterminal_browser_snapshot",
            "Get the accessibility tree of the current browser page.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the browser pane."
    }
  },
  "additionalProperties": false
})"
        },
        {
            "breadterminal_browser_click",
            "Click an element in the browser by accessibility reference.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the browser pane."
    },
    "ref": {
      "type": "string",
      "description": "Accessibility tree reference ID of the element to click."
    },
    "selector": {
      "type": "string",
      "description": "CSS selector as fallback if ref is not provided."
    }
  },
  "additionalProperties": false
})"
        },
        {
            "breadterminal_browser_fill",
            "Fill a form field in the browser.",
            R"({
  "type": "object",
  "properties": {
    "pane_id": {
      "type": "string",
      "description": "UUID of the browser pane."
    },
    "ref": {
      "type": "string",
      "description": "Accessibility tree reference ID of the input element."
    },
    "selector": {
      "type": "string",
      "description": "CSS selector as fallback if ref is not provided."
    },
    "value": {
      "type": "string",
      "description": "Value to fill into the field."
    }
  },
  "required": ["value"],
  "additionalProperties": false
})"
        }
    };
}

// --- allTools ---
std::vector<McpToolSpec> McpServerSpec::allTools() {
    std::vector<McpToolSpec> all;
    auto append = [&](std::vector<McpToolSpec>&& tools) {
        all.insert(all.end(),
                   std::make_move_iterator(tools.begin()),
                   std::make_move_iterator(tools.end()));
    };
    append(workspaceTools());
    append(paneTools());
    append(agentTools());
    append(browserTools());
    append(statusTools());
    append(notificationTools());
    return all;
}

} // namespace termcore
