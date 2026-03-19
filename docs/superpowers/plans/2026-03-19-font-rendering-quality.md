# Font Rendering Quality Improvement Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Achieve Ghostty-level text rendering quality across all platforms (macOS/Windows/Linux) with correct baseline alignment, CJK support, emoji rendering, and variable font handling.

**Architecture:** Fix the CoreText bitmap Y-axis mismatch (CG Y-up → Metal Y-down) using text position adjustment (Kitty approach: `height - baseline`) or post-render row swap. Add sRGB framebuffer (`BGRA8Unorm_sRGB`) for automatic gamma-correct blending (Ghostty approach). Switch to premultiplied alpha, pixel-coordinate sampling, dynamic scale factor, ASCII pre-cache, CJK double-width centering, and procedural box drawing. Each OS platform uses its native rasterizer (CoreText/DirectWrite/FreeType) with shared glyph atlas and cache.

**Tech Stack:** CoreText + Metal (macOS), DirectWrite + Direct2D (Windows), FreeType + Fontconfig + OpenGL (Linux), HarfBuzz (shaping, all platforms)

---

## File Structure

### Files to Modify

| File | Responsibility | Changes |
|------|---------------|---------|
| `platform/macos/src/CoreTextRasterizer.mm` | macOS glyph rasterization | Add bitmap Y-flip, dynamic scale, padding fix |
| `platform/macos/src/MetalTextRenderer.mm:246-345` | Cell buffer building | CJK centering, premultiplied alpha, ASCII pre-cache trigger |
| `platform/macos/src/Shaders/cell.metal:87-107` | Fragment shader | Premultiplied alpha blending, gamma correction |
| `platform/macos/src/MetalTextRenderer.mm:78-110` | Pipeline state | Switch to premultiplied alpha blend factors |
| `platform/macos/src/TerminalView.mm:42-50` | View initialization | Pass scale factor to rasterizer |
| `platform/macos/include/MetalTextRenderer.h` | Renderer header | Add scale factor parameter |
| `platform/macos/include/CoreTextRasterizer.h` | Rasterizer header | Add scale factor to interface |
| `platform/macos/src/CoreTextDiscovery.mm:213-248` | Font fallback | Improve CTFontCreateForString usage |
| `core/include/termcore/font/i_font_rasterizer.h` | Rasterizer interface | Add setScaleFactor() |
| `core/src/font/glyph_cache.cpp:40-73` | Cache miss path | No change (works correctly once rasterizer fixed) |
| `core/src/font/box_drawing.cpp` | Box drawing | Improve pixel-perfect rendering |

### Files to Create

| File | Responsibility |
|------|---------------|
| `tests/test_bitmap_flip.cpp` | Unit test for bitmap Y-flip correctness |
| `tests/test_glyph_positioning.cpp` | Unit test for bearing/offset calculations |

---

## Chunk 1: Critical Rendering Fixes (macOS)

### Task 1: Bitmap Y-Flip in CoreTextRasterizer

The root cause of all baseline alignment issues. CoreGraphics renders with Y-up (row 0 = bottom), Metal textures expect Y-down (row 0 = top).

**Approaches used by other terminals:**
- **Kitty**: `CGContextSetTextPosition(ctx, x, height - baseline)` — adjusts draw position so CG renders right-side-up
- **Alacritty**: Handles in vertex shader `glyphOffset.y = cellDim.y - glyphOffset.y`
- **Ghostty**: CTM manipulation before rendering

**Our approach**: Post-render memory row swap — simplest fix that requires no shader changes and keeps bearing math intuitive. One-time cost per glyph, cached in atlas.

**Files:**
- Modify: `platform/macos/src/CoreTextRasterizer.mm:155-250`
- Create: `tests/test_bitmap_flip.cpp`

- [ ] **Step 1: Write failing test for bitmap Y-flip**

In `tests/test_bitmap_flip.cpp`:
```cpp
#include <gtest/gtest.h>
#include <vector>
#include <cstdint>

// Helper: flip bitmap rows in place
static void flipBitmapRows(uint8_t* data, int width, int height, int bytesPerPixel) {
    int rowBytes = width * bytesPerPixel;
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = data + y * rowBytes;
        uint8_t* bot = data + (height - 1 - y) * rowBytes;
        for (int i = 0; i < rowBytes; ++i) {
            std::swap(top[i], bot[i]);
        }
    }
}

TEST(BitmapFlip, FlipsGrayscaleCorrectly) {
    // 3x3 grayscale bitmap: row0=[1,2,3], row1=[4,5,6], row2=[7,8,9]
    std::vector<uint8_t> bitmap = {1,2,3, 4,5,6, 7,8,9};
    flipBitmapRows(bitmap.data(), 3, 3, 1);
    // After flip: row0=[7,8,9], row1=[4,5,6], row2=[1,2,3]
    std::vector<uint8_t> expected = {7,8,9, 4,5,6, 1,2,3};
    EXPECT_EQ(bitmap, expected);
}

TEST(BitmapFlip, FlipsBGRACorrectly) {
    // 2x2 BGRA bitmap (8 bytes per row)
    std::vector<uint8_t> bitmap = {
        10,20,30,40, 50,60,70,80,   // row 0
        90,100,110,120, 130,140,150,160  // row 1
    };
    flipBitmapRows(bitmap.data(), 2, 2, 4);
    std::vector<uint8_t> expected = {
        90,100,110,120, 130,140,150,160,  // row 0 (was row 1)
        10,20,30,40, 50,60,70,80          // row 1 (was row 0)
    };
    EXPECT_EQ(bitmap, expected);
}

TEST(BitmapFlip, SingleRowNoOp) {
    std::vector<uint8_t> bitmap = {1,2,3};
    std::vector<uint8_t> original = bitmap;
    flipBitmapRows(bitmap.data(), 3, 1, 1);
    EXPECT_EQ(bitmap, original);
}

TEST(BitmapFlip, EvenHeightFlip) {
    // 2x4 grayscale
    std::vector<uint8_t> bitmap = {1,2, 3,4, 5,6, 7,8};
    flipBitmapRows(bitmap.data(), 2, 4, 1);
    std::vector<uint8_t> expected = {7,8, 5,6, 3,4, 1,2};
    EXPECT_EQ(bitmap, expected);
}
```

