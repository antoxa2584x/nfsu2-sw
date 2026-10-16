/*
 * ios_import.m -- import the user's own Xbox ISO or extracted disc into the app.
 *
 * Runs before the title boots, when NFSU2_GAME_DIR (Documents/game) has no
 * compatible default.xbe: a plain screen accepts an extracted Xbox folder
 * or ISO through the system picker. It checks the executable first, copies
 * or extracts through a bounded buffer into <game>.import, then installs
 * with save preservation and rollback. Direct Files/Finder copies are also
 * checked before boot. NFSU2_IMPORT_FROM=<folder-or-iso> starts the
 * same import from that folder or ISO without the picker (the Simulator can read
 * the Mac's disc folder: tests).
 *
 * Main thread only for UIKit; the copy runs on a background queue. The
 * caller's main() turns the run loop here until the game is in place.
 */
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "ios_scene.h"
#include "ios_xiso.h"

/* The XBE the lifted code in gen/ comes from (docs/REGIONS.md). */
#include "../region_profile.h"
#define XBE_SIZE NFSU2_XBE_SIZE
#define XBE_SHA1 NFSU2_XBE_SHA1
#define COPY_CHUNK (4u << 20)

static NSString *xbe_sha1(NSString *path)
{
    unsigned char md[CC_SHA1_DIGEST_LENGTH];
    NSMutableString *hex = [NSMutableString string];
    FILE *f = fopen(path.fileSystemRepresentation, "rb");
    char buf[1 << 16];
    size_t n;
    CC_SHA1_CTX c;

    if (!f)
        return nil;
    /* Identifies a known file; nothing here relies on SHA-1 for security. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_SHA1_Init(&c);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        CC_SHA1_Update(&c, buf, (CC_LONG)n);
    CC_SHA1_Final(md, &c);
#pragma clang diagnostic pop
    BOOL readOK = !ferror(f);
    fclose(f);
    if (!readOK) return nil;
    for (int i = 0; i < CC_SHA1_DIGEST_LENGTH; i++)
        [hex appendFormat:@"%02x", md[i]];
    return hex;
}

/* The picked folder, or a folder right inside it, that holds default.xbe. */
static NSURL *disc_root(NSURL *picked)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSArray<NSURL *> *kids;

    if ([fm fileExistsAtPath:[picked URLByAppendingPathComponent:@"default.xbe"].path])
        return picked;
    kids = [fm contentsOfDirectoryAtURL:picked includingPropertiesForKeys:nil options:0 error:nil];
    for (NSURL *k in kids)
        if ([fm fileExistsAtPath:[k URLByAppendingPathComponent:@"default.xbe"].path])
            return k;
    return nil;
}

