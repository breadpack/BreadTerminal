#import "AppDelegate.h"
#import "TerminalView.h"
#import <Metal/Metal.h>

#include "termcore/config.h"

@implementation AppDelegate {
    TerminalView* _terminalView;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // --- Load config ---
    std::string configPath = termcore::defaultConfigPath();
    termcore::Config config = termcore::parseConfigFile(configPath);
    if (!config.theme.empty()) {
        auto* theme = termcore::getBuiltinTheme(config.theme);
        if (theme) termcore::applyTheme(config, *theme);
    }

    // --- Metal device ---
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSLog(@"BreadTerminal: Metal is not supported on this machine.");
        [NSApp terminate:nil];
        return;
    }

    // --- Window ---
    int winW = config.window_width  > 0 ? config.window_width  : 800;
    int winH = config.window_height > 0 ? config.window_height : 600;
    NSRect frame = NSMakeRect(0, 0, winW, winH);
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
    [_terminalView applyConfig:config];
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
