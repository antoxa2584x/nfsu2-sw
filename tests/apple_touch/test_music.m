#include "../../src/ios/ios_music.m"
#include <assert.h>
static void pump(double seconds) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false); }
int main(int argc, char **argv)
{
    @autoreleasepool {
        assert(argc == 2);
        NSString *suite = [@"NFSU2MusicTest-" stringByAppendingString:NSUUID.UUID.UUIDString];
        NSUserDefaults *prefs = [[NSUserDefaults alloc] initWithSuiteName:suite];
        NSURL *folder = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:suite]];
        NFSMusic *music = [[NFSMusic alloc] initWithFolder:folder defaults:prefs];
        NFSMusicController *controller = [[NFSMusicController alloc] initWithMusic:music];
        [controller loadViewIfNeeded];
        controller.view.frame = CGRectMake(0, 0, 844, 390);
        [controller.view layoutIfNeeded];
        UITableViewCell *switchCell = [controller tableView:controller.tableView
            cellForRowAtIndexPath:[NSIndexPath indexPathForRow:0 inSection:0]];
        assert([switchCell.accessoryView isKindOfClass:UISwitch.class]);
        assert(!music.enabled && music.tracks.count == 0);
        NSURL *track = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
        NSURL *bad = [folder URLByAppendingPathComponent:@"bad.mp3"];
        [@"invalid audio" writeToURL:bad atomically:YES encoding:NSUTF8StringEncoding error:nil];
        [controller documentPicker:nil didPickDocumentsAtURLs:@[track]];
        for (int i = 0; i < 100 && controller.importing; i++) pump(.05);
        assert(!controller.importing && music.tracks.count == 2);
        music.suspended = NO; music.enabled = YES;
        assert(music.player && music.player.playing); // invalid entry is skipped
        pump(2.4); assert(music.player.playing && music.player.currentTime < 1); // automatic loop
        music.volume = .25; assert(music.player.volume == .25f);
        [music background]; assert(!music.player.playing);
        [music foreground]; assert(music.player.playing);
        [music interruption:[NSNotification notificationWithName:AVAudioSessionInterruptionNotification object:nil
            userInfo:@{AVAudioSessionInterruptionTypeKey:@(AVAudioSessionInterruptionTypeBegan)}]];
        assert(!music.player.playing);
        [music interruption:[NSNotification notificationWithName:AVAudioSessionInterruptionNotification object:nil
            userInfo:@{AVAudioSessionInterruptionTypeKey:@(AVAudioSessionInterruptionTypeEnded),
                       AVAudioSessionInterruptionOptionKey:@(AVAudioSessionInterruptionOptionShouldResume)}]];
        assert(music.player.playing);
        [music next]; assert(music.player.playing);
        music.enabled = NO; assert(!music.player.playing);
        music.enabled = YES; assert(music.player.playing);
        NFSMusic *reload = [[NFSMusic alloc] initWithFolder:folder defaults:prefs];
        assert(reload.enabled && reload.volume == .25f && reload.tracks.count == 2);
        [reload background]; [music background];
        // Duplicate imports retain both copies and do not overwrite the first.
        [controller documentPicker:nil didPickDocumentsAtURLs:@[track]];
        for (int i = 0; i < 100 && controller.importing; i++) pump(.05);
        assert(!controller.importing && music.tracks.count == 3);
        [controller.tableView reloadData]; [controller.view layoutIfNeeded];
        UIGraphicsImageRenderer *image = [[UIGraphicsImageRenderer alloc] initWithSize:controller.view.bounds.size];
        UIImage *preview = [image imageWithActions:^(UIGraphicsImageRendererContext *context) {
            [controller.view.layer renderInContext:context.CGContext];
        }];
        NSString *previewPath = [[NSString stringWithUTF8String:argv[1]].stringByDeletingLastPathComponent
            stringByAppendingPathComponent:@"music.png"];
        assert([UIImagePNGRepresentation(preview) writeToFile:previewPath atomically:YES]);
        [NSFileManager.defaultManager removeItemAtURL:folder error:nil];
        [prefs removePersistentDomainForName:suite];
        puts("MP3 import, unreadable-track skip, playback, next, switch, volume, persistence and lifecycle: passed");
    }
}
