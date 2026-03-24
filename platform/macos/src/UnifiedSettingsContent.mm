#if defined(__APPLE__)

#import "UnifiedSettingsContent.h"
#import "UnifiedSettingsWindowController.h"

#include "termcore/settings_model.h"
#include "termcore/config.h"
#include "termcore/config_value_adapter.h"

#include <string>

static const CGFloat kContentPadding  = 24.0;
static const CGFloat kItemSpacing     = 16.0;
static const CGFloat kModifiedBarWidth = 3.0;
static const CGFloat kLabelFontSize   = 14.0;
static const CGFloat kDescFontSize    = 12.0;
static const CGFloat kTitleFontSize   = 20.0;

/// Helper: get a string value from config as NSString.
static NSString* configNSStringValue(const termcore::Config& cfg, const std::string& key) {
    std::string val = termcore::getConfigString(cfg, key);
    return [NSString stringWithUTF8String:val.c_str()];
}

/// Helper: set a config string value from NSString.
static void setConfigNSStringValue(termcore::Config& cfg, const std::string& key, NSString* strValue) {
    termcore::setConfigString(cfg, key, std::string([strValue UTF8String]));
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
        // Filter items to only include those available on this platform
        for (const auto& item : category->items) {
            if (item.platforms & termcore::currentPlatform())
                _items.push_back(item);
        }
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
            toggle.state = termcore::getConfigBool(ctrl.config, key) ? NSControlStateValueOn : NSControlStateValueOff;
            toggle.target = self;
            toggle.action = @selector(toggleChanged:);
            toggle.tag = [self tagForKey:key];
            return toggle;
        }

        case termcore::SettingType::Text: {
            NSTextField* textField = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, MIN(width, 300), 24)];
            textField.stringValue = configNSStringValue(ctrl.config, key);
            textField.placeholderString = [NSString stringWithUTF8String:key.c_str()];
            textField.bezelStyle = NSTextFieldRoundedBezel;
            textField.target = self;
            textField.action = @selector(textFieldChanged:);
            textField.tag = [self tagForKey:key];
            return textField;
        }

        case termcore::SettingType::Number: {
            NSView* numView = [[NSView alloc] initWithFrame:NSMakeRect(x, y, 160, 24)];

            float currentVal = termcore::getConfigFloat(ctrl.config, key);
            if (currentVal == 0) currentVal = (float)termcore::getConfigInt(ctrl.config, key);

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

            float currentVal = termcore::getConfigFloat(ctrl.config, key);

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
            const auto& options = item.meta.options;
            const auto& labels = item.meta.option_labels;
            bool hasLabels = !labels.empty() && labels.size() == options.size();
            std::string currentValue = termcore::getConfigString(ctrl.config, key);

            int activeIdx = 0;
            for (int i = 0; i < (int)options.size(); ++i) {
                const std::string& display = hasLabels ? labels[i] : options[i];
                NSString* nsDisplay = [NSString stringWithUTF8String:display.c_str()];
                [popup addItemWithTitle:nsDisplay];
                if (options[i] == currentValue) activeIdx = i;
            }
            [popup selectItemAtIndex:activeIdx];
            popup.tag = [self tagForKey:key];
            popup.target = self;
            popup.action = @selector(dropdownChanged:);
            return popup;
        }

        case termcore::SettingType::ColorPicker: {
            uint32_t rgb = termcore::getConfigColor(ctrl.config, key);
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
    termcore::setConfigBool(ctrl.config, key, val);
    [ctrl configDidChange];
}

- (void)textFieldChanged:(NSTextField*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    setConfigNSStringValue(ctrl.config, key, sender.stringValue);
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

    if (item && termcore::getConfigInt(ctrl.config, key) != 0) {
        termcore::setConfigInt(ctrl.config, key, (int)val);
    } else {
        termcore::setConfigFloat(ctrl.config, key, val);
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

    if (termcore::getConfigInt(ctrl.config, key) != 0 || key == "scrollback_limit" ||
        key == "window_width" || key == "window_height" || key == "window_padding" ||
        key == "sidebar_width") {
        termcore::setConfigInt(ctrl.config, key, (int)val);
    } else {
        termcore::setConfigFloat(ctrl.config, key, val);
    }
    [ctrl configDidChange];
}

- (void)sliderChanged:(NSSlider*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    float val = (float)sender.doubleValue;
    termcore::setConfigFloat(ctrl.config, key, val);

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

    // Find the SettingItem to get options (stored values)
    for (const auto& item : _items) {
        if ([self tagForKey:item.key] == sender.tag && item.type == termcore::SettingType::Dropdown) {
            NSInteger idx = [sender indexOfSelectedItem];
            if (idx >= 0 && idx < (NSInteger)item.meta.options.size()) {
                termcore::setConfigString(ctrl.config, key, item.meta.options[idx]);
            }
            break;
        }
    }
    [ctrl configDidChange];
}

- (void)colorWellChanged:(NSColorWell*)sender {
    std::string key = [self keyForTag:sender.tag];
    if (key.empty()) return;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    uint32_t rgb = rgbFromNSColor(sender.color);
    termcore::setConfigColor(ctrl.config, key, rgb);
    [ctrl configDidChange];
}

@end

#endif // __APPLE__
