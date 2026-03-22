#include "WaylandPlatformHost.h"
#include "termcore/pty.h"

#include <cstdio>

namespace termcore {

WaylandPlatformHost::WaylandPlatformHost() = default;
WaylandPlatformHost::~WaylandPlatformHost() = default;

bool WaylandPlatformHost::init() {
    if (!window_.init()) {
        return false;
    }

    if (!window_.createEGLContext()) {
        return false;
    }

    clipboard_.init(&window_.state());
    return true;
}

WaylandWindow& WaylandPlatformHost::window() {
    return window_;
}

const WaylandWindow& WaylandPlatformHost::window() const {
    return window_;
}

// ─── Rendering ─────────────────────────────────────────────────────

void WaylandPlatformHost::invalidate() {
    needsRedraw_ = true;
}

void WaylandPlatformHost::getViewportSize(int& w, int& h) {
    window_.getSize(w, h);
}

// ─── Clipboard ─────────────────────────────────────────────────────

std::string WaylandPlatformHost::getClipboardText() {
    return clipboard_.paste();
}

void WaylandPlatformHost::setClipboardText(const std::string& text) {
    if (!text.empty()) {
        clipboard_.copy(text);
    }
}

// ─── Window ────────────────────────────────────────────────────────

void WaylandPlatformHost::setWindowTitle(const std::string& title) {
    window_.setTitle(title);
}

void WaylandPlatformHost::toggleFullscreen() {
    window_.toggleFullscreen();
}

void WaylandPlatformHost::closeWindow() {
    window_.close();
}

void WaylandPlatformHost::showConfirmDialog(const std::string& msg,
                                             std::function<void(bool)> cb) {
    // Wayland has no built-in dialog API; for now log and auto-confirm.
    // A real implementation would render an in-terminal overlay or use
    // an external dialog tool (e.g., zenity).
    fprintf(stderr, "WaylandPlatformHost: confirm dialog: %s\n", msg.c_str());
    if (cb) cb(true);
}

// ─── Search UI ─────────────────────────────────────────────────────

void WaylandPlatformHost::showSearchBar() {
    // Search bar is rendered as an overlay by GLTextRenderer;
    // the host just needs to trigger a redraw.
    needsRedraw_ = true;
}

void WaylandPlatformHost::hideSearchBar() {
    needsRedraw_ = true;
}

void WaylandPlatformHost::updateSearchResults(int /*current*/, int /*total*/) {
    needsRedraw_ = true;
}

void WaylandPlatformHost::setSearchBarText(const std::string& /*text*/) {
    needsRedraw_ = true;
}

// ─── IME ───────────────────────────────────────────────────────────

void WaylandPlatformHost::positionIME(int x, int y, int height) {
    // Set the cursor rectangle so the compositor can position the IME
    // candidate window near the text cursor.  Uses zwp_text_input_v3
    // when available; otherwise a no-op (graceful fallback).
    window_.setIMECursorRect(x, y, 1, height);
}

// ─── Font/color notifications ──────────────────────────────────────

void WaylandPlatformHost::onFontChanged(float /*cellW*/, float /*cellH*/) {
    needsRedraw_ = true;
}

void WaylandPlatformHost::onColorsChanged() {
    needsRedraw_ = true;
}

void WaylandPlatformHost::onGridSizeChanged(int /*rows*/, int /*cols*/) {
    needsRedraw_ = true;
}

// ─── Notifications ─────────────────────────────────────────────────

void WaylandPlatformHost::showNotification(const std::string& title,
                                            const std::string& body) {
    // Desktop notification via freedesktop D-Bus is beyond scope;
    // log for now.
    fprintf(stderr, "BreadTerminal: notification: %s - %s\n",
            title.c_str(), body.c_str());
}

// ─── Settings ──────────────────────────────────────────────────────

void WaylandPlatformHost::openSettingsWindow(const Config& /*config*/) {
    // Settings window requires a toolkit (GTK or rendered in-terminal).
    // Not implemented in the pure Wayland path yet.
    fprintf(stderr, "BreadTerminal: settings window not available in "
                    "Wayland-only mode\n");
}

// ─── DPI ───────────────────────────────────────────────────────────

float WaylandPlatformHost::dpiScale() {
    return static_cast<float>(window_.scaleFactor());
}

// ─── PTY factory ───────────────────────────────────────────────────

std::unique_ptr<Pty> WaylandPlatformHost::createPty(
        const Profile& profile, int rows, int cols) {
    auto pty = termcore::createPty();
    if (!pty->spawn(profile.command, profile.args, profile.working_dir,
                    rows, cols)) {
        fprintf(stderr, "BreadTerminal: failed to spawn shell for pane\n");
    }
    return pty;
}

} // namespace termcore
