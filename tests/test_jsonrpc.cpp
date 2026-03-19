#include <gtest/gtest.h>
#include "termcore/socket/jsonrpc.h"

using namespace termcore::rpc;

TEST(JsonRpcTest, ParseValidRequest) {
    auto req = parseRequest(
        R"({"jsonrpc":"2.0","method":"workspace.create","params":{"name":"test"},"id":1})");
    EXPECT_EQ(req.method, "workspace.create");
    EXPECT_EQ(req.jsonrpc, "2.0");
    ASSERT_TRUE(req.id.has_value());
    EXPECT_EQ(req.id.value(), 1);
    EXPECT_EQ(req.params["name"], "test");
}

TEST(JsonRpcTest, ParseNotification) {
    auto req = parseRequest(
        R"({"jsonrpc":"2.0","method":"notify.send","params":{"title":"hi"}})");
    EXPECT_EQ(req.method, "notify.send");
    EXPECT_FALSE(req.id.has_value());
}

TEST(JsonRpcTest, ParseRequestNoParams) {
    auto req = parseRequest(
        R"({"jsonrpc":"2.0","method":"workspace.list","id":42})");
    EXPECT_EQ(req.method, "workspace.list");
    EXPECT_TRUE(req.params.is_object());
    EXPECT_TRUE(req.params.empty());
    ASSERT_TRUE(req.id.has_value());
    EXPECT_EQ(req.id.value(), 42);
}

TEST(JsonRpcTest, ParseMalformedJson) {
    std::string err;
    auto req = parseRequest("{not valid json", &err);
    EXPECT_EQ(req.method, "");
    EXPECT_FALSE(err.empty());
}

TEST(JsonRpcTest, ParseMissingMethod) {
    std::string err;
    auto req = parseRequest(R"({"jsonrpc":"2.0","id":1})", &err);
    EXPECT_EQ(req.method, "");
    EXPECT_FALSE(err.empty());
}

TEST(JsonRpcTest, ParseNullId) {
    auto req = parseRequest(
        R"({"jsonrpc":"2.0","method":"test","id":null})");
    EXPECT_EQ(req.method, "test");
    EXPECT_FALSE(req.id.has_value());
}

TEST(JsonRpcTest, SerializeSuccessResponse) {
    auto resp = makeResult(1, {{"workspace_id", 5}});
    auto json = serializeResponse(resp);

    auto j = nlohmann::json::parse(json);
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 1);
    EXPECT_EQ(j["result"]["workspace_id"], 5);
    EXPECT_FALSE(j.contains("error"));
}

TEST(JsonRpcTest, SerializeErrorResponse) {
    auto resp = makeError(1, kMethodNotFound, "Method not found: foo");
    auto json = serializeResponse(resp);

    auto j = nlohmann::json::parse(json);
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 1);
    EXPECT_EQ(j["error"]["code"], kMethodNotFound);
    EXPECT_EQ(j["error"]["message"], "Method not found: foo");
    EXPECT_FALSE(j.contains("result"));
}

TEST(JsonRpcTest, SerializeNullIdResponse) {
    auto resp = makeResult(std::nullopt, {{"ok", true}});
    auto json = serializeResponse(resp);

    auto j = nlohmann::json::parse(json);
    EXPECT_TRUE(j["id"].is_null());
}

TEST(JsonRpcTest, Roundtrip) {
    // Create a request, serialize/parse a response
    std::string req_str = R"({"jsonrpc":"2.0","method":"tab.create","params":{"workspace_id":1},"id":7})";
    auto req = parseRequest(req_str);
    EXPECT_EQ(req.method, "tab.create");
    ASSERT_TRUE(req.id.has_value());
    EXPECT_EQ(req.id.value(), 7);

    auto resp = makeResult(req.id, {{"tab_id", 42}});
    auto resp_str = serializeResponse(resp);

    auto j = nlohmann::json::parse(resp_str);
    EXPECT_EQ(j["id"], 7);
    EXPECT_EQ(j["result"]["tab_id"], 42);
}
