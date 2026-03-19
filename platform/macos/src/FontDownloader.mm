#import "FontDownloader.h"
#import <CoreText/CoreText.h>

@interface FontDownloader () <NSURLSessionDownloadDelegate>
@end

@implementation FontDownloader {
    NSURLSession* _session;
    NSMutableDictionary<NSURLSessionTask*, FontDownloadProgress>* _progressBlocks;
    NSMutableDictionary<NSURLSessionTask*, FontDownloadCompletion>* _completionBlocks;
    NSMutableDictionary<NSURLSessionTask*, NSString*>* _fontNames;
}

+ (instancetype)sharedDownloader {
    static FontDownloader* shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[FontDownloader alloc] init];
    });
    return shared;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration defaultSessionConfiguration];
        config.timeoutIntervalForRequest = 30.0;
        config.timeoutIntervalForResource = 120.0;
        _session = [NSURLSession sessionWithConfiguration:config
                                                 delegate:self
                                            delegateQueue:nil];
        _progressBlocks = [NSMutableDictionary new];
        _completionBlocks = [NSMutableDictionary new];
        _fontNames = [NSMutableDictionary new];
    }
    return self;
}

- (void)downloadFont:(NSString*)fontName
             fromURL:(NSString*)urlString
            progress:(FontDownloadProgress)progress
          completion:(FontDownloadCompletion)completion {
    if (!fontName || !urlString || fontName.length == 0 || urlString.length == 0) {
        if (completion) {
            NSError* err = [NSError errorWithDomain:@"FontDownloader"
                                               code:-1
                                           userInfo:@{NSLocalizedDescriptionKey: @"Invalid font name or URL"}];
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, err); });
        }
        return;
    }

    NSURL* url = [NSURL URLWithString:urlString];
    if (!url) {
        if (completion) {
            NSError* err = [NSError errorWithDomain:@"FontDownloader"
                                               code:-2
                                           userInfo:@{NSLocalizedDescriptionKey: @"Invalid URL"}];
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, err); });
        }
        return;
    }

    // Enforce HTTPS to prevent MITM attacks
    if (![url.scheme.lowercaseString isEqualToString:@"https"]) {
        if (completion) {
            NSError* err = [NSError errorWithDomain:@"FontDownloader"
                                               code:-6
                                           userInfo:@{NSLocalizedDescriptionKey: @"Only HTTPS URLs are allowed for font downloads"}];
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, err); });
        }
        return;
    }

    NSURLSessionDownloadTask* task = [_session downloadTaskWithURL:url];
    @synchronized (self) {
        if (progress) _progressBlocks[task] = [progress copy];
        if (completion) _completionBlocks[task] = [completion copy];
        _fontNames[task] = fontName;
    }
    [task resume];
}

#pragma mark - NSURLSessionDownloadDelegate

- (void)URLSession:(NSURLSession*)session
      downloadTask:(NSURLSessionDownloadTask*)downloadTask
