#pragma once

#include <any>
#include <functional>
#include <string>
#include <typeindex>
#include <vector>

#include "newui/component.h"
#include "newui/delegate.h"

// Data binding: a Binding connects one reflected property of a control (the target) to one value of a
// source object, and keeps the two in step. Everything here is a plain-data Component, so a whole set of
// bindings is declared in a Frame's .newui file as a "bindings" list and edited by a form designer, e.g.
//
//   bindings: [
//     { type: "Binding", target: "volume", property: "value", source: "settings", key: "volume", mode: "TwoWay" },
//     { type: "Binding", target: "label", property: "text", source: "settings", key: "volume",
//       converter: "percent" },
//   ],
//   converters: [ { type: "NumberTextConverter", name: "percent", decimals: 0, suffix: "%" } ],
//
// Sources and converters are referred to by name. A source is either registered by the application
// (Frame::setBindingSource) - any reflected object, or a Model, whose values are addressed by key - or is
// simply another control in the same frame.

namespace newui {

    class Frame;
    class Model;
    class View;
    namespace reflection { class Class; class Property; }

    // Which way values flow.
    enum class BindingMode {
        // Source -> target once, when the binding is resolved.
        OneTime,
        // Source -> target, now and whenever the source changes.
        OneWay,
        // OneWay, plus target -> source whenever the target changes (subject to the trigger).
        TwoWay,
        // Target only: no source or key. The control's value is converted and checked by the rules (and
        // by a group's rules, e.g. a "repeat password" box compared with the real one) but never written
        // anywhere, and nothing is ever loaded into it. Triggers apply as for TwoWay.
        ValidateOnly,
    };

    // When a changed target value is written back to the source (TwoWay only).
    enum class BindingTrigger {
        // Immediately, each time the target property changes.
        OnChange,
        // When the target control loses keyboard focus (Tab, arrow keys or a click elsewhere - all move
        // focus through UIInputManager/RootView::setFocusedSubView), when Enter is pressed in it (a
        // TextField; a multi-line TextControl keeps Enter for new lines), and when Binding::commit() is
        // called (a BindingGroup does). Enter with a valid value is not consumed, so the window's default
        // button still gets it; with an invalid one it is, and the focus stays put.
        OnCommit,
        // Only when the application (or a BindingGroup) calls Binding::commit().
        Manual,
    };

    // Converts a value of any simple type (bool, integers, float, double, std::string, std::wstring) to
    // another, the way a Binding does when it has no converter: numbers widen and narrow, text is parsed
    // strictly ("12abc" is not a number), bool accepts "true"/"false"/"1"/"0". A value whose type already
    // equals toType passes through unchanged, so any other type (a Color, say) binds to its own kind.
    // Returns false, with a description in error, if the conversion is not possible.
    // @reflect ignore=true
    bool convertValue(const std::any& in, std::type_index toType, std::any& out, std::string& error);

    // Translates between the form a source holds a value in and the form a control shows. The base class
    // is the default conversion (see convertValue()); the subclasses below add the common cases. Declared
    // in a Frame's "converters" list and referred to from a Binding by name.
    class Converter : public Component {
    public:
        Converter() = default;
        virtual ~Converter() = default;

        // Source value -> the value for the target property, which is of targetType.
        // @reflect ignore=true
        virtual bool toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const;

        // Target value -> the value for the source, which is of sourceType.
        // @reflect ignore=true
        virtual bool toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const;
    };

    // target = source * scale + offset (and the inverse going back): a Slider's 0..100 against a model's
    // 0..1, or Celsius against Fahrenheit. A scale of 0 cannot be reversed and fails on the way back.
    class ScaleConverter : public Converter {
    public:
        ScaleConverter() = default;

        double scale() const { return scale_; }
        void setScale(double scale) { scale_ = scale; }
        double offset() const { return offset_; }
        void setOffset(double offset) { offset_ = offset; }

        // @reflect ignore=true
        bool toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const override;
        // @reflect ignore=true
        bool toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const override;

    private:
        double scale_ = 1.0;
        double offset_ = 0.0;
    };

    // Negates a bool in both directions: a "hidden" flag against a "visible" property.
    class InvertConverter : public Converter {
    public:
        InvertConverter() = default;

        // @reflect ignore=true
        bool toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const override;
        // @reflect ignore=true
        bool toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const override;
    };

