#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <type_traits>

// Texts the user reads (error messages, undo labels, default names) are
// written in Spanish and translated by the interface, which knows the
// language; the logic has no Qt. Each one is a template marked with
// LogicText::tr, so Qt's lupdate collects it (context "LogicText"), and text()
// translates it through the hook the interface installs and fills in its
// %1, %2…:
//
//     throw std::logic_error(text(LogicText::tr("La caja «%1» ya existe."), name));
//
// Without a hook (the logic's own tests, the benchmarks) texts stay in Spanish.

// Only marks the template: it returns it untouched. Kept outside any
// namespace so lupdate always gives it the same context.
struct LogicText {
    static const char* tr(const char* source) { return source; }
};

namespace hypergraph_logic {

    // Turns a Spanish template into the current language.
    using Translator = std::function<std::string(const char* source)>;

    // Installed by the interface; an empty one restores Spanish.
    void setTranslator(Translator translator);

    // The template in the current language, %1, %2… replaced by the values in
    // order (%1 is the first). A value is a string or a number.
    std::string text(const char* source, std::initializer_list<std::string> values = {});

    template <typename... Values>
        requires (sizeof...(Values) > 0)
    std::string text(const char* source, const Values&... values) {
        auto toString = [](const auto& v) -> std::string {
            if constexpr (std::is_arithmetic_v<std::decay_t<decltype(v)>>) return std::to_string(v);
            else return std::string(v);
        };
        return text(source, { toString(values)... });
    }

} // namespace hypergraph_logic
