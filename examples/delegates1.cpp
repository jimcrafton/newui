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
// the Sender - there's no real "instance" here, but every listener still
// receives it by reference (Delegate requires Sender to be a plain,
// non-reference type; the & is added for you).
//
// add() returns a Connection token; remove() takes that token back. Since
// listeners are stored type-erased (so lambdas, member functions and free
// functions can all live in the same list), there's no way to remove a
// listener "by value" the way a raw function pointer could be compared -
// the Connection is what makes a specific add() undoable.
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
    newui::Connection logConnection = validators.add(&LogValue);

    std::cout << "syncCall(42):\n";
    int good = 42;
    validators.syncCall(good);

    std::cout << "syncCall(-1):\n";
    int bad = -1;
    validators.syncCall(bad);  // RejectNegative errors, so LogValue never runs

    validators.remove(logConnection);
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
// Demo 6: += / add() also accept a lambda, and - unlike a raw function
// pointer - it's free to capture. Listeners are stored as
// std::function<SyncReturn(SenderRefT, Args...)>, so any callable whose
// result converts to SyncReturn works; a lambda returning the
// SyncReturn::ReturnCode enum (e.g. via `return SyncReturn::Handled;`
// with no trailing return type) converts implicitly, same as it would
// assigning into a SyncReturn variable.
// ---------------------------------------------------------------------

void demoLambdaListener() {
    std::cout << "\n== Demo 6: add()/+= with lambdas, capturing and not ==\n";

    newui::Delegate<int> onTick;
    onTick += [](int& tick) {
        std::cout << "  anonymous handler: tick = " << tick << "\n";
        return newui::SyncReturn::Handled;
    };

    int totalSeen = 0;
    onTick += [&totalSeen](int& tick) {
        totalSeen += tick;
        return newui::SyncReturn::Handled;
    };

    int tick = 3;
    onTick.syncCall(tick);
    std::cout << "  totalSeen (captured by reference) = " << totalSeen << "\n";
}

// ---------------------------------------------------------------------
// Demo 7: binding a real member function directly - no static trampoline
// or capturing lambda needed for the common case of "call this method on
// this instance". T is deduced from the arguments, so there's no
// <Class, &Class::Method> template noise at the call site:
//
//   delegate.add(instance, &T::Method);
//
// Method still has the same (SenderRefT, Args...) shape any listener
// does; instance is just who it's called on, and here that happens to be
// the same Counter that owns the delegate. remove() just needs the
// Connection add() returned.
// ---------------------------------------------------------------------

class Counter {
public:
    newui::Delegate<Counter, int> onChanged;  // Sender = Counter, arg = new value

    void increment() {
        ++value_;
        onChanged(*this, value_);
    }

    newui::SyncReturn logChange(Counter& /*counter*/, int newValue) {
        std::cout << "  Counter::logChange: value is now " << newValue << "\n";
        return newui::SyncReturn::Handled;
    }

private:
    int value_ = 0;
};

void demoBoundMemberFunction() {
    std::cout << "\n== Demo 7: binding a member function directly with add(instance, &T::Method) ==\n";

    Counter counter;
    counter.onChanged.add(&counter, &Counter::logChange);

    counter.increment();
    counter.increment();
}

// ---------------------------------------------------------------------
// Demo 8: const member functions bind the same way, bound to a const
// instance. Also shows remove(Connection) taking out one listener while
// leaving the other running.
// ---------------------------------------------------------------------

class Logger {
public:
    explicit Logger(std::string name) : name_(std::move(name)) {}

    newui::SyncReturn onButtonClicked(Button& button, int clickCount) {
        std::cout << "  [" << name_ << "] '" << button.label
                  << "' clicked (count = " << clickCount << ")\n";
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn onButtonClickedConst(Button& button, int clickCount) const {
        std::cout << "  [" << name_ << ", const] '" << button.label
                   << "' clicked (count = " << clickCount << ")\n";
        return newui::SyncReturn::Handled;
    }

private:
    std::string name_;
};

void demoConstMemberAndRemoval() {
    std::cout << "\n== Demo 8: const member functions, and remove(Connection) ==\n";

    Button saveButton;
    saveButton.label = "Save";

    Logger logger("audit-log");
    const Logger constLogger("const-audit-log");

    newui::Connection loggerConnection = saveButton.onClicked.add(&logger, &Logger::onButtonClicked);
    saveButton.onClicked.add(&constLogger, &Logger::onButtonClickedConst);

    saveButton.click();

    saveButton.onClicked.remove(loggerConnection);
    std::cout << "after removing the non-const listener:\n";
    saveButton.click();
}

int main() {
    std::cout << "newui " << newui::version() << " - delegate examples\n";

    demoBasicMulticast();
    demoSenderWithExtraArgs();
    demoSyncCallFirst();
    demoPostCall();
    demoLambdaListener();
    demoBoundMemberFunction();
    demoConstMemberAndRemoval();

    return 0;
}
