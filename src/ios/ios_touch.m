/*
 * ios_touch.m -- on-screen controls for playing without a game controller.
 *
 * A transparent UIView over SDL's Metal view draws the controls and feeds a
 * virtual SDL game controller (SDL_JoystickAttachVirtualEx). The title reads
 * it through the ordinary SDL controller path (xinput_device.c), exactly as
 * it reads a DualSense, so nothing below the host knows about touch.
 *
 * Only one of the two is attached at a time: when a physical controller
 * connects, the overlay hides and the virtual controller is detached, so the
 * real one takes port 0; when the last one goes away, both come back.
 *
 * Menu layout: D-pad, confirm/back/start and auxiliary Xbox buttons.
 * Drive layout: steering arrows, pedals, handbrake, nitrous, camera and pause.
 * The verified PAL game state selects layouts automatically. The top-centre
 * button allows a manual choice until the next game/menu transition.
 * NFSU2_TOUCH_LAYOUT=drive starts with the driving layout.
 *
 * NFSU2_TOUCH=0 (Documents/nfsu2x_env.txt) turns the overlay off, =2 keeps
 * it on even with a controller (the virtual one then takes the next port).
 * The iOS Simulator always reports a controller of its own ("Gamepad").
 * Main thread only, like everything UIKit; SDL's joystick calls take SDL's
 * joystick lock, so the title's thread may read the controller meanwhile.
 */
#import <UIKit/UIKit.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ios_touch_controls.h"
#import "ios_touch_settings.h"
#include "../apple/apple_game_state.h"
#import "ios_music.h"
extern void ios_controls_editor_pause(int on);
static int physical_controllers(void);
static const char *k_menu[TOUCH_COUNT] = {
    "OK", "BACK", "X", "Y", "WHITE", "BLACK", "SELECT", "START",
    "▲", "▼", "◀", "▶", "", "", "", ""
};
static const char *k_drive[TOUCH_COUNT] = {
    "E-BRAKE", "NOS", "SHIFT −", "SHIFT +", "CAMERA", "LOOK BACK", "SELECT", "Ⅱ",
    "", "", "", "", "BRAKE", "GAS", "❮", "❯"
};
enum { NCTL = TOUCH_COUNT };

/* The virtual controller: every button up to the D-pad and all six axes,
 * so SDL's automatic mapping numbers them exactly as the enums do. */
static SDL_Joystick *s_vpad;

static void vpad_attach(void)
{
    SDL_VirtualJoystickDesc d;
    int index;

    if (s_vpad)
        return;
    SDL_zero(d);
    d.version  = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type     = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes    = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.name     = "Touch Controls";
    index = SDL_JoystickAttachVirtualEx(&d);
    s_vpad = index >= 0 ? SDL_JoystickOpen(index) : NULL;
    if (s_vpad) {
        /* Zero is half a trigger press in SDL's signed axis range. */
        SDL_JoystickSetVirtualAxis(s_vpad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_JOYSTICK_AXIS_MIN);
        SDL_JoystickSetVirtualAxis(s_vpad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_JOYSTICK_AXIS_MIN);
    }
    if (!s_vpad)
        fprintf(stderr, "[ios] touch controller: %s\n", SDL_GetError());
}

static void vpad_detach(void)
{
    SDL_JoystickID id;

    if (!s_vpad)
        return;
    id = SDL_JoystickInstanceID(s_vpad);
    SDL_JoystickClose(s_vpad);
    s_vpad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_JoystickGetDeviceInstanceID(i) == id) {
            SDL_JoystickDetachVirtual(i);
            break;
        }
}

@interface NFSTouchView : UIView
@property(nonatomic) BOOL padVisible;
- (instancetype)initWithFrame:(CGRect)frame defaults:(NSUserDefaults *)defaults;
- (void)releaseAll;
@end