static BOOL copy_file(NSString *from, NSString *to, char *buf,
                      void (^progress)(uint64_t), NSString **err)
{
    FILE *in = fopen(from.fileSystemRepresentation, "rb");
    FILE *out = in ? fopen(to.fileSystemRepresentation, "wb") : NULL;
    size_t n;
    BOOL ok = YES;

    if (!in || !out) {
        *err = [NSString stringWithFormat:@"%@: %s%@", from.lastPathComponent, strerror(errno),
                errno == ENOENT ? @" (if it is in iCloud, download it in Files first)" : @""];
        if (in)
            fclose(in);
        return NO;
    }
    while ((n = fread(buf, 1, COPY_CHUNK, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            *err = [NSString stringWithFormat:@"writing %@: %s", to.lastPathComponent, strerror(errno)];
            ok = NO;
            break;
        }
        progress(n);
    }
    if (ok && ferror(in)) {
        *err = [NSString stringWithFormat:@"reading %@: %s", from.lastPathComponent, strerror(errno)];
        ok = NO;
    }
    fclose(in);
    if (fclose(out) != 0 && ok) {
        *err = [NSString stringWithFormat:@"writing %@: %s", to.lastPathComponent, strerror(errno)];
        ok = NO;
    }
    return ok;
}

/* Enumeration stays in C; this bridge keeps the manifest on the import queue. */
static int iso_entry(void *context, const char *path, uint64_t offset, uint32_t size, int directory)
{
    NSMutableArray *entries = (__bridge NSMutableArray *)context;
    NSString *name = [NSString stringWithUTF8String:path];
    if ([name caseInsensitiveCompare:@"default.xbe"] == NSOrderedSame) name = @"default.xbe";
    [entries addObject:@{ @"path": name, @"offset": @(offset), @"size": @(size), @"directory": @(directory) }];
    return 1;
}

static NSString *iso_xbe_sha1(const NfsIso *iso, uint64_t offset, uint32_t size)
{
    CC_SHA1_CTX c;
    unsigned char buffer[65536], digest[CC_SHA1_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_SHA1_Init(&c);
    while (size) {
        size_t n = MIN(size, sizeof buffer);
        if (!nfs_iso_read(iso, offset, buffer, n)) return nil;
        CC_SHA1_Update(&c, buffer, (CC_LONG)n);
        offset += n; size -= (uint32_t)n;
    }
    CC_SHA1_Final(digest, &c);
#pragma clang diagnostic pop
    NSMutableString *hex = [NSMutableString string];
    for (int i = 0; i < CC_SHA1_DIGEST_LENGTH; i++) [hex appendFormat:@"%02x", digest[i]];
    return hex;
}

/* A different executable cannot use the NTSC function addresses and overrides.
 * Region-neutral ISO extraction must not bypass this compatibility check. */
static NSString *xbe_error(NSString *sha)
{
    NSString *detected = [sha isEqualToString:@"144deb954e0e9208e0b7e08e09b4e5e67b2f3356"]
        ? @"PAL Europe" : [sha isEqualToString:@"428b5a70b9400f19cc5be7679db6c72a6f80a1c9"]
        ? @"NTSC-U" : @"an unrecognized executable revision";
    return [NSString stringWithFormat:@"Detected %@. This app contains the %@ translation. "
        @"ISO files from any region can be read, but running them requires a matching translation. "
        @"Use a build for your executable (SHA-1 %@). Existing files and saves have been kept.",
        detected, @NFSU2_REGION_NAME, sha ?: @"unreadable"];
}

/* Publish a complete, verified import with rollback. Preserve existing saves. */
static NSString *install_import(NSString *tmp, NSString *game)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSError *e = nil;
    NSString *backup = [game stringByAppendingString:@".previous"];
    BOOL hadGame = [fm fileExistsAtPath:game];
    /* Recover a previous interrupted rename before replacing anything. */
    if ([fm fileExistsAtPath:backup]) {
        if (!hadGame) {
            if (![fm moveItemAtPath:backup toPath:game error:&e]) return e.localizedDescription;
            hadGame = YES;
        } else return @"A previous import backup exists. Keep your saves and remove game.previous in Files before importing again.";
    }
    for (NSString *name in @[ @"UDATA", @"TDATA" ]) {
        NSString *save = [game stringByAppendingPathComponent:name];
        NSString *to = [tmp stringByAppendingPathComponent:name];
        if ([fm fileExistsAtPath:save]) {
            [fm removeItemAtPath:to error:nil];
            if (![fm copyItemAtPath:save toPath:to error:&e]) return e.localizedDescription;
        }
    }
    if (hadGame && ![fm moveItemAtPath:game toPath:backup error:&e]) return e.localizedDescription;
    if (![fm moveItemAtPath:tmp toPath:game error:&e]) {
        if (hadGame) [fm moveItemAtPath:backup toPath:game error:nil];
        return e.localizedDescription;
    }
    if (hadGame) [fm removeItemAtPath:backup error:nil];
    return nil;
}

@interface NFSImportController : UIViewController <UIDocumentPickerDelegate>
@property (nonatomic, copy) NSString *gameDir;
@property (nonatomic) BOOL done;
- (void)importFrom:(NSURL *)picked;
@end

@implementation NFSImportController {
    UILabel        *_status;
    UIButton       *_choose;
    UIProgressView *_bar;
}

- (void)viewDidLoad
{
    UILabel *title = [UILabel new], *body = [UILabel new];
    UIStackView *stack;
    UIButtonConfiguration *cfg = [UIButtonConfiguration filledButtonConfiguration];

    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    title.text = @"Need for Speed: Underground 2";
    title.font = [UIFont boldSystemFontOfSize:24];
    title.textColor = UIColor.whiteColor;
    body.text = [NSString stringWithFormat:@"Choose your Xbox ISO or extracted game folder "
                @"(default.xbe and NFSUNDER). The extracted game needs several GB of free space. "
                @"This app runs the %@ executable.\n\n"
                @"Extracted files are read from:\nFiles → On My iPhone/iPad → NFSU2 → game\n"
                @"Put default.xbe and NFSUNDER directly inside game.", @NFSU2_REGION_NAME];
    body.numberOfLines = 0;
    body.textAlignment = NSTextAlignmentCenter;
    body.textColor = [UIColor colorWithWhite:0.8 alpha:1];
    cfg.title = @"Choose Folder or ISO…";
    _choose = [UIButton buttonWithConfiguration:cfg primaryAction:
               [UIAction actionWithHandler:^(UIAction *a) { [self pick]; }]];
    _bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    _bar.hidden = YES;
    _status = [UILabel new];
    _status.numberOfLines = 0;
    _status.textAlignment = NSTextAlignmentCenter;
    _status.textColor = [UIColor colorWithWhite:0.7 alpha:1];
    _status.font = [UIFont systemFontOfSize:14];

    stack = [[UIStackView alloc] initWithArrangedSubviews:@[ title, body, _choose, _bar, _status ]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.alignment = UIStackViewAlignmentCenter;
    stack.spacing = 18;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [stack.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [stack.widthAnchor constraintLessThanOrEqualToConstant:560],
        [stack.widthAnchor constraintLessThanOrEqualToAnchor:self.view.safeAreaLayoutGuide.widthAnchor
                                                    constant:-40],
        [body.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
        [_status.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
        [_bar.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
    ]];
}

- (void)pick
{
    UIDocumentPickerViewController *p =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder, [UTType typeWithFilenameExtension:@"iso"] ?: UTTypeData ]];
    p.delegate = self;
    p.allowsMultipleSelection = NO;
    [self presentViewController:p animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)picker didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    if (urls.count)
        [self importFrom:urls.firstObject];
}

- (void)fail:(NSString *)why
{
    fprintf(stderr, "[ios] import failed: %s\n", why.UTF8String);
    _status.text = why;
    _bar.hidden = YES;
    _choose.enabled = YES;
}

- (void)importFrom:(NSURL *)picked
{
    NSString *game = self.gameDir, *tmp = [game stringByAppendingString:@".import"];
    BOOL scoped = [picked startAccessingSecurityScopedResource];
    UIApplication *app = UIApplication.sharedApplication;
    BOOL previousIdleTimerDisabled = app.idleTimerDisabled;
    __block UIBackgroundTaskIdentifier task;

    _choose.enabled = NO;
    _bar.hidden = NO;
    _bar.progress = 0;
    _status.text = @"Copying…";
    app.idleTimerDisabled = YES;
    task = [app beginBackgroundTaskWithExpirationHandler:^{
        [app endBackgroundTask:task];
        task = UIBackgroundTaskInvalid;
    }];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *err = [self copyFrom:picked into:tmp game:game];
        if (scoped)
            [picked stopAccessingSecurityScopedResource];
        dispatch_async(dispatch_get_main_queue(), ^{
            app.idleTimerDisabled = previousIdleTimerDisabled;
            if (task != UIBackgroundTaskInvalid)
                [app endBackgroundTask:task];
            if (err) {
                [self fail:err];
            } else {
                fprintf(stderr, "[ios] game imported into %s\n", game.fileSystemRepresentation);
                self.done = YES;
            }
        });
    });
}