- [ ] **Step 2: Add test file to CMakeLists.txt and verify it compiles and fails**

In `tests/CMakeLists.txt`, add `test_bitmap_flip.cpp` to the source list.

Run: `cd /Users/milennium9/BreadTerminal/build && cmake .. && cmake --build . --target termcore_tests 2>&1 | tail -20`
Then: `./tests/termcore_tests --gtest_filter='BitmapFlip.*'`
Expected: All 4 tests PASS (these test the standalone helper, not the rasterizer yet)

- [ ] **Step 3: Add flipBitmapRows helper and apply to CoreTextRasterizer**

In `platform/macos/src/CoreTextRasterizer.mm`, add a helper function in the anonymous namespace (after `setupContext`):

```cpp
/// Flip bitmap rows in-place: converts CG Y-up to Metal Y-down.
void flipBitmapRows(uint8_t* data, int width, int height, int bytesPerPixel) {
    int rowBytes = width * bytesPerPixel;
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = data + y * rowBytes;
        uint8_t* bot = data + (height - 1 - y) * rowBytes;
        for (int i = 0; i < rowBytes; ++i) {
            std::swap(top[i], bot[i]);
        }
    }
}
```

Then apply in the `rasterize()` method after each `CTFontDrawGlyphs` call:

**Color path** (after line 214, before closing `}`):
```cpp
        // Flip CG Y-up to Metal Y-down
        flipBitmapRows(result.bitmap.data(), w, h, 4);
```

**Grayscale path** — flip the RGBA buffer before extracting alpha (after `CTFontDrawGlyphs` on line 234, before the alpha extraction loop on line 238):
```cpp
        // Flip CG Y-up to Metal Y-down (flip RGBA buffer before alpha extraction)
        flipBitmapRows(rgbaBuf.data(), w, h, 4);
```

- [ ] **Step 4: Fix bearing_y after Y-flip**

After the Y-flip, the bitmap's top row is now the visual top of the glyph. The `bearing_y` value (baseline to glyph top) is still correct because it's a font metric, not a bitmap coordinate. **No change needed to bearing_y calculation.**

Verify the existing calculation at line 194 is correct:
```cpp
result.bearing_y = static_cast<int32_t>(std::round(bboxTop * scale));
```
This gives the distance from baseline to glyph top in pixels, which is correct regardless of bitmap orientation.

- [ ] **Step 5: Build and run tests**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Run: `./tests/termcore_tests --gtest_filter='BitmapFlip.*'`
Expected: All 4 tests PASS

- [ ] **Step 6: Visual verification**

Run the app and check:
1. Glyphs should no longer appear vertically inverted
2. Baseline alignment should be significantly improved
3. Characters like 'g', 'y', 'p' should have descenders below the baseline

- [ ] **Step 7: Commit**

```bash
git add platform/macos/src/CoreTextRasterizer.mm tests/test_bitmap_flip.cpp tests/CMakeLists.txt
git commit -m "fix: add bitmap Y-flip for CG→Metal coordinate conversion

All major terminals (Alacritty, Kitty, WezTerm) flip bitmap rows after
CTFontDrawGlyphs because CG renders Y-up while Metal textures are Y-down.
This was the root cause of baseline misalignment."
```

---

### Task 2: Fix Padding Offset in Bearing Calculation

The rasterizer adds +2px padding to bitmap dimensions (line 185-186) but doesn't compensate in bearing values. This shifts glyphs by up to 1px.

**Files:**
- Modify: `platform/macos/src/CoreTextRasterizer.mm:180-194`

- [ ] **Step 1: Understand the padding issue**

Current code:
```cpp
float drawX = -std::min(bboxLeft, 0.0f) + subX;   // Offset for negative bearing
float drawY = -std::min(bboxBottom, 0.0f) + subY;  // Offset for descenders
```

The glyph is drawn at `(drawX, drawY)` within the bitmap. The bearing should reflect the actual pixel position of the glyph within the bitmap, not just the raw bbox metric.

When `bboxLeft < 0`, the glyph is drawn at `drawX = -bboxLeft` pixels from the left edge. The bearing_x should account for this: the glyph's left edge is at `bboxLeft` from the pen, but the bitmap starts at `0` with the glyph at `drawX` pixels in.

Current bearing:
```cpp
result.bearing_x = round(bboxLeft * scale);  // e.g., -3 if overhanging
```

This is correct — bearing_x is the offset from pen to glyph left edge in the output coordinate system. The bitmap's draw position already accounts for negative origins. The renderer uses `offset_x = bearing_x` which places the glyph at `cell_origin + bearing_x`, which correctly offsets for overhang.

**However**, the +2px padding (1px on each side effectively) means the glyph is not at position 0 in the bitmap but at position `drawX * scale` (plus padding effect). Since the atlas stores the full bitmap including padding, and the UV coordinates sample the full bitmap, the padding is included in the rendered quad. This extra pixel of transparent space around the glyph is harmless for rendering but makes the quad slightly larger than necessary.

**Action:** The +2 padding is actually fine as-is — it prevents texture sampling bleed at glyph edges. No bearing adjustment needed because the glyph is drawn at the correct relative position within the padded bitmap. The bearing values correctly describe where the pen position maps to the glyph's visual content.

- [ ] **Step 2: Verify with a test — skip if padding is correct**

