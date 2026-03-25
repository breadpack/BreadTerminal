#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

#include <nlohmann/json.hpp>
#include <string>

using namespace termcore;

class Osc7770Test : public ::testing::Test {
protected:
    Screen screen{24, 80};
    VtParser parser{screen};
    nlohmann::json last_event;
    bool callback_fired = false;

    void SetUp() override {
        screen.setOscHookCallback([this](const std::string& json_str) {
            last_event = nlohmann::json::parse(json_str, nullptr, false);
            callback_fired = true;
        });
    }

    void feed(const std::string& data) {
        parser.feed(data.c_str(), data.size());
    }
};

TEST_F(Osc7770Test, StateChangeEvent) {
    feed("\033]7770;{\"event\":\"StateChange\",\"state\":\"thinking\"}\033\\");
    ASSERT_TRUE(callback_fired);
    EXPECT_EQ(last_event["event"], "StateChange");
    EXPECT_EQ(last_event["state"], "thinking");
}

TEST_F(Osc7770Test, NotificationEvent) {
    feed("\033]7770;{\"event\":\"Notification\",\"title\":\"Done\",\"body\":\"OK\"}\033\\");
    ASSERT_TRUE(callback_fired);
    EXPECT_EQ(last_event["event"], "Notification");
    EXPECT_EQ(last_event["title"], "Done");
}

TEST_F(Osc7770Test, MalformedJsonIgnored) {
    feed("\033]7770;not json at all\033\\");
    EXPECT_FALSE(callback_fired);
}

TEST_F(Osc7770Test, EmptyPayloadIgnored) {
    feed("\033]7770;\033\\");
    EXPECT_FALSE(callback_fired);
}

TEST_F(Osc7770Test, BellTerminator) {
    feed("\033]7770;{\"event\":\"StateChange\",\"state\":\"idle\"}\007");
    ASSERT_TRUE(callback_fired);
    EXPECT_EQ(last_event["state"], "idle");
}