didFinishDownloadingToURL:(NSURL*)location {
    FontDownloadCompletion completion;
    NSString* fontName;
    @synchronized (self) {
        completion = _completionBlocks[downloadTask];
        fontName = _fontNames[downloadTask];
        [_progressBlocks removeObjectForKey:downloadTask];
        [_completionBlocks removeObjectForKey:downloadTask];
        [_fontNames removeObjectForKey:downloadTask];
    }

    // Check HTTP status
    if ([downloadTask.response isKindOfClass:[NSHTTPURLResponse class]]) {
        NSInteger status = ((NSHTTPURLResponse*)downloadTask.response).statusCode;
        if (status < 200 || status >= 300) {
            NSError* httpErr = [NSError errorWithDomain:@"FontDownloader"
                                                  code:status
                                              userInfo:@{NSLocalizedDescriptionKey:
                                [NSString stringWithFormat:@"HTTP %ld", (long)status]}];
            if (completion) {
                dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, httpErr); });
            }
            return;
        }
    }

    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* fontsDir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Fonts"];

    // Ensure ~/Library/Fonts/ exists
    NSError* dirError = nil;
    if (![fm fileExistsAtPath:fontsDir]) {
        [fm createDirectoryAtPath:fontsDir
      withIntermediateDirectories:YES
                       attributes:nil
                            error:&dirError];
        if (dirError) {
            if (completion) {
                dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, dirError); });
            }
            return;
        }
    }

    NSString* filePath = location.path;
    NSString* ext = [[downloadTask.response suggestedFilename] pathExtension].lowercaseString;
    if (!ext) ext = @"";

    // Determine if this is a zip archive
    BOOL isZip = [ext isEqualToString:@"zip"];
    if (!isZip) {
        // Check magic bytes
        NSData* header = [NSData dataWithContentsOfURL:location
                                               options:NSDataReadingMappedIfSafe
                                                 error:nil];
        if (header.length >= 4) {
            const uint8_t* bytes = (const uint8_t*)header.bytes;
            isZip = (bytes[0] == 0x50 && bytes[1] == 0x4B &&
                     bytes[2] == 0x03 && bytes[3] == 0x04);
        }
    }

    if (isZip) {
        [self installFromZip:filePath toFontsDir:fontsDir completion:completion];
    } else {
        // Single font file — copy directly
        [self installSingleFont:filePath
                       withName:fontName ?: @"font"
                     toFontsDir:fontsDir
                     completion:completion];
    }
}

- (void)URLSession:(NSURLSession*)session
      downloadTask:(NSURLSessionDownloadTask*)downloadTask
      didWriteData:(int64_t)bytesWritten
 totalBytesWritten:(int64_t)totalBytesWritten
totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {
    FontDownloadProgress progress;
    @synchronized (self) {
        progress = _progressBlocks[downloadTask];
    }
    if (progress && totalBytesExpectedToWrite > 0) {
        double fraction = (double)totalBytesWritten / (double)totalBytesExpectedToWrite;
        dispatch_async(dispatch_get_main_queue(), ^{ progress(fraction); });
    }
}

- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
didCompleteWithError:(NSError*)error {
    if (!error) return;

    FontDownloadCompletion completion;
    @synchronized (self) {
        completion = _completionBlocks[task];
        [_progressBlocks removeObjectForKey:task];
        [_completionBlocks removeObjectForKey:task];
        [_fontNames removeObjectForKey:task];
    }

    if (completion) {
        dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, error); });
    }
}

#pragma mark - Install Helpers