/* Stream an ISO, validating its executable before copying gigabytes. */
- (NSString *)copyISO:(NSURL *)picked into:(NSString *)tmp game:(NSString *)game
{
    NfsIso iso;
    char message[256];
    NSMutableArray *entries = [NSMutableArray array];
    NSMutableSet *paths = [NSMutableSet set];
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *err = nil;
    NSError *e = nil;
    uint64_t total = 0, done = 0, shown = 0;
    char *buffer = NULL;
    if (!nfs_iso_open(&iso, picked.fileSystemRepresentation, message, sizeof message)) return @(message);
    if (!nfs_iso_walk(&iso, iso_entry, (__bridge void *)entries, message, sizeof message)) {
        nfs_iso_close(&iso);
        return @(message);
    }
    NSDictionary *xbe = nil;
    for (NSDictionary *entry in entries) {
        NSString *key = [entry[@"path"] lowercaseString];
        if ([paths containsObject:key]) { err = @"ISO contains duplicate file paths."; break; }
        [paths addObject:key];
        if (![entry[@"directory"] boolValue]) total += [entry[@"size"] unsignedLongLongValue];
        if ([key isEqualToString:@"default.xbe"] && ![entry[@"directory"] boolValue]) xbe = entry;
    }
    if (!err) {
        NSString *sha = xbe && [xbe[@"size"] unsignedLongLongValue] == XBE_SIZE
            ? iso_xbe_sha1(&iso, [xbe[@"offset"] unsignedLongLongValue], [xbe[@"size"] unsignedIntValue]) : nil;
        if (!xbe) err = @"No default.xbe at the root of this Xbox ISO.";
        else if ([xbe[@"size"] unsignedLongLongValue] != XBE_SIZE || ![sha isEqualToString:@XBE_SHA1])
            err = xbe_error(sha);
    }
    if (!err) {
        NSDictionary *space = [[NSURL fileURLWithPath:game.stringByDeletingLastPathComponent]
            resourceValuesForKeys:@[ NSURLVolumeAvailableCapacityForImportantUsageKey ] error:&e];
        if (!space) err = e.localizedDescription;
        else if ([space[NSURLVolumeAvailableCapacityForImportantUsageKey] unsignedLongLongValue] < total + (256ull << 20))
            err = [NSString stringWithFormat:@"Not enough space: the extracted game needs %.1f GB in addition to the ISO.", total / 1e9];
    }
    if (!err) {
        [fm removeItemAtPath:tmp error:nil];
        if (![fm createDirectoryAtPath:tmp withIntermediateDirectories:YES attributes:nil error:&e]) err = e.localizedDescription;
        else if (!(buffer = malloc(COPY_CHUNK))) err = @"Out of memory.";
    }
    for (NSDictionary *entry in entries) {
        if (err) break;
        NSString *dst = [tmp stringByAppendingPathComponent:entry[@"path"]];
        BOOL dir = [entry[@"directory"] boolValue];
        if (![fm createDirectoryAtPath:dir ? dst : dst.stringByDeletingLastPathComponent
            withIntermediateDirectories:YES attributes:nil error:&e]) { err = e.localizedDescription; break; }
        if (dir) continue;
        FILE *out = fopen(dst.fileSystemRepresentation, "wb");
        if (!out) { err = [NSString stringWithFormat:@"Cannot write %@: %s", entry[@"path"], strerror(errno)]; break; }
        uint64_t offset = [entry[@"offset"] unsignedLongLongValue];
        uint32_t left = [entry[@"size"] unsignedIntValue];
        while (left) {
            size_t n = MIN(left, COPY_CHUNK);
            if (!nfs_iso_read(&iso, offset, buffer, n) || fwrite(buffer, 1, n, out) != n) {
                err = @"ISO read or write failed. Check storage and download iCloud files first.";
                break;
            }
            left -= (uint32_t)n; offset += n; done += n;
            if (done - shown >= (32ull << 20) || done == total) {
                shown = done;
                float progress = total ? (float)done / (float)total : 1;
                dispatch_async(dispatch_get_main_queue(), ^{
                    self->_bar.progress = progress;
                    self->_status.text = [NSString stringWithFormat:@"Extracting ISO… %.0f%%", progress * 100];
                });
            }
        }
        if (fclose(out) && !err) err = @"Unable to finish writing imported data.";
    }
    free(buffer);
    nfs_iso_close(&iso);
    if (!err) err = install_import(tmp, game);
    [fm removeItemAtPath:tmp error:nil];
    return err;
}

