#include "newui/binding.h"
#include "newui/bundle.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/models.h"
#include "newui/splitter.h"
#include "newui/uiinputmanager.h"
#include "newui/rootview.h"

#include <gtest/gtest.h>

#include <map>

using namespace newui;

namespace {

bool loadText(Frame& frame, const std::string& text) {
    return Bundle::instance().loadFrameFromText(frame, text);
}

// A Model holding one value per string key.
class MapModel : public Model {
public:
    std::any value(const std::any& key = std::any()) override {
        const std::string* k = std::any_cast<std::string>(&key);
        auto it = values.find(k != nullptr ? *k : std::string());
        return it != values.end() ? it->second : std::any();
    }
    void setValue(const std::any& newValue, const std::any& key = std::any()) override {
        const std::string* k = std::any_cast<std::string>(&key);
        values[k != nullptr ? *k : std::string()] = newValue;
        Model::setValue(newValue, key);
    }
    std::map<std::string, std::any> values;
};

}

TEST(ConvertValue, NumbersWidenAndNarrow) {
    std::any out;
    std::string error;
    ASSERT_TRUE(convertValue(3, typeid(double), out, error));
    EXPECT_DOUBLE_EQ(std::any_cast<double>(out), 3.0);
    ASSERT_TRUE(convertValue(2.6, typeid(int), out, error));
    EXPECT_EQ(std::any_cast<int>(out), 3);
    ASSERT_TRUE(convertValue(1.5f, typeid(std::string), out, error));
    EXPECT_EQ(std::any_cast<std::string>(out), "1.5");
}

TEST(ConvertValue, TextIsParsedStrictly) {
    std::any out;
    std::string error;
    ASSERT_TRUE(convertValue(std::string(" 42 "), typeid(int), out, error));
    EXPECT_EQ(std::any_cast<int>(out), 42);
    EXPECT_FALSE(convertValue(std::string("12abc"), typeid(int), out, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(convertValue(std::string(""), typeid(double), out, error));
    ASSERT_TRUE(convertValue(std::string("true"), typeid(bool), out, error));
    EXPECT_TRUE(std::any_cast<bool>(out));
}

TEST(ConvertValue, WideAndNarrowText) {
    std::any out;
    std::string error;
    ASSERT_TRUE(convertValue(std::wstring(L"7"), typeid(int), out, error));
    EXPECT_EQ(std::any_cast<int>(out), 7);
    ASSERT_TRUE(convertValue(7, typeid(std::wstring), out, error));
    EXPECT_EQ(std::any_cast<std::wstring>(out), L"7");
}

TEST(PropertyChanged, SliderToggleAndTextControlNotify) {
    Slider slider;
    Toggle toggle;
    TextControl text;
    std::vector<std::string> seen;
    auto listen = [&](Component& c) {
        c.onPropertyChanged.add([&](Component&, const std::string& name) {
            seen.push_back(name);
            return SyncReturn(SyncReturn::Handled);
        });
    };
    listen(slider);
    listen(toggle);
    listen(text);

    slider.setValue(30.0f);
    slider.setValue(30.0f);  // unchanged: no second notification
    toggle.setChecked(true);
    text.setText(L"hello");

    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], "value");
    EXPECT_EQ(seen[1], "checked");
    EXPECT_EQ(seen[2], "text");
}

TEST(Binding, TwoWayBetweenControlsDeclaredInNewui) {
    Frame frame;
    frame.setName("x");
    ASSERT_TRUE(loadText(frame, R"({
        type: "Frame", name: "x",
        rootView: { type: "RootView", childViews: [
            { type: "Slider", name: "src" },
            { type: "Slider", name: "dst" },
        ] },
        bindings: [
            { type: "Binding", target: "dst", property: "value", source: "src", key: "value", mode: "TwoWay" },
        ],
    })"));
    ASSERT_EQ(frame.bindings().size(), 1u);
    EXPECT_TRUE(frame.bindings()[0]->isResolved()) << frame.bindings()[0]->lastError();

    auto* src = frame.rootView().findView<Slider>("src");
    auto* dst = frame.rootView().findView<Slider>("dst");
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    src->setValue(40.0f);
    EXPECT_FLOAT_EQ(dst->value(), 40.0f);
    dst->setValue(10.0f);
    EXPECT_FLOAT_EQ(src->value(), 10.0f);
}

