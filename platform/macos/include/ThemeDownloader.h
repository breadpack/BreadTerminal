#ifndef BREADTERMINAL_THEME_DOWNLOADER_H
#define BREADTERMINAL_THEME_DOWNLOADER_H

#import <Cocoa/Cocoa.h>

typedef void (^ThemeDownloadCompletion)(BOOL success, NSError* _Nullable error);

@interface ThemeDownloader : NSObject
+ (instancetype)sharedDownloader;
- (void)downloadTheme:(NSString*)name
              fromURL:(NSString*)urlString
           completion:(ThemeDownloadCompletion)completion;
@end

#endif