/* Background queue. Returns nil when the game is in place, else why not. */
- (NSString *)copyFrom:(NSURL *)picked into:(NSString *)tmp game:(NSString *)game
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSArray *keys = @[ NSURLIsDirectoryKey, NSURLFileSizeKey ];
    NSNumber *pickedDirectory = nil;
    [picked getResourceValue:&pickedDirectory forKey:NSURLIsDirectoryKey error:nil];
    if (!pickedDirectory.boolValue) return [self copyISO:picked into:tmp game:game];
    NSURL *root = disc_root(picked);
    NSMutableArray<NSURL *> *items = [NSMutableArray array];
    NSDictionary *space;
    NSError *e = nil;
    __block uint64_t done = 0, total = 0, shown = 0;
    NSString *err = nil, *sha;
    char *buf;

    if (!root)
        return @"No default.xbe in that folder. Pick the folder of the extracted disc.";
    NSString *sourceXbe = [root.path stringByAppendingPathComponent:@"default.xbe"];
    NSString *sourceSha = xbe_sha1(sourceXbe);
    if ([[fm attributesOfItemAtPath:sourceXbe error:nil] fileSize] != XBE_SIZE ||
        ![sourceSha isEqualToString:@XBE_SHA1]) return xbe_error(sourceSha);
    for (NSURL *u in [fm enumeratorAtURL:root includingPropertiesForKeys:keys options:0 errorHandler:nil]) {
        NSNumber *dir = nil, *size = nil;
        [u getResourceValue:&dir forKey:NSURLIsDirectoryKey error:nil];
        [u getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
        [items addObject:u];
        if (!dir.boolValue)
            total += size.unsignedLongLongValue;
    }
    space = [[NSURL fileURLWithPath:game.stringByDeletingLastPathComponent]
             resourceValuesForKeys:@[ NSURLVolumeAvailableCapacityForImportantUsageKey ] error:nil];
    if ([space[NSURLVolumeAvailableCapacityForImportantUsageKey] unsignedLongLongValue] < total + (256ull << 20))
        return [NSString stringWithFormat:@"Not enough free space: the game needs %.1f GB.",
                total / 1e9];

    [fm removeItemAtPath:tmp error:nil];
    if (![fm createDirectoryAtPath:tmp withIntermediateDirectories:YES attributes:nil error:&e])
        return e.localizedDescription;
    buf = malloc(COPY_CHUNK);
    if (!buf)
        return @"Out of memory.";
    fprintf(stderr, "[ios] importing %s (%llu MB)\n", root.fileSystemRepresentation,
            (unsigned long long)(total >> 20));
    for (NSURL *u in items) {
        NSString *rel = [u.path substringFromIndex:root.path.length];
        NSString *dst = [tmp stringByAppendingPathComponent:rel];
        NSNumber *dir = nil;
        [u getResourceValue:&dir forKey:NSURLIsDirectoryKey error:nil];
        if (dir.boolValue) {
            if (![fm createDirectoryAtPath:dst withIntermediateDirectories:YES attributes:nil error:&e]) {
                err = e.localizedDescription;
                break;
            }
            continue;
        }
        if (!copy_file(u.path, dst, buf, ^(uint64_t n) {
                done += n;
                if (done - shown >= (32ull << 20) || done == total) {
                    float f = total ? (float)done / (float)total : 1.0f;
                    shown = done;
                    dispatch_async(dispatch_get_main_queue(), ^{
                        self->_bar.progress = f;
                        self->_status.text = [NSString stringWithFormat:@"Copying… %.0f%%", f * 100];
                    });
                }
            }, &err))
            break;
    }
    free(buf);

    if (!err) {
        NSString *xbe = [tmp stringByAppendingPathComponent:@"default.xbe"];
        NSDictionary *a = [fm attributesOfItemAtPath:xbe error:nil];
        dispatch_async(dispatch_get_main_queue(), ^{ self->_status.text = @"Checking default.xbe…"; });
        sha = xbe_sha1(xbe);
        if (a.fileSize != XBE_SIZE || ![sha isEqualToString:@XBE_SHA1])
            err = xbe_error(sha);
    }
    if (!err) err = install_import(tmp, game);
    [fm removeItemAtPath:tmp error:nil];
    return err;
}
@end