@implementation NFSTouchView {
    CAShapeLayer *_shape[NCTL];
    CATextLayer *_text[NCTL];
    CGRect _frame[NCTL];
    unsigned _held[NCTL];
    BOOL _driving, _extras, _sentValid;
    int _lastAutomatic;
    NfsTouchInput _sent;
    UIButton *_mode, *_more, *_settings;
    NFSTouchLayouts *_layouts;
    UINavigationController *_editor;
    BOOL _padVisible, _previewing;
    UIEdgeInsets _previewInsets;
    NSMapTable<UITouch *, NSNumber *> *_bound;
    UIImpactFeedbackGenerator *_haptic;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    return [self initWithFrame:frame defaults:NSUserDefaults.standardUserDefaults];
}

- (instancetype)initWithFrame:(CGRect)frame defaults:(NSUserDefaults *)defaults
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _layouts = [[NFSTouchLayouts alloc] initWithDefaults:defaults];
    _padVisible = YES;
    _lastAutomatic = -1;
    self.backgroundColor = UIColor.clearColor;
    self.multipleTouchEnabled = YES;
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _bound = [NSMapTable weakToStrongObjectsMapTable];
    _haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    for (int i = 0; i < NCTL; i++) {
        _shape[i] = [CAShapeLayer layer];
        _shape[i].strokeColor = [UIColor colorWithWhite:1 alpha:.45].CGColor;
        _shape[i].fillColor = [UIColor colorWithWhite:1 alpha:.10].CGColor;
        _shape[i].lineWidth = 1.5;
        [self.layer addSublayer:_shape[i]];
        _text[i] = [CATextLayer layer];
        _text[i].font = (__bridge CFTypeRef)[UIFont boldSystemFontOfSize:13];
        _text[i].foregroundColor = [UIColor colorWithWhite:1 alpha:.85].CGColor;
        _text[i].alignmentMode = kCAAlignmentCenter;
        _text[i].contentsScale = UIScreen.mainScreen.scale;
        [self.layer addSublayer:_text[i]];
    }
    _mode = [UIButton buttonWithType:UIButtonTypeSystem];
    _mode.backgroundColor = [UIColor colorWithWhite:0 alpha:.35];
    _mode.layer.cornerRadius = 12;
    [_mode setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    _mode.titleLabel.font = [UIFont boldSystemFontOfSize:12];
    [_mode addTarget:self action:@selector(toggleLayout) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_mode];
    _more = [UIButton buttonWithType:UIButtonTypeSystem];
    _more.backgroundColor = [UIColor colorWithWhite:0 alpha:.35];
    _more.layer.cornerRadius = 12;
    [_more setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    _more.titleLabel.font = [UIFont boldSystemFontOfSize:12];
    [_more addTarget:self action:@selector(toggleExtras) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_more];
    _settings = [UIButton buttonWithType:UIButtonTypeSystem];
    _settings.backgroundColor = [UIColor colorWithWhite:0 alpha:.35];
    _settings.layer.cornerRadius = 12;
    [_settings setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    _settings.titleLabel.font = [UIFont boldSystemFontOfSize:12];
    [_settings setTitle:@"EDIT" forState:UIControlStateNormal];
    _settings.accessibilityLabel = @"Customize mobile controls";
    [_settings addTarget:self action:@selector(openSettings) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_settings];
    _driving = getenv("NFSU2_TOUCH_LAYOUT") && !strcmp(getenv("NFSU2_TOUCH_LAYOUT"), "drive");
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(releaseAll)
        name:UIApplicationWillResignActiveNotification object:nil];
    return self;
}

- (void)placeControl:(int)i rect:(CGRect)r
{
    _frame[i] = r;
    _shape[i].hidden = _text[i].hidden = CGRectIsEmpty(r);
    if (CGRectIsEmpty(r)) return;
    int action = [[_layouts control:i driving:_driving][@"action"] intValue];
    float opacity = [_layouts opacityForDriving:_driving];
    _shape[i].opacity = _text[i].opacity = opacity;
    BOOL circle = _driving && (i == TOUCH_A || i == TOUCH_B || i >= TOUCH_BRAKE);
    _shape[i].path = (circle ? [UIBezierPath bezierPathWithOvalInRect:r]
                           : [UIBezierPath bezierPathWithRoundedRect:r cornerRadius:14]).CGPath;
    const char *label = action < TOUCH_COUNT ? (_driving ? k_drive : k_menu)[action] : "";
    static const char *fallback[TOUCH_ACTION_COUNT] = { "A", "B", "X", "Y", "WHITE", "BLACK", "SELECT", "START",
        "▲", "▼", "◀", "▶", "BRAKE", "GAS", "❮", "❯",
        "L3", "R3", "L ↑", "L ↓", "R ←", "R →", "R ↑", "R ↓" };
    _text[i].string = [NSString stringWithUTF8String:*label ? label : fallback[action]];
    CGFloat font = (action == TOUCH_STEER_LEFT || action == TOUCH_STEER_RIGHT) ? 40 : 13;
    _text[i].fontSize = font;
    _text[i].frame = CGRectMake(r.origin.x, CGRectGetMidY(r) - (font + 4) / 2, r.size.width, font + 4);
}

- (void)toggleExtras
{
    [self releaseAll];
    _extras = !_extras;
    [self setNeedsLayout];
}

- (void)toggleLayout
{
    [self releaseAll];
    _driving = !_driving;
    [self setNeedsLayout];
}

/* Apply only actual mode transitions: a manual choice is retained until the
 * next game/menu transition, and an open layout editor stays on its profile. */
- (void)applyAutomaticMode:(int)mode
{
    if (mode < 0 || mode > 1 || _editor || _previewing || mode == _lastAutomatic) return;
    _lastAutomatic = mode;
    if (_driving == (mode != 0)) return;
    [self releaseAll];
    _driving = mode != 0;
    _extras = NO;
    [self setNeedsLayout];
}

- (CGRect)defaultFrameForControl:(int)i driving:(BOOL)driving extras:(BOOL)extras
{
    UIEdgeInsets in = self.safeAreaInsets;
    CGFloat L = in.left + 12, R = self.bounds.size.width - in.right - 12;
    CGFloat T = in.top + 8, B = self.bounds.size.height - MAX(in.bottom, 12) - 8;
    CGFloat mid = self.bounds.size.width / 2;
    CGFloat big = MIN(112, MIN((R - L) / 7, (B - T) / 2.6));
    CGFloat d = MIN(54, (B - T - 72) / 3), small = 44;
    CGRect r = CGRectZero;
    if (driving) {
        switch (i) {
        case TOUCH_STEER_LEFT:  r = CGRectMake(L, B - big, big, big); break;
        case TOUCH_STEER_RIGHT: r = CGRectMake(L + big + 16, B - big, big, big); break;
        case TOUCH_GAS:   r = CGRectMake(R - big, B - big, big, big); break;
        case TOUCH_BRAKE: r = CGRectMake(R - 2 * big - 16, B - big * .75, big * .75, big * .75); break;
        case TOUCH_A: r = CGRectMake(R - 2 * big - 16, B - big * 1.6 - 12, big * .75, big * .75); break;
        case TOUCH_B: r = CGRectMake(R - big, B - 2 * big - 16, big * .85, big * .85); break;
        case TOUCH_START: r = CGRectMake(R - small, T, small, small); break;
        case TOUCH_WHITE: r = CGRectMake(R - 150, T, 98, small); break;
        case TOUCH_BLACK: r = CGRectMake(L, T + small + 8, 98, small); break;
        case TOUCH_BACK: r = CGRectMake(L, T, 72, small); break;
        case TOUCH_X: r = CGRectMake(L, B - big - small - 16, 84, small); break;
        case TOUCH_Y: r = CGRectMake(L + 92, B - big - small - 16, 84, small); break;
        }
    } else {
        CGFloat dx = L + 1.5 * d, dy = B - 1.5 * d;
        switch (i) {
        case TOUCH_UP:    r = CGRectMake(dx - d / 2, dy - 1.5 * d, d, d); break;
        case TOUCH_DOWN:  r = CGRectMake(dx - d / 2, dy + .5 * d, d, d); break;
        case TOUCH_LEFT:  r = CGRectMake(dx - 1.5 * d, dy - d / 2, d, d); break;
        case TOUCH_RIGHT: r = CGRectMake(dx + .5 * d, dy - d / 2, d, d); break;
        case TOUCH_A: r = CGRectMake(R - 88, B - 88, 88, 88); break;
        case TOUCH_B: r = CGRectMake(R - 168, B - 64, 64, 64); break;
        case TOUCH_START: r = CGRectMake(R - 80, T, 80, small); break;
        case TOUCH_X: case TOUCH_Y: case TOUCH_WHITE: case TOUCH_BLACK:
            if (extras) r = CGRectMake(mid - 138 + (i - TOUCH_X) * 70, T + small + 16, 64, small); break;
        }
    }
    return r;
}

- (CGRect)usableArea
{
    UIEdgeInsets in = self.safeAreaInsets;
    CGRect r = UIEdgeInsetsInsetRect(self.bounds, UIEdgeInsetsMake(in.top + 8, in.left + 12,
        MAX(in.bottom, 12) + 8, in.right + 12));
    return r;
}

- (CGRect)configuredFrame:(int)i
{
    NSDictionary *c = [_layouts control:i driving:_driving];
    int visible = [c[@"visible"] intValue];
    CGRect r = [self defaultFrameForControl:i driving:_driving extras:_extras];
    if (!_padVisible || visible == 0) return CGRectZero;
    if (CGRectIsEmpty(r) && visible != 1) return CGRectZero;
    if (CGRectIsEmpty(r)) {
        r = [self defaultFrameForControl:i driving:_driving extras:YES];
        if (CGRectIsEmpty(r)) r = [self defaultFrameForControl:i driving:!_driving extras:YES];
        if (CGRectIsEmpty(r)) r = CGRectMake(100, 100, 64, 64);
    }
    CGRect area = self.usableArea;
    CGFloat scale = [c[@"scale"] doubleValue];
    CGSize size = CGSizeMake(MIN(area.size.width, MAX(44, r.size.width * scale)),
                             MIN(area.size.height, MAX(44, r.size.height * scale)));
    CGPoint centre = CGPointMake(CGRectGetMidX(r), CGRectGetMidY(r));
    if ([c[@"custom"] boolValue]) {
        centre.x = area.origin.x + [c[@"x"] doubleValue] * area.size.width;
        centre.y = area.origin.y + [c[@"y"] doubleValue] * area.size.height;
    }
    CGFloat x = MAX(area.origin.x, MIN(CGRectGetMaxX(area) - size.width, centre.x - size.width / 2));
    CGFloat y = MAX(area.origin.y, MIN(CGRectGetMaxY(area) - size.height, centre.y - size.height / 2));
    return CGRectMake(x, y, size.width, size.height);
}

- (UIEdgeInsets)safeAreaInsets
{
    return _previewing ? _previewInsets : [super safeAreaInsets];
}

- (UIView *)previewWithLayouts:(NFSTouchLayouts *)layouts driving:(BOOL)driving
{
    NFSTouchView *v = [[NFSTouchView alloc] initWithFrame:self.bounds defaults:nil];
    v->_layouts = layouts;
    v->_driving = driving;
    v->_extras = YES;
    v->_previewing = YES;
    v->_previewInsets = self.safeAreaInsets;
    v.autoresizingMask = UIViewAutoresizingNone;
    v.userInteractionEnabled = NO;
    return v;
}

- (BOOL)padVisible { return _padVisible; }
- (void)setPadVisible:(BOOL)visible
{
    [self releaseAll];
    _padVisible = visible;
    [self setNeedsLayout];
}

- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event
{
    if (!_padVisible) return CGRectContainsPoint(_settings.frame, point);
    return [super pointInside:point withEvent:event];
}

- (void)openSettings
{
    if (_editor) return;
    UIViewController *presenter = nil;
    for (UIResponder *r = self.nextResponder; r; r = r.nextResponder)
        if ([r isKindOfClass:UIViewController.class]) { presenter = (UIViewController *)r; break; }
    if (!presenter || presenter.presentedViewController) return;
    [self releaseAll];
    ios_controls_editor_pause(1);
    NFSTouchSettingsController *settings = [[NFSTouchSettingsController alloc] initWithLayouts:_layouts
        driving:_driving controllerConnected:physical_controllers() != 0];
    __weak typeof(self) weakSelf = self;
    settings.defaultPosition = ^CGPoint(int control, BOOL driving) {
        NFSTouchView *v = weakSelf;
        if (!v) return CGPointMake(.5, .5);
        CGRect r = [v defaultFrameForControl:control driving:driving extras:YES];
        if (CGRectIsEmpty(r)) r = [v defaultFrameForControl:control driving:!driving extras:YES];
        CGRect area = v.usableArea;
        return CGPointMake((CGRectGetMidX(r) - area.origin.x) / MAX(1, area.size.width),
                           (CGRectGetMidY(r) - area.origin.y) / MAX(1, area.size.height));
    };
    settings.preview = ^UIView *(NFSTouchLayouts *draft, BOOL driving) {
        return [weakSelf previewWithLayouts:draft driving:driving];
    };
    __weak NFSTouchSettingsController *weakSettings = settings;
    settings.finished = ^(BOOL saved) { [weakSelf finishSettings:weakSettings saved:saved]; };
    _editor = [[UINavigationController alloc] initWithRootViewController:settings];
    _editor.modalPresentationStyle = UIModalPresentationFullScreen;
    _editor.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [presenter presentViewController:_editor animated:YES completion:nil];
}

- (void)finishSettings:(NFSTouchSettingsController *)settings saved:(BOOL)saved
{
    settings.finished = nil;
    if (saved) { _layouts = settings.draft; [_layouts save]; }
    [self releaseAll];
    [self setNeedsLayout];
    UINavigationController *editor = _editor;
    _editor = nil;
    [editor dismissViewControllerAnimated:YES completion:nil];
    ios_controls_editor_pause(0);
}

/* The picture fills the display; only controls respect the notch/home indicator. */
- (void)layoutSubviews
{
    [super layoutSubviews];
    [self releaseAll];
    UIEdgeInsets in = self.safeAreaInsets;
    CGFloat L = in.left + 12;
    CGFloat T = in.top + 8;
    CGFloat mid = self.bounds.size.width / 2;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _mode.frame = CGRectMake(mid - 64, T, 128, 44);
    _settings.frame = CGRectMake(mid - 148, T, 76, 44);
    _mode.hidden = !_padVisible;
    _more.hidden = _driving || !_padVisible;
    _more.frame = CGRectMake(L, T, 72, 44);
    [_more setTitle:_extras ? @"LESS" : @"MORE" forState:UIControlStateNormal];
    [_mode setTitle:_driving ? @"MENU CONTROLS" : @"DRIVE CONTROLS" forState:UIControlStateNormal];
    for (int i = 0; i < NCTL; i++) {
        [self placeControl:i rect:[self configuredFrame:i]];
    }
    [CATransaction commit];
}

- (int)controlAt:(CGPoint)p
{
    if (!_padVisible || _editor) return -1;
    for (int i = 0; i < NCTL; i++)
        if (!_shape[i].hidden && CGRectContainsPoint(_frame[i], p)) return i;
    return -1;
}

/* Opposite arrows cancel. Send changed axes/buttons only. */
- (void)sendInput
{
    if (_previewing) return;
    NfsTouchInput input;
    int actions[NCTL];
    for (int i = 0; i < NCTL; i++) actions[i] = [[_layouts control:i driving:_driving][@"action"] intValue];
    nfs_touch_mapped_input(_held, actions, &input);
    if (s_vpad) {
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
            if (!_sentValid || input.buttons[i] != _sent.buttons[i])
                SDL_JoystickSetVirtualButton(s_vpad, i, input.buttons[i]);
        for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
            if (!_sentValid || input.axes[i] != _sent.axes[i])
                SDL_JoystickSetVirtualAxis(s_vpad, i, input.axes[i]);
        _sent = input;
        _sentValid = YES;
    } else _sentValid = NO;
}

- (void)setHeld:(int)i delta:(int)delta
{
    BOOL was = _held[i] != 0;
    if (delta > 0) _held[i] += (unsigned)delta;
    else _held[i] = (unsigned)(-delta) > _held[i] ? 0 : _held[i] - (unsigned)(-delta);
    if ((_held[i] != 0) == was) return;
    if (!was) [_haptic impactOccurred];
    [self sendInput];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _shape[i].fillColor = [UIColor colorWithWhite:1 alpha:was ? .10 : .4].CGColor;
    [CATransaction commit];
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches) {
        int c = [self controlAt:[t locationInView:self]];
        [_bound setObject:@(c) forKey:t];
        if (c >= 0) [self setHeld:c delta:1];
    }
}

