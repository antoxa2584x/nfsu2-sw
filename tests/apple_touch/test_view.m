/* Run on Mac Catalyst to exercise the real UIKit overlay without a disc. */
#include "../../src/ios/ios_touch.m"
#include "../../src/ios/ios_import.m"
UIWindowScene *ios_window_scene(void) { return nil; }
#include <assert.h>
#include <objc/runtime.h>
#undef main
void *SDL_memset(void *p, int c, size_t n) { return memset(p, c, n); }
static NSString *s_previewDirectory;
static NfsTouchInput recorded;
static int editorPaused, simulatedPhysical, simulatedVirtual, virtualIndex;
int nfsu2_mobile_layout_mode(void) { return -1; }
void ios_controls_editor_pause(int on) { editorPaused = on; }
int SDL_JoystickSetVirtualAxis(SDL_Joystick *j, int a, Sint16 v) { (void)j; recorded.axes[a] = v; return 0; }
int SDL_JoystickSetVirtualButton(SDL_Joystick *j, int b, Uint8 v) { (void)j; recorded.buttons[b] = v; return 0; }
int SDL_JoystickAttachVirtualEx(const SDL_VirtualJoystickDesc *d) { (void)d; simulatedVirtual = 1; virtualIndex = simulatedPhysical; return virtualIndex; }
SDL_Joystick *SDL_JoystickOpen(int i) { (void)i; return (SDL_Joystick *)1; }
void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { (void)j; return 100; }
int SDL_NumJoysticks(void) { return simulatedVirtual + simulatedPhysical; }
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int i) { return simulatedVirtual && i == virtualIndex ? 100 : 200; }
int SDL_JoystickDetachVirtual(int i) { (void)i; simulatedVirtual = 0; return 0; }
SDL_bool SDL_JoystickIsVirtual(int i) { return simulatedVirtual && i == virtualIndex ? SDL_TRUE : SDL_FALSE; }
SDL_bool SDL_IsGameController(int i) { (void)i; return SDL_TRUE; }
const char *SDL_JoystickNameForIndex(int i) { (void)i; return "test"; }
const char *SDL_GetError(void) { return "test"; }
SDL_bool SDL_GetWindowWMInfo(SDL_Window *w, SDL_SysWMinfo *i) { (void)w; (void)i; return SDL_FALSE; }