    // A number shown as text and parsed back: decimals digits after the point (negative: as many as
    // needed), with a fixed prefix and suffix ("$", "%", " px"). Surrounding spaces and the prefix/suffix
    // are tolerated when parsing, so a user may type either "42" or "42%".
    class NumberTextConverter : public Converter {
    public:
        NumberTextConverter() = default;

        int decimals() const { return decimals_; }
        void setDecimals(int decimals) { decimals_ = decimals; }
        std::string prefix() const { return prefix_; }
        void setPrefix(const std::string& prefix) { prefix_ = prefix; }
        std::string suffix() const { return suffix_; }
        void setSuffix(const std::string& suffix) { suffix_ = suffix; }

        // @reflect ignore=true
        bool toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const override;
        // @reflect ignore=true
        bool toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const override;

    private:
        int decimals_ = -1;
        std::string prefix_;
        std::string suffix_;
    };

    // ---- Validation rules ----------------------------------------------------------------------
    // A rule checks the value a binding is about to write to its source (after conversion, so a number is
    // a number, not the text the user typed). Rules are plain-data Components declared in a Binding's
    // "rules" list, e.g.  rules: [ { type: "RangeRule", min: 0, max: 100, message: "Pick 0 to 100" } ].
    // All of a binding's rules must pass (AND); use AnyOfRule for OR.

    class Rule : public Component {
    public:
        Rule() = default;
        virtual ~Rule() = default;

        // Text shown when the rule fails. Empty: the rule's own default text.
        std::string message() const { return message_; }
        void setMessage(const std::string& message) { message_ = message; }

        // True if value passes. Otherwise false, with the reason in error.
        // @reflect ignore=true
        virtual bool validate(const std::any& value, std::string& error) const = 0;

    protected:
        // Sets error to message() if one is declared, else fallback; returns false.
        bool reject(std::string& error, const std::string& fallback) const;

    private:
        std::string message_;
    };

    // Fails for a missing value or text that is empty (or only spaces).
    class RequiredRule : public Rule {
    public:
        RequiredRule() = default;
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    };

    // Numbers (or text that reads as one) must be >= min.
    class MinRule : public Rule {
    public:
        MinRule() = default;
        double min() const { return min_; }
        void setMin(double min) { min_ = min; }
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        double min_ = 0.0;
    };

    // Numbers (or text that reads as one) must be <= max.
    class MaxRule : public Rule {
    public:
        MaxRule() = default;
        double max() const { return max_; }
        void setMax(double max) { max_ = max; }
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        double max_ = 0.0;
    };

    // Numbers must lie within min..max inclusive.
    class RangeRule : public Rule {
    public:
        RangeRule() = default;
        double min() const { return min_; }
        void setMin(double min) { min_ = min; }
        double max() const { return max_; }
        void setMax(double max) { max_ = max; }
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        double min_ = 0.0;
        double max_ = 1.0;
    };

    // Text must have minLength..maxLength characters (a negative maxLength means no upper limit).
    class LengthRule : public Rule {
    public:
        LengthRule() = default;
        int minLength() const { return minLength_; }
        void setMinLength(int minLength) { minLength_ = minLength; }
        int maxLength() const { return maxLength_; }
        void setMaxLength(int maxLength) { maxLength_ = maxLength; }
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        int minLength_ = 0;
        int maxLength_ = -1;
    };

    // The whole text must match an ECMAScript regular expression. A bad pattern fails every value.
    class RegexRule : public Rule {
    public:
        RegexRule() = default;
        std::string pattern() const { return pattern_; }
        void setPattern(const std::string& pattern) { pattern_ = pattern; }
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        std::string pattern_;
    };

    // The value, as text, must be one of a comma separated list: choices: "small,medium,large".
    class OneOfRule : public Rule {
    public:
        OneOfRule() = default;
        std::string choices() const { return choices_; }
        void setChoices(const std::string& choices) { choices_ = choices; }
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        std::string choices_;
    };

    // Passes when at least one of its own rules passes. Fails with its message, or else the first
    // failure's text. With no rules it passes.
    class AnyOfRule : public Rule {
    public:
        AnyOfRule() = default;
        ~AnyOfRule() override;
        AnyOfRule(const AnyOfRule&) = delete;
        AnyOfRule& operator=(const AnyOfRule&) = delete;

