#include <gtest/gtest.h>
#include "termcore/kitty_graphics.h"
#include <string>

using namespace termcore;

// Helper: create a simple base64-encoded payload
// "AAAA" in base64 decodes to 3 zero bytes
static const std::string kTinyPayload = "AAAA";

// 1. parseControl extracts key-value pairs
TEST(KittyGraphicsTest, ParseControlExtractsKeyValuePairs) {
    KittyGraphicsManager mgr;
    // We test parseControl indirectly via processCommand.
    // a=q triggers query which proves parsing works.
    auto resp = mgr.processCommand("a=q,i=42", "");
    EXPECT_EQ(resp, "\033_Gok\033\\");
}

// 2. base64Decode works correctly
TEST(KittyGraphicsTest, Base64DecodeWorks) {
    KittyGraphicsManager mgr;
    // "SGVsbG8=" is base64 for "Hello"
    // We verify by transmitting with known base64 and checking decoded data.
    mgr.processCommand("a=t,s=5,v=1,f=24,i=1", "SGVsbG8=");
    auto* img = mgr.getImage(1);
    ASSERT_NE(img, nullptr);
    ASSERT_EQ(img->data.size(), 5u);
    EXPECT_EQ(img->data[0], 'H');
    EXPECT_EQ(img->data[1], 'e');
    EXPECT_EQ(img->data[2], 'l');
    EXPECT_EQ(img->data[3], 'l');
    EXPECT_EQ(img->data[4], 'o');
}

// 3. Transmit RGBA image -> stored with correct dimensions
TEST(KittyGraphicsTest, TransmitRGBAImage) {
    KittyGraphicsManager mgr;
    // 2x2 RGBA = 16 bytes. Base64 of 16 zero bytes = "AAAAAAAAAAAAAAAAAAAAAA=="
    mgr.processCommand("a=t,s=2,v=2,f=32,i=10", "AAAAAAAAAAAAAAAAAAAAAA==");
    auto* img = mgr.getImage(10);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->width, 2);
    EXPECT_EQ(img->height, 2);
    EXPECT_EQ(img->format, 32);
    EXPECT_EQ(img->data.size(), 16u);
    EXPECT_TRUE(img->complete);
}

// 4. Transmit+display -> image stored + placement created
TEST(KittyGraphicsTest, TransmitAndDisplay) {
    KittyGraphicsManager mgr;
    mgr.processCommand("a=T,s=1,v=1,f=32,i=5,x=3,y=7,z=2", "AAAAAA==");
    auto* img = mgr.getImage(5);
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(img->complete);

    ASSERT_EQ(mgr.placements().size(), 1u);
    const auto& pl = mgr.placements()[0];
    EXPECT_EQ(pl.image_id, 5u);
    EXPECT_EQ(pl.src_x, 3);
    EXPECT_EQ(pl.src_y, 7);
    EXPECT_EQ(pl.z_index, 2);
}

// 5. Query -> returns response
TEST(KittyGraphicsTest, QueryReturnsResponse) {
    KittyGraphicsManager mgr;
    auto resp = mgr.processCommand("a=q", "");
    EXPECT_EQ(resp, "\033_Gok\033\\");
}

// 6. Delete by image ID
TEST(KittyGraphicsTest, DeleteByImageId) {
    KittyGraphicsManager mgr;
    mgr.processCommand("a=T,s=1,v=1,f=32,i=20", "AAAAAA==");
    mgr.processCommand("a=T,s=1,v=1,f=32,i=21", "AAAAAA==");
    ASSERT_EQ(mgr.imageCount(), 2u);
    ASSERT_EQ(mgr.placements().size(), 2u);

    mgr.deleteByImageId(20);
    EXPECT_EQ(mgr.imageCount(), 1u);
    EXPECT_EQ(mgr.getImage(20), nullptr);
    EXPECT_NE(mgr.getImage(21), nullptr);
    // Placement for image 20 should also be removed
    EXPECT_EQ(mgr.placements().size(), 1u);
    EXPECT_EQ(mgr.placements()[0].image_id, 21u);
}

