#ifndef BREADTERMINAL_APP_DELEGATE_H
#define BREADTERMINAL_APP_DELEGATE_H

#import <Cocoa/Cocoa.h>

@protocol SidebarViewControllerDelegate;

@interface AppDelegate : NSObject <NSApplicationDelegate, SidebarViewControllerDelegate>

@property (strong) NSWindow* mainWindow;

/// Toggle sidebar visibility (Cmd+Shift+B).
- (IBAction)toggleSidebar:(id)sender;

/// Open the Preferences window (Cmd+,).
- (IBAction)openPreferences:(id)sender;

/// Open the Theme Hub window (Cmd+Shift+T).
- (IBAction)openThemeHub:(id)sender;

/// Open the Font Hub window (Cmd+Shift+P).
- (IBAction)openFontHub:(id)sender;

/// Create a new tab (Cmd+T).
- (IBAction)newTab:(id)sender;

/// Close the current tab (Cmd+W).
- (IBAction)closeTab:(id)sender;

@end

#endif // BREADTERMINAL_APP_DELEGATE_H