TEST(Binding, OneWayDoesNotWriteBack) {
    Frame frame;
    frame.setName("x");
    ASSERT_TRUE(loadText(frame, R"({
        type: "Frame", name: "x",
        rootView: { type: "RootView", childViews: [
            { type: "Slider", name: "src" }, { type: "Slider", name: "dst" } ] },
        bindings: [ { type: "Binding", target: "dst", property: "value", source: "src", key: "value", mode: "OneWay" } ],
    })"));
    auto* src = frame.rootView().findView<Slider>("src");
    auto* dst = frame.rootView().findView<Slider>("dst");
    src->setValue(25.0f);
    EXPECT_FLOAT_EQ(dst->value(), 25.0f);
    dst->setValue(90.0f);
    EXPECT_FLOAT_EQ(src->value(), 25.0f);
}

TEST(Binding, OneTimeCopiesOnceOnly) {
    Frame frame;
    Slider* src = new Slider();
    src->setName("src");
    src->setValue(60.0f);
    Slider* dst = new Slider();
    dst->setName("dst");
    frame.rootView().addChild(src);
    frame.rootView().addChild(dst);
    auto* binding = new Binding();
    binding->setTarget("dst");
    binding->setProperty("value");
    binding->setSource("src");
    binding->setKey("value");
    binding->setMode(BindingMode::OneTime);
    frame.addBinding(binding);
    ASSERT_TRUE(frame.resolveBindings());
    EXPECT_FLOAT_EQ(dst->value(), 60.0f);
    src->setValue(5.0f);
    EXPECT_FLOAT_EQ(dst->value(), 60.0f);
}

TEST(Binding, ConvertersShapeTheValue) {
    Frame frame;
    frame.setName("x");
    ASSERT_TRUE(loadText(frame, R"({
        type: "Frame", name: "x",
        rootView: { type: "RootView", childViews: [
            { type: "Slider", name: "volume" },
            { type: "TextControl", name: "readout" },
            { type: "Toggle", name: "muted" },
            { type: "Toggle", name: "audible" },
        ] },
        converters: [
            { type: "NumberTextConverter", name: "percent", decimals: 0, suffix: "%" },
            { type: "InvertConverter", name: "not" },
        ],
        bindings: [
            { type: "Binding", target: "readout", property: "text", source: "volume", key: "value", converter: "percent" },
            { type: "Binding", target: "audible", property: "checked", source: "muted", key: "checked", converter: "not" },
        ],
    })"));
    ASSERT_EQ(frame.converters().size(), 2u);
    for (Binding* b : frame.bindings()) {
        EXPECT_TRUE(b->isResolved()) << b->lastError();
    }

    auto* volume = frame.rootView().findView<Slider>("volume");
    auto* readout = frame.rootView().findView<TextControl>("readout");
    auto* muted = frame.rootView().findView<Toggle>("muted");
    auto* audible = frame.rootView().findView<Toggle>("audible");

    volume->setValue(40.0f);
    EXPECT_EQ(readout->text(), L"40%");

    readout->setText(L"75 %");
    EXPECT_FLOAT_EQ(volume->value(), 75.0f);

    // Not a number: the slider keeps its value and the binding reports why.
    readout->setText(L"loud");
    EXPECT_FLOAT_EQ(volume->value(), 75.0f);
    EXPECT_FALSE(frame.bindings()[0]->lastError().empty());

    EXPECT_TRUE(audible->isChecked() == !muted->isChecked());
    muted->setChecked(true);
    EXPECT_FALSE(audible->isChecked());
    audible->setChecked(true);
    EXPECT_FALSE(muted->isChecked());
}

TEST(Binding, ScaleConverterRoundTrips) {
    ScaleConverter scale;
    scale.setScale(100.0);
    std::any out;
    std::string error;
    ASSERT_TRUE(scale.toTarget(0.25, typeid(float), out, error));
    EXPECT_FLOAT_EQ(std::any_cast<float>(out), 25.0f);
    ASSERT_TRUE(scale.toSource(25.0f, typeid(double), out, error));
    EXPECT_DOUBLE_EQ(std::any_cast<double>(out), 0.25);
    scale.setScale(0.0);
    EXPECT_FALSE(scale.toSource(1.0, typeid(double), out, error));
}

