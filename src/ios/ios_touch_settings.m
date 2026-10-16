#import "ios_touch_settings.h"
#import "ios_music.h"
#include "../region_profile.h"
#include <stdlib.h>

static NSString *action_name(int action)
{
    static NSString *const names[TOUCH_ACTION_COUNT] = {
        @"Xbox A", @"Xbox B", @"Xbox X", @"Xbox Y", @"White / RB", @"Black / LB",
        @"Back / Select", @"Start / Pause", @"D-pad Up", @"D-pad Down",
        @"D-pad Left", @"D-pad Right", @"Brake / LT", @"Accelerator / RT",
        @"Steer Left", @"Steer Right", @"Left stick press / L3", @"Right stick press / R3",
        @"Left stick Up", @"Left stick Down", @"Right stick Left", @"Right stick Right",
        @"Right stick Up", @"Right stick Down"
    };
    return action >= 0 && action < TOUCH_ACTION_COUNT ? names[action] : @"Unknown";
}
static NSString *control_name(int control, BOOL driving)
{
    if (!driving) {
        if (control == TOUCH_A) return @"Confirm / A";
        if (control == TOUCH_B) return @"Back / B";
        if (control == TOUCH_START) return @"Start";
        return action_name(control);
    }
    static NSString *const names[TOUCH_COUNT] = {
        @"Handbrake", @"Nitrous", @"Shift down", @"Shift up", @"Camera", @"Look back",
        @"Select", @"Pause", @"D-pad Up", @"D-pad Down", @"D-pad Left", @"D-pad Right",
        @"Brake", @"Accelerator", @"Left steering arrow", @"Right steering arrow"
    };
    return names[control];
}
static BOOL default_visible(int control, BOOL driving)
{
    return driving ? (control < TOUCH_UP || control >= TOUCH_BRAKE)
                   : (control < TOUCH_BRAKE && control != TOUCH_BACK);
}

