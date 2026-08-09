// A tour of newui::Delegate, from a plain multicast callback list up to
// the sender/args pattern used by View, SubView and Frame, and finally
// asynchronous dispatch via postCall + RunLoop.

#include "newui/newui.h"
#include "newui/delegate.h"
#include "newui/rootview.h"
#include "newui/runloop.h"

#include <iostream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------
// Demo 1: a delegate with no owning class at all. Delegate<int> makes int
// the Sender - there's no real "instance" here, but every FunctionPtr
// still receives it by reference (Delegate requires Sender to be a plain,
// non-reference type; the & is added for you).
// ---------------------------------------------------------------------

newui::SyncReturn LogValue(int& value) {
    std::cout << "  LogValue: value = " << value << "\n";
    return newui::SyncReturn::Handled;
}

newui::SyncReturn RejectNegative(int& value) {
    if (value < 0) {
        std::cout << "  RejectNegative: " << value << " is invalid, stopping\n";
        return newui::SyncReturn::Error;
    }
    return newui::SyncReturn::Ignored;
}

void demoBasicMulticast() {
    std::cout << "\n== Demo 1: basic multicast Delegate<int> ==\n";

    newui::Delegate<int> validators;
    validators += &RejectNegative;
    validators += &LogValue;

    std::cout << "syncCall(42):\n";
    int good = 42;
    validators.syncCall(good);

    std::cout << "syncCall(-1):\n";
    int bad = -1;
    validators.syncCall(bad);  // RejectNegative errors, so LogValue never runs

    validators -= &LogValue;
    std::cout << "after removing LogValue, syncCall(5):\n";
    int five = 5;
    validators.syncCall(five);  // no output - the only remaining handler ignores it
}



// ---------------------------------------------------------------------
// Demo 3: Delegate<Sender, Args...> - the sender plus extra call data,
// the same shape as Frame::TitleChangedDelegate (Frame, old title, new
// title). Any type can play Sender, not just newui classes.
// ---------------------------------------------------------------------

struct Button {
    std::string label;
    int clickCount = 0;
    newui::Delegate<Button, int> onClicked;  // Sender = Button, extra arg = click count

    void click() {
        ++clickCount;
        onClicked(*this, clickCount);
    }
};

newui::SyncReturn OnButtonClicked(Button& button, int clickCount) {
    std::cout << "  Button '" << button.label << "' clicked (count = " << clickCount << ")\n";
    return newui::SyncReturn::Handled;
}

void demoSenderWithExtraArgs() {
    std::cout << "\n== Demo 3: Delegate<Sender, Args...> - sender plus extra data ==\n";

    Button okButton;
    okButton.label = "OK";
    okButton.onClicked += &OnButtonClicked;

    okButton.click();
    okButton.click();
}

// ---------------------------------------------------------------------
// Demo 4: syncCallFirst - like syncCall, but stops as soon as a handler
// returns Handled instead of running every handler. Useful for chains
// where handlers compete to claim an event (e.g. shortcut vs. text input).
// ---------------------------------------------------------------------

newui::SyncReturn TryHandleAsShortcut(int& key) {
    if (key == 27) {  // Esc
        std::cout << "  TryHandleAsShortcut: claimed Esc\n";
        return newui::SyncReturn::Handled;
    }
    std::cout << "  TryHandleAsShortcut: ignoring key " << key << "\n";
    return newui::SyncReturn::Ignored;
}

newui::SyncReturn TryHandleAsTextInput(int& key) {
    std::cout << "  TryHandleAsTextInput: consumed key " << key << " as text\n";
    return newui::SyncReturn::Handled;
}

