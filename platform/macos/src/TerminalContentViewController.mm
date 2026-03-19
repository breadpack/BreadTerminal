#import "TerminalContentViewController.h"
#import "TerminalView.h"

@implementation TerminalContentViewController {
    id<MTLDevice> _device;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _device = device;
        // Create terminal view immediately (not lazily via loadView)
        // so it's available before the view hierarchy is set up
        NSRect frame = NSMakeRect(0, 0, 600, 400);
        _terminalView = [[TerminalView alloc] initWithFrame:frame device:_device];
    }
    return self;
}

- (void)loadView {
    self.view = _terminalView;
}

- (void)applyConfig:(const termcore::Config&)config {
    [_terminalView applyConfig:config];
}

- (void)startShell {
    [_terminalView startShell];
}

@end
