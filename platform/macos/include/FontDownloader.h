#ifndef BREADTERMINAL_FONT_DOWNLOADER_H
#define BREADTERMINAL_FONT_DOWNLOADER_H

#import <Cocoa/Cocoa.h>

typedef void (^FontDownloadProgress)(double fractionCompleted);
typedef void (^FontDownloadCompletion)(BOOL success, NSError* _Nullable error);

@interface FontDownloader : NSObject

+ (instancetype)sharedDownloader;

/// Download a font from the given URL and install it to ~/Library/Fonts/.
/// For .zip archives, extracts .ttf/.otf files and registers them with CoreText.
/// @param fontName  Display name (used for logging)
/// @param urlString Download URL
/// @param progress  Optional progress callback (called on main thread)
/// @param completion Called on main thread when done
- (void)downloadFont:(NSString*)fontName
             fromURL:(NSString*)urlString
            progress:(FontDownloadProgress _Nullable)progress
          completion:(FontDownloadCompletion)completion;

@end

#endif
