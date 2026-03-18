#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;

        // --- Minimal menu bar ---
        NSMenu* menuBar = [[NSMenu alloc] init];

        // App menu
        NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
        [menuBar addItem:appMenuItem];
        NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"BreadTerminal"];
        [appMenu addItemWithTitle:@"About BreadTerminal"
                           action:@selector(orderFrontStandardAboutPanel:)
                    keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Quit BreadTerminal"
                           action:@selector(terminate:)
                    keyEquivalent:@"q"];
        appMenuItem.submenu = appMenu;

        // Edit menu (needed for Cmd+C / Cmd+V to be routed correctly)
        NSMenuItem* editMenuItem = [[NSMenuItem alloc] init];
        [menuBar addItem:editMenuItem];
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
        [editMenu addItemWithTitle:@"Copy"  action:@selector(copy:)  keyEquivalent:@"c"];
        [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
        [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
        editMenuItem.submenu = editMenu;

        app.mainMenu = menuBar;

        [app run];
    }
    return 0;
}
