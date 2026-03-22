#ifndef TERMCORE_ACCESSIBILITY_H
#define TERMCORE_ACCESSIBILITY_H

namespace termcore {

/// Cross-platform accessibility preferences reported by the OS.
struct AccessibilityPreferences {
    bool high_contrast = false;
    bool reduced_motion = false;
    float animation_speed_factor = 1.0f;  // 0.0 = no animation, 1.0 = normal
};

} // namespace termcore

#endif // TERMCORE_ACCESSIBILITY_H