/* Slide off to release; slide between arrows/pedals without lifting. */
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches) {
        NSNumber *old = [_bound objectForKey:t];
        if (!old) continue;
        int c = [self controlAt:[t locationInView:self]];
        if (c == old.intValue) continue;
        if (old.intValue >= 0) [self setHeld:old.intValue delta:-1];
        [_bound setObject:@(c) forKey:t];
        if (c >= 0) [self setHeld:c delta:1];
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches) {
        NSNumber *old = [_bound objectForKey:t];
        if (old && old.intValue >= 0) [self setHeld:old.intValue delta:-1];
        [_bound removeObjectForKey:t];
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touchesEnded:touches withEvent:event];
}

- (void)releaseAll
{
    memset(_held, 0, sizeof _held);
    [_bound removeAllObjects];
    [self sendInput];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (int i = 0; i < NCTL; i++) _shape[i].fillColor = [UIColor colorWithWhite:1 alpha:.10].CGColor;
    [CATransaction commit];
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}
@end

static NFSTouchView *s_view;

void ios_touch_update_auto(void)
{
    if (s_view) [s_view applyAutomaticMode:nfsu2_mobile_layout_mode()];
}

static int           s_always;          /* NFSU2_TOUCH=2 */

/* A game controller that is not ours. */
static int physical_controllers(void)
{
    int n = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (!SDL_JoystickIsVirtual(i) && SDL_IsGameController(i))
            n++;
    return n;
}

