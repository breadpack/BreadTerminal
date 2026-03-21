#if defined(__APPLE__)

#import "UnifiedSettingsContent.h"
#import "UnifiedSettingsWindowController.h"

#include "termcore/settings_model.h"
#include "termcore/config.h"

#include <string>

static const CGFloat kContentPadding  = 24.0;
static const CGFloat kItemSpacing     = 16.0;
static const CGFloat kModifiedBarWidth = 3.0;
static const CGFloat kLabelFontSize   = 14.0;
static const CGFloat kDescFontSize    = 12.0;
static const CGFloat kTitleFontSize   = 20.0;

/// Helper: get a string value from config by key.
static NSString* configStringValue(const termcore::Config& cfg, const std::string& key) {
    if (key == "font_family") return [NSString stringWithUTF8String:cfg.font_family.c_str()];
    if (key == "shell") return [NSString stringWithUTF8String:cfg.shell.c_str()];
    if (key == "cursor_style") return [NSString stringWithUTF8String:cfg.cursor_style.c_str()];
    if (key == "theme") return [NSString stringWithUTF8String:cfg.theme.c_str()];
    if (key == "clipboard_paste_protection") return [NSString stringWithUTF8String:cfg.clipboard_paste_protection.c_str()];
    if (key == "quick_terminal_hotkey") return [NSString stringWithUTF8String:cfg.quick_terminal_hotkey.c_str()];
    return @"";
}

/// Helper: get a float value from config by key.
static float configFloatValue(const termcore::Config& cfg, const std::string& key) {
    if (key == "font_size") return cfg.font_size;
    if (key == "background_opacity") return cfg.background_opacity;
    if (key == "cursor_blink_interval") return cfg.cursor_blink_interval;
    if (key == "minimum_contrast") return cfg.minimum_contrast;
    if (key == "notify_after_seconds") return cfg.notify_after_seconds;
    return 0.0f;
}

/// Helper: get an int value from config by key.
static int configIntValue(const termcore::Config& cfg, const std::string& key) {
    if (key == "scrollback_limit") return cfg.scrollback_limit;
    if (key == "window_width") return cfg.window_width;
    if (key == "window_height") return cfg.window_height;
    if (key == "window_padding") return cfg.window_padding;
    if (key == "background_blur") return cfg.background_blur;
    if (key == "sidebar_width") return cfg.sidebar_width;
    return 0;
}

/// Helper: get a bool value from config by key.
static bool configBoolValue(const termcore::Config& cfg, const std::string& key) {
    if (key == "cursor_blink") return cfg.cursor_blink;
    if (key == "sidebar_visible") return cfg.sidebar_visible;
    if (key == "clipboard_paste_bracketed_safe") return cfg.clipboard_paste_bracketed_safe;
    if (key == "allow_clipboard_write") return cfg.allow_clipboard_write;
    if (key == "notify_on_command_finish") return cfg.notify_on_command_finish;
    return false;
}

/// Helper: get a uint32 color value from config by key.
static uint32_t configColorValue(const termcore::Config& cfg, const std::string& key) {
    if (key == "background") return cfg.background;
    if (key == "foreground") return cfg.foreground;
    if (key == "cursor_color") return cfg.cursor_color;
    if (key == "selection_background") return cfg.selection_background;
    if (key == "selection_foreground") return cfg.selection_foreground;
    // palette colors: "palette_0" through "palette_15"
    if (key.rfind("palette_", 0) == 0) {
        int idx = std::stoi(key.substr(8));
        if (idx >= 0 && idx < 16) return cfg.palette[idx];
    }
    return 0;
}

/// Helper: set a config value by key.
static void setConfigValue(termcore::Config& cfg, const std::string& key, NSString* strValue) {
    std::string val([strValue UTF8String]);
    if (key == "font_family") { cfg.font_family = val; return; }
    if (key == "shell") { cfg.shell = val; return; }
    if (key == "cursor_style") { cfg.cursor_style = val; return; }
    if (key == "theme") { cfg.theme = val; return; }
    if (key == "clipboard_paste_protection") { cfg.clipboard_paste_protection = val; return; }
    if (key == "quick_terminal_hotkey") { cfg.quick_terminal_hotkey = val; return; }
}

