#include "Text.h"

#include <utility>
#include <vector>

namespace hypergraph_logic {

    namespace {
        Translator& translator() {
            static Translator current;
            return current;
        }
    }

    void setTranslator(Translator translator_function) {
        translator() = std::move(translator_function);
    }

    std::string text(const char* source, std::initializer_list<std::string> values) {
        const std::string pattern = translator() ? translator()(source) : std::string(source);
        const std::vector<std::string> args(values);
        // %1…%9, like QString::arg; anything else is copied as it is.
        std::string out;
        out.reserve(pattern.size());
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9') {
                const std::size_t index = static_cast<std::size_t>(pattern[i + 1] - '1');
                if (index < args.size()) {
                    out += args[index];
                    ++i;
                    continue;
                }
            }
            out += pattern[i];
        }
        return out;
    }

} // namespace hypergraph_logic