/* 1 once <game_dir>/default.xbe exists: at once, or after an import. */
int ios_import_game(const char *game_dir)
{
    NSString *game = [NSString stringWithUTF8String:game_dir];
    UIWindowScene *scene;
    UIWindow *win;
    NFSImportController *vc;

    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *backup = [game stringByAppendingString:@".previous"];
    if (![fm fileExistsAtPath:game] && [fm fileExistsAtPath:backup])
        [fm moveItemAtPath:backup toPath:game error:nil];
    NSString *installedXbe = [game stringByAppendingPathComponent:@"default.xbe"];
    if ([NSFileManager.defaultManager fileExistsAtPath:installedXbe] &&
        [xbe_sha1(installedXbe) isEqualToString:@XBE_SHA1]) return 1;
    scene = ios_window_scene();
    if (!scene) {
        fprintf(stderr, "[ios] no window scene for the game import\n");
        return 0;
    }
    fprintf(stderr, "[ios] no compatible default.xbe in %s: asking for a folder or ISO\n", game_dir);
    vc = [NFSImportController new];
    vc.gameDir = game;
    win = [[UIWindow alloc] initWithWindowScene:scene];
    win.rootViewController = vc;
    [win makeKeyAndVisible];
    if (getenv("NFSU2_IMPORT_FROM"))
        [vc importFrom:[NSURL fileURLWithPath:@(getenv("NFSU2_IMPORT_FROM")) isDirectory:NO]];
    while (!vc.done)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    win.hidden = YES;
    return 1;
}

