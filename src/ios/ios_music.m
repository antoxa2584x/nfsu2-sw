#import "ios_music.h"
#import <AVFoundation/AVFoundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static NSString *const MusicChanged = @"NFSMusicChanged";
@interface NFSMusic : NSObject <AVAudioPlayerDelegate>
@property(nonatomic, strong) NSURL *musicFolder;
@property(nonatomic, strong) NSUserDefaults *defaults;
@property(nonatomic) BOOL enabled;
@property(nonatomic) float volume;
@property(nonatomic, strong) NSArray<NSURL *> *tracks;
@property(nonatomic, strong) AVAudioPlayer *player;
@property(nonatomic) NSUInteger index;
@property(nonatomic) BOOL suspended, interrupted;
@property(nonatomic, copy) NSString *error;
+ (instancetype)shared;
- (instancetype)initWithFolder:(NSURL *)folder defaults:(NSUserDefaults *)defaults;
- (void)reload;
- (void)play;
- (void)next;
@end
@implementation NFSMusic
+ (instancetype)shared
{
    static NFSMusic *music;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ music = [NFSMusic new]; });
    return music;
}
- (NSURL *)folder { return self.musicFolder; }
- (instancetype)init
{
    NSURL *docs = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    return [self initWithFolder:[docs URLByAppendingPathComponent:@"Music" isDirectory:YES]
                      defaults:NSUserDefaults.standardUserDefaults];
}
- (instancetype)initWithFolder:(NSURL *)folder defaults:(NSUserDefaults *)defaults
{
    if ((self = [super init])) {
        _musicFolder = folder; _defaults = defaults;
        _enabled = [defaults boolForKey:@"NFSMyMusicEnabled"];
        _volume = [defaults objectForKey:@"NFSMyMusicVolume"] ? [defaults floatForKey:@"NFSMyMusicVolume"] : .65f;
        if (!isfinite(_volume)) _volume = .65f;
        _volume = fmaxf(0, fminf(1, _volume));
        _suspended = UIApplication.sharedApplication.applicationState != UIApplicationStateActive;
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
        [nc addObserver:self selector:@selector(background) name:UIApplicationWillResignActiveNotification object:nil];
        [nc addObserver:self selector:@selector(foreground) name:UIApplicationDidBecomeActiveNotification object:nil];
        [nc addObserver:self selector:@selector(interruption:) name:AVAudioSessionInterruptionNotification object:nil];
        [nc addObserver:self selector:@selector(route:) name:AVAudioSessionRouteChangeNotification object:nil];
        [self reload];
        [self play];
    }
    return self;
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (void)changed { [NSNotificationCenter.defaultCenter postNotificationName:MusicChanged object:self]; }
- (void)reload
{
    [NSFileManager.defaultManager createDirectoryAtURL:self.folder withIntermediateDirectories:YES attributes:nil error:nil];
    NSArray *files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:self.folder
        includingPropertiesForKeys:@[NSURLIsRegularFileKey, NSURLIsSymbolicLinkKey] options:NSDirectoryEnumerationSkipsHiddenFiles error:nil];
    NSMutableArray *tracks = [NSMutableArray array];
    for (NSURL *url in files) {
        NSNumber *regular = nil, *symlink = nil;
        [url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
        [url getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:nil];
        if (regular.boolValue && !symlink.boolValue && [url.pathExtension.lowercaseString isEqualToString:@"mp3"])
            [tracks addObject:url];
    }
    self.tracks = [tracks sortedArrayUsingComparator:^NSComparisonResult(NSURL *a, NSURL *b) {
        return [a.lastPathComponent localizedStandardCompare:b.lastPathComponent];
    }];
    NSString *last = [self.defaults stringForKey:@"NFSMyMusicTrack"];
    self.index = 0;
    for (NSUInteger i = 0; i < self.tracks.count; i++)
        if ([self.tracks[i].lastPathComponent isEqualToString:last]) self.index = i;
    [self changed];
}
- (void)setEnabled:(BOOL)enabled
{
    _enabled = enabled;
    [self.defaults setBool:enabled forKey:@"NFSMyMusicEnabled"];
    if (enabled) { self.interrupted = NO; [self play]; } else [self.player pause];
    [self changed];
}
- (void)setVolume:(float)volume
{
    _volume = fmaxf(0, fminf(1, volume));
    self.player.volume = _volume;
    [self.defaults setFloat:_volume forKey:@"NFSMyMusicVolume"];
}
- (void)play
{
    if (!self.enabled || self.suspended || self.interrupted || !self.tracks.count) return;
    if (self.player && [self.player.url isEqual:self.tracks[self.index]]) {
        [self.player play]; return;
    }
    // Skip unreadable tracks once around the playlist instead of looping forever.
    [self.player stop]; self.player = nil;
    for (NSUInteger tried = 0; tried < self.tracks.count; tried++) {
        NSError *error = nil;
        AVAudioPlayer *player = [[AVAudioPlayer alloc] initWithContentsOfURL:self.tracks[self.index] error:&error];
        if (player && [player prepareToPlay]) {
            self.player = player; player.delegate = self; player.volume = self.volume;
            [self.defaults setObject:player.url.lastPathComponent forKey:@"NFSMyMusicTrack"];
            if ([player play]) { self.error = nil; [self changed]; return; }
        }
        self.index = (self.index + 1) % self.tracks.count;
    }
    self.player = nil; self.error = @"These tracks could not be played. Import a readable MP3.";
    [self changed];
}
- (void)next
{
    [self.player stop]; self.player = nil;
    if (self.tracks.count) self.index = (self.index + 1) % self.tracks.count;
    [self play]; [self changed];
}
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player successfully:(BOOL)success { [self next]; }
- (void)audioPlayerDecodeErrorDidOccur:(AVAudioPlayer *)player error:(NSError *)error { [self next]; }
- (void)background { self.suspended = YES; [self.player pause]; }
- (void)foreground { self.suspended = NO; [self play]; }
- (void)interruption:(NSNotification *)note
{
    if (!NSThread.isMainThread) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self interruption:note]; }); return;
    }
    if ([note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan) {
        self.interrupted = YES; [self.player pause];
    } else {
        self.interrupted = !([note.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue] & AVAudioSessionInterruptionOptionShouldResume);
        [self play];
    }
}
- (void)route:(NSNotification *)note
{
    if (!NSThread.isMainThread) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self route:note]; }); return;
    }
    if ([note.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue] == AVAudioSessionRouteChangeReasonOldDeviceUnavailable) {
        self.interrupted = YES; [self.player pause]; [self changed];
    }
}
@end
void ios_music_start(void) { (void)[NFSMusic shared]; }

