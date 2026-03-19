#ifndef BREADTERMINAL_TERMINAL_CONTENT_VIEW_CONTROLLER_H
#define BREADTERMINAL_TERMINAL_CONTENT_VIEW_CONTROLLER_H

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#include "termcore/config.h"

@class TerminalView;

@interface TerminalContentViewController : NSViewController

/// The underlying terminal view.
@property (nonatomic, readonly, strong) TerminalView* terminalView;

/// Initialize with a Metal device.
- (instancetype)initWithDevice:(id<MTLDevice>)device;

/// Apply configuration to the terminal view.
- (void)applyConfig:(const termcore::Config&)config;

/// Start a shell session.
- (void)startShell;

@end

#endif // BREADTERMINAL_TERMINAL_CONTENT_VIEW_CONTROLLER_H
