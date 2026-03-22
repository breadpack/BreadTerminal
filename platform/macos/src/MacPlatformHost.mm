#import "MacPlatformHost.h"
#import "TerminalView.h"

#include "termcore/pty.h"

#import <UserNotifications/UserNotifications.h>

MacPlatformHost::MacPlatformHost(TerminalView* view, NSWindow* window)
    : view_(view)
    , window_(window)
{}

// --- Rendering ---

void MacPlatformHost::invalidate() {
    TerminalView* v = view_;
    if (v) [v setNeedsRender];
}

void MacPlatformHost::getViewportSize(int& w, int& h) {
    TerminalView* v = view_;
    if (!v) { w = 0; h = 0; return; }
    NSWindow* win = window_;
    CGFloat scale = win ? win.backingScaleFactor : 2.0;
    NSSize bounds = v.bounds.size;
    w = static_cast<int>(bounds.width * scale);
    h = static_cast<int>(bounds.height * scale);
}

// --- Clipboard ---

std::string MacPlatformHost::getClipboardText() {
    NSString* text = [[NSPasteboard generalPasteboard]
                       stringForType:NSPasteboardTypeString];
    return text ? std::string([text UTF8String]) : std::string();
}

void MacPlatformHost::setClipboardText(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:[NSString stringWithUTF8String:text.c_str()]
          forType:NSPasteboardTypeString];
}

// --- Window ---

void MacPlatformHost::setWindowTitle(const std::string& title) {
    NSWindow* win = window_;
    if (!win) return;
    NSString* t = [NSString stringWithUTF8String:title.c_str()];
    if (t && ![win.title isEqualToString:t]) {
        win.title = t;
    }
}

void MacPlatformHost::toggleFullscreen() {
    NSWindow* win = window_;
    if (win) [win toggleFullScreen:nil];
}

void MacPlatformHost::closeWindow() {
    NSWindow* win = window_;
    if (win) [win close];
}

void MacPlatformHost::showConfirmDialog(const std::string& msg,
                                         std::function<void(bool)> cb) {
    NSWindow* win = window_;
    if (!win) { if (cb) cb(false); return; }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Confirmation";
    alert.informativeText = [NSString stringWithUTF8String:msg.c_str()];
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    auto callback = std::move(cb);
    [alert beginSheetModalForWindow:win
                  completionHandler:^(NSModalResponse response) {
        if (callback) {
            callback(response == NSAlertFirstButtonReturn);
        }
    }];
}

// --- Search UI ---

void MacPlatformHost::showSearchBar() {
    TerminalView* v = view_;
    if (v) [v openSearch];
}

void MacPlatformHost::hideSearchBar() {
    TerminalView* v = view_;
    if (v) [v closeSearch];
}

void MacPlatformHost::updateSearchResults(int current, int total) {
    // Search result count display is not yet wired in macOS search bar.
    // The search field is a simple NSTextField without a results label.
    (void)current;
    (void)total;
}

// --- IME ---

void MacPlatformHost::positionIME(int x, int y, int height) {
    // IME positioning is handled natively by NSTextInputClient protocol.
    // firstRectForCharacterRange: returns cursor position.
    (void)x;
    (void)y;
    (void)height;
}

// --- Font/color update notifications ---

void MacPlatformHost::onFontChanged(float cellW, float cellH) {
    TerminalView* v = view_;
    if (v) [v onCellSizeChanged:cellW height:cellH];
}

void MacPlatformHost::onColorsChanged() {
    TerminalView* v = view_;
    if (v) [v setNeedsRender];
}

void MacPlatformHost::onGridSizeChanged(int rows, int cols) {
    // Grid size changes are driven by the controller's onResize.
    // Renderer is updated during renderFrame.
    (void)rows;
    (void)cols;
}

// --- Notifications ---

void MacPlatformHost::showNotification(const std::string& title,
                                        const std::string& body) {
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];

    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError* _Nullable error) {
        if (!granted) return;

        UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
        content.title = [NSString stringWithUTF8String:title.c_str()];
        content.body = [NSString stringWithUTF8String:body.c_str()];
        content.sound = [UNNotificationSound defaultSound];

        UNNotificationRequest* request =
            [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                 content:content
                                                 trigger:nil];
        [center addNotificationRequest:request withCompletionHandler:nil];
    }];
}

// --- Settings/Hub windows ---

void MacPlatformHost::openSettingsWindow(const termcore::Config& config) {
    (void)config;
    [[NSNotificationCenter defaultCenter]
        postNotificationName:@"BreadTerminalOpenSettings" object:nil];
}

// --- DPI ---

float MacPlatformHost::dpiScale() {
    NSWindow* win = window_;
    return win ? static_cast<float>(win.backingScaleFactor) : 2.0f;
}

// --- PTY factory ---

std::unique_ptr<termcore::Pty> MacPlatformHost::createPty(
        const termcore::Profile& profile, int rows, int cols) {
    auto pty = termcore::createPty();
    std::string workDir = profile.working_dir;
    if (workDir.empty()) {
        if (const char* home = std::getenv("HOME")) workDir = home;
    }
    if (!pty->spawn(profile.command, profile.args, workDir, rows, cols)) {
        NSLog(@"BreadTerminal: failed to spawn shell for pane");
    }
    return pty;
}
