#ifndef BREADTERMINAL_QUICK_TERMINAL_PANEL_H
#define BREADTERMINAL_QUICK_TERMINAL_PANEL_H

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>

@class TerminalView;

/// A visor-style terminal panel that slides down from the top of the screen.
/// Activated via a configurable global hotkey.
@interface QuickTerminalPanel : NSObject

/// Designated initializer.
/// @param device The Metal device for the embedded TerminalView.
- (instancetype)initWithDevice:(id<MTLDevice>)device;

/// Toggle the panel visibility (slide in/out).
- (void)toggle;

/// Show the panel (slide down).
- (void)show;

/// Hide the panel (slide up).
- (void)hide;

/// Whether the panel is currently visible.
@property (nonatomic, readonly) BOOL isVisible;

/// The embedded terminal view.
@property (nonatomic, readonly) TerminalView* terminalView;

/// Register a global hotkey monitor for the given hotkey string (e.g., "ctrl+`").
/// Returns YES if successfully registered.
- (BOOL)registerGlobalHotkey:(NSString*)hotkeyString;

/// Unregister any active global hotkey monitor.
- (void)unregisterGlobalHotkey;

@end

#endif // BREADTERMINAL_QUICK_TERMINAL_PANEL_H