TEST(Binding, ModelSourceIsBoundByKey) {
    Frame frame;
    frame.setName("x");
    MapModel model;
    model.values["level"] = 12.0;
    frame.setBindingSource("model", model);
    ASSERT_TRUE(loadText(frame, R"({
        type: "Frame", name: "x",
        rootView: { type: "RootView", childViews: [ { type: "Slider", name: "s" } ] },
        bindings: [ { type: "Binding", target: "s", property: "value", source: "model", key: "level" } ],
    })"));
    auto* s = frame.rootView().findView<Slider>("s");
    EXPECT_FLOAT_EQ(s->value(), 12.0f);

    model.setValue(80.0, std::string("level"));
    EXPECT_FLOAT_EQ(s->value(), 80.0f);

    s->setValue(33.0f);
    EXPECT_DOUBLE_EQ(std::any_cast<double>(model.values["level"]), 33.0);
}

TEST(Binding, UnresolvableBindingsAreReportedNotFatal) {
    Frame frame;
    frame.setName("x");
    ASSERT_TRUE(loadText(frame, R"({
        type: "Frame", name: "x",
        rootView: { type: "RootView", childViews: [ { type: "Slider", name: "s" } ] },
        bindings: [
            { type: "Binding", name: "noTarget", target: "ghost", property: "value", source: "s", key: "value" },
            { type: "Binding", name: "noProperty", target: "s", property: "nope", source: "s", key: "value" },
            { type: "Binding", name: "noSource", target: "s", property: "value", source: "ghost", key: "value" },
            { type: "Binding", name: "noConverter", target: "s", property: "value", source: "s", key: "value", converter: "ghost" },
        ],
    })"));
    ASSERT_EQ(frame.bindings().size(), 4u);
    for (Binding* b : frame.bindings()) {
        EXPECT_FALSE(b->isResolved());
        EXPECT_FALSE(b->lastError().empty());
    }
    EXPECT_FALSE(frame.resolveBindings());
}

TEST(Binding, SplitterPositionIsBindable) {
    Frame frame;
    auto* splitter = new Splitter();
    splitter->setName("sp");
    auto* slider = new Slider();
    slider->setName("sl");
    frame.rootView().addChild(splitter);
    frame.rootView().addChild(slider);
    auto* binding = new Binding();
    binding->setTarget("sl");
    binding->setProperty("value");
    binding->setSource("sp");
    binding->setKey("splitPosition");
    binding->setMode(BindingMode::OneWay);
    frame.addBinding(binding);
    EXPECT_TRUE(frame.resolveBindings()) << binding->lastError();
}

// ---- Rules and the convert -> validate -> commit pipeline ------------------------------------------

namespace {
std::string failure(const Rule& rule, const std::any& value) {
    std::string error;
    return rule.validate(value, error) ? std::string() : (error.empty() ? "?" : error);
}
}

TEST(Rules, NumericRules) {
    MinRule min;
    min.setMinValue(5);
    EXPECT_TRUE(failure(min, 5).empty());
    EXPECT_FALSE(failure(min, 4.5).empty());
    EXPECT_FALSE(failure(min, std::string("abc")).empty());

    MaxRule max;
    max.setMaxValue(5);
    EXPECT_TRUE(failure(max, std::string("5")).empty());
    EXPECT_FALSE(failure(max, 6).empty());

    RangeRule range;
    range.setMinValue(0);
    range.setMaxValue(1);
    range.setMessage("custom");
    EXPECT_TRUE(failure(range, 0.5).empty());
    EXPECT_EQ(failure(range, 2), "custom");
}

TEST(Rules, RequiredAndLength) {
    RequiredRule required;
    EXPECT_FALSE(failure(required, std::any()).empty());
    EXPECT_FALSE(failure(required, std::string("  ")).empty());
    EXPECT_TRUE(failure(required, std::string("x")).empty());
    EXPECT_TRUE(failure(required, 0).empty());

    LengthRule length;
    length.setMinLength(2);
    length.setMaxLength(4);
    EXPECT_FALSE(failure(length, std::string("a")).empty());
    EXPECT_TRUE(failure(length, std::wstring(L"abcd")).empty());
    EXPECT_FALSE(failure(length, std::string("abcde")).empty());
}

