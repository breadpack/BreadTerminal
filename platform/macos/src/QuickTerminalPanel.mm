#import "QuickTerminalPanel.h"
#import "TerminalView.h"

#include "termcore/config.h"

#import <QuartzCore/QuartzCore.h>
#include <Carbon/Carbon.h>

/// Height of the quick terminal as a fraction of the screen.
static const CGFloat kQuickTerminalHeightFraction = 0.4;

/// Animation duration in seconds.
static const NSTimeInterval kAnimationDuration = 0.25;

// ---------------------------------------------------------------------------
// Hotkey string parser (e.g., "ctrl+`" -> modifiers + keyCode)
// ---------------------------------------------------------------------------
static bool parseHotkeyString(NSString* str, NSEventModifierFlags* outMods, unsigned short* outKeyCode) {
    if (!str || str.length == 0) return false;

    NSArray<NSString*>* parts = [str.lowercaseString componentsSeparatedByString:@"+"];
    if (parts.count < 1) return false;

    NSEventModifierFlags mods = 0;
    NSString* keyPart = parts.lastObject;

    for (NSUInteger i = 0; i < parts.count - 1; ++i) {
        NSString* mod = [parts[i] stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceCharacterSet]];
        if ([mod isEqualToString:@"ctrl"] || [mod isEqualToString:@"control"]) {
            mods |= NSEventModifierFlagControl;
        } else if ([mod isEqualToString:@"cmd"] || [mod isEqualToString:@"command"] ||
                   [mod isEqualToString:@"super"]) {
            mods |= NSEventModifierFlagCommand;
        } else if ([mod isEqualToString:@"alt"] || [mod isEqualToString:@"option"] ||
                   [mod isEqualToString:@"opt"]) {
            mods |= NSEventModifierFlagOption;
        } else if ([mod isEqualToString:@"shift"]) {
            mods |= NSEventModifierFlagShift;
        }
    }

    keyPart = [keyPart stringByTrimmingCharactersInSet:
               [NSCharacterSet whitespaceCharacterSet]];

    // Map common key names to macOS virtual keycodes
    unsigned short kc = UINT16_MAX;
    if ([keyPart isEqualToString:@"`"] || [keyPart isEqualToString:@"grave"]) {
        kc = kVK_ANSI_Grave;
    } else if ([keyPart isEqualToString:@"space"]) {
        kc = kVK_Space;
    } else if ([keyPart isEqualToString:@"escape"] || [keyPart isEqualToString:@"esc"]) {
        kc = kVK_Escape;
    } else if ([keyPart isEqualToString:@"tab"]) {
        kc = kVK_Tab;
    } else if ([keyPart isEqualToString:@"return"] || [keyPart isEqualToString:@"enter"]) {
        kc = kVK_Return;
    } else if (keyPart.length == 1) {
        // Map single characters to their virtual keycodes
        unichar ch = [keyPart characterAtIndex:0];
        // Use a lookup for common ASCII keys
        static const unsigned short asciiToKeyCode[128] = {
            [0 ... 127] = UINT16_MAX,
            ['a'] = kVK_ANSI_A, ['b'] = kVK_ANSI_B, ['c'] = kVK_ANSI_C,
            ['d'] = kVK_ANSI_D, ['e'] = kVK_ANSI_E, ['f'] = kVK_ANSI_F,
            ['g'] = kVK_ANSI_G, ['h'] = kVK_ANSI_H, ['i'] = kVK_ANSI_I,
            ['j'] = kVK_ANSI_J, ['k'] = kVK_ANSI_K, ['l'] = kVK_ANSI_L,
            ['m'] = kVK_ANSI_M, ['n'] = kVK_ANSI_N, ['o'] = kVK_ANSI_O,
            ['p'] = kVK_ANSI_P, ['q'] = kVK_ANSI_Q, ['r'] = kVK_ANSI_R,
            ['s'] = kVK_ANSI_S, ['t'] = kVK_ANSI_T, ['u'] = kVK_ANSI_U,
            ['v'] = kVK_ANSI_V, ['w'] = kVK_ANSI_W, ['x'] = kVK_ANSI_X,
            ['y'] = kVK_ANSI_Y, ['z'] = kVK_ANSI_Z,
            ['1'] = kVK_ANSI_1, ['2'] = kVK_ANSI_2, ['3'] = kVK_ANSI_3,
            ['4'] = kVK_ANSI_4, ['5'] = kVK_ANSI_5, ['6'] = kVK_ANSI_6,
            ['7'] = kVK_ANSI_7, ['8'] = kVK_ANSI_8, ['9'] = kVK_ANSI_9,
            ['0'] = kVK_ANSI_0,
            ['-'] = kVK_ANSI_Minus, ['='] = kVK_ANSI_Equal,
            ['['] = kVK_ANSI_LeftBracket, [']'] = kVK_ANSI_RightBracket,
            ['\\'] = kVK_ANSI_Backslash, [';'] = kVK_ANSI_Semicolon,
            ['\''] = kVK_ANSI_Quote, [','] = kVK_ANSI_Comma,
            ['.'] = kVK_ANSI_Period, ['/'] = kVK_ANSI_Slash,
        };
        if (ch < 128) {
            kc = asciiToKeyCode[ch];
        }
    }

    if (kc == UINT16_MAX) return false;

    *outMods = mods;
    *outKeyCode = kc;
    return true;
}

