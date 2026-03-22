#include "bench_font.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/font_metrics.h"

namespace bench {

void runFontBenchmarks(BenchmarkRunner& runner) {
    // --- Glyph cache lookup performance (hits) ---
    {
        runner.run("glyph_cache_hit_rate", "Mops/s", [&]() -> double {
            termcore::GlyphCache cache(8192);

            // Pre-populate cache with ASCII range
            for (uint32_t i = 32; i < 127; ++i) {
                termcore::GlyphKey key{1, i, {0, 0}};
                termcore::GlyphInfo info{};
                info.region = {0, static_cast<int>(i * 10), 0, 8, 16, 0, 14};
                info.advance_x = 8.0f;
                info.is_color = false;
                cache.put(key, info);
            }
            cache.resetStats();

            constexpr int ops = 500000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::GlyphKey key{1, static_cast<uint32_t>(32 + (i % 95)), {0, 0}};
                volatile auto result = cache.get(key);
                (void)result;
            }
            double sec = t.elapsedSec();

            // Verify hit rate
            double hit_rate = static_cast<double>(cache.hits()) /
                              static_cast<double>(cache.hits() + cache.misses()) * 100.0;
            (void)hit_rate; // Should be ~100%

            return (static_cast<double>(ops) / 1e6) / sec;
        });
    }

    // --- Glyph cache miss + insert performance ---
    {
        runner.run("glyph_cache_miss_insert", "Kops/s", [&]() -> double {
            termcore::GlyphCache cache(4096);

            constexpr int ops = 50000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::GlyphKey key{1, static_cast<uint32_t>(i), {0, 0}};
                auto result = cache.get(key);
                if (!result) {
                    termcore::GlyphInfo info{};
                    info.region = {0, (i % 64) * 10, (i / 64) * 20, 8, 16, 0, 14};
                    info.advance_x = 8.0f;
                    cache.put(key, info);
                }
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(ops) / 1000.0) / sec;
        });
    }

    // --- Glyph cache LRU eviction performance ---
    {
        runner.run("glyph_cache_eviction", "Kops/s", [&]() -> double {
            termcore::GlyphCache cache(1024); // Small cache forces evictions

            constexpr int ops = 100000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::GlyphKey key{1, static_cast<uint32_t>(i), {0, 0}};
                auto result = cache.get(key);
                if (!result) {
                    termcore::GlyphInfo info{};
                    info.region = {0, 0, 0, 8, 16, 0, 14};
                    info.advance_x = 8.0f;
                    cache.put(key, info);
                }
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(ops) / 1000.0) / sec;
        });
    }

    // --- Atlas packing performance ---
    {
        runner.run("atlas_pack_glyphs", "Kops/s", [&]() -> double {
            termcore::GlyphAtlas atlas(512, 4096);

            constexpr int ops = 2000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::RasterizedGlyph glyph;
                glyph.width = 8 + (i % 8);
                glyph.height = 14 + (i % 6);
                glyph.bearing_x = 0;
                glyph.bearing_y = 12;
                glyph.format = termcore::PixelFormat::Grayscale;
                glyph.bitmap.resize(glyph.width * glyph.height, 128);
                atlas.pack(glyph);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(ops) / 1000.0) / sec;
        });
    }

    // --- Atlas page blit performance ---
    {
        runner.run("atlas_page_blit", "Kops/s", [&]() -> double {
            termcore::AtlasPage page(1024, 1024, termcore::AtlasFormat::R8);

            constexpr int ops = 5000;
            std::vector<uint8_t> bitmap(16 * 20, 200);
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                auto region = page.pack(16, 20, 0, 16);
                if (region) {
                    page.blit(*region, bitmap.data(), 16);
                }
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(ops) / 1000.0) / sec;
        });
    }

    // --- Cache hit percentage after warmup ---
    {
        runner.run("cache_hit_percentage_ascii", "%", [&]() -> double {
            termcore::GlyphCache cache(8192);

            // Warmup: insert all ASCII
            for (uint32_t i = 32; i < 127; ++i) {
                termcore::GlyphKey key{1, i, {0, 0}};
                termcore::GlyphInfo info{};
                info.region = {0, static_cast<int>(i * 10), 0, 8, 16, 0, 14};
                info.advance_x = 8.0f;
                cache.put(key, info);
            }
            cache.resetStats();

            // Simulate typical terminal text access pattern
            std::string text = "Hello, World! This is a typical terminal output line with numbers 12345.";
            for (int rep = 0; rep < 1000; ++rep) {
                for (char c : text) {
                    termcore::GlyphKey key{1, static_cast<uint32_t>(c), {0, 0}};
                    cache.get(key);
                }
            }

            double hits = static_cast<double>(cache.hits());
            double total = hits + static_cast<double>(cache.misses());
            return (total > 0) ? (hits / total * 100.0) : 0.0;
        });
    }
}

} // namespace bench
