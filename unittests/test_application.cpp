#include "newui/application.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

TEST(Application, InstanceReturnsSameObject) {
    EXPECT_EQ(&newui::Application::instance(), &newui::Application::instance());
}

TEST(Application, InstanceIsSameAcrossThreads) {
    constexpr int kThreadCount = 8;

    std::vector<std::thread> threads;
    std::vector<newui::Application*> results(kThreadCount);

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([i, &results]() {
            results[i] = &newui::Application::instance();
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (int i = 1; i < kThreadCount; ++i) {
        EXPECT_EQ(results[0], results[i]);
    }
}

TEST(Application, SetNameIsVisibleThroughInstance) {
    newui::Application::instance().setName("newui-test");
    EXPECT_EQ(newui::Application::instance().getName(), "newui-test");
}