@interface NFSMusicController () <UIDocumentPickerDelegate>
@property(nonatomic) BOOL importing;
@property(nonatomic, strong) NFSMusic *music;
- (instancetype)initWithMusic:(NFSMusic *)music;
@end
@implementation NFSMusicController
- (instancetype)init { return [self initWithMusic:NFSMusic.shared]; }
- (instancetype)initWithMusic:(NFSMusic *)music
{ if ((self = [super initWithStyle:UITableViewStyleInsetGrouped])) _music = music; return self; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (void)viewDidLoad
{
    [super viewDidLoad]; self.title = @"My Music";
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refresh) name:MusicChanged object:nil];
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (void)refresh { if (self.isViewLoaded) [self.tableView reloadData]; }
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { return 2; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return section ? 1 : 4; }
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
    return section == 0 ? @"Your MP3s play over the game audio. Turn down Music in the game's audio options to hear only your playlist while keeping engine sounds. Imports and music settings apply immediately." : nil;
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path
{
    NFSMusic *m = self.music;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    if (path.section) {
        cell.textLabel.text = m.player.url.lastPathComponent.stringByDeletingPathExtension ?: @"No track playing";
        cell.detailTextLabel.text = m.error ?: [NSString stringWithFormat:@"%lu imported tracks", (unsigned long)m.tracks.count];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (path.row == 0) {
        cell.textLabel.text = @"My Music";
        UISwitch *toggle = [UISwitch new]; toggle.on = m.enabled;
        toggle.accessibilityLabel = @"Play my MP3 playlist";
        [toggle addTarget:self action:@selector(toggled:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle; cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (path.row == 1) {
        cell.textLabel.text = self.importing ? @"Importing MP3s…" : @"Add MP3s from Files";
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    } else if (path.row == 2) cell.textLabel.text = @"Next Track";
    else {
        cell.textLabel.text = @"Playlist volume";
        UISlider *volume = [[UISlider alloc] initWithFrame:CGRectMake(0, 0, 180, 44)];
        volume.value = m.volume; volume.accessibilityLabel = @"MP3 volume";
        [volume addTarget:self action:@selector(volume:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = volume; cell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    return cell;
}
- (void)toggled:(UISwitch *)sender { self.music.enabled = sender.on; }
- (void)volume:(UISlider *)sender { self.music.volume = sender.value; }
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path
{
    [tableView deselectRowAtIndexPath:path animated:YES];
    if (path.section) return;
    if (path.row == 2) { [self.music next]; return; }
    if (path.row != 1 || self.importing) return;
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeMP3] asCopy:NO];
    picker.allowsMultipleSelection = YES; picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    if (self.importing) return;
    self.importing = YES; [self refresh];
    NSURL *folder = self.music.folder;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSMutableArray *failures = [NSMutableArray array];
        for (NSURL *url in urls) {
            BOOL scoped = [url startAccessingSecurityScopedResource];
            NSError *error = nil;
            NSNumber *regular = nil, *size = nil;
            [url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:&error];
            [url getResourceValue:&size forKey:NSURLFileSizeKey error:&error];
            NSURL *staged = [folder URLByAppendingPathComponent:[@"." stringByAppendingString:NSUUID.UUID.UUIDString]];
            BOOL valid = regular.boolValue && size.unsignedLongLongValue > 0 && size.unsignedLongLongValue <= 512ull * 1024 * 1024
                && [url.pathExtension.lowercaseString isEqualToString:@"mp3"];
            if (valid) {
                // Coordinate Files/iCloud providers before copying their data.
                __block BOOL copied = NO;
                NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
                [coordinator coordinateReadingItemAtURL:url options:NSFileCoordinatorReadingWithoutChanges
                    error:&error byAccessor:^(NSURL *readURL) {
                        copied = [NSFileManager.defaultManager copyItemAtURL:readURL toURL:staged error:nil];
                    }];
                valid = copied;
            }
            if (valid) valid = [[AVAudioPlayer alloc] initWithContentsOfURL:staged error:&error] != nil;
            NSURL *destination = [folder URLByAppendingPathComponent:url.lastPathComponent];
            if ([NSFileManager.defaultManager fileExistsAtPath:destination.path])
                destination = [folder URLByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.mp3", url.lastPathComponent.stringByDeletingPathExtension, NSUUID.UUID.UUIDString]];
            if (valid) valid = [NSFileManager.defaultManager moveItemAtURL:staged toURL:destination error:&error];
            if (!valid) {
                [failures addObject:url.lastPathComponent];
                [NSFileManager.defaultManager removeItemAtURL:staged error:nil];
            }
            if (scoped) [url stopAccessingSecurityScopedResource];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            self.importing = NO;
            [self.music reload]; [self.music play]; [self refresh];
            if (failures.count) {
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Some MP3s could not be imported"
                    message:[failures componentsJoinedByString:@"\n"] preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                [self presentViewController:alert animated:YES completion:nil];
            }
        });
    });
}
@end