TEST(Rules, RegexOneOfAnyOfAndCustom) {
    RegexRule regex;
    regex.setPattern("[a-z]+@[a-z]+\\.com");
    EXPECT_TRUE(failure(regex, std::string("me@site.com")).empty());
    EXPECT_FALSE(failure(regex, std::string("me@site")).empty());
    regex.setPattern("([");
    EXPECT_FALSE(failure(regex, std::string("x")).empty());

    OneOfRule oneOf;
    oneOf.setChoices("small, medium,large");
    EXPECT_TRUE(failure(oneOf, std::string("medium")).empty());
    EXPECT_FALSE(failure(oneOf, std::string("huge")).empty());

    AnyOfRule any;
    EXPECT_TRUE(failure(any, 1).empty());  // no rules: passes
    auto* lo = new MaxRule();
    lo->setMaxValue(0);
    auto* hi = new MinRule();
    hi->setMinValue(100);
    any.addRule(lo);
    any.addRule(hi);
    EXPECT_TRUE(failure(any, -3).empty());
    EXPECT_TRUE(failure(any, 150).empty());
    EXPECT_FALSE(failure(any, 50).empty());

    registerValidator("even", [](const std::any& v, std::string& error) {
        const bool ok = std::any_cast<int>(v) % 2 == 0;
        if (!ok) error = "odd";
        return ok;
    });
    CustomRule custom;
    custom.setHandler("even");
    EXPECT_TRUE(failure(custom, 4).empty());
    EXPECT_EQ(failure(custom, 3), "odd");
    custom.setHandler("nope");
    EXPECT_FALSE(failure(custom, 4).empty());
    unregisterValidator("even");
}

TEST(Binding, RulesGateTheCommitAndAreDeclaredInNewui) {
    Frame frame;
    frame.setName("x");
    ASSERT_TRUE(loadText(frame, R"({
        type: "Frame", name: "x",
        rootView: { type: "RootView", childViews: [ { type: "TextControl", name: "age" } ] },
        converters: [ { type: "NumberTextConverter", name: "int", decimals: 0 } ],
        bindings: [
            { type: "Binding", name: "ageBinding", target: "age", property: "text", source: "person", key: "age",
              converter: "int", trigger: "OnChange",
              rules: [ { type: "RangeRule", minValue: 0, maxValue: 120, message: "Age must be 0 to 120" },
                       { type: "AnyOfRule", rules: [ { type: "MinRule", minValue: 18 }, { type: "MaxRule", maxValue: 10 } ] } ] },
        ],
    })"));
    ASSERT_EQ(frame.bindings().size(), 1u);
    Binding& b = *frame.bindings()[0];
    ASSERT_EQ(b.rules().size(), 2u);
    ASSERT_NE(dynamic_cast<AnyOfRule*>(b.rules()[1]), nullptr);
    EXPECT_EQ(dynamic_cast<AnyOfRule*>(b.rules()[1])->rules().size(), 2u);
}

TEST(Binding, PipelineReportsConversionAndValidationSeparately) {
    Frame frame;
    auto* text = new TextControl();
    text->setName("t");
    frame.rootView().addChild(text);
    MapModel model;
    model.values["n"] = 5.0;
    frame.setBindingSource("m", model);

    auto* binding = new Binding();
    binding->setTarget("t");
    binding->setProperty("text");
    binding->setSource("m");
    binding->setKey("n");
    auto* range = new RangeRule();
    range->setMinValue(0);
    range->setMaxValue(10);
    binding->addRule(range);
    frame.addBinding(binding);
    ASSERT_TRUE(frame.resolveBindings()) << binding->lastError();

    text->setText(L"7");  // valid: written through
    EXPECT_DOUBLE_EQ(std::any_cast<double>(model.values["n"]), 7.0);
    EXPECT_TRUE(binding->isValid());

    text->setText(L"99");  // converts, rule rejects: source keeps 7
    EXPECT_DOUBLE_EQ(std::any_cast<double>(model.values["n"]), 7.0);
    EXPECT_EQ(binding->errorKind(), BindingErrorKind::Validation);
    EXPECT_FALSE(binding->isValid());

    text->setText(L"abc");  // cannot convert
    EXPECT_DOUBLE_EQ(std::any_cast<double>(model.values["n"]), 7.0);
    EXPECT_EQ(binding->errorKind(), BindingErrorKind::Conversion);

    EXPECT_FALSE(binding->validate());
    text->setText(L"3");
    EXPECT_TRUE(binding->validate());
    EXPECT_EQ(binding->errorKind(), BindingErrorKind::None);
    EXPECT_DOUBLE_EQ(std::any_cast<double>(model.values["n"]), 3.0);
}

