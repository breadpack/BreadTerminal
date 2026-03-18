#include <gtest/gtest.h>
#include "termcore/webview.h"

using namespace termcore;

class WebViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        wv_ = createWebView();
        if (!wv_) {
            GTEST_SKIP() << "WebView not supported on this platform";
        }
    }

    std::unique_ptr<IWebView> wv_;
};

TEST_F(WebViewTest, CreateReturnsNonNull) {
    ASSERT_NE(wv_, nullptr);
}

TEST_F(WebViewTest, TitleInitiallyEmpty) {
    EXPECT_TRUE(wv_->title().empty());
}

TEST_F(WebViewTest, CanGoBackFalseInitially) {
    EXPECT_FALSE(wv_->canGoBack());
}

TEST_F(WebViewTest, CanGoForwardFalseInitially) {
    EXPECT_FALSE(wv_->canGoForward());
}

TEST_F(WebViewTest, GoBackDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(wv_->goBack());
}

TEST_F(WebViewTest, GoForwardDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(wv_->goForward());
}

TEST_F(WebViewTest, ReloadDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(wv_->reload());
}

TEST_F(WebViewTest, StopDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(wv_->stop());
}

TEST_F(WebViewTest, ResizeDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(wv_->resize(800, 600));
}

TEST_F(WebViewTest, NavigateDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(wv_->navigate("https://example.com"));
}

TEST_F(WebViewTest, CurrentUrlInitiallyEmpty) {
    EXPECT_TRUE(wv_->currentUrl().empty());
}

TEST_F(WebViewTest, IsLoadingFalseInitially) {
    EXPECT_FALSE(wv_->isLoading());
}

TEST_F(WebViewTest, SetEventCallbackDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(
        wv_->setEventCallback([](WebViewEvent, const std::string&) {}));
}

TEST_F(WebViewTest, ExecuteJavaScriptDoesNotCrash) {
    // Note: On macOS, JS evaluation requires a run loop.
    // This test verifies no crash; result validation would need async wait.
    EXPECT_NO_FATAL_FAILURE(
        wv_->executeJavaScript("1+1", [](const std::string& result) {
            // In a full integration test with a run loop, expect "2"
        }));
}
