#import "AppDelegate.h"
#import "TerminalView.h"
#import <Metal/Metal.h>

@implementation AppDelegate {
    TerminalView* _terminalView;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // --- Metal device ---
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSLog(@"BreadTerminal: Metal is not supported on this machine.");
        [NSApp terminate:nil];
        return;
    }

    // --- Window ---
    NSRect frame = NSMakeRect(0, 0, 800, 600);
    NSWindowStyleMask style = NSWindowStyleMaskTitled
                            | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable
                            | NSWindowStyleMaskResizable;

    self.mainWindow = [[NSWindow alloc] initWithContentRect:frame
                                                  styleMask:style
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    self.mainWindow.title = @"BreadTerminal";
    [self.mainWindow center];
    self.mainWindow.minSize = NSMakeSize(320, 240);

    // --- Terminal view ---
    _terminalView = [[TerminalView alloc] initWithFrame:frame device:device];
    self.mainWindow.contentView = _terminalView;

    // --- Show & focus ---
    [self.mainWindow makeKeyAndOrderFront:nil];
    [self.mainWindow makeFirstResponder:_terminalView];

    // --- Start shell ---
    [_terminalView startShell];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end
