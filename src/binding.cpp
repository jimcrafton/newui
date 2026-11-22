#include "newui/binding.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <regex>
#include <variant>

#include "newui/frame.h"
#include "newui/keyboard_constants.h"
#include "newui/models.h"
#include "newui/reflection.h"
#include "newui/utils.h"
#include "newui/view.h"

namespace newui {

    // ---------------------------------------------------------------------
    // Default value conversion
    // ---------------------------------------------------------------------

    namespace {

        // Every simple value reduces to one of these and is rebuilt as the requested type.
        using Simple = std::variant<bool, std::int64_t, double, std::string>;

        template<typename T>
        bool readIntegral(const std::any& a, Simple& out) {
            if (a.type() != typeid(T)) {
                return false;
            }
            out = static_cast<std::int64_t>(std::any_cast<T>(a));
            return true;
        }

        bool toSimple(const std::any& a, Simple& out) {
            if (a.type() == typeid(bool)) { out = std::any_cast<bool>(a); return true; }
            if (a.type() == typeid(float)) { out = static_cast<double>(std::any_cast<float>(a)); return true; }
            if (a.type() == typeid(double)) { out = std::any_cast<double>(a); return true; }
            if (a.type() == typeid(std::string)) { out = std::any_cast<std::string>(a); return true; }
            if (a.type() == typeid(std::wstring)) { out = wideToUtf8(std::any_cast<std::wstring>(a)); return true; }
            if (a.type() == typeid(std::u16string)) { out = utf16ToUtf8(std::any_cast<std::u16string>(a)); return true; }
            return readIntegral<int>(a, out) || readIntegral<unsigned int>(a, out)
                || readIntegral<long>(a, out) || readIntegral<unsigned long>(a, out)
                || readIntegral<long long>(a, out) || readIntegral<unsigned long long>(a, out)
                || readIntegral<short>(a, out) || readIntegral<unsigned short>(a, out)
                || readIntegral<signed char>(a, out) || readIntegral<unsigned char>(a, out);
        }

        std::string trimmed(const std::string& s) {
            const char* ws = " \t\r\n";
            const std::size_t first = s.find_first_not_of(ws);
            if (first == std::string::npos) {
                return std::string();
            }
            return s.substr(first, s.find_last_not_of(ws) - first + 1);
        }

        bool parseDouble(const std::string& text, double& out) {
            const std::string s = trimmed(text);
            if (s.empty()) {
                return false;
            }
            const char* begin = s.data();
            const char* end = s.data() + s.size();
            if (*begin == '+') {
                ++begin;
            }
            const auto result = std::from_chars(begin, end, out);
            return result.ec == std::errc() && result.ptr == end;
        }

        bool asDouble(const Simple& v, double& out) {
            if (const auto* d = std::get_if<double>(&v)) { out = *d; return true; }
            if (const auto* i = std::get_if<std::int64_t>(&v)) { out = static_cast<double>(*i); return true; }
            if (const auto* b = std::get_if<bool>(&v)) { out = *b ? 1.0 : 0.0; return true; }
            return parseDouble(std::get<std::string>(v), out);
        }

        bool asInt64(const Simple& v, std::int64_t& out) {
            if (const auto* i = std::get_if<std::int64_t>(&v)) { out = *i; return true; }
            if (const auto* b = std::get_if<bool>(&v)) { out = *b ? 1 : 0; return true; }
            double d = 0;
            if (!asDouble(v, d) || !std::isfinite(d)) {
                return false;
            }
            out = static_cast<std::int64_t>(std::llround(d));
            return true;
        }

        bool asBool(const Simple& v, bool& out) {
            if (const auto* b = std::get_if<bool>(&v)) { out = *b; return true; }
            if (const auto* i = std::get_if<std::int64_t>(&v)) { out = *i != 0; return true; }
            if (const auto* d = std::get_if<double>(&v)) { out = *d != 0.0; return true; }
            const std::string s = trimmed(std::get<std::string>(v));
            if (s == "true" || s == "True" || s == "TRUE" || s == "1") { out = true; return true; }
            if (s == "false" || s == "False" || s == "FALSE" || s == "0") { out = false; return true; }
            return false;
        }