After visual verification in Task 1 Step 6, if alignment looks correct, the padding is working as intended. Move to Task 3.

---

### Task 3: Dynamic Scale Factor

Replace hardcoded `scale = 2.0f` with actual display scale factor.

**Files:**
- Modify: `core/include/termcore/font/i_font_rasterizer.h:29-48`
- Modify: `platform/macos/src/CoreTextRasterizer.mm:60-66,155-175,252-290`
- Modify: `platform/macos/include/CoreTextRasterizer.h`
- Modify: `platform/macos/src/TerminalView.mm:42-50`

- [ ] **Step 1: Add setScaleFactor to IFontRasterizer interface**

In `core/include/termcore/font/i_font_rasterizer.h`, add to the `IFontRasterizer` class:
```cpp
    /// Set display scale factor (1.0 for standard, 2.0 for Retina).
    /// Must be called before rasterize() or getMetrics().
    virtual void setScaleFactor(float scale) { scale_ = scale; }
    float scaleFactor() const { return scale_; }
protected:
    float scale_ = 2.0f;  // Default to Retina for backward compatibility
```

- [ ] **Step 2: Use scale_ in CoreTextRasterizerImpl**

In `CoreTextRasterizer.mm`, replace all `float scale = 2.0f;` with `float scale = scale_;` (appears in `rasterize()` at line 172 and `getMetrics()` at line 260).

Override `setScaleFactor` if needed (base class default is sufficient).

- [ ] **Step 3: Pass scale from TerminalView**

In `TerminalView.mm` `initWithFrame:device:` (after line 43 where rasterizer is created):
```objc
    CGFloat scale = self.window.backingScaleFactor ?: 2.0;
    _impl->rasterizer->setScaleFactor(static_cast<float>(scale));
```

Also in `viewDidMoveToWindow` (line 143-146):
```objc
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window) {
        _metalLayer.contentsScale = self.window.backingScaleFactor;
        _impl->rasterizer->setScaleFactor(
            static_cast<float>(self.window.backingScaleFactor));
        // Invalidate cache when scale changes
        _impl->cache->clear();
        _impl->needsRender = true;
    }
}
```

- [ ] **Step 4: Build and test**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Expected: Compiles cleanly. Visual verification: text should look identical on Retina (scale=2.0 is default).

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/font/i_font_rasterizer.h \
        platform/macos/src/CoreTextRasterizer.mm \
        platform/macos/src/TerminalView.mm
git commit -m "feat: dynamic scale factor for non-Retina display support"
```

---

### Task 4: Premultiplied Alpha Blending

CoreText renders with premultiplied alpha. The shader and pipeline should use premultiplied alpha blending for correct compositing.

**Files:**
- Modify: `platform/macos/src/Shaders/cell.metal:87-107`
- Modify: `platform/macos/src/MetalTextRenderer.mm:78-110` (pipeline state)
- Modify: `platform/macos/src/MetalTextRenderer.mm:144-240` (inline shader copy)

- [ ] **Step 1: Update pipeline blend factors**

In `MetalTextRenderer.mm`, change the pipeline blend factors (lines 94-100):

```objc
        // Premultiplied alpha blending
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
```

- [ ] **Step 2: Update fragment shader for premultiplied alpha**

In `cell.metal`, change the grayscale glyph rendering (line 103-105):

```metal
    if (is_color) {
        // Color emoji: already premultiplied by CoreText
        return atlas_color.sample(s, in.texCoord);
    } else {
        float alpha = atlas_gray.sample(s, in.texCoord).r;
        // Premultiplied: multiply RGB by alpha
        return float4(in.fg_color.rgb * alpha, alpha);
    }
```

Also update the **inline shader copy** in MetalTextRenderer.mm (the `compileShaderSource()` method around lines 144-240) with the same fragment shader change.

- [ ] **Step 3: Build and verify**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: Text should have smoother edges, especially on non-black backgrounds. No "halo" effect around glyphs.

- [ ] **Step 4: Commit**

```bash
git add platform/macos/src/MetalTextRenderer.mm platform/macos/src/Shaders/cell.metal
git commit -m "fix: switch to premultiplied alpha blending for correct compositing"
```

---

### Task 4b: sRGB Framebuffer for Gamma-Correct Blending

Ghostty uses `bgra8unorm_srgb` for the framebuffer, which makes Metal perform blending in linear space automatically. This eliminates dark fringes around text on light backgrounds. This single change replaces the need for manual gamma correction in the shader.

**Files:**
- Modify: `platform/macos/src/TerminalView.mm:36` (layer pixel format)
- Modify: `platform/macos/src/MetalTextRenderer.mm:87` (pipeline pixel format must match)

- [ ] **Step 1: Change CAMetalLayer pixel format**

In `TerminalView.mm` line 36:
```objc
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;  // was MTLPixelFormatBGRA8Unorm
```

- [ ] **Step 2: Update pipeline descriptor to match**

In `MetalTextRenderer.mm`, the pipeline descriptor reads the pixel format from the layer (line 87):
```objc
        desc.colorAttachments[0].pixelFormat = layer.pixelFormat;