@interface NFSTouchControlController : UIViewController
@property(nonatomic, strong) NFSTouchLayouts *draft;
@property(nonatomic) int control;
@property(nonatomic) BOOL driving;
@property(nonatomic) CGPoint defaultPosition;
@property(nonatomic, copy) UIView *(^preview)(NFSTouchLayouts *layouts, BOOL driving);
@end
@implementation NFSTouchControlController {
    UISlider *_sliders[4];
    UILabel *_values[4];
    UISwitch *_visible;
    UIButton *_mapping;
    UIView *_previewHost, *_previewView;
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (NSMutableDictionary *)config { return [self.draft control:self.control driving:self.driving]; }
- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = control_name(self.control, self.driving);
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    UIScrollView *scroll = [UIScrollView new];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:scroll];
    UIStackView *stack = [UIStackView new];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 12;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:stack];
    UILabel *hint = [UILabel new];
    hint.text = @"Set position within the usable screen area. Changes apply when you tap Save. Controls stay within the screen and remain at least 44 points wide.";
    hint.numberOfLines = 0;
    hint.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    [stack addArrangedSubview:hint];
    if (self.preview) {
        _previewHost = [UIView new];
        _previewHost.backgroundColor = [UIColor colorWithWhite:.04 alpha:1];
        _previewHost.layer.cornerRadius = 12;
        _previewHost.clipsToBounds = YES;
        [_previewHost.heightAnchor constraintEqualToConstant:140].active = YES;
        _previewHost.accessibilityLabel = @"Live touch layout preview";
        _previewView = self.preview(self.draft, self.driving);
        [_previewHost addSubview:_previewView];
        [stack addArrangedSubview:_previewHost];
    }
    NSArray *labels = @[ @"Horizontal position", @"Vertical position", @"Button size", @"Layout opacity" ];
    for (int i = 0; i < 4; i++) {
        _values[i] = [UILabel new];
        _values[i].font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
        _sliders[i] = [UISlider new];
        _sliders[i].tag = i;
        _sliders[i].minimumValue = i == 2 ? .5f : i == 3 ? .15f : 0;
        _sliders[i].maximumValue = i == 2 ? 2 : 1;
        _sliders[i].accessibilityLabel = labels[i];
        [_sliders[i] addTarget:self action:@selector(sliderChanged:) forControlEvents:UIControlEventValueChanged];
        [stack addArrangedSubview:_values[i]];
        [stack addArrangedSubview:_sliders[i]];
    }
    UILabel *visibleLabel = [UILabel new];
    visibleLabel.text = @"Show this button";
    _visible = [UISwitch new];
    _visible.accessibilityLabel = visibleLabel.text;
    [_visible addTarget:self action:@selector(visibilityChanged:) forControlEvents:UIControlEventValueChanged];
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[visibleLabel, _visible]];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.distribution = UIStackViewDistributionEqualSpacing;
    [stack addArrangedSubview:row];
    _mapping = [UIButton buttonWithType:UIButtonTypeSystem];
    _mapping.showsMenuAsPrimaryAction = YES;
    [_mapping.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    [stack addArrangedSubview:_mapping];
    UIButton *reset = [UIButton buttonWithType:UIButtonTypeSystem];
    [reset setTitle:@"Reset this button" forState:UIControlStateNormal];
    [reset addTarget:self action:@selector(resetControl) forControlEvents:UIControlEventTouchUpInside];
    [reset.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    [stack addArrangedSubview:reset];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40]
    ]];
    [self refresh];
}
- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    if (_previewView && _previewView.bounds.size.width > 0 && _previewView.bounds.size.height > 0) {
        CGFloat scale = MIN(_previewHost.bounds.size.width / _previewView.bounds.size.width,
                            _previewHost.bounds.size.height / _previewView.bounds.size.height);
        _previewView.transform = CGAffineTransformMakeScale(scale, scale);
        _previewView.center = CGPointMake(CGRectGetMidX(_previewHost.bounds), CGRectGetMidY(_previewHost.bounds));
    }
}
- (void)refreshPreview
{
    [_previewView setNeedsLayout];
    [_previewView layoutIfNeeded];
}
- (void)refresh
{
    NSMutableDictionary *c = self.config;
    BOOL custom = [c[@"custom"] boolValue];
    _sliders[0].value = custom ? [c[@"x"] floatValue] : self.defaultPosition.x;
    _sliders[1].value = custom ? [c[@"y"] floatValue] : self.defaultPosition.y;
    _sliders[2].value = [c[@"scale"] floatValue];
    _sliders[3].value = [self.draft opacityForDriving:self.driving];
    _visible.on = [c[@"visible"] intValue] < 0 ? default_visible(self.control, self.driving)
                                              : [c[@"visible"] boolValue];
    [self updateLabels];
    [self updateMapping];
    [self refreshPreview];
}
- (void)updateLabels
{
    _values[0].text = [NSString stringWithFormat:@"Horizontal position: %.0f%%", _sliders[0].value * 100];
    _values[1].text = [NSString stringWithFormat:@"Vertical position: %.0f%%", _sliders[1].value * 100];
    _values[2].text = [NSString stringWithFormat:@"Button size: %.0f%%", _sliders[2].value * 100];
    _values[3].text = [NSString stringWithFormat:@"Layout opacity: %.0f%%", _sliders[3].value * 100];
}
- (void)updateMapping
{
    NSMutableArray *items = [NSMutableArray array];
    int current = [self.config[@"action"] intValue];
    __weak typeof(self) weakSelf = self;
    for (int i = 0; i < TOUCH_ACTION_COUNT; i++) {
        UIAction *a = [UIAction actionWithTitle:action_name(i) image:nil identifier:nil handler:^(UIAction *action) {
            NFSTouchControlController *s = weakSelf;
            if (!s) return;
            s.config[@"action"] = @(i);
            [s updateMapping];
            [s refreshPreview];
        }];
        a.state = i == current ? UIMenuElementStateOn : UIMenuElementStateOff;
        [items addObject:a];
    }
    _mapping.menu = [UIMenu menuWithTitle:@"Button action" children:items];
    [_mapping setTitle:[@"Action: " stringByAppendingString:action_name(current)] forState:UIControlStateNormal];
}
- (void)sliderChanged:(UISlider *)slider
{
    NSMutableDictionary *c = self.config;
    if (slider.tag < 2) {
        c[@"custom"] = @YES;
        c[@"x"] = @(_sliders[0].value);
        c[@"y"] = @(_sliders[1].value);
    } else if (slider.tag == 2) c[@"scale"] = @(slider.value);
    else [self.draft setOpacity:slider.value driving:self.driving];
    [self updateLabels];
    [self refreshPreview];
}
- (void)visibilityChanged:(UISwitch *)sender
{ self.config[@"visible"] = @(sender.on); [self refreshPreview]; }
- (void)resetControl
{
    [self.config setDictionary:@{ @"x": @.5, @"y": @.5, @"scale": @1, @"visible": @(-1),
        @"custom": @NO, @"action": @(self.control) }];
    [self refresh];
}
@end