// 7. Clear removes all
TEST(KittyGraphicsTest, ClearRemovesAll) {
    KittyGraphicsManager mgr;
    mgr.processCommand("a=T,s=1,v=1,f=32,i=1", "AAAAAA==");
    mgr.processCommand("a=T,s=1,v=1,f=32,i=2", "AAAAAA==");
    ASSERT_EQ(mgr.imageCount(), 2u);

    mgr.clear();
    EXPECT_EQ(mgr.imageCount(), 0u);
    EXPECT_TRUE(mgr.placements().empty());
}

// 8. imageCount tracks correctly
TEST(KittyGraphicsTest, ImageCountTracksCorrectly) {
    KittyGraphicsManager mgr;
    EXPECT_EQ(mgr.imageCount(), 0u);

    mgr.processCommand("a=t,s=1,v=1,f=32,i=1", "AAAAAA==");
    EXPECT_EQ(mgr.imageCount(), 1u);

    mgr.processCommand("a=t,s=1,v=1,f=32,i=2", "AAAAAA==");
    EXPECT_EQ(mgr.imageCount(), 2u);

    mgr.processCommand("a=t,s=1,v=1,f=32,i=3", "AAAAAA==");
    EXPECT_EQ(mgr.imageCount(), 3u);
}

// 9. Chunked transmit (m=1 then m=0) accumulates data
TEST(KittyGraphicsTest, ChunkedTransmitAccumulatesData) {
    KittyGraphicsManager mgr;
    // "SGVs" decodes to "Hel", "bG8=" decodes to "lo"
    // But since we accumulate base64 before decoding:
    // "SGVs" + "bG8=" = "SGVsbG8=" which decodes to "Hello"
    mgr.processCommand("a=t,s=5,v=1,f=24,i=50,m=1", "SGVs");
    // Image should not be stored yet
    EXPECT_EQ(mgr.getImage(50), nullptr);

    mgr.processCommand("a=t,s=5,v=1,f=24,i=50,m=0", "bG8=");
    auto* img = mgr.getImage(50);
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(img->complete);
    ASSERT_EQ(img->data.size(), 5u);
    EXPECT_EQ(img->data[0], 'H');
    EXPECT_EQ(img->data[4], 'o');
}

// 10. getImage returns nullptr for unknown ID
TEST(KittyGraphicsTest, GetImageReturnsNullForUnknown) {
    KittyGraphicsManager mgr;
    EXPECT_EQ(mgr.getImage(999), nullptr);
}

// 11. Multiple images stored independently
TEST(KittyGraphicsTest, MultipleImagesStoredIndependently) {
    KittyGraphicsManager mgr;
    // "QQ==" decodes to "A" (1 byte), "Qg==" decodes to "B" (1 byte)
    mgr.processCommand("a=t,s=1,v=1,f=24,i=100", "QQ==");
    mgr.processCommand("a=t,s=1,v=1,f=24,i=101", "Qg==");

    auto* img1 = mgr.getImage(100);
    auto* img2 = mgr.getImage(101);
    ASSERT_NE(img1, nullptr);
    ASSERT_NE(img2, nullptr);
    EXPECT_EQ(img1->id, 100u);
    EXPECT_EQ(img2->id, 101u);
    ASSERT_EQ(img1->data.size(), 1u);
    ASSERT_EQ(img2->data.size(), 1u);
    EXPECT_EQ(img1->data[0], 'A');
    EXPECT_EQ(img2->data[0], 'B');
}

// 12. Placement has correct position
TEST(KittyGraphicsTest, PlacementHasCorrectPosition) {
    KittyGraphicsManager mgr;
    mgr.processCommand("a=t,s=1,v=1,f=32,i=60", "AAAAAA==");
    mgr.processCommand("a=p,i=60,p=7,x=10,y=20,c=5,r=3,z=-1", "");

    ASSERT_EQ(mgr.placements().size(), 1u);
    const auto& pl = mgr.placements()[0];
    EXPECT_EQ(pl.image_id, 60u);
    EXPECT_EQ(pl.placement_id, 7u);
    EXPECT_EQ(pl.src_x, 10);
    EXPECT_EQ(pl.src_y, 20);
    EXPECT_EQ(pl.cols, 5);
    EXPECT_EQ(pl.rows, 3);
    EXPECT_EQ(pl.z_index, -1);
}
