/* Persisted touch profiles. Draft copies let the editor cancel cleanly. */
#import <Foundation/Foundation.h>
#include "ios_touch_controls.h"
@interface NFSTouchLayouts : NSObject <NSCopying>
- (instancetype)initWithDefaults:(NSUserDefaults *)defaults;
- (NSMutableDictionary *)control:(int)index driving:(BOOL)driving;
- (double)opacityForDriving:(BOOL)driving;
- (void)setOpacity:(double)value driving:(BOOL)driving;
- (void)resetDriving:(BOOL)driving;
- (void)save;
@end
