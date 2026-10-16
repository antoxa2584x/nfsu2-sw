#import <UIKit/UIKit.h>
#import "ios_touch_layout.h"
/* The editor owns a draft; Save/Cancel are explicit and affect both profiles. */
@interface NFSTouchSettingsController : UITableViewController
@property(nonatomic, strong, readonly) NFSTouchLayouts *draft;
@property(nonatomic, copy) void (^finished)(BOOL saved);
@property(nonatomic, copy) UIView *(^preview)(NFSTouchLayouts *layouts, BOOL driving);
@property(nonatomic, copy) CGPoint (^defaultPosition)(int control, BOOL driving);
- (instancetype)initWithLayouts:(NFSTouchLayouts *)layouts driving:(BOOL)driving
                      controllerConnected:(BOOL)connected;
@end