- (void)installFromZip:(NSString*)zipPath
            toFontsDir:(NSString*)fontsDir
            completion:(FontDownloadCompletion)completion {
    NSFileManager* fm = [NSFileManager defaultManager];

    // Create temp directory for extraction
    NSString* tmpDir = [NSTemporaryDirectory()
        stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]];
    NSError* mkdirErr = nil;
    [fm createDirectoryAtPath:tmpDir
  withIntermediateDirectories:YES
                   attributes:nil
                        error:&mkdirErr];
    if (mkdirErr) {
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, mkdirErr); });
        }
        return;
    }

    // Unzip using /usr/bin/unzip
    NSTask* unzipTask = [[NSTask alloc] init];
    unzipTask.executableURL = [NSURL fileURLWithPath:@"/usr/bin/unzip"];
    unzipTask.arguments = @[@"-o", @"-j", zipPath, @"-d", tmpDir];
    unzipTask.standardOutput = [NSPipe pipe];
    unzipTask.standardError = [NSPipe pipe];

    NSError* launchErr = nil;
    [unzipTask launchAndReturnError:&launchErr];
    if (launchErr) {
        [fm removeItemAtPath:tmpDir error:nil];
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, launchErr); });
        }
        return;
    }
    [unzipTask waitUntilExit];

    if (unzipTask.terminationStatus != 0) {
        [fm removeItemAtPath:tmpDir error:nil];
        NSError* unzipErr = [NSError errorWithDomain:@"FontDownloader"
                                                code:-3
                                            userInfo:@{NSLocalizedDescriptionKey: @"Failed to unzip font archive"}];
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, unzipErr); });
        }
        return;
    }

    // Find all .ttf and .otf files recursively
    NSDirectoryEnumerator* enumerator = [fm enumeratorAtPath:tmpDir];
    NSMutableArray<NSString*>* fontFiles = [NSMutableArray new];
    NSString* entry;
    while ((entry = [enumerator nextObject])) {
        NSString* ext = entry.pathExtension.lowercaseString;
        if ([ext isEqualToString:@"ttf"] || [ext isEqualToString:@"otf"]) {
            [fontFiles addObject:[tmpDir stringByAppendingPathComponent:entry]];
        }
    }

    if (fontFiles.count == 0) {
        [fm removeItemAtPath:tmpDir error:nil];
        NSError* noFontErr = [NSError errorWithDomain:@"FontDownloader"
                                                 code:-4
                                             userInfo:@{NSLocalizedDescriptionKey: @"No font files found in archive"}];
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, noFontErr); });
        }
        return;
    }

    // Move font files to ~/Library/Fonts/ and register
    BOOL anyInstalled = NO;
    for (NSString* fontPath in fontFiles) {
        NSString* fileName = fontPath.lastPathComponent;
        NSString* destPath = [fontsDir stringByAppendingPathComponent:fileName];

        [fm removeItemAtPath:destPath error:nil]; // remove existing
        NSError* moveErr = nil;
        [fm moveItemAtPath:fontPath toPath:destPath error:&moveErr];
        if (moveErr) continue;

        // Register with CoreText
        NSURL* fontURL = [NSURL fileURLWithPath:destPath];
        CFErrorRef ctErr = NULL;
        if (CTFontManagerRegisterFontsForURL((__bridge CFURLRef)fontURL,
                                              kCTFontManagerScopeUser, &ctErr)) {
            anyInstalled = YES;
        } else if (ctErr) {
            CFRelease(ctErr);
        }
    }

    // Cleanup
    [fm removeItemAtPath:tmpDir error:nil];

    if (completion) {
        if (anyInstalled) {
            dispatch_async(dispatch_get_main_queue(), ^{ completion(YES, nil); });
        } else {
            NSError* installErr = [NSError errorWithDomain:@"FontDownloader"
                                                      code:-5
                                                  userInfo:@{NSLocalizedDescriptionKey: @"Failed to install font files"}];
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, installErr); });
        }
    }
}

- (void)installSingleFont:(NSString*)filePath
                  withName:(NSString*)fontName
                toFontsDir:(NSString*)fontsDir
                completion:(FontDownloadCompletion)completion {
    NSFileManager* fm = [NSFileManager defaultManager];

    NSString* ext = filePath.pathExtension.lowercaseString;
    if (![ext isEqualToString:@"ttf"] && ![ext isEqualToString:@"otf"]) {
        ext = @"ttf"; // default
    }
    NSString* fileName = [fontName stringByAppendingPathExtension:ext];
    NSString* destPath = [fontsDir stringByAppendingPathComponent:fileName];

    [fm removeItemAtPath:destPath error:nil];
    NSError* copyErr = nil;
    [fm copyItemAtPath:filePath toPath:destPath error:&copyErr];
    if (copyErr) {
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, copyErr); });
        }
        return;
    }

    // Register with CoreText
    NSURL* fontURL = [NSURL fileURLWithPath:destPath];
    CFErrorRef ctErr = NULL;
    BOOL registered = CTFontManagerRegisterFontsForURL((__bridge CFURLRef)fontURL,
                                                        kCTFontManagerScopeUser, &ctErr);
    if (ctErr) CFRelease(ctErr);

    if (completion) {
        dispatch_async(dispatch_get_main_queue(), ^{ completion(registered, nil); });
    }
}

@end