        // @reflect collection add=addRule remove=removeRule
        const std::vector<Rule*>& rules() const { return rules_; }
        // Takes ownership.
        void addRule(Rule* rule);
        // The caller owns rule again.
        void removeRule(Rule* rule);

        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        std::vector<Rule*> rules_;
    };

    // A check written in C++ and registered under a name with registerValidator(); the .newui file (and a
    // form designer) only holds the name. An unregistered name fails every value.
    class CustomRule : public Rule {
    public:
        CustomRule() = default;
        std::string handler() const { return handler_; }
        void setHandler(const std::string& handler) { handler_ = handler; }
        // @reflect ignore=true
        bool validate(const std::any& value, std::string& error) const override;
    private:
        std::string handler_;
    };

    // A validator returns true if value is acceptable; otherwise false, optionally setting error.
    using ValidatorFunction = std::function<bool(const std::any& value, std::string& error)>;
    // @reflect ignore=true
    void registerValidator(const std::string& name, ValidatorFunction function);
    // @reflect ignore=true
    void unregisterValidator(const std::string& name);

    // What kind of problem a Binding last reported.
    enum class BindingErrorKind {
        None,
        // Setting up or reading/writing a value: missing control, property or source.
        Source,
        // The value could not be converted (text that is not a number).
        Conversion,
        // The value converted but a rule rejected it.
        Validation,
    };

    // Where a Binding finds a source by name: the object itself, plus what kind of thing it is.
    // @reflect ignore=true
    struct BindingSource {
        void* object = nullptr;                          // the most-derived object
        std::type_index type = std::type_index(typeid(void));
        Component* component = nullptr;                  // set when the object is a Component
        Model* model = nullptr;                          // set when the object is a Model
    };

    // One link between a target control's property and a source value. See the file comment.
    class Binding : public Component {
    public:
        Binding() = default;
        ~Binding() override;
        Binding(const Binding&) = delete;
        Binding& operator=(const Binding&) = delete;

        // ---- Declared (persisted) ----------------------------------------------------------------

        // Name of the control that shows the value.
        std::string target() const { return target_; }
        void setTarget(const std::string& target) { target_ = target; }

        // The reflected property of the target ("value", "text", "checked", ...).
        std::string property() const { return property_; }
        void setProperty(const std::string& property) { property_ = property; }

        // Name of the source: one registered with Frame::setBindingSource(), or another control.
        std::string source() const { return source_; }
        void setSource(const std::string& source) { source_ = source; }

        // What to read from the source: a property name when the source is a reflected object, or a Model
        // key (passed to Model::value()/setValue() as a std::string). Empty for a Model: its default value().
        std::string key() const { return key_; }
        void setKey(const std::string& key) { key_ = key; }

        // Name of a converter in the Frame's "converters" list. Empty: the default conversion.
        std::string converter() const { return converter_; }
        void setConverter(const std::string& converter) { converter_ = converter; }

        BindingMode mode() const { return mode_; }
        void setMode(BindingMode mode) { mode_ = mode; }

        BindingTrigger trigger() const { return trigger_; }
        void setTrigger(BindingTrigger trigger) { trigger_ = trigger; }

        // Optional: name of a control (usually a Label, placed beside or below the field, with room reserved
        // by the layout) whose "text" shows this binding's error message, and is emptied again when the
        // value is fine. The target control is also outlined in red either way.
        std::string errorTarget() const { return errorTarget_; }
        void setErrorTarget(const std::string& errorTarget) { errorTarget_ = errorTarget; }

        // Name of the BindingGroup this binding belongs to. Empty: the Frame's default (unnamed) group.
        std::string group() const { return group_; }
        void setGroup(const std::string& group) { group_ = group; }

        // Checked, in order, against the converted value before it is written to the source.
        // @reflect collection add=addRule remove=removeRule
        const std::vector<Rule*>& rules() const { return rules_; }
        // Takes ownership.
        void addRule(Rule* rule);
        // The caller owns rule again.
        void removeRule(Rule* rule);

        // ---- Runtime (not persisted) -------------------------------------------------------------

        // The pipeline without its last step: reads the target, converts it for the source and runs the
        // rules, but writes nothing. False if either fails (see errorKind()/lastError()).
        // @reflect ignore=true
        bool validate();