```
This already uses the layer's format, so it will automatically pick up the sRGB change. **No code change needed here**, just verify.

- [ ] **Step 3: Build and verify**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: Text on colored backgrounds should no longer have dark fringes. Color accuracy improved.

- [ ] **Step 4: Commit**

```bash
git add platform/macos/src/TerminalView.mm
git commit -m "fix: use sRGB framebuffer for gamma-correct alpha blending (Ghostty approach)"
```

---

### Task 4c: Pixel-Coordinate Texture Sampling

Switch from normalized UV coordinates to pixel coordinates for atlas sampling. This eliminates precision issues at atlas boundaries and prevents bleeding between adjacent glyphs. Ghostty uses `coord::pixel` with `filter::nearest`.

**Files:**
- Modify: `platform/macos/src/Shaders/cell.metal:62-71` (vertex shader UV computation)
- Modify: `platform/macos/src/Shaders/cell.metal:97` (sampler definition)
- Modify: `platform/macos/src/MetalTextRenderer.mm:144-240` (inline shader copy)

- [ ] **Step 1: Update vertex shader to pass pixel coordinates**

In `cell.metal`, change the glyph UV computation (lines 70-71):
```metal
        // Pass pixel coordinates instead of normalized UVs
        // No division by atlas_size — sampler uses coord::pixel
        tex_coord = float2(cell.glyph_uv) + corner * glyph_size;
```

- [ ] **Step 2: Update samplers to use pixel coordinates**

```metal
    if (is_color) {
        constexpr sampler emojiSampler(coord::pixel, address::clamp_to_edge,
                                        filter::linear);
        return atlas_color.sample(emojiSampler, in.texCoord);
    } else {
        constexpr sampler textSampler(coord::pixel, address::clamp_to_edge,
                                       filter::nearest);
        float alpha = atlas_gray.sample(textSampler, in.texCoord).r;
        return float4(in.fg_color.rgb * alpha, alpha);
    }
```

- [ ] **Step 3: Remove atlas_size from uniforms (optional)**

With pixel-coordinate sampling, `atlas_size` in uniforms is no longer needed for UV normalization. It can be removed or kept for debugging.

- [ ] **Step 4: Update inline shader copy and build**

Update the inline shader in `compileShaderSource()` to match. Build and verify.

- [ ] **Step 5: Commit**

```bash
git add platform/macos/src/Shaders/cell.metal platform/macos/src/MetalTextRenderer.mm
git commit -m "fix: use pixel-coordinate sampling for precise glyph atlas lookup"
```

---

### Task 5: ASCII Pre-cache on Font Load

Trigger ASCII pre-cache when the font is first loaded to avoid first-frame cache misses.

**Files:**
- Modify: `platform/macos/src/MetalTextRenderer.mm:360-368` (setFontStack)

- [ ] **Step 1: Add pre-cache call in setFontStack**

In `MetalTextRenderer.mm`, after `setFontStack` assigns pointers (line 364-367), add:

```cpp
void MetalTextRenderer::setFontStack(FontCollection* collection,
                                      GlyphCache* cache,
                                      GlyphAtlas* atlas,
                                      IFontRasterizer* rasterizer) {
    impl_->fontCollection = collection;
    impl_->glyphCache = cache;
    impl_->glyphAtlas = atlas;
    impl_->rasterizer = rasterizer;

    // Pre-cache ASCII glyphs for instant first-frame display
    if (collection && cache && atlas && rasterizer) {
        FontFaceId primaryFace = collection->rasterizerFaceId(
            collection->resolveFace('A'));
        if (primaryFace != kInvalidFontFace) {
            cache->precacheAscii(primaryFace, collection->fontSize(),
                                  *rasterizer, *atlas);
        }
    }
}
```

- [ ] **Step 2: Build and verify**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: First frame with shell prompt should render instantly without visible glyph pop-in.

- [ ] **Step 3: Commit**

```bash
git add platform/macos/src/MetalTextRenderer.mm
git commit -m "perf: pre-cache ASCII glyphs on font load for instant first frame"
```

---

## Chunk 2: CJK & Font Fallback Improvements

### Task 6: Improve System Font Fallback via CTFontCreateForString

Current fallback in CoreTextDiscovery uses a basic approach. Improve to use `CTFontCreateForString()` which is the standard macOS API for finding the best font for a given codepoint.

**Files:**
- Modify: `platform/macos/src/CoreTextDiscovery.mm:213-248`

- [ ] **Step 1: Read current findFallback implementation**

Read `platform/macos/src/CoreTextDiscovery.mm` lines 213-248 to understand current approach.

- [ ] **Step 2: Improve findFallback with CTFontCreateForString**

The current implementation should already use `CTFontCreateForString`. Verify and ensure:

```objc
FontDescriptor CoreTextDiscoveryImpl::findFallback(char32_t codepoint, FontStyle style) {
    // Create a string from the codepoint
    UniChar characters[2];
    CFIndex charCount = 0;
    if (codepoint <= 0xFFFF) {
        characters[0] = static_cast<UniChar>(codepoint);
        charCount = 1;
    } else {
        char32_t code = codepoint - 0x10000;
        characters[0] = static_cast<UniChar>(0xD800 + (code >> 10));
        characters[1] = static_cast<UniChar>(0xDC00 + (code & 0x3FF));
        charCount = 2;
    }

    CFPtr<CFStringRef> str(CFStringCreateWithCharacters(
        kCFAllocatorDefault, characters, charCount));
    if (!str) return {};

    // Use system monospace as base font for fallback
    CTFontRef baseFont = CTFontCreateUIFontForLanguage(
        kCTFontUIFontUserFixedPitch, 14.0, nullptr);
    if (!baseFont) {
        baseFont = CTFontCreateWithName(CFSTR("Menlo"), 14.0, nullptr);
    }
    if (!baseFont) return {};
    CFPtr<CTFontRef> base(baseFont);

    // CTFontCreateForString returns a font that can render the string
    CTFontRef fallbackFont = CTFontCreateForString(base.get(), str.get(),
        CFRangeMake(0, CFStringGetLength(str.get())));
    if (!fallbackFont) return {};
    CFPtr<CTFontRef> fallback(fallbackFont);

    // Extract descriptor from fallback font
    return descriptorFromCTFont(fallback.get());
}
```

- [ ] **Step 3: Build and test with CJK characters**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: Type Korean/Japanese/Chinese characters. They should render using system CJK fonts.

- [ ] **Step 4: Commit**

```bash
git add platform/macos/src/CoreTextDiscovery.mm
git commit -m "fix: improve font fallback via CTFontCreateForString for CJK support"
```

---

### Task 7: CJK Double-Width Cell Centering

CJK characters occupy 2 terminal cells. The glyph must be centered across both cells.

**Files:**
- Modify: `platform/macos/src/MetalTextRenderer.mm:286-344` (Pass 2 glyph building)
- Read: `core/include/termcore/font/unicode_width.h` (for width detection)

- [ ] **Step 1: Read unicode_width.h to find wcwidth function**

Read `core/include/termcore/font/unicode_width.h` and `core/src/font/unicode_width.cpp` to understand how character width is determined. Also check how `Screen::cellAt()` marks wide characters.

- [ ] **Step 2: Add double-width centering in buildCellBuffer**

In `MetalTextRenderer.mm` `buildCellBuffer()`, in Pass 2 (after getting the glyph info, around line 322):

```cpp
                // Check if this is a double-width character
                bool is_wide = (cell.attributes & AttrWide) != 0;
                float cellSpan = is_wide ? 2.0f : 1.0f;

                inst.offset_x = static_cast<int16_t>(info->region.bearing_x);
                inst.offset_y = static_cast<int16_t>(
                    static_cast<int>(ascent) - info->region.bearing_y);

                // Center wide glyphs across their cell span
                if (is_wide && info->region.width < static_cast<int>(cellW * cellSpan)) {
                    int16_t center_offset = static_cast<int16_t>(
                        (cellW * cellSpan - info->region.width) / 2.0f);
                    inst.offset_x = static_cast<int16_t>(
                        center_offset + info->region.bearing_x);
                }