@interface UIViewController (NFSTestSettingsEvents)
- (void)sliderChanged:(UISlider *)slider;
- (void)visibilityChanged:(UISwitch *)sender;
@end
@interface TestView : NFSTouchView
@end
@implementation TestView
- (UIEdgeInsets)safeAreaInsets { return UIEdgeInsetsMake(0, 47, 21, 47); }
@end
@interface TestTouch : UITouch
@property(nonatomic) CGPoint point;
@end
@implementation TestTouch
- (CGPoint)locationInView:(UIView *)v { (void)v; return self.point; }
@end
static void save_view(UIView *v, NSString *path)
{
    [v layoutIfNeeded];
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, .15, false);
    UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:v.bounds.size];
    UIImage *image = [r imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
        [[UIColor colorWithRed:.035 green:.045 blue:.065 alpha:1] setFill];
        UIRectFill(v.bounds);
        [v.layer renderInContext:ctx.CGContext];
    }];
    assert([UIImagePNGRepresentation(image) writeToFile:path atomically:YES]);
}
static void check_auto_layout(void)
{
    TestView *v = [[TestView alloc] initWithFrame:CGRectMake(0, 0, 844, 390) defaults:nil];
    s_vpad = (SDL_Joystick *)1;
    [v applyAutomaticMode:0]; [v layoutSubviews];
    [v applyAutomaticMode:1]; [v layoutSubviews];
    assert([[v valueForKey:@"_driving"] boolValue]);
    unsigned *held = (unsigned *)((char *)(__bridge void *)v + ivar_getOffset(class_getInstanceVariable(NFSTouchView.class, "_held")));
    held[TOUCH_GAS] = 1; [v sendInput];
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == SDL_JOYSTICK_AXIS_MAX);
    [v applyAutomaticMode:0]; [v layoutSubviews];
    assert(![[v valueForKey:@"_driving"] boolValue]);
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == SDL_JOYSTICK_AXIS_MIN);
    [v applyAutomaticMode:1]; [v layoutSubviews];
    [v toggleLayout]; [v applyAutomaticMode:1];
    assert(![[v valueForKey:@"_driving"] boolValue]); /* manual choice until next transition */
    [v applyAutomaticMode:0]; [v applyAutomaticMode:1];
    assert([[v valueForKey:@"_driving"] boolValue]);
    [v releaseAll]; s_vpad = NULL;
    puts("Automatic menu/drive transitions release held gas and retain a manual choice until the next transition: passed");
}
static void check_layouts(void)
{
    const CGSize sizes[] = { {568, 320}, {667, 375}, {844, 390}, {932, 430}, {1024, 768}, {1366, 1024} };
    Ivar frames = class_getInstanceVariable(NFSTouchView.class, "_frame");
    assert(frames);
    for (unsigned n = 0; n < sizeof sizes / sizeof sizes[0]; n++) {
        TestView *v = [[TestView alloc] initWithFrame:(CGRect){CGPointZero, sizes[n]} defaults:nil];
        for (int mode = 0; mode < 3; mode++) {
            [v layoutSubviews];
            const CGRect *rects = (const CGRect *)((const char *)(__bridge void *)v + ivar_getOffset(frames));
            for (int i = 0; i < TOUCH_COUNT; i++) {
                if (CGRectIsEmpty(rects[i])) continue;
                assert(CGRectContainsRect(v.bounds, rects[i]));
                assert(rects[i].size.width >= 44 && rects[i].size.height >= 44);
                for (UIView *toggle in v.subviews)
                    if (!toggle.hidden) assert(!CGRectIntersectsRect(rects[i], toggle.frame));
                for (int j = i + 1; j < TOUCH_COUNT; j++)
                    if (!CGRectIsEmpty(rects[j])) {
                        if (CGRectIntersectsRect(rects[i], rects[j]))
                            fprintf(stderr, "overlap %.0fx%.0f mode %d: %d / %d\n", sizes[n].width, sizes[n].height, mode, i, j);
                        assert(!CGRectIntersectsRect(rects[i], rects[j]));
                    }
            }
            if (mode == 0) [v toggleExtras]; else [v toggleLayout];
        }
    }
    puts("Menu/Drive geometry: six phone/tablet sizes, no overlaps, 44pt minimum targets: passed");
}
static void check_customization(void)
{
    NSString *suite = [@"NFSU2TouchTest." stringByAppendingString:NSUUID.UUID.UUIDString];
    NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:suite];
    TestView *v = [[TestView alloc] initWithFrame:CGRectMake(0, 0, 844, 390) defaults:defaults];
    NFSTouchLayouts *live = [v valueForKey:@"_layouts"];
    NFSTouchSettingsController *editor = [[NFSTouchSettingsController alloc] initWithLayouts:live driving:YES controllerConnected:NO];
    NSMutableDictionary *gas = [editor.draft control:TOUCH_GAS driving:YES];
    gas[@"action"] = @(TOUCH_BRAKE); gas[@"custom"] = @YES;
    gas[@"x"] = @.5; gas[@"y"] = @.5; gas[@"scale"] = @1.5;
    [editor.draft control:TOUCH_A driving:NO][@"action"] = @(TOUCH_X);
    [editor.draft setOpacity:.4 driving:YES];
    assert([[live control:TOUCH_GAS driving:YES][@"action"] intValue] == TOUCH_GAS);
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:editor];
    nav.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    nav.view.frame = CGRectMake(0, 0, 844, 390);
    [nav.view layoutIfNeeded];
    if (s_previewDirectory) save_view(nav.view, [s_previewDirectory stringByAppendingPathComponent:@"touch-settings.png"]);
    // Cancel must not mutate live or saved settings.
    [v finishSettings:editor saved:NO];
    assert([[live control:TOUCH_GAS driving:YES][@"action"] intValue] == TOUCH_GAS);
    NFSTouchLayouts *loaded = [[NFSTouchLayouts alloc] initWithDefaults:defaults];
    assert([[loaded control:TOUCH_A driving:NO][@"action"] intValue] == TOUCH_A);
    // Save both modes, reopen the store/view, and exercise their actual touch mapping.
    [v finishSettings:editor saved:YES];
    TestView *reopened = [[TestView alloc] initWithFrame:v.frame defaults:defaults];
    loaded = [reopened valueForKey:@"_layouts"];
    assert([[loaded control:TOUCH_A driving:NO][@"action"] intValue] == TOUCH_X);
    assert([[loaded control:TOUCH_GAS driving:YES][@"scale"] floatValue] == 1.5);
    assert(fabs([loaded opacityForDriving:YES] - .4) < .0001);
    s_vpad = (SDL_Joystick *)1;
    [reopened layoutSubviews];
    TestTouch *t = [TestTouch new];
    CGRect r = [reopened configuredFrame:TOUCH_A];
    t.point = CGPointMake(CGRectGetMidX(r), CGRectGetMidY(r));
    [reopened touchesBegan:[NSSet setWithObject:t] withEvent:nil];
    assert(recorded.buttons[SDL_CONTROLLER_BUTTON_X]); assert(!recorded.buttons[SDL_CONTROLLER_BUTTON_A]);
    [reopened releaseAll]; [reopened toggleLayout]; [reopened layoutSubviews];
    r = [reopened configuredFrame:TOUCH_GAS];
    assert(r.size.width > 112); t.point = CGPointMake(CGRectGetMidX(r), CGRectGetMidY(r));
    [reopened touchesBegan:[NSSet setWithObject:t] withEvent:nil];
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] == 32767);
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == -32768);
    [reopened releaseAll];
    gas = [loaded control:TOUCH_GAS driving:YES];
    gas[@"x"] = @0; gas[@"y"] = @1; gas[@"scale"] = @.5;
    r = [reopened configuredFrame:TOUCH_GAS];
    assert(CGRectContainsRect(reopened.usableArea, r)); assert(r.size.width >= 44 && r.size.height >= 44);
    gas[@"visible"] = @0; assert(CGRectIsEmpty([reopened configuredFrame:TOUCH_GAS]));
    [loaded resetDriving:YES];
    assert([[loaded control:TOUCH_GAS driving:YES][@"action"] intValue] == TOUCH_GAS);
    assert([[loaded control:TOUCH_A driving:NO][@"action"] intValue] == TOUCH_X);
    [loaded save];
    // Exercise the editor's actual sliders/switch instead of only editing dictionaries.
    UIViewController *detail = [NSClassFromString(@"NFSTouchControlController") new];
    [detail setValue:loaded forKey:@"draft"]; [detail setValue:@(TOUCH_GAS) forKey:@"control"];
    [detail setValue:@YES forKey:@"driving"];
    [detail setValue:[NSValue valueWithCGPoint:CGPointMake(.8, .7)] forKey:@"defaultPosition"];
    UIView *(^previewBlock)(NFSTouchLayouts *, BOOL) = ^UIView *(NFSTouchLayouts *layouts, BOOL driving) {
        return [reopened previewWithLayouts:layouts driving:driving];
    };
    [detail setValue:previewBlock forKey:@"preview"];
    [detail loadViewIfNeeded];
    [nav pushViewController:detail animated:NO];
    [nav.view layoutIfNeeded];
    if (s_previewDirectory) save_view(nav.view, [s_previewDirectory stringByAppendingPathComponent:@"touch-button-editor.png"]);
    Ivar sliderVar = class_getInstanceVariable(detail.class, "_sliders");
    void *rawSlider;
    memcpy(&rawSlider, (char *)(__bridge void *)detail + ivar_getOffset(sliderVar), sizeof rawSlider);
    UISlider *slider = (__bridge UISlider *)rawSlider;
    slider.value = .3; [detail sliderChanged:slider];
    assert([[loaded control:TOUCH_GAS driving:YES][@"custom"] boolValue]);
    assert(fabs([[loaded control:TOUCH_GAS driving:YES][@"x"] doubleValue] - .3) < .0001);
    assert(fabs([[loaded control:TOUCH_GAS driving:YES][@"y"] doubleValue] - .7) < .0001);
    NFSTouchView *preview = [detail valueForKey:@"_previewView"];
    CGRect previewRect = [preview configuredFrame:TOUCH_GAS];
    assert(fabs(CGRectGetMidX(previewRect) - (preview.usableArea.origin.x + .3 * preview.usableArea.size.width)) < .01);
    recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 123;
    [preview layoutSubviews];
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == 123); // previews never inject input
    UISwitch *visible = [detail valueForKey:@"_visible"];
    visible.on = NO; [detail visibilityChanged:visible];
    assert([[loaded control:TOUCH_GAS driving:YES][@"visible"] intValue] == 0);
    assert(CGRectIsEmpty([preview configuredFrame:TOUCH_GAS]));
    // Invalid saved types/numbers cannot produce invalid geometry or action indices.
    [defaults setObject:@{ @"menu": @"bad", @"drive": @{ @"opacity": @"bad", @"controls": @[
        @{ @"x": @(-10), @"y": @200, @"scale": @1000, @"action": @1000 }, @"bad" ] } }
        forKey:@"NFSU2TouchProfilesV1"];
    loaded = [[NFSTouchLayouts alloc] initWithDefaults:defaults];
    assert([[loaded control:TOUCH_A driving:YES][@"x"] doubleValue] == 0);
    assert([[loaded control:TOUCH_A driving:YES][@"y"] doubleValue] == 1);
    assert([[loaded control:TOUCH_A driving:YES][@"scale"] doubleValue] == 2);
    assert([[loaded control:TOUCH_A driving:YES][@"action"] intValue] < TOUCH_ACTION_COUNT);
    assert([[loaded control:TOUCH_B driving:YES][@"action"] intValue] == TOUCH_B);
    [defaults removePersistentDomainForName:suite];
    puts("Customization: independent saved profiles, Save/Cancel/reset, remapping, clamping and editor sliders: passed");
}
static void check_controller_handoff(void)
{
    s_view = [[TestView alloc] initWithFrame:CGRectMake(0, 0, 844, 390) defaults:nil];
    [s_view toggleLayout]; [s_view layoutSubviews];
    s_vpad = (SDL_Joystick *)1; simulatedVirtual = 1; simulatedPhysical = 0; s_always = 0;
    TestTouch *gas = [TestTouch new]; gas.point = CGPointMake(740, 300);
    [s_view touchesBegan:[NSSet setWithObject:gas] withEvent:nil];
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == 32767);
    simulatedPhysical = 1; ios_touch_controllers_changed(); [s_view layoutSubviews];
    assert(!s_view.padVisible && !s_vpad && !simulatedVirtual);
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == -32768);
    assert([s_view controlAt:gas.point] == -1);
    assert(![s_view pointInside:gas.point withEvent:nil]);
    UIButton *settings = [s_view valueForKey:@"_settings"];
    assert(!settings.hidden);
    assert([s_view pointInside:CGPointMake(CGRectGetMidX(settings.frame), CGRectGetMidY(settings.frame)) withEvent:nil]);
    simulatedPhysical = 0; ios_touch_controllers_changed(); [s_view layoutSubviews];
    assert(s_view.padVisible && s_vpad && simulatedVirtual);
    [s_view touchesBegan:[NSSet setWithObject:gas] withEvent:nil];
    assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == 32767);
    [s_view releaseAll];
    // Always-show touch must still reattach behind a real controller.
    s_always = 1; simulatedPhysical = 1; ios_touch_controllers_changed();
    assert(s_view.padVisible && simulatedVirtual && virtualIndex == 1);
    assert(SDL_JoystickGetDeviceInstanceID(0) == 200);
    s_always = 0; simulatedPhysical = 0; virtualIndex = 0;
    [s_view releaseAll]; s_view = nil; simulatedVirtual = 0; s_vpad = NULL;
    puts("Controller hot-plug: releases/detaches touch, keeps editor available, restores neutral touch on disconnect: passed");
}
static void put_u32(unsigned char *p, uint32_t n)
{
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(n >> (8 * i));
}
static void check_iso_preflight(void)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    [fm createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *game = [root stringByAppendingPathComponent:@"game"];
    [fm createDirectoryAtPath:game withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *keep = [game stringByAppendingPathComponent:@"keep"];
    [@"existing game" writeToFile:keep atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSMutableData *disc = [NSMutableData dataWithLength:35 * 2048];
    unsigned char *bytes = disc.mutableBytes;
    memcpy(bytes + 32 * 2048, "MICROSOFT*XBOX*MEDIA", 20);
    put_u32(bytes + 32 * 2048 + 0x14, 33); put_u32(bytes + 32 * 2048 + 0x18, 2048);
    unsigned char *entry = bytes + 33 * 2048;
    put_u32(entry + 4, 34); put_u32(entry + 8, 4); entry[13] = 11;
    memcpy(entry + 14, "default.xbe", 11); memcpy(bytes + 34 * 2048, "TEST", 4);
    NSString *path = [root stringByAppendingPathComponent:@"disc.iso"];
    assert([disc writeToFile:path atomically:YES]);
    NFSImportController *vc = [NFSImportController new];
    NSString *tmp = [game stringByAppendingString:@".import"];
    NSString *error = [vc copyISO:[NSURL fileURLWithPath:path] into:tmp game:game];
    assert(error && [error containsString:@NFSU2_REGION_NAME]);
    assert([fm fileExistsAtPath:keep]);
    assert(![fm fileExistsAtPath:tmp]);
    const char *testXbe = getenv("NFSU2_TEST_XBE");
    if (testXbe) {
        /* Private fixture supplied at test time; never stored in the repo. */
        NSData *xbe = [NSData dataWithContentsOfFile:@(testXbe)];
        assert(xbe.length == XBE_SIZE);
        assert([xbe_sha1(@(testXbe)) isEqualToString:@XBE_SHA1]);
        [disc setLength:34 * 2048 + xbe.length];
        bytes = disc.mutableBytes;
        put_u32(bytes + 33 * 2048 + 8, (uint32_t)xbe.length);
        memcpy(bytes + 34 * 2048, xbe.bytes, xbe.length);
        assert([disc writeToFile:path atomically:YES]);
        error = [vc copyISO:[NSURL fileURLWithPath:path] into:tmp game:game];
        if (error) fprintf(stderr, "Matching-region import failed: %s\n", error.UTF8String);
        assert(!error);
        assert([xbe_sha1([game stringByAppendingPathComponent:@"default.xbe"]) isEqualToString:@XBE_SHA1]);
        puts("Matching regional executable: accepted, streamed and published with correct SHA-1: passed");
    }
    assert([fm removeItemAtPath:root error:nil]);
    puts("Incompatible ISO executable: rejected before extraction; existing data intact: passed");
}
static void check_install(void)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSString *game = [root stringByAppendingPathComponent:@"game"];
    NSString *tmp = [game stringByAppendingString:@".import"];
    NSString *save = [game stringByAppendingPathComponent:@"UDATA/save"];
    [fm createDirectoryAtPath:save.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    [@"old save" writeToFile:save atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [fm createDirectoryAtPath:tmp withIntermediateDirectories:YES attributes:nil error:nil];
    [@"new game" writeToFile:[tmp stringByAppendingPathComponent:@"data"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    assert(install_import(tmp, game) == nil);
    assert([[NSString stringWithContentsOfFile:save encoding:NSUTF8StringEncoding error:nil] isEqual:@"old save"]);
    assert([fm fileExistsAtPath:[game stringByAppendingPathComponent:@"data"]]);
    assert(![fm fileExistsAtPath:[game stringByAppendingString:@".previous"]]);
    // A missing staging folder fails while leaving the installed game intact.
    assert(install_import(tmp, game) != nil);
    assert([fm fileExistsAtPath:[game stringByAppendingPathComponent:@"data"]]);
    // Recover a rename interrupted before publication.
    NSString *backup = [game stringByAppendingString:@".previous"];
    assert([fm moveItemAtPath:game toPath:backup error:nil]);
    [fm createDirectoryAtPath:tmp withIntermediateDirectories:YES attributes:nil error:nil];
    [@"replacement" writeToFile:[tmp stringByAppendingPathComponent:@"data"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    assert(install_import(tmp, game) == nil);
    assert([[NSString stringWithContentsOfFile:save encoding:NSUTF8StringEncoding error:nil] isEqual:@"old save"]);
    assert([fm removeItemAtPath:root error:nil]);
    puts("Import publication, save preservation and interrupted-import recovery: passed");
}
int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc > 1) s_previewDirectory = @(argv[1]);
        check_customization();
        check_controller_handoff();
        check_iso_preflight();
        check_auto_layout();
    check_layouts();
        check_install();
        s_vpad = (SDL_Joystick *)1;
        TestView *v = [[TestView alloc] initWithFrame:CGRectMake(0, 0, 844, 390) defaults:nil];
        [v layoutSubviews];
        if (argc > 1) save_view(v, [@(argv[1]) stringByAppendingPathComponent:@"touch-menu.png"]);
        [v toggleLayout]; [v layoutSubviews];
        if (argc > 1) save_view(v, [@(argv[1]) stringByAppendingPathComponent:@"touch-drive.png"]);
        TestTouch *left = [TestTouch new], *gas = [TestTouch new];
        left.point = CGPointMake(100, 300); gas.point = CGPointMake(740, 300);
        [v touchesBegan:[NSSet setWithObjects:left, gas, nil] withEvent:nil];
        assert(recorded.axes[SDL_CONTROLLER_AXIS_LEFTX] == -32767);
        assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == 32767);
        left.point = CGPointMake(500, 200);
        [v touchesMoved:[NSSet setWithObject:left] withEvent:nil];
        assert(recorded.axes[SDL_CONTROLLER_AXIS_LEFTX] == 0);
        assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == 32767);
        [v touchesCancelled:[NSSet setWithObject:gas] withEvent:nil];
        assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == -32768);
        [v touchesBegan:[NSSet setWithObject:gas] withEvent:nil];
        [[NSNotificationCenter defaultCenter] postNotificationName:UIApplicationWillResignActiveNotification object:nil];
        assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == -32768);
        [v touchesBegan:[NSSet setWithObject:gas] withEvent:nil];
        [v toggleLayout]; [v layoutSubviews];
        assert(recorded.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == -32768);
        puts("UIKit touch slide/cancel/background/mode transitions: passed");
        return 0;
    }
}