static void setConfigFloatValue(termcore::Config& cfg, const std::string& key, float val) {
    if (key == "font_size") { cfg.font_size = val; return; }
    if (key == "background_opacity") { cfg.background_opacity = val; return; }
    if (key == "cursor_blink_interval") { cfg.cursor_blink_interval = val; return; }
    if (key == "minimum_contrast") { cfg.minimum_contrast = val; return; }
    if (key == "notify_after_seconds") { cfg.notify_after_seconds = val; return; }
}

static void setConfigIntValue(termcore::Config& cfg, const std::string& key, int val) {
    if (key == "scrollback_limit") { cfg.scrollback_limit = val; return; }
    if (key == "window_width") { cfg.window_width = val; return; }
    if (key == "window_height") { cfg.window_height = val; return; }
    if (key == "window_padding") { cfg.window_padding = val; return; }
    if (key == "background_blur") { cfg.background_blur = val; return; }
    if (key == "sidebar_width") { cfg.sidebar_width = val; return; }
}

static void setConfigBoolValue(termcore::Config& cfg, const std::string& key, bool val) {
    if (key == "cursor_blink") { cfg.cursor_blink = val; return; }
    if (key == "sidebar_visible") { cfg.sidebar_visible = val; return; }
    if (key == "clipboard_paste_bracketed_safe") { cfg.clipboard_paste_bracketed_safe = val; return; }
    if (key == "allow_clipboard_write") { cfg.allow_clipboard_write = val; return; }
    if (key == "notify_on_command_finish") { cfg.notify_on_command_finish = val; return; }
}

static void setConfigColorValue(termcore::Config& cfg, const std::string& key, uint32_t val) {
    if (key == "background") { cfg.background = val; return; }
    if (key == "foreground") { cfg.foreground = val; return; }
    if (key == "cursor_color") { cfg.cursor_color = val; return; }
    if (key == "selection_background") { cfg.selection_background = val; return; }
    if (key == "selection_foreground") { cfg.selection_foreground = val; return; }
    if (key.rfind("palette_", 0) == 0) {
        int idx = std::stoi(key.substr(8));
        if (idx >= 0 && idx < 16) cfg.palette[idx] = val;
    }
}

/// Convert uint32 RGB to NSColor.
static NSColor* nsColorFromRGB(uint32_t rgb) {
    CGFloat r = ((rgb >> 16) & 0xFF) / 255.0;
    CGFloat g = ((rgb >> 8) & 0xFF) / 255.0;
    CGFloat b = (rgb & 0xFF) / 255.0;
    return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0];
}

/// Convert NSColor to uint32 RGB.
static uint32_t rgbFromNSColor(NSColor* color) {
    NSColor* rgb = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!rgb) return 0;
    uint32_t r = (uint32_t)(rgb.redComponent * 255.0) & 0xFF;
    uint32_t g = (uint32_t)(rgb.greenComponent * 255.0) & 0xFF;
    uint32_t b = (uint32_t)(rgb.blueComponent * 255.0) & 0xFF;
    return (r << 16) | (g << 8) | b;
}

@implementation UnifiedSettingsContent {
    __weak UnifiedSettingsWindowController* _controller;
    std::string _categoryId;
    std::vector<termcore::SettingItem> _items;
    NSString* _sectionTitle;
}

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller
                          category:(const termcore::SettingsCategory*)category {
    self = [super initWithFrame:NSMakeRect(0, 0, 500, 400)];
    if (self) {
        _controller = controller;
        _categoryId = category->id;
        _items = category->items;
        _sectionTitle = [NSString stringWithUTF8String:category->label.c_str()];

        [self buildItemViews];
    }
    return self;
}

