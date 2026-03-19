#import "ThemeDownloader.h"
#include "termcore/theme_loader.h"

#include <string>
#include <sys/stat.h>

@implementation ThemeDownloader {
    NSURLSession* _session;
}

+ (instancetype)sharedDownloader {
    static ThemeDownloader* shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[ThemeDownloader alloc] init];
    });
    return shared;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration defaultSessionConfiguration];
        config.timeoutIntervalForRequest = 30.0;
        config.timeoutIntervalForResource = 60.0;
        _session = [NSURLSession sessionWithConfiguration:config];
    }
    return self;
}

- (void)downloadTheme:(NSString*)name
              fromURL:(NSString*)urlString
           completion:(ThemeDownloadCompletion)completion {
    if (!name || !urlString || name.length == 0 || urlString.length == 0) {
        if (completion) {
            NSError* err = [NSError errorWithDomain:@"ThemeDownloader"
                                               code:-1
                                           userInfo:@{NSLocalizedDescriptionKey: @"Invalid name or URL"}];
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, err); });
        }
        return;
    }

    NSURL* url = [NSURL URLWithString:urlString];
    if (!url) {
        if (completion) {
            NSError* err = [NSError errorWithDomain:@"ThemeDownloader"
                                               code:-2
                                           userInfo:@{NSLocalizedDescriptionKey: @"Invalid URL"}];
            dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, err); });
        }
        return;
    }

    // Ensure theme directory exists
    std::string themeDir = termcore::defaultThemeDir();
    NSString* themeDirPath = [NSString stringWithUTF8String:themeDir.c_str()];
    NSFileManager* fm = [NSFileManager defaultManager];
    NSError* dirError = nil;
    if (![fm fileExistsAtPath:themeDirPath]) {
        [fm createDirectoryAtPath:themeDirPath
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

    NSString* destPath = [themeDirPath stringByAppendingPathComponent:name];

    NSURLSessionDownloadTask* task = [_session downloadTaskWithURL:url
        completionHandler:^(NSURL* location, NSURLResponse* response, NSError* error) {
            if (error) {
                if (completion) {
                    dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, error); });
                }
                return;
            }

            // Check HTTP status
            if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
                NSInteger status = ((NSHTTPURLResponse*)response).statusCode;
                if (status < 200 || status >= 300) {
                    NSError* httpErr = [NSError errorWithDomain:@"ThemeDownloader"
                                                          code:status
                                                      userInfo:@{NSLocalizedDescriptionKey:
                                        [NSString stringWithFormat:@"HTTP %ld", (long)status]}];
                    if (completion) {
                        dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, httpErr); });
                    }
                    return;
                }
            }

            // Atomic move: write to tmp, then move
            NSString* tmpPath = [destPath stringByAppendingString:@".tmp"];
            NSError* copyError = nil;
            [fm removeItemAtPath:tmpPath error:nil]; // remove any leftover
            [fm copyItemAtURL:location toURL:[NSURL fileURLWithPath:tmpPath] error:&copyError];
            if (copyError) {
                if (completion) {
                    dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, copyError); });
                }
                return;
            }

            NSError* moveError = nil;
            [fm removeItemAtPath:destPath error:nil]; // remove existing
            [fm moveItemAtPath:tmpPath toPath:destPath error:&moveError];
            if (moveError) {
                if (completion) {
                    dispatch_async(dispatch_get_main_queue(), ^{ completion(NO, moveError); });
                }
                return;
            }

            if (completion) {
                dispatch_async(dispatch_get_main_queue(), ^{ completion(YES, nil); });
            }
        }];

    [task resume];
}

@end
