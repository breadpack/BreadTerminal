#import "TerminalContentViewController.h"
#import "TerminalView.h"

@implementation TerminalContentViewController {
    id<MTLDevice> _device;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _device = device;
    }
    return self;
}

- (void)loadView {
    NSRect frame = NSMakeRect(0, 0, 600, 400);
    _terminalView = [[TerminalView alloc] initWithFrame:frame device:_device];
    self.view = _terminalView;
}

- (void)applyConfig:(const termcore::Config&)config {
    [_terminalView applyConfig:config];
}

- (void)startShell {
    [_terminalView startShell];
}

@end