- (void)buildItemViews {
    // Remove all subviews
    for (NSView* sub in [self.subviews copy]) {
        [sub removeFromSuperview];
    }

    CGFloat yOffset = kContentPadding;
    CGFloat contentWidth = 500.0;  // will be adjusted by scroll view

    // Section title
    NSTextField* titleLabel = [NSTextField labelWithString:_sectionTitle];
    titleLabel.font = [NSFont boldSystemFontOfSize:kTitleFontSize];
    titleLabel.textColor = [NSColor labelColor];
    titleLabel.frame = NSMakeRect(kContentPadding, 0, contentWidth - 2 * kContentPadding, 28);
    titleLabel.translatesAutoresizingMaskIntoConstraints = YES;
    [self addSubview:titleLabel];
    yOffset += 28 + kItemSpacing;

    // Setting items
    for (const auto& item : _items) {
        NSView* itemView = [self createViewForItem:item width:contentWidth];
        CGFloat itemHeight = itemView.frame.size.height;
        itemView.frame = NSMakeRect(0, 0, contentWidth, itemHeight);
        itemView.translatesAutoresizingMaskIntoConstraints = YES;
        [self addSubview:itemView];
        yOffset += itemHeight + kItemSpacing;
    }

    // Layout items top-to-bottom (flipped coordinate system for scroll view)
    self.frame = NSMakeRect(0, 0, contentWidth, yOffset + kContentPadding);

    // Position items from top to bottom using flipped coordinates
    [self setNeedsLayout:YES];
}

- (BOOL)isFlipped {
    return YES;  // Top-to-bottom layout for scroll view
}

- (void)layout {
    [super layout];

    CGFloat yOffset = kContentPadding;
    CGFloat contentWidth = self.frame.size.width > 0 ? self.frame.size.width : 500.0;

    for (NSView* sub in self.subviews) {
        CGFloat h = sub.frame.size.height;
        sub.frame = NSMakeRect(0, yOffset, contentWidth, h);
        yOffset += h + kItemSpacing;
    }

    // Update total height
    CGFloat totalHeight = yOffset + kContentPadding;
    if (self.frame.size.height != totalHeight) {
        [self setFrameSize:NSMakeSize(contentWidth, totalHeight)];
    }
}

#pragma mark - Item View Factory

- (NSView*)createViewForItem:(const termcore::SettingItem&)item width:(CGFloat)width {
    NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, 80)];

    CGFloat leftPad = kContentPadding;
    CGFloat rightPad = kContentPadding;
    CGFloat usableW = width - leftPad - rightPad;

    // Modified indicator (3px blue bar)
    if (item.modified) {
        NSView* modBar = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, kModifiedBarWidth, 80)];
        modBar.wantsLayer = YES;
        // #007ACC
        modBar.layer.backgroundColor = [NSColor colorWithCalibratedRed:0.0
                                                                 green:0.478
                                                                  blue:0.8
                                                                 alpha:1.0].CGColor;
        [container addSubview:modBar];
    }

    // Label
    NSString* labelStr = [NSString stringWithUTF8String:item.label.c_str()];
    NSTextField* label = [NSTextField labelWithString:labelStr];
    label.font = [NSFont systemFontOfSize:kLabelFontSize weight:NSFontWeightMedium];
    label.textColor = [NSColor labelColor];
    label.frame = NSMakeRect(leftPad, 8, usableW, 20);
    [container addSubview:label];

    // Description
    NSString* descStr = [NSString stringWithUTF8String:item.description.c_str()];
    NSTextField* desc = [NSTextField labelWithString:descStr];
    desc.font = [NSFont systemFontOfSize:kDescFontSize];
    desc.textColor = [NSColor secondaryLabelColor];
    desc.lineBreakMode = NSLineBreakByWordWrapping;
    desc.maximumNumberOfLines = 2;
    desc.preferredMaxLayoutWidth = usableW;
    desc.frame = NSMakeRect(leftPad, 30, usableW, 18);
    [container addSubview:desc];

    // Control widget
    CGFloat controlY = 52;
    NSView* control = [self createControlForItem:item x:leftPad y:controlY width:usableW];
    if (control) {
        [container addSubview:control];
        CGFloat totalH = controlY + control.frame.size.height + 8;
        container.frame = NSMakeRect(0, 0, width, totalH);

        // Resize modified bar
        if (item.modified) {
            for (NSView* sub in container.subviews) {
                if (sub.frame.size.width == kModifiedBarWidth) {
                    sub.frame = NSMakeRect(0, 0, kModifiedBarWidth, totalH);
                    break;
                }
            }
        }
    }

    return container;
}

