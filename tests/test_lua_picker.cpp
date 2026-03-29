// Tests for LuaPickerModule (terminal.picker list/input/confirm API).

#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaPicker, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"
#include "lua_bindings/lua_picker_module.h"

using namespace termcore;

class LuaPickerTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        pickerMod_ = std::make_shared<LuaPickerModule>();
        engine_->registerModule(pickerMod_);
        engine_->initializeModules();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
    std::shared_ptr<LuaPickerModule> pickerMod_;
};

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerShowListItems) {
    auto r = engine_->loadString(R"(
        terminal.picker.show({
            title = "Files",
            items = {"alpha.lua", "beta.lua", "gamma.lua"},
            on_select = function(idx, item) end,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    auto* p = pickerMod_->activePicker();
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->open);
    EXPECT_EQ(p->type, "list");
    EXPECT_EQ(p->title, "Files");
    ASSERT_EQ(p->items.size(), 3u);
    EXPECT_EQ(p->items[0], "alpha.lua");
    EXPECT_EQ(p->items[1], "beta.lua");
    EXPECT_EQ(p->items[2], "gamma.lua");
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerSelectCallsCallback) {
    auto r = engine_->loadString(R"(
        _G.selected_index = -1
        _G.selected_item = ""
        terminal.picker.show({
            title = "Pick",
            items = {"aaa", "bbb", "ccc"},
            on_select = function(idx, item)
                _G.selected_index = idx
                _G.selected_item = item
            end,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Select item at index 1 (0-based in C++, becomes 2 in Lua)
    pickerMod_->selectItem(1);

    r = engine_->loadString(R"(
        assert(_G.selected_index == 2, "expected 2, got " .. _G.selected_index)
        assert(_G.selected_item == "bbb")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Picker should be closed after selection
    EXPECT_EQ(pickerMod_->activePicker(), nullptr);
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerCancelCallsCallback) {
    auto r = engine_->loadString(R"(
        _G.cancelled = false
        terminal.picker.show({
            title = "Pick",
            items = {"x"},
            on_cancel = function() _G.cancelled = true end,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    pickerMod_->cancelPicker();

    r = engine_->loadString("assert(_G.cancelled == true)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(pickerMod_->activePicker(), nullptr);
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerFilterItems) {
    auto r = engine_->loadString(R"(
        terminal.picker.show({
            title = "Search",
            items = {"apple", "banana", "avocado", "blueberry"},
            filter = true,
            fuzzy = false,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Filter with substring "an"
    pickerMod_->updateQuery("an");

    auto* p = pickerMod_->activePicker();
    ASSERT_NE(p, nullptr);
    // "banana" and "avocado" do not match "an" as substring... let's check:
    // "banana" contains "an" -> yes. "avocado" does not. "apple" no. "blueberry" no.
    ASSERT_EQ(p->filtered_items.size(), 1u);
    EXPECT_EQ(p->filtered_items[0], "banana");

    // Filter with "a" should match apple, banana, avocado
    pickerMod_->updateQuery("a");
    p = pickerMod_->activePicker();
    ASSERT_EQ(p->filtered_items.size(), 3u);
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerFuzzyFilter) {
    auto r = engine_->loadString(R"(
        terminal.picker.show({
            title = "Fuzzy",
            items = {"file_one.lua", "file_two.cpp", "test.lua"},
            filter = true,
            fuzzy = true,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // "fl" fuzzy matches "file_one.lua" and "file_two.cpp" (f...l)
    pickerMod_->updateQuery("fl");
    auto* p = pickerMod_->activePicker();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->filtered_items.size(), 2u);
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerInputConfirm) {
    auto r = engine_->loadString(R"(
        _G.confirmed_text = ""
        terminal.picker.input({
            title = "Rename",
            prompt = "New name: ",
            default = "old_name",
            on_confirm = function(text)
                _G.confirmed_text = text
            end,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    auto* p = pickerMod_->activePicker();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "input");
    EXPECT_EQ(p->prompt, "New name: ");
    EXPECT_EQ(p->input_text, "old_name");

    pickerMod_->confirmInput("new_name");

    r = engine_->loadString(R"(assert(_G.confirmed_text == "new_name"))");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(pickerMod_->activePicker(), nullptr);
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerConfirmYesNo) {
    auto r = engine_->loadString(R"(
        _G.answer = ""
        terminal.picker.confirm({
            title = "Delete?",
            message = "Cannot undo.",
            on_yes = function() _G.answer = "yes" end,
            on_no = function() _G.answer = "no" end,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    auto* p = pickerMod_->activePicker();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "confirm");
    EXPECT_EQ(p->message, "Cannot undo.");

    pickerMod_->confirmYes();

    r = engine_->loadString(R"(assert(_G.answer == "yes"))");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(pickerMod_->activePicker(), nullptr);
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, PickerConfirmNo) {
    auto r = engine_->loadString(R"(
        _G.answer = ""
        terminal.picker.confirm({
            title = "Delete?",
            message = "Cannot undo.",
            on_yes = function() _G.answer = "yes" end,
            on_no = function() _G.answer = "no" end,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    pickerMod_->confirmNo();

    r = engine_->loadString(R"(assert(_G.answer == "no"))");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
TEST_F(LuaPickerTest, ClearCallbacksClosesPicker) {
    auto r = engine_->loadString(R"(
        _G.cancel_called = false
        terminal.picker.show({
            title = "Test",
            items = {"a"},
            on_cancel = function() _G.cancel_called = true end,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    ASSERT_NE(pickerMod_->activePicker(), nullptr);

    pickerMod_->clearCallbacks();
    EXPECT_EQ(pickerMod_->activePicker(), nullptr);

    r = engine_->loadString("assert(_G.cancel_called == true)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

#endif // TERMCORE_HAS_LUA
