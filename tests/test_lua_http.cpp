#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaHttp, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "lua_bindings/lua_http_module.h"

using namespace termcore;

// Mock HTTP provider that immediately invokes callback with a preset response.
class MockHttpProvider : public IHttpProvider {
public:
    HttpResponse preset_response;
    HttpRequest last_request;
    int call_count = 0;

    void execute(const HttpRequest& request, HttpCallback callback) override {
        last_request = request;
        ++call_count;
        callback(preset_response);
    }
};

class LuaHttpTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        http_ = std::make_shared<LuaHttpModule>();
        mock_ = std::make_unique<MockHttpProvider>();
        http_->setProvider(mock_.get());
        engine_->registerModule(http_);
        engine_->initializeModules();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
        http_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
    std::shared_ptr<LuaHttpModule> http_;
    std::unique_ptr<MockHttpProvider> mock_;
};

TEST_F(LuaHttpTest, HttpGetRegistersCallback) {
    mock_->preset_response.ok = true;
    mock_->preset_response.status = 200;
    mock_->preset_response.body = R"({"result":"ok"})";

    auto result = engine_->loadString(R"(
        __http_test_called = false
        terminal.http.get("https://example.com/api", function(response)
            __http_test_called = true
        end)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
    EXPECT_EQ(mock_->last_request.method, "GET");
    EXPECT_EQ(mock_->last_request.url, "https://example.com/api");
    EXPECT_EQ(mock_->call_count, 1);

    // Process pending responses to invoke Lua callback
    http_->processPendingResponses();

    auto check = engine_->loadString("assert(__http_test_called == true, 'callback not called')");
    EXPECT_TRUE(check.ok()) << check.errorMessage();
}

TEST_F(LuaHttpTest, HttpPostWithBody) {
    mock_->preset_response.ok = true;
    mock_->preset_response.status = 201;

    auto result = engine_->loadString(R"(
        terminal.http.post("https://example.com/data", {
            headers = {["Content-Type"] = "application/json"},
            body = '{"key":"value"}',
        }, function(response)
            __post_status = response.status
        end)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
    EXPECT_EQ(mock_->last_request.method, "POST");
    EXPECT_EQ(mock_->last_request.url, "https://example.com/data");
    EXPECT_EQ(mock_->last_request.body, "{\"key\":\"value\"}");
    EXPECT_EQ(mock_->last_request.headers["Content-Type"], "application/json");

    http_->processPendingResponses();

    auto check = engine_->loadString("assert(__post_status == 201, 'status mismatch')");
    EXPECT_TRUE(check.ok()) << check.errorMessage();
}

TEST_F(LuaHttpTest, HttpRequestCustomMethod) {
    mock_->preset_response.ok = true;
    mock_->preset_response.status = 200;

    auto result = engine_->loadString(R"(
        terminal.http.request({
            method = "PUT",
            url = "https://example.com/resource/1",
            headers = {["Content-Type"] = "application/json"},
            body = '{"updated":true}',
            timeout = 5000,
        }, function(response)
        end)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
    EXPECT_EQ(mock_->last_request.method, "PUT");
    EXPECT_EQ(mock_->last_request.url, "https://example.com/resource/1");
    EXPECT_EQ(mock_->last_request.body, "{\"updated\":true}");
    EXPECT_EQ(mock_->last_request.timeout_ms, 5000);
}

TEST_F(LuaHttpTest, HttpResponseCallsLuaCallback) {
    mock_->preset_response.ok = true;
    mock_->preset_response.status = 200;
    mock_->preset_response.body = "response body";
    mock_->preset_response.headers["X-Custom"] = "test";

    auto result = engine_->loadString(R"(
        __resp = nil
        terminal.http.get("https://example.com", function(response)
            __resp = response
        end)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    http_->processPendingResponses();

    auto check = engine_->loadString(R"(
        assert(__resp ~= nil, "response not set")
        assert(__resp.ok == true, "expected ok=true")
        assert(__resp.status == 200, "expected status=200, got: " .. tostring(__resp.status))
        assert(__resp.body == "response body", "body mismatch")
        assert(__resp.headers["X-Custom"] == "test", "header mismatch")
    )");
    EXPECT_TRUE(check.ok()) << check.errorMessage();
}

TEST_F(LuaHttpTest, HttpErrorResponse) {
    mock_->preset_response.ok = false;
    mock_->preset_response.status = 0;
    mock_->preset_response.error = "Connection refused";

    auto result = engine_->loadString(R"(
        __err_resp = nil
        terminal.http.get("https://unreachable.test", function(response)
            __err_resp = response
        end)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    http_->processPendingResponses();

    auto check = engine_->loadString(R"(
        assert(__err_resp ~= nil, "error response not set")
        assert(__err_resp.ok == false, "expected ok=false")
        assert(__err_resp.error == "Connection refused", "error mismatch")
    )");
    EXPECT_TRUE(check.ok()) << check.errorMessage();
}

TEST_F(LuaHttpTest, ClearCallbacksCancelsPending) {
    mock_->preset_response.ok = true;
    mock_->preset_response.status = 200;

    auto result = engine_->loadString(R"(
        __clear_test = false
        terminal.http.get("https://example.com", function(response)
            __clear_test = true
        end)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    // Clear before processing — callbacks should be dropped
    http_->clearCallbacks();

    // processPendingResponses should be a no-op (luaPtr_ is null)
    http_->processPendingResponses();

    // Note: we can't safely check __clear_test since luaPtr_ was cleared.
    // The main assertion is that no crash occurs.
}

#endif // TERMCORE_HAS_LUA