```

Also skip the continuation cell (the second cell of a wide character is typically marked with a flag or codepoint=0):
```cpp
                // Skip continuation cells of wide characters
                if (cell.codepoint == 0 && col > 0) continue;
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: Korean characters like "안녕하세요" should be properly centered in their 2-cell spans.

- [ ] **Step 4: Commit**

```bash
git add platform/macos/src/MetalTextRenderer.mm
git commit -m "feat: center CJK double-width glyphs across 2-cell spans"
```

---

## Chunk 3: Shader & Rendering Polish

### Task 8: Separate Sampling Modes for Text vs Emoji

Use `nearest` for pixel-perfect text, `linear` for color emoji (which may be scaled).

**Files:**
- Modify: `platform/macos/src/Shaders/cell.metal:87-107`
- Modify: `platform/macos/src/MetalTextRenderer.mm:144-240` (inline shader)

- [ ] **Step 1: Update fragment shader with dual samplers**

In `cell.metal`, change the fragment shader:

```metal
fragment float4 cell_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> atlas_gray [[texture(0)]],
    texture2d<float> atlas_color [[texture(1)]]
) {
    if ((in.flags & 4) != 0) {
        return in.bg_color;
    }

    bool is_color = (in.flags & 2) != 0;

    if (is_color) {
        // Linear filtering for emoji (may be scaled)
        constexpr sampler emojiSampler(mag_filter::linear, min_filter::linear);
        return atlas_color.sample(emojiSampler, in.texCoord);
    } else {
        // Nearest filtering for pixel-perfect text at matched resolution
        constexpr sampler textSampler(mag_filter::nearest, min_filter::nearest);
        float alpha = atlas_gray.sample(textSampler, in.texCoord).r;
        return float4(in.fg_color.rgb * alpha, alpha);
    }
}
```

Update the inline shader copy in `MetalTextRenderer.mm` to match.

- [ ] **Step 2: Build and verify**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: Text should be crisp (no blur). Emoji should be smooth when scaled.

- [ ] **Step 3: Commit**

```bash
git add platform/macos/src/Shaders/cell.metal platform/macos/src/MetalTextRenderer.mm
git commit -m "fix: use nearest filtering for text, linear for emoji"
```

---

### Task 9: Gamma Correction for Text Weight

Text can appear too thin on dark backgrounds or too thick on light backgrounds without gamma correction. Add optional gamma correction in the shader.

**Files:**
- Modify: `platform/macos/src/Shaders/cell.metal`
- Modify: `platform/macos/include/MetalTextRenderer.h` (add gamma to uniforms)
- Modify: `platform/macos/src/MetalTextRenderer.mm` (set gamma uniform)

- [ ] **Step 1: Add gamma to CellUniforms**

In `MetalTextRenderer.h`, expand CellUniforms:

```cpp
struct CellUniforms {
    float viewport_size[2];
    float cell_size[2];
    float atlas_size[2];
    float grid_padding[2];
    float gamma;        // Text gamma correction (1.0 = no correction, 1.8 = macOS default)
    float _pad[3];      // Pad to 48 bytes (multiple of 16)
};
static_assert(sizeof(CellUniforms) == 48, "CellUniforms must be 48 bytes");
```

Update the static_assert accordingly.

- [ ] **Step 2: Apply gamma in fragment shader**

In `cell.metal`, update the Uniforms struct and grayscale path:

```metal
struct Uniforms {
    float2 viewport_size;
    float2 cell_size;
    float2 atlas_size;
    float2 grid_padding;
    float  gamma;
    float3 _pad;
};
```

Fragment shader change:
```metal
    } else {
        constexpr sampler textSampler(mag_filter::nearest, min_filter::nearest);
        float alpha = atlas_gray.sample(textSampler, in.texCoord).r;
        // Gamma correction for text weight consistency across bg colors
        alpha = pow(alpha, 1.0 / u.gamma);
        return float4(in.fg_color.rgb * alpha, alpha);
    }
```

Note: The fragment shader needs access to uniforms. Pass gamma via the vertex output or bind uniforms to fragment stage too.