// ---------------------------------------------------------------------------
// QuickTerminalPanel implementation
// ---------------------------------------------------------------------------

@implementation QuickTerminalPanel {
    NSPanel* _panel;
    TerminalView* _terminalView;
    id<MTLDevice> _device;
    BOOL _isVisible;
    BOOL _isAnimating;
    id _globalMonitor;
    id _localMonitor;
    NSEventModifierFlags _hotkeyMods;
    unsigned short _hotkeyKeyCode;
}

@synthesize isVisible = _isVisible;
@synthesize terminalView = _terminalView;

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (!self) return nil;

    _device = device;
    _isVisible = NO;
    _isAnimating = NO;

    [self createPanel];

    return self;
}

- (void)dealloc {
    [self unregisterGlobalHotkey];
}

- (void)createPanel {
    NSScreen* screen = [NSScreen mainScreen];
    NSRect screenFrame = screen.visibleFrame;
    CGFloat panelHeight = screenFrame.size.height * kQuickTerminalHeightFraction;

    // Start offscreen (above the visible area)
    NSRect panelFrame = NSMakeRect(
        screenFrame.origin.x,
        screenFrame.origin.y + screenFrame.size.height,
        screenFrame.size.width,
        panelHeight);

    _panel = [[NSPanel alloc]
        initWithContentRect:panelFrame
                  styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                    backing:NSBackingStoreBuffered
                      defer:NO];

    _panel.level = NSStatusWindowLevel;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                 NSWindowCollectionBehaviorFullScreenAuxiliary;
    _panel.hasShadow = YES;
    _panel.opaque = NO;
    _panel.backgroundColor = [NSColor clearColor];
    _panel.hidesOnDeactivate = NO;
    _panel.floatingPanel = YES;
    _panel.becomesKeyOnlyIfNeeded = NO;

    // Create the terminal view filling the panel
    _terminalView = [[TerminalView alloc]
        initWithFrame:_panel.contentView.bounds
               device:_device];
    _terminalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_panel.contentView addSubview:_terminalView];
}

- (void)toggle {
    if (_isAnimating) return;

    if (_isVisible) {
        [self hide];
    } else {
        [self show];
    }
}