- (NSView*)createControlForItem:(const termcore::SettingItem&)item
                              x:(CGFloat)x y:(CGFloat)y width:(CGFloat)width {
    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return nil;

    std::string key = item.key;

    switch (item.type) {
        case termcore::SettingType::Toggle: {
            NSSwitch* toggle = [[NSSwitch alloc] initWithFrame:NSMakeRect(x, y, 40, 22)];
            toggle.state = configBoolValue(ctrl.config, key) ? NSControlStateValueOn : NSControlStateValueOff;
            toggle.target = self;
            toggle.action = @selector(toggleChanged:);
            toggle.tag = [self tagForKey:key];
            return toggle;
        }

        case termcore::SettingType::Text: {
            NSTextField* textField = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, MIN(width, 300), 24)];
            textField.stringValue = configStringValue(ctrl.config, key);
            textField.placeholderString = [NSString stringWithUTF8String:key.c_str()];
            textField.bezelStyle = NSTextFieldRoundedBezel;
            textField.target = self;
            textField.action = @selector(textFieldChanged:);
            textField.tag = [self tagForKey:key];
            return textField;
        }

        case termcore::SettingType::Number: {
            NSView* numView = [[NSView alloc] initWithFrame:NSMakeRect(x, y, 160, 24)];

            float currentVal = configFloatValue(ctrl.config, key);
            if (currentVal == 0) currentVal = (float)configIntValue(ctrl.config, key);

            NSTextField* numField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 80, 24)];
            numField.stringValue = [NSString stringWithFormat:@"%g", currentVal];
            numField.bezelStyle = NSTextFieldRoundedBezel;
            numField.tag = [self tagForKey:key];
            numField.target = self;
            numField.action = @selector(numberFieldChanged:);
            [numView addSubview:numField];

            NSStepper* stepper = [[NSStepper alloc] initWithFrame:NSMakeRect(84, 0, 20, 24)];
            stepper.minValue = item.meta.min;
            stepper.maxValue = item.meta.max > 0 ? item.meta.max : 99999;
            stepper.increment = item.meta.step;
            stepper.doubleValue = currentVal;
            stepper.tag = [self tagForKey:key];
            stepper.target = self;
            stepper.action = @selector(stepperChanged:);
            [numView addSubview:stepper];

            return numView;
        }

        case termcore::SettingType::Slider: {
            NSView* sliderView = [[NSView alloc] initWithFrame:NSMakeRect(x, y, MIN(width, 300), 24)];

            float currentVal = configFloatValue(ctrl.config, key);

            NSSlider* slider = [[NSSlider alloc] initWithFrame:NSMakeRect(0, 0, 220, 24)];
            slider.minValue = item.meta.min;
            slider.maxValue = item.meta.max > 0 ? item.meta.max : 1.0;
            slider.doubleValue = currentVal;
            slider.continuous = YES;
            slider.tag = [self tagForKey:key];
            slider.target = self;
            slider.action = @selector(sliderChanged:);
            [sliderView addSubview:slider];

            NSTextField* valLabel = [NSTextField labelWithString:[NSString stringWithFormat:@"%.2f", currentVal]];
            valLabel.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
            valLabel.frame = NSMakeRect(226, 2, 60, 20);
            valLabel.tag = [self tagForKey:key] + 10000;  // offset tag for value label
            [sliderView addSubview:valLabel];

            return sliderView;
        }

        case termcore::SettingType::Dropdown: {
            NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(x, y, 200, 24) pullsDown:NO];
            NSString* currentValue = configStringValue(ctrl.config, key);

            for (const auto& opt : item.meta.options) {
                NSString* nsOpt = [NSString stringWithUTF8String:opt.c_str()];
                [popup addItemWithTitle:nsOpt];
            }
            [popup selectItemWithTitle:currentValue];
            popup.tag = [self tagForKey:key];
            popup.target = self;
            popup.action = @selector(dropdownChanged:);
            return popup;
        }

        case termcore::SettingType::ColorPicker: {
            uint32_t rgb = configColorValue(ctrl.config, key);
            NSColor* color = nsColorFromRGB(rgb);

            NSColorWell* colorWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(x, y, 44, 28)];
            colorWell.color = color;
            colorWell.tag = [self tagForKey:key];
            colorWell.target = self;
            colorWell.action = @selector(colorWellChanged:);
            if (@available(macOS 13.0, *)) {
                colorWell.colorWellStyle = NSColorWellStyleExpanded;
            }
            return colorWell;
        }
    }

    return nil;
}

