#include <gtest/gtest.h>

// Since osc_emitter is in tools/bread, we test the sequence builder directly
// by including the source. In production, this would be a separate test target.
#include "../../tools/bread/osc_emitter.h"
#include "../../tools/bread/osc_emitter.cpp"

TEST(OscEmitterTest, BuildOscSequence) {
    auto seq = bread::buildOscSequence("{\"event\":\"test\"}");
    EXPECT_EQ(seq, "\033]7770;{\"event\":\"test\"}\033\\");
}

TEST(OscEmitterTest, BuildOscSequenceEmpty) {
    auto seq = bread::buildOscSequence("{}");
    EXPECT_EQ(seq, "\033]7770;{}\033\\");
}