Simpler approach — pass gamma through vertex output:

In VertexOut, add:
```metal
    float gamma [[flat]];
```

In vertex shader, set:
```metal
    out.gamma = u.gamma;
```

In fragment shader:
```metal
    float alpha = atlas_gray.sample(textSampler, in.texCoord).r;
    alpha = pow(alpha, 1.0 / in.gamma);
    return float4(in.fg_color.rgb * alpha, alpha);
```

- [ ] **Step 3: Set gamma value in render**

In `MetalTextRenderer.mm`, when building uniforms (around line 394):
```cpp
        uniforms.gamma = 1.8f;  // macOS default, can be made configurable
```

- [ ] **Step 4: Update inline shader copy**

Update the inline shader in `compileShaderSource()` to match.

- [ ] **Step 5: Build and verify**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: Text should have consistent weight on both dark and light backgrounds. `gamma = 1.0` disables correction, `1.8` is typical for macOS.

- [ ] **Step 6: Commit**

```bash
git add platform/macos/src/Shaders/cell.metal \
        platform/macos/include/MetalTextRenderer.h \
        platform/macos/src/MetalTextRenderer.mm
git commit -m "feat: add gamma correction for consistent text weight across backgrounds"
```

---

## Chunk 4: Glyph Positioning Correctness Tests

### Task 10: Glyph Positioning Unit Tests

Write tests that verify the glyph positioning math is correct for various character types.

**Files:**
- Create: `tests/test_glyph_positioning.cpp`

- [ ] **Step 1: Write positioning formula tests**

```cpp
#include <gtest/gtest.h>
#include <cstdint>
#include <cmath>

// Mirror the positioning formula from MetalTextRenderer.mm:324-329
struct PositionResult {
    int16_t offset_x;  // bearing_x
    int16_t offset_y;  // ascent - bearing_y
};

PositionResult computeGlyphOffset(float ascent, int bearing_x, int bearing_y) {
    return {
        static_cast<int16_t>(bearing_x),
        static_cast<int16_t>(static_cast<int>(ascent) - bearing_y)
    };
}

TEST(GlyphPositioning, UppercaseA) {
    // 14pt Menlo at 2x: ascent ≈ 28px
    // 'A': bearing_y ≈ 26 (near top of ascent)
    auto pos = computeGlyphOffset(28.0f, 1, 26);
    EXPECT_EQ(pos.offset_x, 1);
    EXPECT_EQ(pos.offset_y, 2);  // 28 - 26 = 2px below cell top
}

TEST(GlyphPositioning, LowercaseG) {
    // 'g': bearing_y ≈ 18 (ascender portion only)
    auto pos = computeGlyphOffset(28.0f, 1, 18);
    EXPECT_EQ(pos.offset_x, 1);
    EXPECT_EQ(pos.offset_y, 10);  // 28 - 18 = 10px below cell top
}

TEST(GlyphPositioning, Underscore) {
    // '_': bearing_y ≈ -2 (below baseline)
    auto pos = computeGlyphOffset(28.0f, 0, -2);
    EXPECT_EQ(pos.offset_x, 0);
    EXPECT_EQ(pos.offset_y, 30);  // 28 - (-2) = 30px below cell top
}

TEST(GlyphPositioning, NegativeBearingX) {
    // 'j': bearing_x ≈ -2 (overhangs left)
    auto pos = computeGlyphOffset(28.0f, -2, 26);
    EXPECT_EQ(pos.offset_x, -2);
    EXPECT_EQ(pos.offset_y, 2);
}
```

- [ ] **Step 2: Add to CMakeLists.txt, build, run**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . --target termcore_tests && ./tests/termcore_tests --gtest_filter='GlyphPositioning.*'`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_glyph_positioning.cpp tests/CMakeLists.txt
git commit -m "test: add glyph positioning formula verification tests"
```

---

## Chunk 5: Windows DirectWrite Improvements

### Task 11: DirectWrite Rasterizer ClearType Rendering

Improve the Windows DirectWrite rasterizer for ClearType subpixel rendering quality.

**Files:**
- Modify: `platform/windows/src/DirectWriteRasterizer.cpp`
- Read: `platform/windows/include/DirectWriteRasterizer.h`

- [ ] **Step 1: Read current DirectWrite implementation**

Read `platform/windows/src/DirectWriteRasterizer.cpp` to understand current approach.

- [ ] **Step 2: Ensure ClearType rendering mode**

Verify the rasterizer uses `DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC` for best quality:

```cpp
// When creating glyph analysis:
hr = factory->CreateGlyphRunAnalysis(
    &glyphRun,
    pixelsPerDip,
    nullptr,  // no transform
    DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
    DWRITE_MEASURING_MODE_NATURAL,
    0.0f, 0.0f,
    &analysis);
```

- [ ] **Step 3: Add ClearType 3-channel alpha support**

For ClearType, DirectWrite produces 3 bytes per pixel (R, G, B coverage). The rasterizer should output `PixelFormat::RGB` format, and the Windows shader must blend each channel independently:

```hlsl
// In Windows shader (cell.hlsl):
float3 glyphAlpha = glyphTexture.Sample(sampler, uv).rgb;
float3 result = lerp(backgroundColor.rgb, textColor.rgb, glyphAlpha);
```

- [ ] **Step 4: Add color emoji support via IDWriteColorGlyphRunEnumerator**

```cpp
// Check for color glyph support
IDWriteFactory4* factory4 = nullptr;
factory->QueryInterface(__uuidof(IDWriteFactory4), (void**)&factory4);
if (factory4) {
    IDWriteColorGlyphRunEnumerator1* enumerator = nullptr;
    hr = factory4->TranslateColorGlyphRun(
        baselineOrigin, &glyphRun, nullptr,
        DWRITE_GLYPH_IMAGE_FORMATS_COLR |
        DWRITE_GLYPH_IMAGE_FORMATS_SVG |
        DWRITE_GLYPH_IMAGE_FORMATS_PNG,
        DWRITE_MEASURING_MODE_NATURAL,
        nullptr, 0, &enumerator);
    // Iterate color layers...
}
```