#pragma mark - Tag <-> Key Mapping

/// Simple tag assignment: hash the key string to a stable integer.
- (NSInteger)tagForKey:(const std::string&)key {
    NSInteger hash = 0;
    for (char c : key) {
        hash = hash * 31 + c;
    }
    return (hash & 0x7FFFFFFF) % 9000 + 1000;  // range [1000, 9999]
}

- (std::string)keyForTag:(NSInteger)tag {
    for (const auto& item : _items) {
        if ([self tagForKey:item.key] == tag) {
            return item.key;
        }
    }
    return "";
}

#pragma mark - Control Actions

- (void)toggleChanged:(NSSwitch*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    bool val = (sender.state == NSControlStateValueOn);
    setConfigBoolValue(ctrl.config, key, val);
    [ctrl configDidChange];
}

- (void)textFieldChanged:(NSTextField*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    setConfigValue(ctrl.config, key, sender.stringValue);
    [ctrl configDidChange];
}

- (void)numberFieldChanged:(NSTextField*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    float val = sender.floatValue;

    // Determine if this is an int or float field
    const termcore::SettingItem* item = nullptr;
    for (const auto& it : _items) {
        if (it.key == key) { item = &it; break; }
    }

    if (item && configIntValue(ctrl.config, key) != 0) {
        setConfigIntValue(ctrl.config, key, (int)val);
    } else {
        setConfigFloatValue(ctrl.config, key, val);
    }
    [ctrl configDidChange];
}

- (void)stepperChanged:(NSStepper*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    float val = (float)sender.doubleValue;

    // Update paired text field
    NSView* parent = sender.superview;
    if (parent) {
        for (NSView* sub in parent.subviews) {
            if ([sub isKindOfClass:[NSTextField class]] && sub.tag == sender.tag) {
                ((NSTextField*)sub).stringValue = [NSString stringWithFormat:@"%g", val];
                break;
            }
        }
    }

    if (configIntValue(ctrl.config, key) != 0 || key == "scrollback_limit" ||
        key == "window_width" || key == "window_height" || key == "window_padding" ||
        key == "background_blur" || key == "sidebar_width") {
        setConfigIntValue(ctrl.config, key, (int)val);
    } else {
        setConfigFloatValue(ctrl.config, key, val);
    }
    [ctrl configDidChange];
}

- (void)sliderChanged:(NSSlider*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    float val = (float)sender.doubleValue;
    setConfigFloatValue(ctrl.config, key, val);

    // Update value label
    NSView* parent = sender.superview;
    if (parent) {
        NSInteger valLabelTag = sender.tag + 10000;
        for (NSView* sub in parent.subviews) {
            if ([sub isKindOfClass:[NSTextField class]] && sub.tag == valLabelTag) {
                ((NSTextField*)sub).stringValue = [NSString stringWithFormat:@"%.2f", val];
                break;
            }
        }
    }

    [ctrl configDidChange];
}

- (void)dropdownChanged:(NSPopUpButton*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    setConfigValue(ctrl.config, key, sender.titleOfSelectedItem);
    [ctrl configDidChange];
}

- (void)colorWellChanged:(NSColorWell*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    uint32_t rgb = rgbFromNSColor(sender.color);
    setConfigColorValue(ctrl.config, key, rgb);
    [ctrl configDidChange];
}

@end

#endif // __APPLE__