        // True if the target has changed since the value was last copied from or written to the source
        // (refresh() and a successful commit() clear it).
        // @reflect ignore=true
        bool dirty() const { return dirty_; }

        // The converted value commit() would write, after the rules have passed; false (with
        // lastError()) if conversion or a rule fails. Writes nothing.
        // @reflect ignore=true
        bool pendingValue(std::any& out);

        // What kind of problem lastError() describes; None when the last attempt succeeded.
        // @reflect ignore=true
        BindingErrorKind errorKind() const { return errorKind_; }

        // True unless the last commit() or validate() was rejected by conversion or a rule.
        // @reflect ignore=true
        bool isValid() const { return errorKind_ != BindingErrorKind::Conversion && errorKind_ != BindingErrorKind::Validation; }

        // Finds the target, source and converter and connects them, then copies the source's value to the
        // target. Calling it again re-resolves from scratch. Returns false - with the reason in
        // lastError() and the binding left unconnected - if the target, source, key or property can't be
        // found or the first conversion fails.
        // @reflect ignore=true
        bool resolve(Frame& frame);

        // Disconnects from everything resolve() connected to. Safe to call when not resolved.
        // @reflect ignore=true
        void unbind();

        // @reflect ignore=true
        bool isResolved() const { return resolved_; }

        // Source -> target now (also what a source change triggers). False on a conversion failure.
        // @reflect ignore=true
        bool refresh();

        // Target -> source now, whatever the mode or trigger: convert, validate, then write. Nothing is
        // written if conversion or a rule fails (the source keeps its last good value). False on any
        // failure or when the source can't be written.
        // @reflect ignore=true
        bool commit();

        // The reason the last resolve(), refresh() or commit() failed; empty after one that succeeded.
        // @reflect ignore=true
        const std::string& lastError() const { return lastError_; }

        // Fired with the new lastError() each time an attempt fails.
        // @reflect ignore=true
        Delegate<Binding, const std::string&> onError;

    private:
        bool fail(const std::string& message, BindingErrorKind kind = BindingErrorKind::Source);
        bool convertAndCheck(std::any& converted);
        bool readSource(std::any& out);
        bool writeSource(const std::any& value);
        std::type_index sourceType() const;
        SyncReturn handleTargetChanged(Component& sender, const std::string& property);
        SyncReturn handleSourceChanged(Component& sender, const std::string& property);
        SyncReturn handleModelChanged(Model& sender);
        SyncReturn handleTargetDestroyed(View& sender);
        SyncReturn handleTargetLostFocus(View& sender);
        SyncReturn handleTargetKeyDown(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode);
        void showError();

        std::string target_;
        std::string property_;
        std::string source_;
        std::string key_;
        std::string converter_;
        std::string group_;
        std::string errorTarget_;
        bool dirty_ = false;
        BindingMode mode_ = BindingMode::TwoWay;
        BindingTrigger trigger_ = BindingTrigger::OnChange;

        bool resolved_ = false;
        bool updating_ = false;  // re-entrancy guard: a write we made must not bounce back
        std::string lastError_;
        BindingErrorKind errorKind_ = BindingErrorKind::None;
        std::vector<Rule*> rules_;

        void setErrorLabel(const std::string& text);

        View* errorView_ = nullptr;
        const reflection::Property* errorProperty_ = nullptr;
        View* targetView_ = nullptr;
        const reflection::Class* targetClass_ = nullptr;
        const reflection::Property* targetProperty_ = nullptr;
        BindingSource sourceRef_;
        const reflection::Class* sourceClass_ = nullptr;
        const reflection::Property* sourceProperty_ = nullptr;
        const Converter* converterRef_ = nullptr;
        Connection targetChangedConn_;
        Connection targetDestroyedConn_;
        Connection targetLostFocusConn_;
        Connection targetKeyDownConn_;
        Connection sourceChangedConn_;
    };

    // ---- Binding groups ------------------------------------------------------------------------
    // A BindingGroup treats the bindings that name it (Binding::group) as one form: load() fills every
    // control from its source, commit() writes them all back or - if anything is invalid - none, revert()
    // drops the user's edits, dirty() says whether there are any. Declared in a Frame's "groups" list
    // when it has rules of its own; otherwise Frame::bindingGroup(name) makes one on demand:
    //
    //   groups: [ { type: "BindingGroup", name: "account",
    //               rules: [ { type: "CompareRule", left: "pw", right: "pw2", op: "Equal",
    //                          message: "Passwords differ" } ] } ],
    //   bindings: [ { type: "Binding", name: "pw", group: "account", ... }, ... ]