@implementation NFSTouchSettingsController {
    BOOL _driving, _connected;
    UISegmentedControl *_segment;
}
- (instancetype)initWithLayouts:(NFSTouchLayouts *)layouts driving:(BOOL)driving controllerConnected:(BOOL)connected
{
    self = [super initWithStyle:UITableViewStyleInsetGrouped];
    if (self) { _draft = [layouts copy]; _driving = driving; _connected = connected; }
    return self;
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"Mobile Controls";
    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
        target:self action:@selector(cancel)];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSave
        target:self action:@selector(save)];
    _segment = [[UISegmentedControl alloc] initWithItems:@[@"Menu", @"Driving"]];
    _segment.selectedSegmentIndex = _driving;
    [_segment addTarget:self action:@selector(layoutChanged:) forControlEvents:UIControlEventValueChanged];
    UILabel *note = [UILabel new];
    note.text = _connected ? @"Controller connected. Touch controls hide automatically."
                          : @"Connect a controller to play without the touch pad. Both mobile layouts are saved separately.";
    note.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    note.numberOfLines = 0;
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[_segment, note]];
    header.axis = UILayoutConstraintAxisVertical;
    header.spacing = 10;
    header.layoutMargins = UIEdgeInsetsMake(12, 20, 12, 20);
    header.layoutMarginsRelativeArrangement = YES;
    header.frame = CGRectMake(0, 0, self.view.bounds.size.width, 112);
    self.tableView.tableHeaderView = header;
}
- (void)layoutChanged:(UISegmentedControl *)sender
{
    _driving = sender.selectedSegmentIndex == 1;
    [self.tableView reloadData];
}
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { return 4; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{ return section == 0 ? TOUCH_COUNT : 1; }
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{ return section == 0 ? (_driving ? @"Driving buttons" : @"Menu buttons") : section == 1 ? @"Restore defaults" : section == 2 ? @"Music" : @"Game files"; }
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path
{
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    if (path.section == 3) {
        cell.textLabel.text = @"Extracted game directory";
        cell.detailTextLabel.text = @"Files → On My iPhone/iPad → NFSU2 → game";
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    } else if (path.section == 2) {
        cell.textLabel.text = @"My Music · Import MP3s";
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    } else if (path.section == 1) {
        cell.textLabel.text = _driving ? @"Reset driving layout" : @"Reset menu layout";
        cell.textLabel.textColor = UIColor.systemRedColor;
    } else {
        cell.textLabel.text = control_name((int)path.row, _driving);
        NSDictionary *c = [self.draft control:(int)path.row driving:_driving];
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%@ · %.0f%% size%@",
            action_name([c[@"action"] intValue]), [c[@"scale"] doubleValue] * 100,
            [c[@"visible"] intValue] == 0 ? @" · hidden" : @""];
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    }
    return cell;
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path
{
    [tableView deselectRowAtIndexPath:path animated:YES];
    if (path.section == 3) {
        const char *game = getenv("NFSU2_GAME_DIR");
        NSString *path = game ? @(game) : [NSHomeDirectory() stringByAppendingPathComponent:@"Documents/game"];
        NSString *message = [NSString stringWithFormat:@"Files → On My iPhone/iPad → NFSU2 → game\n\n"
            @"default.xbe and NFSUNDER belong directly inside game.\n\nCompiled profile: %@\n\nRead directory:\n%@",
            @NFSU2_REGION_NAME, path];
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Game Files" message:message preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Copy Path" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            UIPasteboard.generalPasteboard.string = path;
        }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
        return;
    }
    if (path.section == 2) {
        [self.navigationController pushViewController:[NFSMusicController new] animated:YES];
        return;
    }
    if (path.section == 1) {
        [self.draft resetDriving:_driving];
        [tableView reloadData];
        return;
    }
    NFSTouchControlController *c = [NFSTouchControlController new];
    c.draft = self.draft;
    c.control = (int)path.row;
    c.driving = _driving;
    c.preview = self.preview;
    c.defaultPosition = self.defaultPosition ? self.defaultPosition(c.control, _driving) : CGPointMake(.5, .5);
    [self.navigationController pushViewController:c animated:YES];
}
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self.tableView reloadData]; }
- (void)save { if (self.finished) self.finished(YES); }
- (void)cancel { if (self.finished) self.finished(NO); }
@end
