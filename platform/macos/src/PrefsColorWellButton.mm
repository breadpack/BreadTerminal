#import "PrefsColorWellButton.h"

@implementation PrefsColorWellButton

- (uint32_t)hexColor {
    NSColor* c = [self.color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!c) c = self.color;
    uint8_t r = (uint8_t)(c.redComponent   * 255.0 + 0.5);
    uint8_t g = (uint8_t)(c.greenComponent * 255.0 + 0.5);
    uint8_t b = (uint8_t)(c.blueComponent  * 255.0 + 0.5);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

- (void)setHexColor:(uint32_t)hexColor {
    CGFloat r = ((hexColor >> 16) & 0xFF) / 255.0;
    CGFloat g = ((hexColor >>  8) & 0xFF) / 255.0;
    CGFloat b = ((hexColor >>  0) & 0xFF) / 255.0;
    self.color = [NSColor colorWithSRGBRed:r green:g blue:b alpha:1.0];
}

- (void)activate:(BOOL)exclusive {
    [super activate:exclusive];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(colorDidChange:)
                                                 name:NSColorPanelColorDidChangeNotification
                                               object:nil];
}

- (void)deactivate {
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSColorPanelColorDidChangeNotification
                                                  object:nil];
    [super deactivate];
}

- (void)colorDidChange:(NSNotification*)notification {
    (void)notification;
    if (self.onColorChanged) {
        self.onColorChanged(self.hexColor);
    }
}

@end
