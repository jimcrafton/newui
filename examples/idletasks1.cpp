// A tour of newui::RunLoop's idle task support (RunLoop::postIdle()): a
// task that runs whenever the loop has no pending Windows messages, and
// keeps being called - once per idle pass - until it reports it's done by
// returning true. Each demo spins up its own RunLoop on a background
// thread (the same pattern as delegates1.cpp's postCall demo), since
// run()/post()/postIdle() all need the loop already pumping messages.

#include "newui/newui.h"
#include "newui/runloop.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------
// Demo 1: a one-shot idle task - returns true immediately, so it runs
// exactly once, the next time the loop finds itself with no pending
// Windows messages, then is removed.
// ---------------------------------------------------------------------

void demoOneShotIdleTask() {
    std::cout << "\n== Demo 1: one-shot idle task ==\n";

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool ran = false;

    runLoop.postIdle([&]() {
        std::cout << "  idle task ran (loop thread " << std::this_thread::get_id() << ")\n";
        {
            std::lock_guard<std::mutex> lock(doneMutex);
            ran = true;
        }
        doneCv.notify_all();
        return true;  // done after this one call
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return ran; });
    }

    runLoop.quit();
    loopThread.join();
}

// ---------------------------------------------------------------------
// Demo 2: an idle task that needs several idle passes to finish - it
// returns false ("not done yet, call me again next time you're idle")
// until its work is complete, then true. Useful for spreading a chunk of
// background work (loading assets, a slow computation) across many idle
// turns instead of blocking the message loop with it all at once.
// ---------------------------------------------------------------------

void demoMultiStepIdleTask() {
    std::cout << "\n== Demo 2: idle task spread across multiple idle passes ==\n";

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    int progress = 0;

    runLoop.postIdle([&]() {
        std::lock_guard<std::mutex> lock(doneMutex);
        progress += 20;
        std::cout << "  loading... " << progress << "%\n";
        bool done = progress >= 100;
        if (done) {
            doneCv.notify_all();
        }
        return done;
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return progress >= 100; });
    }

    runLoop.quit();
    loopThread.join();
}

// ---------------------------------------------------------------------
// Demo 3: two unfinished idle tasks interleave round-robin rather than
// one running to completion before the other gets a turn - each call to
// idleProcessing() runs one task off the front of the queue and, if it
// isn't done yet, requeues it at the back instead of retrying it in
// place.
// ---------------------------------------------------------------------

void demoRoundRobinIdleTasks() {
    std::cout << "\n== Demo 3: multiple idle tasks interleave round-robin ==\n";

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    int tasksRemaining = 2;

    for (char name : {'A', 'B'}) {
        auto callsLeft = std::make_shared<int>(3);
        runLoop.postIdle([&, name, callsLeft]() {
            std::lock_guard<std::mutex> lock(doneMutex);
            --(*callsLeft);
            std::cout << "  task " << name << " ran (" << *callsLeft << " call(s) left)\n";
            bool complete = *callsLeft <= 0;
            if (complete && --tasksRemaining == 0) {
                doneCv.notify_all();
            }
            return complete;
            });
    }

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return tasksRemaining == 0; });
    }

    runLoop.quit();
    loopThread.join();
}

// ---------------------------------------------------------------------
// Demo 4: idle tasks only run when the loop has no pending Windows
// messages, so a "real" posted task (post(), not postIdle()) always cuts
// in ahead of idle work. Here an idle task ticks away in the background;
// posting a normal task partway through interrupts it before its next
// tick, and the idle task resumes afterward.
// ---------------------------------------------------------------------

void demoIdleYieldsToRealMessages() {
    std::cout << "\n== Demo 4: idle work yields to a posted task ==\n";

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    int idleTicks = 0;
    bool idleDone = false;
    bool interruptedIdleWork = false;

    runLoop.postIdle([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));  // simulate a chunk of work
        std::lock_guard<std::mutex> lock(doneMutex);
        ++idleTicks;
        std::cout << "  idle tick " << idleTicks << "\n";
        bool done = idleTicks >= 10;
        if (done) {
            idleDone = true;
            doneCv.notify_all();
        }
        return done;
        });

    // Give the idle task a couple of ticks' head start, then post a
    // normal task - it should be processed before the idle task's next
    // tick, not after all 10 ticks finish.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runLoop.post([&]() {
        std::lock_guard<std::mutex> lock(doneMutex);
        interruptedIdleWork = idleTicks < 10;
        std::cout << "  posted task ran after " << idleTicks << " idle tick(s)\n";
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return idleDone; });
    }

    std::cout << "  posted task cut in ahead of remaining idle work: "
              << std::boolalpha << interruptedIdleWork << "\n";

    runLoop.quit();
    loopThread.join();
}

int main() {
    std::cout << "newui " << newui::version() << " - idle task examples\n";

    demoOneShotIdleTask();
    demoMultiStepIdleTask();
    demoRoundRobinIdleTasks();
    demoIdleYieldsToRealMessages();

    return 0;
}
