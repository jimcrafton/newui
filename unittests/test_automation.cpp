#include "newui/automation.h"
#include "newui/controls.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/segmentedcontrol.h"

#include <gtest/gtest.h>

#include <string>

using namespace newui;

namespace {

// A headless RootView (no HWND) holding one named TextControl, as the other view tests do.
struct AutomationFixture : public ::testing::Test {
    RootView* root = nullptr;
    TextControl* editor = nullptr;

    void SetUp() override {
        root = new RootView(nullptr, Rect(0, 0, 400, 300), "autoRoot");
        editor = new TextControl();
        editor->setName("autoEditor");
        editor->setBounds(Rect(10, 20, 200, 100));
        editor->setText(L"hello \"world\"");
        root->addChild(editor);
        Automation::expose(*root);
    }

    void TearDown() override {
        root->destroy();
    }
};

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}

TEST_F(AutomationFixture, HelloReportsTheProtocolAndEchoesTheId) {
    std::string answer = Automation::handle("{\"id\":7,\"cmd\":\"hello\"}");
    EXPECT_TRUE(contains(answer, "\"id\":7"));
    EXPECT_TRUE(contains(answer, "\"ok\":true"));
    EXPECT_TRUE(contains(answer, "\"protocol\":1"));
}

TEST_F(AutomationFixture, FindViewReturnsTheViewsBoundsInScreenSpace) {
    std::string answer = Automation::handle("{\"cmd\":\"findView\",\"name\":\"autoEditor\"}");
    EXPECT_TRUE(contains(answer, "\"ok\":true")) << answer;
    EXPECT_TRUE(contains(answer, "\"w\":200.00")) << answer;
    EXPECT_TRUE(contains(answer, "\"h\":100.00")) << answer;
}

TEST_F(AutomationFixture, FindViewOfAMissingNameFailsWithAMessage) {
    std::string answer = Automation::handle("{\"cmd\":\"findView\",\"name\":\"nope\"}");
    EXPECT_TRUE(contains(answer, "\"ok\":false")) << answer;
    EXPECT_TRUE(contains(answer, "no view named 'nope'")) << answer;
}

TEST_F(AutomationFixture, GetTextReturnsTheEditorsTextEscaped) {
    std::string answer = Automation::handle("{\"cmd\":\"getText\",\"name\":\"autoEditor\"}");
    EXPECT_TRUE(contains(answer, "\"text\":\"hello \\\"world\\\"\"")) << answer;
}

TEST_F(AutomationFixture, AMalformedRequestAndAnUnknownCommandAreRefused) {
    EXPECT_TRUE(contains(Automation::handle("not json"), "\"ok\":false"));
    EXPECT_TRUE(contains(Automation::handle("{\"cmd\":\"dance\"}"), "unknown command 'dance'"));
}

TEST_F(AutomationFixture, FocusedNamesTheFocusedView) {
    root->setFocusedSubView(editor);
    std::string answer = Automation::handle("{\"cmd\":\"focused\"}");
    EXPECT_TRUE(contains(answer, "\"name\":\"autoEditor\"")) << answer;
    EXPECT_TRUE(contains(answer, "\"type\":\"TextControl\"")) << answer;
}

TEST(AutomationServer, AnswersRequestsOverTheNamedPipe) {
    RunLoop::RunLoopThread loopThread = RunLoop::runThreaded();
    loopThread.loop->waitForStart();
    ASSERT_TRUE(Automation::start("newui_automation_test", *loopThread.loop));

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 50 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
        pipe = ::CreateFileW(L"\\\\.\\pipe\\newui_automation_test", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            ::Sleep(20);
        }
    }
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);

    auto ask = [pipe](const std::string& request) {
        std::string line = request + "\n";
        DWORD done = 0;
        ::WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &done, nullptr);
        std::string answer;
        char c;
        while (::ReadFile(pipe, &c, 1, &done, nullptr) && done == 1 && c != '\n') {
            answer += c;
        }
        return answer;
    };

    EXPECT_TRUE(contains(ask("{\"id\":1,\"cmd\":\"hello\"}"), "\"protocol\":1"));
    EXPECT_TRUE(contains(ask("{\"id\":2,\"cmd\":\"idle\"}"), "\"id\":2"));

    ::CloseHandle(pipe);
    Automation::stop();
    loopThread.loop->quit();
    loopThread.thread.join();
}

TEST_F(AutomationFixture, ScrollInfoReportsABarsStateAndThumb) {
    auto* scroll = new ScrollView();
    scroll->setName("autoScroll");
    scroll->setBounds(Rect(0, 120, 200, 100));
    root->addChild(scroll);

    std::string answer = Automation::handle("{\"cmd\":\"scrollInfo\",\"name\":\"autoScroll\"}");
    EXPECT_TRUE(contains(answer, "\"ok\":true")) << answer;
    EXPECT_TRUE(contains(answer, "\"vValue\":0.00")) << answer;
    EXPECT_TRUE(contains(answer, "\"vh\":")) << answer;

    EXPECT_TRUE(contains(Automation::handle("{\"cmd\":\"scrollInfo\",\"name\":\"autoEditor\"}"), "is not a ScrollView"));
}

TEST_F(AutomationFixture, SegmentReportsALabelAndTheSelectionAndScreenRect) {
    auto* modes = new SegmentedControl();
    modes->setName("autoModes");
    modes->setSegments({ "One", "Two", "Three" });
    modes->setBounds(Rect(0, 230, 300, 24));
    root->addChild(modes);
    modes->setSelectedIndex(2);

    std::string answer = Automation::handle("{\"cmd\":\"segment\",\"name\":\"autoModes\",\"index\":1}");
    EXPECT_TRUE(contains(answer, "\"count\":3")) << answer;
    EXPECT_TRUE(contains(answer, "\"selected\":2")) << answer;
    EXPECT_TRUE(contains(answer, "\"label\":\"Two\"")) << answer;

    EXPECT_TRUE(contains(Automation::handle("{\"cmd\":\"segment\",\"name\":\"autoModes\",\"index\":9}"), "out of range"));
    EXPECT_TRUE(contains(Automation::handle("{\"cmd\":\"segment\",\"name\":\"autoEditor\",\"index\":0}"), "is not a SegmentedControl"));
}