namespace {

// A form of two text fields bound OnCommit to a model, in group "account".
struct GroupForm {
    Frame frame;
    TextControl* pw = nullptr;
    TextControl* pw2 = nullptr;
    MapModel model;

    bool build() {
        pw = new TextControl();
        pw->setName("pwField");
        pw2 = new TextControl();
        pw2->setName("pw2Field");
        frame.rootView().addChild(pw);
        frame.rootView().addChild(pw2);
        model.values["pw"] = std::string("secret");
        model.values["pw2"] = std::string("secret");
        frame.setBindingSource("m", model);
        for (auto [bname, field, key] : {std::tuple<const char*, const char*, const char*>{"pw", "pwField", "pw"},
                                          {"pw2", "pw2Field", "pw2"}}) {
            auto* b = new Binding();
            b->setName(bname);
            b->setTarget(field);
            b->setProperty("text");
            b->setSource("m");
            b->setKey(key);
            b->setTrigger(BindingTrigger::OnCommit);
            b->setGroup("account");
            auto* len = new LengthRule();
            len->setMinLength(4);
            b->addRule(len);
            frame.addBinding(b);
        }
        auto* group = new BindingGroup();
        group->setName("account");
        auto* cmp = new CompareRule();
        cmp->setLeft("pw");
        cmp->setRight("pw2");
        cmp->setMessage("Passwords differ");
        group->addRule(cmp);
        frame.addGroup(group);
        return frame.resolveBindings();
    }
    std::string stored(const char* key) { return std::any_cast<std::string>(model.values[key]); }
};

}

TEST(BindingGroup, LoadDirtyRevert) {
    GroupForm f;
    ASSERT_TRUE(f.build());
    BindingGroup& group = f.frame.bindingGroup("account");
    EXPECT_EQ(group.bindings().size(), 2u);
    EXPECT_FALSE(group.dirty());

    f.pw->setText(L"changed");
    EXPECT_TRUE(group.dirty());
    EXPECT_EQ(f.stored("pw"), "secret");  // OnCommit: not written yet

    EXPECT_TRUE(group.revert());
    EXPECT_FALSE(group.dirty());
    EXPECT_EQ(f.pw->text(), L"secret");
}

TEST(BindingGroup, CommitIsAllOrNothing) {
    GroupForm f;
    ASSERT_TRUE(f.build());
    BindingGroup& group = f.frame.bindingGroup("account");

    f.pw->setText(L"newpass");
    f.pw2->setText(L"abc");  // too short
    EXPECT_FALSE(group.commit());
    ASSERT_EQ(group.errors().size(), 1u);
    EXPECT_EQ(f.stored("pw"), "secret");  // the valid field was not written either
    EXPECT_EQ(f.stored("pw2"), "secret");

    f.pw2->setText(L"different");  // valid alone, but the cross-field rule fails
    EXPECT_FALSE(group.commit());
    ASSERT_EQ(group.errors().size(), 1u);
    EXPECT_EQ(group.errors()[0], "Passwords differ");
    EXPECT_EQ(f.stored("pw"), "secret");

    f.pw2->setText(L"newpass");
    EXPECT_TRUE(group.commit()) << (group.errors().empty() ? "" : group.errors()[0]);
    EXPECT_TRUE(group.errors().empty());
    EXPECT_EQ(f.stored("pw"), "newpass");
    EXPECT_EQ(f.stored("pw2"), "newpass");
    EXPECT_FALSE(group.dirty());
}