/* NFSU2_VERIFY=1: the SHA-1 and size of every disc file under the game
 * folder, in the log ("[verify] <sha1> <bytes> <path>"), to compare with the
 * copy the game came from (shasum on the Mac) -- a transfer that damaged the
 * data files. UDATA and TDATA are the title's own writes and are skipped. */
void ios_verify_game(const char *game_dir)
{
    @autoreleasepool {
        NSString *root = @(game_dir), *rel;
        NSDirectoryEnumerator<NSString *> *e = [NSFileManager.defaultManager enumeratorAtPath:root];
        CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
        unsigned long long total = 0;
        unsigned n = 0, bad = 0;

        while ((rel = [e nextObject])) {
            NSDictionary *a = e.fileAttributes;
            NSString *sha;
            if ([a.fileType isEqualToString:NSFileTypeDirectory]) {
                if ([rel isEqualToString:@"UDATA"] || [rel isEqualToString:@"TDATA"])
                    [e skipDescendants];
                continue;
            }
            if (![a.fileType isEqualToString:NSFileTypeRegular])
                continue;
            sha = xbe_sha1([root stringByAppendingPathComponent:rel]);
            if (!sha)
                bad++;
            fprintf(stderr, "[verify] %s %llu %s\n", sha ? sha.UTF8String : "unreadable",
                    a.fileSize, rel.UTF8String);
            n++;
            total += a.fileSize;
        }
        fprintf(stderr, "[verify] %u files, %llu MB, %u unreadable, %.0f s\n", n, total >> 20, bad,
                CFAbsoluteTimeGetCurrent() - t0);
    }
}
