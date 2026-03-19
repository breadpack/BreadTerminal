#pragma once

namespace bread {

/// Install Claude Code hook scripts.
/// Creates ~/.claude/hooks/ directory, copies hook scripts, and optionally
/// updates ~/.claude/settings.json to register hooks.
/// Returns 0 on success, 1 on failure.
int installHooks();

}  // namespace bread