TEST(BindingGroup, DeclaredInNewuiWithCustomRule) {
    registerGroupValidator("never", [](BindingGroup&, std::string& error) { error = "nope"; return false; });
    Frame frame;
    frame.setName("x");
    ASSERT_TRUE(loadText(frame, R"({
        type: "Frame", name: "x",
        rootView: { type: "RootView", childViews: [ { type: "TextControl", name: "a" } ] },
        groups: [ { type: "BindingGroup", name: "g",
                    rules: [ { type: "CompareRule", left: "ba", right: "ba", op: "NotEqual", message: "same" },
                             { type: "CustomGroupRule", handler: "never" } ] } ],
        bindings: [ { type: "Binding", name: "ba", group: "g", target: "a", property: "text", source: "a", key: "text" } ],
    })"));
    ASSERT_EQ(frame.groups().size(), 1u);
    BindingGroup& g = frame.bindingGroup("g");
    EXPECT_EQ(&g, frame.groups()[0]);
    ASSERT_EQ(g.rules().size(), 2u);
    EXPECT_EQ(g.bindings().size(), 1u);
    EXPECT_FALSE(g.validate());
    EXPECT_EQ(g.errors().size(), 2u);
    EXPECT_EQ(g.errors()[1], "nope");
    unregisterGroupValidator("never");
}

namespace {

struct FocusForm {
    Frame frame;
    TextControl* a = nullptr;
    TextControl* b = nullptr;
    MapModel model;
    Binding* binding = nullptr;

    bool build() {
        a = new TextControl();
        a->setName("a");
        a->setAcceptsFocus(true);
        a->setBounds(Rect(0, 0, 100, 20));
        b = new TextControl();
        b->setName("b");
        b->setAcceptsFocus(true);
        b->setBounds(Rect(0, 40, 100, 20));
        frame.rootView().addChild(a);
        frame.rootView().addChild(b);
        model.values["n"] = 5.0;
        frame.setBindingSource("m", model);
        binding = new Binding();
        binding->setTarget("a");
        binding->setProperty("text");
        binding->setSource("m");
        binding->setKey("n");
        binding->setTrigger(BindingTrigger::OnCommit);
        auto* range = new RangeRule();
        range->setMinValue(0);
        range->setMaxValue(10);
        binding->addRule(range);
        frame.addBinding(binding);
        return frame.resolveBindings();
    }
    double stored() { return std::any_cast<double>(model.values["n"]); }
};

}

TEST(BindingFocus, OnCommitWritesWhenFocusMovesAwayViaUIInputManager) {
    FocusForm f;
    ASSERT_TRUE(f.build()) << f.binding->lastError();
    RootView& root = f.frame.rootView();
    root.setFocusedSubView(f.a);
    ASSERT_EQ(root.focusedSubView(), f.a);

    f.a->setText(L"8");
    EXPECT_DOUBLE_EQ(f.stored(), 5.0);  // still editing: not committed
    EXPECT_TRUE(f.binding->dirty());

    UIInputManager::instance().moveFocus(root, FocusNavigationDirection::Next);
    ASSERT_EQ(root.focusedSubView(), f.b);
    EXPECT_DOUBLE_EQ(f.stored(), 8.0);
    EXPECT_FALSE(f.binding->dirty());
    EXPECT_TRUE(f.a->errorText().empty());
}

TEST(BindingFocus, RejectedValueMarksTheControlAndClearsWhenFixed) {
    FocusForm f;
    ASSERT_TRUE(f.build());
    RootView& root = f.frame.rootView();
    root.setFocusedSubView(f.a);

    f.a->setText(L"99");
    UIInputManager::instance().moveFocus(root, FocusNavigationDirection::Next);
    EXPECT_DOUBLE_EQ(f.stored(), 5.0);  // rule rejected it
    EXPECT_FALSE(f.a->errorText().empty());

    f.a->setText(L"7");
    EXPECT_TRUE(f.binding->commit());
    EXPECT_TRUE(f.a->errorText().empty());
    EXPECT_DOUBLE_EQ(f.stored(), 7.0);
}

