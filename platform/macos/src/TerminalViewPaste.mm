#import "TerminalViewImpl.h"
#include "termcore/paste_guard.h"
#include "termcore/screen.h"
#include "termcore/pty.h"

@implementation TerminalView (Paste)

- (void)executePaste:(NSString*)text bracketed:(BOOL)bracketed {
    if (!text || text.length == 0) return;
    const char* utf8 = [text UTF8String];
    size_t len = strlen(utf8);
    if (bracketed) {
        _impl->pty->write("\033[200~", 6);
        _impl->pty->write(utf8, len);
        _impl->pty->write("\033[201~", 6);
    } else {
        _impl->pty->write(utf8, len);
    }
}

- (void)confirmPaste:(NSString*)text
            analysis:(const termcore::PasteAnalysis&)analysis
           bracketed:(BOOL)bracketed {
    // Build preview (truncate to 2000 chars)
    NSString* preview = text;
    if (preview.length > 2000) {
        preview = [NSString stringWithFormat:@"%@\n... (%lu more characters)",
                   [text substringToIndex:2000], (unsigned long)(text.length - 2000)];
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Paste Protection";

    // Build informative text
    NSMutableString* info = [NSMutableString string];
    if (analysis.line_count > 1)
        [info appendFormat:@"\u26A0 Multi-line paste (%d lines)\n", analysis.line_count];
    if (analysis.ends_with_newline)
        [info appendString:@"\u26A0 Ends with newline (will auto-execute)\n"];
    if (analysis.signals & static_cast<uint32_t>(termcore::PasteSignal::SudoCommand))
        [info appendString:@"\u26A0 Contains sudo command\n"];
    if (analysis.signals & static_cast<uint32_t>(termcore::PasteSignal::RmRf))
        [info appendString:@"\u26A0 Contains rm -rf\n"];
    if (analysis.signals & static_cast<uint32_t>(termcore::PasteSignal::CurlPipe))
        [info appendString:@"\u26A0 Contains curl/wget piped to shell\n"];
    if (analysis.signals & static_cast<uint32_t>(termcore::PasteSignal::HomeDirectoryWipe))
        [info appendString:@"\u26A0 May wipe home directory\n"];

    alert.informativeText = info;
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"Paste Anyway"];
    [alert addButtonWithTitle:@"Cancel"];

    // Add scrollable text view with preview
    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 150)];
    scrollView.hasVerticalScroller = YES;
    NSTextView* textView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 400, 150)];
    textView.editable = NO;
    textView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    [textView.textStorage setAttributedString:[[NSAttributedString alloc] initWithString:preview]];
    scrollView.documentView = textView;
    alert.accessoryView = scrollView;

    // Show as sheet
    __weak TerminalView* weakSelf = self;
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        TerminalView* s = weakSelf;
        if (!s) return;
        if (response == NSAlertFirstButtonReturn) {
            [s executePaste:text bracketed:bracketed];
        }
    }];
}

@end
