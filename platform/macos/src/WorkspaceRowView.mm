#import "WorkspaceRowView.h"
#include "termcore/agent.h"
#include <string>

// Agent state to NSColor mapping
static NSColor* colorForAgentState(termcore::AgentState state) {
    switch (state) {
        case termcore::AgentState::Inactive:
            return [NSColor clearColor];
        case termcore::AgentState::Idle:
            return [NSColor systemGrayColor];
        case termcore::AgentState::Running:
            return [NSColor systemBlueColor];
        case termcore::AgentState::NeedsInput:
            return [NSColor systemOrangeColor];
        case termcore::AgentState::Starting:
            return [NSColor systemYellowColor];
        case termcore::AgentState::Exited:
            return [NSColor systemRedColor];
    }
    return [NSColor clearColor];
}

@implementation WorkspaceRowView {
    uint32_t _workspaceId;
    NSString* _name;
    NSString* _gitBranch;
    termcore::AgentState _agentState;
    size_t _notificationCount;
    BOOL _isActive;
    uint32_t _workspaceNumber;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _name = @"";
        _gitBranch = @"";
        _agentState = termcore::AgentState::Inactive;
        _notificationCount = 0;
        _isActive = NO;
        _workspaceNumber = 0;
    }
    return self;
}

- (void)updateWithSnapshot:(const termcore::WorkspaceStatusSnapshot&)snapshot
                  isActive:(BOOL)active {
    _workspaceId = snapshot.id;
    _name = [NSString stringWithUTF8String:snapshot.name.c_str()];
    _gitBranch = [NSString stringWithUTF8String:snapshot.git_branch.c_str()];
    _agentState = snapshot.dominant_agent_state;
    _notificationCount = snapshot.unread_notification_count;
    _isActive = active;
    _workspaceNumber = snapshot.id;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    NSRect bounds = self.bounds;
    CGFloat padding = 8.0;
    CGFloat y = bounds.origin.y;
    CGFloat rowHeight = bounds.size.height;

    // Background highlight for active workspace
    if (_isActive) {
        NSColor* highlight = [NSColor selectedContentBackgroundColor];
        [highlight setFill];
        NSBezierPath* bgPath = [NSBezierPath bezierPathWithRoundedRect:
            NSInsetRect(bounds, 4, 2) xRadius:6 yRadius:6];
        [bgPath fill];
    }

    // --- Workspace number badge (rounded rect) ---
    CGFloat badgeSize = 22.0;
    CGFloat badgeX = padding;
    CGFloat badgeY = y + (rowHeight - badgeSize) / 2.0;
    NSRect badgeRect = NSMakeRect(badgeX, badgeY, badgeSize, badgeSize);

    NSColor* badgeColor = _isActive
        ? [NSColor controlAccentColor]
        : [NSColor tertiaryLabelColor];
    [badgeColor setFill];
    NSBezierPath* badgePath = [NSBezierPath bezierPathWithRoundedRect:badgeRect
                                                              xRadius:5
                                                              yRadius:5];
    [badgePath fill];

    // Badge number text
    NSString* numStr = [NSString stringWithFormat:@"%u", _workspaceNumber];
    NSDictionary* numAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:11],
        NSForegroundColorAttributeName: [NSColor whiteColor],
    };
    NSSize numSize = [numStr sizeWithAttributes:numAttrs];
    NSPoint numPoint = NSMakePoint(
        badgeX + (badgeSize - numSize.width) / 2.0,
        badgeY + (badgeSize - numSize.height) / 2.0);
    [numStr drawAtPoint:numPoint withAttributes:numAttrs];

    // --- Text area (to the right of badge) ---
    CGFloat textX = badgeX + badgeSize + 8.0;
    CGFloat textWidth = bounds.size.width - textX - padding - 20.0; // reserve space for dots/badges

    // Workspace name (bold 13pt)
    NSDictionary* nameAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:13],
        NSForegroundColorAttributeName: _isActive
            ? [NSColor labelColor]
            : [NSColor secondaryLabelColor],
    };
    NSString* displayName = (_name.length > 0) ? _name : @"Workspace";
    CGFloat nameY = y + rowHeight / 2.0 + 1.0;
    if (_gitBranch.length > 0) {
        // Two-line layout: name on top, branch below
        nameY = y + rowHeight / 2.0 + 2.0;
    }
    [displayName drawInRect:NSMakeRect(textX, nameY, textWidth, 18)
             withAttributes:nameAttrs];

    // Git branch (11pt gray, below name)
    if (_gitBranch.length > 0) {
        NSDictionary* branchAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11],
            NSForegroundColorAttributeName: [NSColor tertiaryLabelColor],
        };
        NSString* branchDisplay = [NSString stringWithFormat:@"\u{E0A0} %@", _gitBranch];
        CGFloat branchY = nameY - 16.0;
        [branchDisplay drawInRect:NSMakeRect(textX, branchY, textWidth, 15)
                   withAttributes:branchAttrs];
    }

    // --- Agent state dot (colored circle on the right) ---
    if (_agentState != termcore::AgentState::Inactive) {
        CGFloat dotSize = 8.0;
        CGFloat dotX = bounds.size.width - padding - dotSize - 2.0;
        CGFloat dotY = y + (rowHeight - dotSize) / 2.0;
        if (_notificationCount > 0) {
            dotY += 10.0; // shift up if badge present
        }
        NSRect dotRect = NSMakeRect(dotX, dotY, dotSize, dotSize);
        NSColor* dotColor = colorForAgentState(_agentState);
        [dotColor setFill];
        [[NSBezierPath bezierPathWithOvalInRect:dotRect] fill];
    }

    // --- Notification badge (red circle with count) ---
    if (_notificationCount > 0) {
        CGFloat badgeH = 16.0;
        NSString* countStr = [NSString stringWithFormat:@"%zu", _notificationCount];
        NSDictionary* countAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:10],
            NSForegroundColorAttributeName: [NSColor whiteColor],
        };
        NSSize countSize = [countStr sizeWithAttributes:countAttrs];
        CGFloat badgeW = MAX(badgeH, countSize.width + 8.0);
        CGFloat nbX = bounds.size.width - padding - badgeW;
        CGFloat nbY = y + (rowHeight - badgeH) / 2.0 - 8.0;
        NSRect nbRect = NSMakeRect(nbX, nbY, badgeW, badgeH);

        [[NSColor systemRedColor] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:nbRect
                                         xRadius:badgeH / 2.0
                                         yRadius:badgeH / 2.0] fill];

        NSPoint countPoint = NSMakePoint(
            nbX + (badgeW - countSize.width) / 2.0,
            nbY + (badgeH - countSize.height) / 2.0);
        [countStr drawAtPoint:countPoint withAttributes:countAttrs];
    }
}

@end