TEST(BindingFocus, EnterCommitsATextFieldAndPassesOnWhenValid) {
    Frame frame;
    auto* field = new TextField();
    field->setName("f");
    field->setBounds(Rect(0, 0, 100, 20));
    frame.rootView().addChild(field);
    MapModel model;
    model.values["n"] = 5.0;
    frame.setBindingSource("m", model);
    auto* binding = new Binding();
    binding->setTarget("f");
    binding->setProperty("text");
    binding->setSource("m");
    binding->setKey("n");
    binding->setTrigger(BindingTrigger::OnCommit);
    auto* range = new RangeRule();
    range->setMinValue(0);
    range->setMaxValue(10);
    binding->addRule(range);
    frame.addBinding(binding);
    ASSERT_TRUE(frame.resolveBindings()) << binding->lastError();

    RootView& root = frame.rootView();
    root.setFocusedSubView(field);
    const UIInputManager& input = UIInputManager::instance();

    field->setText(L"99");  // invalid: Enter is consumed, nothing written, outlined
    EXPECT_TRUE(input.routeDialogKeyDown(root, 0, 0, 1, vkReturn));
    EXPECT_DOUBLE_EQ(std::any_cast<double>(model.values["n"]), 5.0);
    EXPECT_FALSE(field->errorText().empty());
    EXPECT_EQ(root.focusedSubView(), field);

    field->setText(L"7");  // valid: written; no default button, so the key is left unhandled
    EXPECT_FALSE(input.routeDialogKeyDown(root, 0, 0, 1, vkReturn));
    EXPECT_DOUBLE_EQ(std::any_cast<double>(model.values["n"]), 7.0);
    EXPECT_TRUE(field->errorText().empty());
}

TEST(BindingFocus, ErrorTargetLabelShowsAndClearsTheMessage) {
    FocusForm f;
    auto* label = new Label();
    label->setName("err");
    f.frame.rootView().addChild(label);
    ASSERT_TRUE(f.build());
    f.binding->setErrorTarget("err");
    ASSERT_TRUE(f.frame.resolveBindings()) << f.binding->lastError();

    f.a->setText(L"99");
    EXPECT_FALSE(f.binding->commit());
    EXPECT_FALSE(label->text().empty());
    EXPECT_EQ(label->text(), f.binding->lastError());

    f.a->setText(L"7");
    EXPECT_TRUE(f.binding->commit());
    EXPECT_TRUE(label->text().empty());

    f.binding->setErrorTarget("nope");
    EXPECT_FALSE(f.frame.resolveBindings());  // reported, not fatal
}

TEST(BindingGroup, ValidateOnlyNeverWritesAndStillTakesPartInGroupRules) {
    GroupForm f;
    ASSERT_TRUE(f.build());
    // Replace the second binding with a source-less, validate-only one.
    Binding* second = f.frame.bindings()[1];
    second->setMode(BindingMode::ValidateOnly);
    second->setSource("");
    second->setKey("");
    f.pw2->setText(L"");
    ASSERT_TRUE(f.frame.resolveBindings()) << second->lastError();
    BindingGroup& group = f.frame.bindingGroup("account");

    f.pw->setText(L"newpass");
    f.pw2->setText(L"other!");  // valid on its own, but the passwords differ
    EXPECT_FALSE(group.commit());
    EXPECT_EQ(group.errors().size(), 1u);
    EXPECT_EQ(f.stored("pw"), "secret");

    f.pw2->setText(L"newpass");
    EXPECT_TRUE(group.commit());
    EXPECT_EQ(f.stored("pw"), "newpass");
    EXPECT_EQ(f.stored("pw2"), "secret");  // never written: it has no source
    EXPECT_EQ(f.pw2->text(), L"newpass");  // and never overwritten by a refresh
    EXPECT_FALSE(group.dirty());
}

TEST(BindingGroup, GroupRuleFailureShowsOnTheBlamedFieldAsSoonAsAFieldCommits) {
    GroupForm f;
    ASSERT_TRUE(f.build());
    Binding* first = f.frame.bindings()[0];
    Binding* second = f.frame.bindings()[1];

    f.pw->setText(L"newpass");
    EXPECT_TRUE(first->commit());  // fine alone, but pw2 still holds "secret": the rule blames pw2 (right side)
    EXPECT_EQ(f.pw2->errorText(), "Passwords differ");
    EXPECT_TRUE(f.pw->errorText().empty());
    EXPECT_EQ(f.stored("pw"), "newpass");  // a field that is valid on its own is still written

    f.pw2->setText(L"newpass");
    EXPECT_TRUE(second->commit());  // now they match: the message goes away
    EXPECT_TRUE(f.pw2->errorText().empty());

    f.pw2->setText(L"zzzzzz");
    EXPECT_TRUE(second->commit());
    EXPECT_EQ(f.pw2->errorText(), "Passwords differ");
    EXPECT_TRUE(f.frame.bindingGroup("account").load());  // revert clears it
    EXPECT_TRUE(f.pw2->errorText().empty());
}
