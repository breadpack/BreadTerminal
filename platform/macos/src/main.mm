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
        [appMenu addItemWithTitle:@"Preferences..."
                           action:@selector(openPreferences:)
                    keyEquivalent:@","];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Quit BreadTerminal"
                           action:@selector(terminate:)
                    keyEquivalent:@"q"];
        appMenuItem.submenu = appMenu;

        // Shell menu (tabs)
        NSMenuItem* shellMenuItem = [[NSMenuItem alloc] init];
        [menuBar addItem:shellMenuItem];
        NSMenu* shellMenu = [[NSMenu alloc] initWithTitle:@"Shell"];
        [shellMenu addItemWithTitle:@"New Tab" action:@selector(newTab:) keyEquivalent:@"t"];
        [shellMenu addItemWithTitle:@"Close Tab" action:@selector(closeTab:) keyEquivalent:@"w"];
        [shellMenu addItem:[NSMenuItem separatorItem]];
        {
            NSMenuItem* nextTab = [shellMenu addItemWithTitle:@"Show Next Tab"
                                                       action:@selector(selectNextTab:)
                                                keyEquivalent:@"}"];
            nextTab.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        }
        {
            NSMenuItem* prevTab = [shellMenu addItemWithTitle:@"Show Previous Tab"
                                                       action:@selector(selectPreviousTab:)
                                                keyEquivalent:@"{"];
            prevTab.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        }
        [shellMenu addItem:[NSMenuItem separatorItem]];
        for (int i = 1; i <= 9; i++) {
            NSString* title = [NSString stringWithFormat:@"Select Tab %d", i];
            NSString* key = [NSString stringWithFormat:@"%d", i];
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                          action:@selector(selectTabByNumber:)
                                                   keyEquivalent:key];
            item.tag = i;
            [shellMenu addItem:item];
        }
        shellMenuItem.submenu = shellMenu;

        // Edit menu (needed for Cmd+C / Cmd+V to be routed correctly)
        NSMenuItem* editMenuItem = [[NSMenuItem alloc] init];
        [menuBar addItem:editMenuItem];
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
        [editMenu addItemWithTitle:@"Copy"  action:@selector(copy:)  keyEquivalent:@"c"];
        [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
        [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
        editMenuItem.submenu = editMenu;

        // Window menu (for macOS tab support)
        NSMenuItem* windowMenuItem = [[NSMenuItem alloc] init];
        [menuBar addItem:windowMenuItem];
        NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
        [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
        [windowMenu addItem:[NSMenuItem separatorItem]];
        [windowMenu addItemWithTitle:@"Show All Tabs" action:@selector(toggleTabOverview:) keyEquivalent:@""];
        windowMenuItem.submenu = windowMenu;
        app.windowsMenu = windowMenu;  // Required for native tab management

        app.mainMenu = menuBar;

        [app run];
    }
    return 0;
}