void ios_touch_controllers_changed(void)
{
    static int lastPhysical = -1;
    int show, physical;

    if (!s_view)
        return;
    physical = physical_controllers();
    show = s_always || physical == 0;
    int reorder = s_always && physical > 0 && physical != lastPhysical;
    lastPhysical = physical;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        fprintf(stderr, "[ios] joystick %d: %s%s%s\n", i,
                SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "?",
                SDL_JoystickIsVirtual(i) ? " (virtual)" : "",
                SDL_IsGameController(i) ? "" : " (not a game controller)");
    if (show == s_view.padVisible && !reorder && (!show || s_vpad))
        return;
    if (show) {
        /* Reattach last when a real controller arrives: SDL's index order
         * then gives the physical pad port 0, including NFSU2_TOUCH=2. */
        if (reorder) { [s_view releaseAll]; vpad_detach(); }
        vpad_attach();
        [s_view releaseAll];
    } else {
        [s_view releaseAll];
        vpad_detach();
    }
    s_view.padVisible = show;
    fprintf(stderr, "[ios] touch controls %s\n", show ? "on" : "off (controller connected)");
}

/* Called once SDL's Metal view exists (the Vulkan surface is made on the
 * main thread), so the overlay goes on top of it. */
void ios_touch_attach(SDL_Window *window)
{
    ios_music_start();
    SDL_SysWMinfo info;
    const char *e = getenv("NFSU2_TOUCH");
    UIView *root;

    if (e && strcmp(e, "0") == 0)
        return;
    s_always = e && strcmp(e, "2") == 0;
    SDL_VERSION(&info.version);
    if (!window || !SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT)
        return;
    root = info.info.uikit.window.rootViewController.view;
    if (!root)
        return;
    if (!s_view) {
        s_view = [[NFSTouchView alloc] initWithFrame:root.bounds];
        s_view.padVisible = NO;
        [root addSubview:s_view];
    }
    [root bringSubviewToFront:s_view];
    ios_touch_controllers_changed();
}