- [ ] **Step 5: Build on Windows (or skip if not available)**

This task requires a Windows build environment. If unavailable, mark as TODO and commit with implementation notes.

- [ ] **Step 6: Commit**

```bash
git add platform/windows/src/DirectWriteRasterizer.cpp
git commit -m "feat: improve DirectWrite ClearType rendering quality"
```

---

## Chunk 6: Linux FreeType Improvements

### Task 12: FreeType LCD Subpixel Rendering

Improve the Linux FreeType rasterizer for LCD subpixel rendering with gamma correction.

**Files:**
- Modify: `platform/linux/src/FreeTypeRasterizer.cpp`
- Read: `platform/linux/include/FreeTypeRasterizer.h`

- [ ] **Step 1: Read current FreeType implementation**

Read `platform/linux/src/FreeTypeRasterizer.cpp` to understand current approach.

- [ ] **Step 2: Ensure LCD rendering mode**

```cpp
// Set LCD filter before rendering
FT_Library_SetLcdFilter(library_, FT_LCD_FILTER_DEFAULT);

// Load glyph with LCD target
FT_Load_Glyph(face, glyph_index, FT_LOAD_TARGET_LCD);

// Render to LCD format
FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);

// Note: bitmap.width is 3x actual pixel width for LCD
int actual_width = face->glyph->bitmap.width / 3;
```

- [ ] **Step 3: Add gamma correction for FreeType output**

FreeType outputs linear coverage values. Convert to sRGB:

```cpp
static float linearToSrgb(float linear) {
    if (linear <= 0.0031308f)
        return linear * 12.92f;
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// Apply after rendering:
for (int i = 0; i < width * 3; i++) {
    float linear = bitmap.buffer[row * bitmap.pitch + i] / 255.0f;
    output[i] = static_cast<uint8_t>(linearToSrgb(linear) * 255.0f);
}
```

- [ ] **Step 4: Ensure Fontconfig integration for font discovery**

Verify `FontconfigDiscovery.cpp` properly uses:
```cpp
FcPattern* pattern = FcPatternCreate();
FcPatternAddString(pattern, FC_FAMILY, (FcChar8*)family.c_str());
FcPatternAddDouble(pattern, FC_SIZE, size);
FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
FcDefaultSubstitute(pattern);
FcResult result;
FcPattern* matched = FcFontMatch(nullptr, pattern, &result);
```

- [ ] **Step 5: Add color emoji support via FT_LOAD_COLOR**

```cpp
// Detect and render color emoji
FT_Load_Glyph(face, glyph_index, FT_LOAD_COLOR);
if (face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
    // Color emoji — use BGRA bitmap directly
    result.format = PixelFormat::BGRA;
    // Copy bitmap...
}
```

- [ ] **Step 6: Build on Linux (or skip if not available)**

This task requires a Linux build environment. If unavailable, mark as TODO.

- [ ] **Step 7: Commit**

```bash
git add platform/linux/src/FreeTypeRasterizer.cpp
git commit -m "feat: improve FreeType LCD subpixel rendering with gamma correction"
```

---

## Chunk 7: Box Drawing & Special Glyphs

### Task 13: Pixel-Perfect Procedural Box Drawing

Improve the box drawing renderer for pixel-perfect alignment. Kitty-style approach: procedurally generate box drawing glyphs instead of using font glyphs, ensuring perfect cell-grid alignment.

**Files:**
- Modify: `core/src/font/box_drawing.cpp:1-241`
- Modify: `platform/macos/src/MetalTextRenderer.mm:286-344` (intercept box drawing codepoints)

- [ ] **Step 1: Read current box drawing implementation**

Read `core/src/font/box_drawing.cpp` to understand current rendering.

- [ ] **Step 2: Integrate box drawing into glyph cache path**

In `MetalTextRenderer.mm` `buildCellBuffer()`, before resolving font face for a codepoint, check if it's a box drawing character:

```cpp
                // Check for procedural box drawing (U+2500-U+259F, U+2800-U+28FF, E0B0-E0B3)
                bool is_box_drawing =
                    (cell.codepoint >= 0x2500 && cell.codepoint <= 0x259F) ||
                    (cell.codepoint >= 0x2800 && cell.codepoint <= 0x28FF) ||
                    (cell.codepoint >= 0xE0B0 && cell.codepoint <= 0xE0B3);

                if (is_box_drawing) {
                    // Use procedural rendering — generate bitmap and cache
                    GlyphKey boxKey{kInvalidFontFace, cell.codepoint, {0, 0}};
                    auto boxInfo = glyphCache->get(boxKey);
                    if (!boxInfo) {
                        BoxGlyphBitmap boxBitmap = renderBoxGlyph(
                            cell.codepoint,
                            static_cast<int>(cellW),
                            static_cast<int>(cellH));
                        if (!boxBitmap.bitmap.empty()) {
                            RasterizedGlyph rg;
                            rg.bitmap = std::move(boxBitmap.bitmap);
                            rg.width = boxBitmap.width;
                            rg.height = boxBitmap.height;
                            rg.bearing_x = 0;
                            rg.bearing_y = static_cast<int32_t>(ascent);
                            rg.format = PixelFormat::Grayscale;
                            auto region = glyphAtlas->pack(rg);
                            if (region) {
                                GlyphInfo gi;
                                gi.region = *region;
                                gi.is_color = false;
                                glyphCache->put(boxKey, gi);
                                boxInfo = gi;
                            }
                        }
                    }
                    if (boxInfo) {
                        // Build CellInstance from boxInfo...
                        // offset_x = 0, offset_y = 0 (box drawing fills entire cell)
                    }
                    continue;
                }
```