- (void)show {
    if (_isVisible || _isAnimating) return;
    _isAnimating = YES;

    NSScreen* screen = [NSScreen mainScreen];
    NSRect screenFrame = screen.visibleFrame;
    CGFloat panelHeight = screenFrame.size.height * kQuickTerminalHeightFraction;

    // Set initial position: above visible area
    NSRect startFrame = NSMakeRect(
        screenFrame.origin.x,
        screenFrame.origin.y + screenFrame.size.height,
        screenFrame.size.width,
        panelHeight);
    [_panel setFrame:startFrame display:NO];
    [_panel orderFront:nil];

    // Target position: top of visible area
    NSRect endFrame = NSMakeRect(
        screenFrame.origin.x,
        screenFrame.origin.y + screenFrame.size.height - panelHeight,
        screenFrame.size.width,
        panelHeight);

    [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
        context.duration = kAnimationDuration;
        context.timingFunction = [CAMediaTimingFunction
            functionWithName:kCAMediaTimingFunctionEaseOut];
        [self->_panel.animator setFrame:endFrame display:YES];
    } completionHandler:^{
        self->_isVisible = YES;
        self->_isAnimating = NO;
        [self->_panel makeKeyAndOrderFront:nil];
        [self->_panel makeFirstResponder:self->_terminalView];
    }];
}

- (void)hide {
    if (!_isVisible || _isAnimating) return;
    _isAnimating = YES;

    NSScreen* screen = [NSScreen mainScreen];
    NSRect screenFrame = screen.visibleFrame;
    CGFloat panelHeight = _panel.frame.size.height;

    // Slide up above the screen
    NSRect endFrame = NSMakeRect(
        screenFrame.origin.x,
        screenFrame.origin.y + screenFrame.size.height,
        screenFrame.size.width,
        panelHeight);

    [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
        context.duration = kAnimationDuration;
        context.timingFunction = [CAMediaTimingFunction
            functionWithName:kCAMediaTimingFunctionEaseIn];
        [self->_panel.animator setFrame:endFrame display:YES];
    } completionHandler:^{
        [self->_panel orderOut:nil];
        self->_isVisible = NO;
        self->_isAnimating = NO;
    }];
}

- (BOOL)registerGlobalHotkey:(NSString*)hotkeyString {
    [self unregisterGlobalHotkey];

    NSEventModifierFlags mods = 0;
    unsigned short keyCode = 0;
    if (!parseHotkeyString(hotkeyString, &mods, &keyCode)) {
        NSLog(@"QuickTerminalPanel: failed to parse hotkey '%@'", hotkeyString);
        return NO;
    }

    _hotkeyMods = mods;
    _hotkeyKeyCode = keyCode;

    // Mask for the modifier flags we care about
    NSEventModifierFlags modMask = NSEventModifierFlagControl |
                                    NSEventModifierFlagCommand |
                                    NSEventModifierFlagOption |
                                    NSEventModifierFlagShift;

    __weak QuickTerminalPanel* weakSelf = self;

    // Global monitor (when app is not active)
    _globalMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                            handler:^(NSEvent* event) {
        QuickTerminalPanel* s = weakSelf;
        if (!s) return;
        if (event.keyCode == s->_hotkeyKeyCode &&
            (event.modifierFlags & modMask) == s->_hotkeyMods) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [s toggle];
            });
        }
    }];

    // Local monitor (when app is active)
    _localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                         handler:^NSEvent*(NSEvent* event) {
        QuickTerminalPanel* s = weakSelf;
        if (!s) return event;
        if (event.keyCode == s->_hotkeyKeyCode &&
            (event.modifierFlags & modMask) == s->_hotkeyMods) {
            [s toggle];
            return nil;  // consume the event
        }
        return event;
    }];

    NSLog(@"QuickTerminalPanel: registered hotkey '%@'", hotkeyString);
    return YES;
}

- (void)unregisterGlobalHotkey {
    if (_globalMonitor) {
        [NSEvent removeMonitor:_globalMonitor];
        _globalMonitor = nil;
    }
    if (_localMonitor) {
        [NSEvent removeMonitor:_localMonitor];
        _localMonitor = nil;
    }
}

@end