    class BindingGroup;

    // A check across several of a group's bindings, run by BindingGroup::validate() after each binding has
    // passed its own rules.
    class GroupRule : public Component {
    public:
        GroupRule() = default;
        virtual ~GroupRule() = default;

        std::string message() const { return message_; }
        void setMessage(const std::string& message) { message_ = message; }

        // True if the group passes. Otherwise false, with the reason in error.
        // @reflect ignore=true
        virtual bool validate(BindingGroup& group, std::string& error) const = 0;

    protected:
        bool reject(std::string& error, const std::string& fallback) const;

    private:
        std::string message_;
    };

    enum class CompareOp { Equal, NotEqual, Less, LessOrEqual, Greater, GreaterOrEqual };

    // Compares the pending (converted) values of two bindings in the group, named by Binding::name():
    // numbers numerically, anything else as text. Passes if either binding has no value to compare.
    class CompareRule : public GroupRule {
    public:
        CompareRule() = default;

        std::string left() const { return left_; }
        void setLeft(const std::string& left) { left_ = left; }
        std::string right() const { return right_; }
        void setRight(const std::string& right) { right_ = right; }
        CompareOp op() const { return op_; }
        void setOp(CompareOp op) { op_ = op; }

        // @reflect ignore=true
        bool validate(BindingGroup& group, std::string& error) const override;
    private:
        std::string left_;
        std::string right_;
        CompareOp op_ = CompareOp::Equal;
    };

    // A group check written in C++, registered by name with registerGroupValidator().
    class CustomGroupRule : public GroupRule {
    public:
        CustomGroupRule() = default;
        std::string handler() const { return handler_; }
        void setHandler(const std::string& handler) { handler_ = handler; }
        // @reflect ignore=true
        bool validate(BindingGroup& group, std::string& error) const override;
    private:
        std::string handler_;
    };

    using GroupValidatorFunction = std::function<bool(BindingGroup& group, std::string& error)>;
    // @reflect ignore=true
    void registerGroupValidator(const std::string& name, GroupValidatorFunction function);
    // @reflect ignore=true
    void unregisterGroupValidator(const std::string& name);

    class BindingGroup : public Component {
    public:
        BindingGroup() = default;
        ~BindingGroup() override;
        BindingGroup(const BindingGroup&) = delete;
        BindingGroup& operator=(const BindingGroup&) = delete;

        // @reflect collection add=addRule remove=removeRule
        const std::vector<GroupRule*>& rules() const { return rules_; }
        // Takes ownership.
        void addRule(GroupRule* rule);
        // The caller owns rule again.
        void removeRule(GroupRule* rule);

        // ---- Runtime (not persisted) -------------------------------------------------------------

        // The Frame this group belongs to (set by Frame). Without one the group is empty.
        // @reflect ignore=true
        Frame* frame() const { return frame_; }
        // @reflect ignore=true
        void setFrame(Frame* frame) { frame_ = frame; }

        // The frame's bindings whose group() equals name(). Only resolved ones take part in the operations below.
        // @reflect ignore=true
        std::vector<Binding*> bindings() const;
        // The member binding named name; nullptr if none.
        // @reflect ignore=true
        Binding* binding(const std::string& name) const;

        // Source -> target for every binding, discarding unsaved edits. False if any fails.
        // @reflect ignore=true
        bool load();
        // The same as load(), for a Cancel button.
        // @reflect ignore=true
        bool revert() { return load(); }

        // True if any binding is dirty.
        // @reflect ignore=true
        bool dirty() const;

        // Runs every binding's pipeline and then the group's rules, writing nothing. Fills errors().
        // @reflect ignore=true
        bool validate();

        // validate(), then - only if all passed - writes every dirty binding to its source. Fails
        // (false) with nothing written otherwise, or if a write fails.
        // @reflect ignore=true
        bool commit();

        // What the last validate()/commit() found wrong, one message per failing binding or rule.
        // @reflect ignore=true
        const std::vector<std::string>& errors() const { return errors_; }

    private:
        Frame* frame_ = nullptr;
        std::vector<GroupRule*> rules_;
        std::vector<std::string> errors_;
    };

}
