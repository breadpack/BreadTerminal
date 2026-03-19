#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "termcore/session.h"

namespace termcore {

class Mux;
class Screen;
class IPaneStateProvider;

/// Information about a persisted session on disk.
struct SessionInfo {
    std::string name;       // Human-readable name (e.g. "session-20260319-143021")
    std::string filepath;   // Full path to the session JSON file
    uint64_t    timestamp;  // File modification time (epoch-like counter)
};

/// High-level session management: list, save, restore named sessions.
///
/// This sits on top of SessionManager (which handles low-level capture/
/// serialize/deserialize) and adds multi-session support with named files
/// in a session directory.
class MultiSessionManager {
public:
    MultiSessionManager();
    ~MultiSessionManager();

    /// List all saved sessions in the given directory (or default directory).
    /// Results are sorted newest-first.
    std::vector<SessionInfo> listSessions(const std::string& dir = "");

    /// Save the current Mux + pane state to a named session file.
    /// @param mux        The multiplexer holding workspace/tab/pane structure
    /// @param provider   Provides per-pane state (working dir, scrollback, etc.)
    /// @param path       Full file path, or empty to auto-generate in default dir
    /// @param window     Optional window geometry to persist
    /// @returns true on success
    bool saveSession(const Mux& mux,
                     const IPaneStateProvider& provider,
                     const std::string& path = "",
                     const WindowGeometry& window = {});

    /// Restore a session from the given file path.
    /// Populates `outData` with the deserialized session.  The caller is
    /// responsible for rebuilding the Mux/pane tree from the returned data.
    /// @param path  Full path to a session file
    /// @param outData  Receives the loaded session
    /// @returns true on success
    bool restoreSession(const std::string& path, SessionData& outData);

    /// Delete a saved session file.
    bool deleteSession(const std::string& path);

    /// Generate a unique session name based on the current timestamp.
    static std::string generateSessionName();

    /// Resolve the sessions directory (platform default or user-supplied).
    static std::string resolveSessionDir(const std::string& dir = "");

private:
    /// Build a full file path from directory + session name.
    static std::string buildFilePath(const std::string& dir,
                                     const std::string& name);
};

} // namespace termcore
