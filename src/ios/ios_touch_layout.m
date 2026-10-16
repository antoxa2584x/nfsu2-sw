#import "ios_touch_layout.h"
#include <math.h>
static NSString *const kProfilesKey = @"NFSU2TouchProfilesV1";
static double number(NSDictionary *d, NSString *key, double fallback, double lo, double hi)
{
    id value = d[key];
    if (![value isKindOfClass:NSNumber.class]) return fallback;
    double n = [value doubleValue];
    return isfinite(n) ? fmin(hi, fmax(lo, n)) : fallback;
}
@implementation NFSTouchLayouts {
    NSUserDefaults *_defaults;
    NSMutableArray *_profiles[2];
    double _opacity[2];
}
- (instancetype)initWithDefaults:(NSUserDefaults *)defaults
{
    self = [super init];
    if (!self) return nil;
    _defaults = defaults;
    id stored = [defaults objectForKey:kProfilesKey];
    NSDictionary *saved = [stored isKindOfClass:NSDictionary.class] ? stored : @{};
    for (int mode = 0; mode < 2; mode++) {
        [self resetDriving:mode];
        id layout = saved[mode ? @"drive" : @"menu"];
        if (![layout isKindOfClass:NSDictionary.class]) continue;
        _opacity[mode] = number(layout, @"opacity", 1, .15, 1);
        id controls = layout[@"controls"];
        if (![controls isKindOfClass:NSArray.class]) continue;
        for (int i = 0; i < TOUCH_COUNT && i < (int)[controls count]; i++) {
            NSDictionary *d = controls[i];
            if (![d isKindOfClass:NSDictionary.class]) continue;
            NSMutableDictionary *c = _profiles[mode][i];
            c[@"x"] = @(number(d, @"x", .5, 0, 1));
            c[@"y"] = @(number(d, @"y", .5, 0, 1));
            c[@"scale"] = @(number(d, @"scale", 1, .5, 2));
            c[@"visible"] = @((int)number(d, @"visible", -1, -1, 1));
            c[@"custom"] = @(number(d, @"custom", 0, 0, 1) != 0);
            c[@"action"] = @((int)number(d, @"action", i, 0, TOUCH_ACTION_COUNT - 1));
        }
    }
    return self;
}
- (NSMutableDictionary *)control:(int)index driving:(BOOL)driving
{
    NSParameterAssert(index >= 0 && index < TOUCH_COUNT);
    return _profiles[driving ? 1 : 0][index];
}
- (double)opacityForDriving:(BOOL)driving { return _opacity[driving ? 1 : 0]; }
- (void)setOpacity:(double)value driving:(BOOL)driving
{
    _opacity[driving ? 1 : 0] = isfinite(value) ? fmin(1, fmax(.15, value)) : 1;
}
- (void)resetDriving:(BOOL)driving
{
    int mode = driving ? 1 : 0;
    _profiles[mode] = [NSMutableArray array];
    _opacity[mode] = 1;
    for (int i = 0; i < TOUCH_COUNT; i++)
        [_profiles[mode] addObject:[@{ @"x": @.5, @"y": @.5, @"scale": @1,
            @"visible": @(-1), @"custom": @NO, @"action": @(i) } mutableCopy]];
}
- (id)copyWithZone:(NSZone *)zone
{
    NFSTouchLayouts *copy = [[[self class] allocWithZone:zone] initWithDefaults:nil];
    copy->_defaults = _defaults;
    for (int mode = 0; mode < 2; mode++) {
        copy->_profiles[mode] = [NSMutableArray array];
        for (NSDictionary *d in _profiles[mode]) [copy->_profiles[mode] addObject:[d mutableCopy]];
        copy->_opacity[mode] = _opacity[mode];
    }
    return copy;
}
- (void)save
{
    [_defaults setObject:@{
        @"menu": @{ @"opacity": @(_opacity[0]), @"controls": _profiles[0] },
        @"drive": @{ @"opacity": @(_opacity[1]), @"controls": _profiles[1] }
    } forKey:kProfilesKey];
}
@end