- [ ] **Step 3: Ensure box drawing bitmap size matches cell size**

The `renderBoxGlyph()` function in `box_drawing.cpp` already takes `cell_width` and `cell_height` parameters. Verify the output bitmap dimensions match exactly.

- [ ] **Step 4: Build and test**

Run: `cd /Users/milennium9/BreadTerminal/build && cmake --build . 2>&1 | tail -20`
Visual: Box drawing characters should have perfect alignment at cell boundaries.

- [ ] **Step 5: Commit**

```bash
git add core/src/font/box_drawing.cpp platform/macos/src/MetalTextRenderer.mm
git commit -m "feat: procedural box drawing for pixel-perfect cell alignment"
```

---

## Chunk 8: Variable Font & Finishing

### Task 14: Variable Font Axis Support

Ensure variable fonts (weight, width axes) work correctly with CoreText.

**Files:**
- Read: `core/include/termcore/font/variable_font.h`
- Read: `core/src/font/variable_font.cpp`
- Modify if needed: `platform/macos/src/CoreTextRasterizer.mm`

- [ ] **Step 1: Read variable font implementation**

Read current `variable_font.h` and `variable_font.cpp`.

- [ ] **Step 2: Verify CoreText variable font axis handling**

CoreText supports variable fonts natively. When loading a variable font, axes can be set via font descriptor:

```objc
// Create font with specific axis values
NSDictionary* variations = @{
    @(kCTFontWeightTrait): @(0.0),  // Normal weight
};
CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(
    (__bridge CFDictionaryRef)@{
        (id)kCTFontVariationAttribute: variations
    });
```

Verify this is integrated into the font loading path. If the existing `variable_font.cpp` already handles this, no changes needed.

- [ ] **Step 3: Commit if changes were made**

```bash
git add core/src/font/variable_font.cpp platform/macos/src/CoreTextRasterizer.mm
git commit -m "feat: verify variable font axis support with CoreText"
```

---

### Task 15: Final Integration Test & Visual Verification

**Files:** None (verification only)

- [ ] **Step 1: Build clean**

```bash
cd /Users/milennium9/BreadTerminal/build && cmake .. && cmake --build . 2>&1 | tail -20
```

- [ ] **Step 2: Run all unit tests**

```bash
./tests/termcore_tests
```
Expected: All tests PASS (including new bitmap flip and positioning tests)

- [ ] **Step 3: Visual verification checklist**

Launch the app and verify:

1. **ASCII text**: `ls -la` output — all characters aligned on baseline, consistent spacing
2. **Colors**: ANSI colors render correctly (red, green, blue, bold)
3. **Descenders**: 'g', 'y', 'p', 'q' — tails extend below baseline
4. **Ascenders**: 'b', 'd', 'h', 'k' — tops near ascent line
5. **Box drawing**: Run `unicode-display` or pipes in `top` — lines connect at cell boundaries
6. **Korean/CJK**: Type "안녕하세요" — characters centered in double-width cells
7. **Emoji**: Type emoji — renders in color, properly sized
8. **Dark/light backgrounds**: Text weight consistent (gamma correction)
9. **First frame**: Prompt appears instantly (ASCII pre-cache)
10. **Input**: Characters don't disappear during typing

- [ ] **Step 4: Screenshot comparison**

Use Cmd+Shift+S to capture debug screenshot. Compare with previous screenshots from `/tmp/bread_*.png`.

- [ ] **Step 5: Final commit if any adjustments needed**

```bash
git add -A && git commit -m "fix: final rendering adjustments from visual verification"
```

---

## Summary: Execution Order

| Priority | Task | Impact | Effort |
|----------|------|--------|--------|
| P0 | Task 1: Bitmap Y-Flip | Fixes root cause of all misalignment | Small |
| P0 | Task 4: Premultiplied Alpha | Fixes compositing artifacts | Small |
| P0 | Task 4b: sRGB Framebuffer | Automatic gamma-correct blending (Ghostty) | Tiny |
| P0 | Task 4c: Pixel-Coord Sampling | Precise atlas lookup, no bleed | Small |
| P1 | Task 3: Dynamic Scale Factor | Fixes non-Retina displays | Small |
| P1 | Task 5: ASCII Pre-cache | Faster first frame | Tiny |
| P1 | Task 7: CJK Centering | Fixes Korean/CJK rendering | Medium |
| P1 | Task 6: Font Fallback | Fixes missing glyphs | Medium |
| P2 | Task 8: Dual Sampling | Crisp text + smooth emoji | Small |
| P2 | Task 9: Gamma Correction | Fine-tune text weight (optional w/ sRGB FB) | Medium |
| P2 | Task 13: Box Drawing | Pixel-perfect lines | Medium |
| P3 | Task 10: Positioning Tests | Quality assurance | Small |
| P3 | Task 11: DirectWrite | Windows quality | Large |
| P3 | Task 12: FreeType LCD | Linux quality | Large |
| P3 | Task 14: Variable Fonts | Font flexibility | Small |

## Key Research Sources

- **Ghostty**: 32-byte CellInstance, `coord::pixel` sampling, `bgra8unorm_srgb` framebuffer, P3 color space, baseline-relative signed offsets
- **Kitty**: `height - baseline` text position (implicit Y-flip), grayscale bitmap context (`kCGImageAlphaNone`), procedural box drawing, HarfBuzz shaping
- **Alacritty**: Shader-based Y-flip (`cellDim.y - glyphOffset.y`), premultiplied ARGB, crossfont library
- **WezTerm**: Formula-based positioning, gamma correction, FreeType on all platforms option, `descender_adjust` for fallback fonts