void demoSyncCallFirst() {
    std::cout << "\n== Demo 4: syncCallFirst stops at the first Handled result ==\n";

    newui::Delegate<int> keyHandlers;
    keyHandlers += &TryHandleAsShortcut;
    keyHandlers += &TryHandleAsTextInput;

    std::cout << "dispatching key 27 (Esc):\n";
    int escKey = 27;
    keyHandlers.syncCallFirst(escKey);  // shortcut handler claims it; text input never runs

    std::cout << "dispatching key 'a':\n";
    int aKey = 'a';
    keyHandlers.syncCallFirst(aKey);  // shortcut ignores it, falls through to text input
}

// ---------------------------------------------------------------------
// Demo 5: postCall defers the whole handler batch onto a RunLoop instead
// of running it synchronously. The returned AsyncReturn can be waited on
// once the caller needs the result.
// ---------------------------------------------------------------------

newui::SyncReturn OnBackgroundWorkDone(int& result) {
    std::cout << "  OnBackgroundWorkDone: result = " << result
              << " (loop thread " << std::this_thread::get_id() << ")\n";
    return newui::SyncReturn::Handled;
}

void demoPostCall() {
    std::cout << "\n== Demo 5: postCall runs the delegate asynchronously on a RunLoop ==\n";
    std::cout << "  main thread: " << std::this_thread::get_id() << "\n";

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    newui::Delegate<int> onWorkDone;
    onWorkDone += &OnBackgroundWorkDone;

    int result = 100;
    newui::AsyncReturn asyncReturn = onWorkDone.postCall(runLoop, &result);
    asyncReturn.wait();  // block until the loop thread has run the handlers

    runLoop.quit();
    loopThread.join();
}

// ---------------------------------------------------------------------
// Demo 6: += also accepts an anonymous function. Delegate stores plain
// function pointers (FunctionPtr), so the lambda must capture nothing -
// only a capture-less lambda converts implicitly to a function pointer.
// ---------------------------------------------------------------------

void demoAnonymousFunction() {
    std::cout << "\n== Demo 6: += with an anonymous function ==\n";

    newui::Delegate<int> onTick;
    // Trailing return type matters here: without it, a lambda that returns
    // SyncReturn::Handled deduces SyncReturn::ReturnCode (the enum), not
    // SyncReturn itself, which won't match FunctionPtr.
    onTick += [](int& tick) -> newui::SyncReturn {
        std::cout << "  anonymous handler: tick = " << tick << "\n";
        return newui::SyncReturn::Handled;
    };

    int tick = 3;
    onTick.syncCall(tick);
}

// ---------------------------------------------------------------------
// Demo 7: routing a delegate to a member function on a class instance.
// Delegate only stores plain function pointers - it can't hold a bound
// method for an arbitrary object, since that would need a capture. But
// when Sender is the object itself, a static member function can act as
// a trampoline: it arrives with a reference to the instance and simply
// forwards the call into a real member function on it.
// ---------------------------------------------------------------------

class Counter {
public:
    newui::Delegate<Counter, int> onChanged;  // Sender = Counter, arg = new value

    void increment() {
        ++value_;
        onChanged(*this, value_);
    }

    // Matches FunctionPtr's signature, so it can be added directly with
    // +=. It has no state of its own - it just forwards to the instance
    // it's given.
    static newui::SyncReturn HandleChanged(Counter& counter, int newValue) {
        counter.logChange(newValue);
        return newui::SyncReturn::Handled;
    }

private:
    // The actual member function being "added" to the delegate, indirectly,
    // via HandleChanged above.
    void logChange(int newValue) {
        std::cout << "  Counter::logChange: value is now " << newValue << "\n";
    }

    int value_ = 0;
};

void demoMemberFunction() {
    std::cout << "\n== Demo 7: routing a delegate to a member function ==\n";

    Counter counter;
    counter.onChanged += &Counter::HandleChanged;

    counter.increment();
    counter.increment();
}

int main() {
    std::cout << "newui " << newui::version() << " - delegate examples\n";

    demoBasicMulticast();
    demoSenderWithExtraArgs();
    demoSyncCallFirst();
    demoPostCall();
    demoAnonymousFunction();
    demoMemberFunction();

    return 0;
}