        std::string asText(const Simple& v) {
            if (const auto* s = std::get_if<std::string>(&v)) { return *s; }
            if (const auto* b = std::get_if<bool>(&v)) { return *b ? "true" : "false"; }
            if (const auto* i = std::get_if<std::int64_t>(&v)) { return std::to_string(*i); }
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), std::get<double>(v));
            return std::string(buffer, result.ptr);
        }

        template<typename T>
        bool makeIntegral(std::type_index type, const Simple& v, std::any& out, bool& matched) {
            if (type != typeid(T)) {
                return false;
            }
            matched = true;
            std::int64_t i = 0;
            if (asInt64(v, i)) {
                out = static_cast<T>(i);
            }
            return true;
        }

        // Empty out on return (with matched true): the value doesn't fit the type.
        // matched false: the type isn't a simple one at all.
        void fromSimple(std::type_index type, const Simple& v, std::any& out, bool& matched) {
            matched = true;
            if (type == typeid(bool)) {
                bool b = false;
                if (asBool(v, b)) out = b;
            }
            else if (type == typeid(float)) {
                double d = 0;
                if (asDouble(v, d)) out = static_cast<float>(d);
            }
            else if (type == typeid(double)) {
                double d = 0;
                if (asDouble(v, d)) out = d;
            }
            else if (type == typeid(std::string)) {
                out = asText(v);
            }
            else if (type == typeid(std::wstring)) {
                out = utf8ToWide(asText(v));
            }
            else if (type == typeid(std::u16string)) {
                out = utf8ToUtf16(asText(v));
            }
            else {
                matched = false;
                (void)(makeIntegral<int>(type, v, out, matched) || makeIntegral<unsigned int>(type, v, out, matched)
                    || makeIntegral<long>(type, v, out, matched) || makeIntegral<unsigned long>(type, v, out, matched)
                    || makeIntegral<long long>(type, v, out, matched) || makeIntegral<unsigned long long>(type, v, out, matched)
                    || makeIntegral<short>(type, v, out, matched) || makeIntegral<unsigned short>(type, v, out, matched)
                    || makeIntegral<signed char>(type, v, out, matched) || makeIntegral<unsigned char>(type, v, out, matched));
            }
        }

        std::string describe(const Simple& v) {
            return asText(v);
        }

        bool toDouble(const std::any& in, double& out, std::string& error) {
            Simple simple;
            if (!toSimple(in, simple) || !asDouble(simple, out)) {
                error = "Cannot read '" + (toSimple(in, simple) ? describe(simple) : std::string("value")) + "' as a number";
                return false;
            }
            return true;
        }

        std::string formatNumber(double value, int decimals) {
            char buffer[512];
            if (decimals < 0) {
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
                return std::string(buffer, result.ptr);
            }
            const int written = std::snprintf(buffer, sizeof(buffer), "%.*f", decimals > 30 ? 30 : decimals, value);
            return written > 0 ? std::string(buffer, static_cast<std::size_t>(written)) : std::string();
        }

    }

    bool convertValue(const std::any& in, std::type_index toType, std::any& out, std::string& error) {
        if (std::type_index(in.type()) == toType) {
            out = in;
            return true;
        }
        Simple simple;
        if (!toSimple(in, simple)) {
            error = "Cannot convert a value of this type";
            return false;
        }
        bool matched = false;
        std::any result;
        fromSimple(toType, simple, result, matched);
        if (!matched) {
            error = "Cannot convert to this property's type";
            return false;
        }
        if (!result.has_value()) {
            error = "'" + describe(simple) + "' is not a valid value";
            return false;
        }
        out = std::move(result);
        return true;
    }

    // ---------------------------------------------------------------------
    // Converters
    // ---------------------------------------------------------------------

    bool Converter::toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const {
        return convertValue(sourceValue, targetType, out, error);
    }

    bool Converter::toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const {
        return convertValue(targetValue, sourceType, out, error);
    }

    bool ScaleConverter::toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const {
        double v = 0;
        return toDouble(sourceValue, v, error) && convertValue(v * scale_ + offset_, targetType, out, error);
    }

    bool ScaleConverter::toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const {
        if (scale_ == 0.0) {
            error = "A scale of 0 cannot be reversed";
            return false;
        }
        double v = 0;
        return toDouble(targetValue, v, error) && convertValue((v - offset_) / scale_, sourceType, out, error);
    }

    namespace {
        bool negate(const std::any& in, std::type_index toType, std::any& out, std::string& error) {
            std::any asBoolValue;
            if (!convertValue(in, typeid(bool), asBoolValue, error)) {
                return false;
            }
            return convertValue(!std::any_cast<bool>(asBoolValue), toType, out, error);
        }
    }

    bool InvertConverter::toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const {
        return negate(sourceValue, targetType, out, error);
    }

    bool InvertConverter::toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const {
        return negate(targetValue, sourceType, out, error);
    }

    bool NumberTextConverter::toTarget(const std::any& sourceValue, std::type_index targetType, std::any& out, std::string& error) const {
        double v = 0;
        if (!toDouble(sourceValue, v, error)) {
            return false;
        }
        return convertValue(prefix_ + formatNumber(v, decimals_) + suffix_, targetType, out, error);
    }

    bool NumberTextConverter::toSource(const std::any& targetValue, std::type_index sourceType, std::any& out, std::string& error) const {
        Simple simple;
        if (!toSimple(targetValue, simple)) {
            error = "Cannot read the text";
            return false;
        }
        std::string text = trimmed(asText(simple));
        if (!prefix_.empty() && text.compare(0, prefix_.size(), prefix_) == 0) {
            text = trimmed(text.substr(prefix_.size()));
        }
        if (!suffix_.empty() && text.size() >= suffix_.size()
            && text.compare(text.size() - suffix_.size(), suffix_.size(), suffix_) == 0) {
            text = trimmed(text.substr(0, text.size() - suffix_.size()));
        }
        double v = 0;
        if (!parseDouble(text, v)) {
            error = "'" + asText(simple) + "' is not a number";
            return false;
        }
        return convertValue(v, sourceType, out, error);
    }

    // ---------------------------------------------------------------------
    // Rules
    // ---------------------------------------------------------------------

    namespace {

        std::map<std::string, ValidatorFunction>& validators() {
            static std::map<std::string, ValidatorFunction> registry;
            return registry;
        }

        // The text of a simple value; false for any other type.
        bool textOf(const std::any& value, std::string& out) {
            Simple simple;
            if (!toSimple(value, simple)) {
                return false;
            }
            out = asText(simple);
            return true;
        }

        std::string numberText(double v) {
            return formatNumber(v, -1);
        }

        // Numeric rules share this: a value that isn't a number fails them.
        bool numberOf(const std::any& value, double& out) {
            Simple simple;
            return toSimple(value, simple) && asDouble(simple, out);
        }

        std::size_t characterCount(const std::any& value, bool& ok) {
            ok = true;
            if (const auto* w = std::any_cast<std::wstring>(&value)) {
                return w->size();
            }
            if (const auto* u = std::any_cast<std::u16string>(&value)) {
                return u->size();
            }
            std::string text;
            if (!textOf(value, text)) {
                ok = false;
                return 0;
            }
            return utf8ToWide(text).size();
        }

    }

    bool Rule::reject(std::string& error, const std::string& fallback) const {
        error = message_.empty() ? fallback : message_;
        return false;
    }

    bool RequiredRule::validate(const std::any& value, std::string& error) const {
        if (!value.has_value()) {
            return reject(error, "A value is required");
        }
        std::string text;
        if ((value.type() == typeid(std::string) || value.type() == typeid(std::wstring)
            || value.type() == typeid(std::u16string))
            && textOf(value, text) && trimmed(text).empty()) {
            return reject(error, "A value is required");
        }
        return true;
    }

    bool MinRule::validate(const std::any& value, std::string& error) const {
        double v = 0;
        if (!numberOf(value, v)) {
            return reject(error, "Must be a number");
        }
        return v >= min_ || reject(error, "Must be at least " + numberText(min_));
    }

    bool MaxRule::validate(const std::any& value, std::string& error) const {
        double v = 0;
        if (!numberOf(value, v)) {
            return reject(error, "Must be a number");
        }
        return v <= max_ || reject(error, "Must be at most " + numberText(max_));
    }

    bool RangeRule::validate(const std::any& value, std::string& error) const {
        double v = 0;
        if (!numberOf(value, v)) {
            return reject(error, "Must be a number");
        }
        return (v >= min_ && v <= max_)
            || reject(error, "Must be between " + numberText(min_) + " and " + numberText(max_));
    }

    bool LengthRule::validate(const std::any& value, std::string& error) const {
        bool ok = false;
        const std::size_t count = characterCount(value, ok);
        if (!ok) {
            return reject(error, "Must be text");
        }
        if (static_cast<long long>(count) < minLength_) {
            return reject(error, "Must be at least " + std::to_string(minLength_) + " characters");
        }
        if (maxLength_ >= 0 && count > static_cast<std::size_t>(maxLength_)) {
            return reject(error, "Must be at most " + std::to_string(maxLength_) + " characters");
        }
        return true;
    }

    bool RegexRule::validate(const std::any& value, std::string& error) const {
        std::string text;
        if (!textOf(value, text)) {
            return reject(error, "Must be text");
        }
        try {
            const std::regex expression(pattern_);
            return std::regex_match(text, expression) || reject(error, "Does not match the required format");
        }
        catch (const std::regex_error&) {
            return reject(error, "Invalid pattern '" + pattern_ + "'");
        }
    }

    bool OneOfRule::validate(const std::any& value, std::string& error) const {
        std::string text;
        if (!textOf(value, text)) {
            return reject(error, "Not an allowed value");
        }
        text = trimmed(text);
        std::size_t start = 0;
        while (start <= choices_.size()) {
            std::size_t end = choices_.find(',', start);
            if (end == std::string::npos) {
                end = choices_.size();
            }
            if (trimmed(choices_.substr(start, end - start)) == text) {
                return true;
            }
            start = end + 1;
        }
        return reject(error, "Must be one of: " + choices_);
    }

    AnyOfRule::~AnyOfRule() {
        for (Rule* rule : rules_) {
            delete rule;
        }
    }

    void AnyOfRule::addRule(Rule* rule) {
        if (rule != nullptr && std::find(rules_.begin(), rules_.end(), rule) == rules_.end()) {
            rules_.push_back(rule);
        }
    }

    void AnyOfRule::removeRule(Rule* rule) {
        rules_.erase(std::remove(rules_.begin(), rules_.end(), rule), rules_.end());
    }

    bool AnyOfRule::validate(const std::any& value, std::string& error) const {
        if (rules_.empty()) {
            return true;
        }
        std::string first;
        for (const Rule* rule : rules_) {
            std::string reason;
            if (rule->validate(value, reason)) {
                return true;
            }
            if (first.empty()) {
                first = reason;
            }
        }
        return reject(error, first);
    }

    bool CustomRule::validate(const std::any& value, std::string& error) const {
        const auto it = validators().find(handler_);
        if (it == validators().end()) {
            return reject(error, "No validator named '" + handler_ + "'");
        }
        std::string reason;
        if (it->second(value, reason)) {
            return true;
        }
        return reject(error, reason.empty() ? "Not a valid value" : reason);
    }

    void registerValidator(const std::string& name, ValidatorFunction function) {
        validators()[name] = std::move(function);
    }

    void unregisterValidator(const std::string& name) {
        validators().erase(name);
    }

    // ---------------------------------------------------------------------
    // Binding
    // ---------------------------------------------------------------------

    namespace {

        // The pointer reflected properties expect: the most-derived object, not a base subobject.
        void* instanceOf(Component& component) {
            return dynamic_cast<void*>(&component);
        }

        const reflection::Class* classOf(Component& component) {
            return reflection::classinfo(std::type_index(typeid(component)));
        }

    }

    Binding::~Binding() {
        unbind();
        for (Rule* rule : rules_) {
            delete rule;
        }
    }

    void Binding::addRule(Rule* rule) {
        if (rule != nullptr && std::find(rules_.begin(), rules_.end(), rule) == rules_.end()) {
            rules_.push_back(rule);
        }
    }

    void Binding::removeRule(Rule* rule) {
        rules_.erase(std::remove(rules_.begin(), rules_.end(), rule), rules_.end());
    }

    bool Binding::fail(const std::string& message, BindingErrorKind kind) {
        lastError_ = message;
        errorKind_ = kind;
        showError();
        onError(*this, lastError_);
        return false;
    }

    void Binding::unbind() {
        if (targetView_ != nullptr) {
            targetView_->onPropertyChanged.remove(targetChangedConn_);
            targetView_->onDestroyed.remove(targetDestroyedConn_);
            targetView_->onLostFocus.remove(targetLostFocusConn_);
            targetView_->onKeyDown.remove(targetKeyDownConn_);
            targetView_->setErrorText(std::string());
            setErrorLabel(std::string());
        }
        errorView_ = nullptr;
        errorProperty_ = nullptr;
        if (sourceRef_.model != nullptr) {
            sourceRef_.model->onChanged.remove(sourceChangedConn_);
        }
        else if (sourceRef_.component != nullptr) {
            sourceRef_.component->onPropertyChanged.remove(sourceChangedConn_);
        }
        targetChangedConn_ = Connection();
        targetDestroyedConn_ = Connection();
        targetLostFocusConn_ = Connection();
        targetKeyDownConn_ = Connection();
        sourceChangedConn_ = Connection();
        targetView_ = nullptr;
        targetClass_ = nullptr;
        targetProperty_ = nullptr;
        sourceRef_ = BindingSource();
        sourceClass_ = nullptr;
        sourceProperty_ = nullptr;
        converterRef_ = nullptr;
        resolved_ = false;
        dirty_ = false;
        frame_ = nullptr;
        groupError_.clear();
    }

    bool Binding::resolve(Frame& frame) {
        unbind();
        lastError_.clear();
        errorKind_ = BindingErrorKind::None;
        frame_ = &frame;
        const std::string label = name().empty() ? ("binding " + target_ + "." + property_) : name();

        const bool validateOnly = mode_ == BindingMode::ValidateOnly;
        if (target_.empty() || property_.empty() || (source_.empty() && !validateOnly)) {
            return fail(label + ": target, property and source must all be set");
        }

        View* view = frame.rootView().findView(target_);
        if (view == nullptr) {
            return fail(label + ": no control named '" + target_ + "'");
        }
        const reflection::Class* tClass = classOf(*view);
        const reflection::Property* tProp = tClass != nullptr ? tClass->property(property_) : nullptr;
        if (tProp == nullptr) {
            return fail(label + ": '" + target_ + "' has no reflected property '" + property_ + "'");
        }

        BindingSource src;
        if (!validateOnly && !frame.findBindingSource(source_, src)) {
            if (View* sourceView = frame.rootView().findView(source_)) {
                src.object = dynamic_cast<void*>(sourceView);
                src.type = std::type_index(typeid(*sourceView));
                src.component = sourceView;
            }
            else {
                return fail(label + ": no binding source or control named '" + source_ + "'");
            }
        }

        const reflection::Class* sClass = nullptr;
        const reflection::Property* sProp = nullptr;
        if (!validateOnly && src.model == nullptr) {
            sClass = reflection::classinfo(src.type);
            sProp = sClass != nullptr && !key_.empty() ? sClass->property(key_) : nullptr;
            if (sProp == nullptr) {
                return fail(label + ": source '" + source_ + "' has no reflected property '" + key_ + "'");
            }
        }

        View* errView = nullptr;
        const reflection::Property* errProp = nullptr;
        if (!errorTarget_.empty()) {
            errView = frame.rootView().findView(errorTarget_);
            const reflection::Class* eClass = errView != nullptr ? classOf(*errView) : nullptr;
            errProp = eClass != nullptr ? eClass->property("text") : nullptr;
            if (errProp == nullptr) {
                return fail(label + ": no control with a 'text' property named '" + errorTarget_ + "'");
            }
        }

        const Converter* conv = nullptr;
        if (!converter_.empty()) {
            conv = frame.findConverter(converter_);
            if (conv == nullptr) {
                return fail(label + ": no converter named '" + converter_ + "'");
            }
        }

        targetView_ = view;
        targetClass_ = tClass;
        targetProperty_ = tProp;
        sourceRef_ = src;
        sourceClass_ = sClass;
        sourceProperty_ = sProp;
        converterRef_ = conv;
        errorView_ = errView;
        errorProperty_ = errProp;

        targetDestroyedConn_ = view->onDestroyed.add(this, &Binding::handleTargetDestroyed);
        if (mode_ == BindingMode::TwoWay || validateOnly) {
            targetChangedConn_ = view->onPropertyChanged.add(this, &Binding::handleTargetChanged);
            if (trigger_ == BindingTrigger::OnCommit) {
                targetLostFocusConn_ = view->onLostFocus.add(this, &Binding::handleTargetLostFocus);
                targetKeyDownConn_ = view->onKeyDown.add(this, &Binding::handleTargetKeyDown);
            }
        }
        if (mode_ != BindingMode::OneTime && !validateOnly) {
            if (src.model != nullptr) {
                sourceChangedConn_ = src.model->onChanged.add(this, &Binding::handleModelChanged);
            }
            else if (src.component != nullptr) {
                sourceChangedConn_ = src.component->onPropertyChanged.add(this, &Binding::handleSourceChanged);
            }
        }
        resolved_ = true;

        if (!refresh()) {
            // Connected (the source may fix itself), but report the first conversion's failure.
            return false;
        }
        return true;
    }

    bool Binding::readSource(std::any& out) {
        if (sourceRef_.model != nullptr) {
            std::any modelKey;
            if (!key_.empty()) {
                modelKey = key_;
            }
            out = sourceRef_.model->value(modelKey);
            return true;
        }
        try {
            out = sourceProperty_->get(sourceRef_.object);
        }
        catch (const std::exception& e) {
            return fail(std::string("cannot read '") + key_ + "': " + e.what());
        }
        return true;
    }

    bool Binding::writeSource(const std::any& value) {
        if (sourceRef_.model != nullptr) {
            std::any modelKey;
            if (!key_.empty()) {
                modelKey = key_;
            }
            sourceRef_.model->setValue(value, modelKey);
            return true;
        }
        try {
            sourceProperty_->set(sourceRef_.object, value);
        }
        catch (const std::exception& e) {
            return fail(std::string("cannot write '") + key_ + "': " + e.what());
        }
        return true;
    }

    std::type_index Binding::sourceType() const {
        if (sourceProperty_ != nullptr) {
            return sourceProperty_->type();
        }
        if (sourceRef_.model != nullptr) {
            std::any current;
            if (!key_.empty()) {
                current = key_;
            }
            const std::any value = sourceRef_.model->value(current);
            if (value.has_value()) {
                return std::type_index(value.type());
            }
        }
        return targetProperty_->type();
    }

    bool Binding::refresh() {
        if (!resolved_ || updating_) {
            return true;
        }
        if (mode_ == BindingMode::ValidateOnly) {
            // Nothing to load. Drops the user's edit marker but leaves the text alone.
            dirty_ = false;
            return true;
        }
        std::any sourceValue;
        if (!readSource(sourceValue)) {
            return false;
        }
        std::any converted;
        std::string error;
        const bool ok = converterRef_ != nullptr
            ? converterRef_->toTarget(sourceValue, targetProperty_->type(), converted, error)
            : convertValue(sourceValue, targetProperty_->type(), converted, error);
        if (!ok) {
            return fail("'" + source_ + "." + key_ + "' -> '" + target_ + "." + property_ + "': " + error);
        }
        updating_ = true;
        bool wrote = true;
        try {
            targetProperty_->set(instanceOf(*targetView_), converted);
        }
        catch (const std::exception& e) {
            wrote = false;
            error = e.what();
        }
        updating_ = false;
        if (!wrote) {
            return fail("cannot write '" + target_ + "." + property_ + "': " + error);
        }
        lastError_.clear();
        errorKind_ = BindingErrorKind::None;
        dirty_ = false;
        showError();
        return true;
    }

    // Steps one and two of the pipeline: target -> converted value -> rules.
    bool Binding::convertAndCheck(std::any& converted) {
        std::any targetValue;
        try {
            targetValue = targetProperty_->get(instanceOf(*targetView_));
        }
        catch (const std::exception& e) {
            return fail(std::string("cannot read '") + target_ + "." + property_ + "': " + e.what());
        }
        std::string error;
        const std::type_index srcType = sourceType();
        const bool ok = converterRef_ != nullptr
            ? converterRef_->toSource(targetValue, srcType, converted, error)
            : convertValue(targetValue, srcType, converted, error);
        if (!ok) {
            return fail(error, BindingErrorKind::Conversion);
        }
        for (const Rule* rule : rules_) {
            if (!rule->validate(converted, error)) {
                return fail(error, BindingErrorKind::Validation);
            }
        }
        lastError_.clear();
        errorKind_ = BindingErrorKind::None;
        showError();
        return true;
    }

    bool Binding::validate() {
        if (!resolved_ || updating_) {
            return true;
        }
        std::any converted;
        return convertAndCheck(converted);
    }

    bool Binding::commit() {
        if (!resolved_ || updating_) {
            return true;
        }
        std::any converted;
        if (!convertAndCheck(converted)) {
            return false;
        }
        if (mode_ == BindingMode::ValidateOnly) {
            dirty_ = false;  // checked, and there is nowhere to write it
            succeeded();
            return true;
        }
        updating_ = true;
        const bool wrote = writeSource(converted);
        updating_ = false;
        if (wrote) {
            dirty_ = false;
            succeeded();
        }
        return wrote;
    }

    bool Binding::pendingValue(std::any& out) {
        if (!resolved_ || updating_) {
            return false;
        }
        return convertAndCheck(out);
    }

    SyncReturn Binding::handleTargetChanged(Component& /*sender*/, const std::string& property) {
        if (property == property_ && !updating_) {
            dirty_ = true;
            if (trigger_ == BindingTrigger::OnChange) {
                commit();
            }
        }
        return SyncReturn::Handled;
    }

    SyncReturn Binding::handleSourceChanged(Component& /*sender*/, const std::string& property) {
        if (property == key_) {
            refresh();
        }
        return SyncReturn::Handled;
    }

    SyncReturn Binding::handleModelChanged(Model& /*sender*/) {
        // Model::onChanged doesn't say which key changed, so a sibling binding's commit lands here too:
        // don't overwrite a pending edit (it is dropped by an explicit refresh(), e.g. BindingGroup::revert()).
        if (!dirty_) {
            refresh();
        }
        return SyncReturn::Handled;
    }

    SyncReturn Binding::handleTargetLostFocus(View& /*sender*/) {
        if (dirty_) {
            commit();
        }
        return SyncReturn::Handled;
    }

    SyncReturn Binding::handleTargetKeyDown(View& /*sender*/, std::uint32_t keyMask, int /*keyCharVal*/,
                                            int /*repeatCount*/, std::uint32_t VKeyCode) {
        if (VKeyCode != vkReturn || (keyMask & (kmAlt | kmCtrl)) != 0 || !dirty_) {
            return SyncReturn::Ignored;
        }
        // Valid: written, and the key goes on to the default button. Invalid: keep the user in the field.
        return commit() ? SyncReturn::Ignored : SyncReturn::Handled;
    }

    // Mirrors the last conversion/validation outcome onto the target so it is drawn as invalid. Only
    // user-correctable problems show; a missing property or source is a setup error, not the user's.
    void Binding::showError() {
        if (targetView_ != nullptr) {
            const std::string text = isValid() ? groupError_ : lastError_;
            targetView_->setErrorText(text);
            setErrorLabel(text);
        }
    }

    void Binding::setGroupError(const std::string& text) {
        groupError_ = text;
        showError();
    }

    // After a commit that worked: the value is fine on its own, so let the group's rules judge it with the others.
    void Binding::succeeded() {
        if (frame_ != nullptr) {
            frame_->bindingGroup(group_).checkRules();
        }
    }

    void Binding::setErrorLabel(const std::string& text) {
        if (errorView_ == nullptr || errorProperty_ == nullptr) {
            return;
        }
        std::any value;
        std::string error;
        if (convertValue(text, errorProperty_->type(), value, error)) {
            try {
                errorProperty_->set(instanceOf(*errorView_), value);
            }
            catch (const std::exception&) {
            }
        }
    }

    SyncReturn Binding::handleTargetDestroyed(View& /*sender*/) {
        unbind();
        return SyncReturn::Handled;
    }

    // ---------------------------------------------------------------------
    // Binding groups
    // ---------------------------------------------------------------------

    namespace {

        std::map<std::string, GroupValidatorFunction>& groupValidators() {
            static std::map<std::string, GroupValidatorFunction> registry;
            return registry;
        }

    }

    bool GroupRule::reject(std::string& error, const std::string& fallback) const {
        error = message_.empty() ? fallback : message_;
        return false;
    }

    bool CompareRule::validate(BindingGroup& group, std::string& error) const {
        Binding* l = group.binding(left_);
        Binding* r = group.binding(right_);
        if (l == nullptr || r == nullptr) {
            return reject(error, "CompareRule: no binding named '" + (l == nullptr ? left_ : right_) + "' in the group");
        }
        std::any lv, rv;
        if (!l->pendingValue(lv) || !r->pendingValue(rv)) {
            return true;  // that binding's own error is already reported
        }
        int order = 0;  // <0 left sorts first
        double ln = 0, rn = 0;
        std::string lt, rt;
        if (numberOf(lv, ln) && numberOf(rv, rn)) {
            order = ln < rn ? -1 : (ln > rn ? 1 : 0);
        }
        else if (textOf(lv, lt) && textOf(rv, rt)) {
            order = lt.compare(rt);
        }
        else {
            return true;  // not comparable
        }
        bool ok = true;
        switch (op_) {
            case CompareOp::Equal: ok = order == 0; break;
            case CompareOp::NotEqual: ok = order != 0; break;
            case CompareOp::Less: ok = order < 0; break;
            case CompareOp::LessOrEqual: ok = order <= 0; break;
            case CompareOp::Greater: ok = order > 0; break;
            case CompareOp::GreaterOrEqual: ok = order >= 0; break;
        }
        return ok ? true : reject(error, "'" + left_ + "' and '" + right_ + "' do not match");
    }

    bool CustomGroupRule::validate(BindingGroup& group, std::string& error) const {
        const auto it = groupValidators().find(handler_);
        if (it == groupValidators().end()) {
            return reject(error, "No group validator named '" + handler_ + "'");
        }
        std::string reason;
        if (it->second(group, reason)) {
            return true;
        }
        return reject(error, reason.empty() ? "Not valid" : reason);
    }

    void registerGroupValidator(const std::string& name, GroupValidatorFunction function) {
        groupValidators()[name] = std::move(function);
    }

    void unregisterGroupValidator(const std::string& name) {
        groupValidators().erase(name);
    }

    BindingGroup::~BindingGroup() {
        for (GroupRule* rule : rules_) {
            delete rule;
        }
    }

    void BindingGroup::addRule(GroupRule* rule) {
        if (rule != nullptr && std::find(rules_.begin(), rules_.end(), rule) == rules_.end()) {
            rules_.push_back(rule);
        }
    }

    void BindingGroup::removeRule(GroupRule* rule) {
        rules_.erase(std::remove(rules_.begin(), rules_.end(), rule), rules_.end());
    }

    std::vector<Binding*> BindingGroup::bindings() const {
        std::vector<Binding*> members;
        if (frame_ != nullptr) {
            for (Binding* binding : frame_->bindings()) {
                if (binding->group() == name()) {
                    members.push_back(binding);
                }
            }
        }
        return members;
    }

    Binding* BindingGroup::binding(const std::string& bindingName) const {
        for (Binding* binding : bindings()) {
            if (binding->name() == bindingName) {
                return binding;
            }
        }
        return nullptr;
    }

    bool BindingGroup::load() {
        for (Binding* binding : bindings()) {
            binding->setGroupError(std::string());
        }
        bool all = true;
        for (Binding* binding : bindings()) {
            if (binding->isResolved() && !binding->refresh()) {
                all = false;
            }
        }
        errors_.clear();
        return all;
    }

    bool BindingGroup::dirty() const {
        for (const Binding* binding : bindings()) {
            if (binding->dirty()) {
                return true;
            }
        }
        return false;
    }

    bool BindingGroup::validate() {
        errors_.clear();
        for (Binding* binding : bindings()) {
            if (binding->isResolved() && !binding->validate()) {
                errors_.push_back(binding->lastError());
            }
        }
        // Cross-field rules only make sense once every field is individually fine.
        if (errors_.empty()) {
            runRules();
        }
        return errors_.empty();
    }

    // Runs the rules, adding failures to errors_ and showing each on the binding it blames.
    bool BindingGroup::runRules() {
        for (Binding* binding : bindings()) {
            binding->setGroupError(std::string());
        }
        bool ok = true;
        for (const GroupRule* rule : rules_) {
            std::string error;
            if (rule->validate(*this, error)) {
                continue;
            }
            ok = false;
            errors_.push_back(error);
            const std::string blame = rule->target().empty() ? rule->defaultTarget() : rule->target();
            if (Binding* binding = this->binding(blame)) {
                binding->setGroupError(error);
            }
        }
        return ok;
    }

    bool BindingGroup::checkRules() {
        if (committing_ || rules_.empty()) {
            return true;
        }
        errors_.clear();
        return runRules();
    }

    bool BindingGroup::commit() {
        if (!validate()) {
            return false;
        }
        committing_ = true;
        bool all = true;
        for (Binding* binding : bindings()) {
            if (binding->isResolved() && binding->dirty() && !binding->commit()) {
                errors_.push_back(binding->lastError());
                all = false;
            }
        }
        committing_ = false;
        return all;
    }

}
