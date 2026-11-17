#include "curvy/wranglecheck.h"

#include <jitnodes/jit/exprParser.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace curvy {

    namespace {

        constexpr const char* kPrefix = "wrangle parse error at offset ";

        // A UTF-8 continuation byte does not start a character.
        bool continuation(char c) {
            return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
        }

    }

    const std::vector<WrangleFunction>& wrangleFunctions() {
        static const std::vector<WrangleFunction> functions = {
            {"min", 2}, {"max", 2}, {"abs", 1}, {"sqrt", 1}, {"clamp", 3}, {"lerp", 3}, {"floor", 1},
            {"ceil", 1}, {"sin", 1}, {"cos", 1}, {"exp", 1}, {"log", 1}, {"pow", 2},
        };
        return functions;
    }

    std::optional<SyntaxProblem> checkWrangle(const std::string& code) {
        try {
            std::vector<jitnodes::WrangleParam> declared;
            jitnodes::parseWrangle(code, {"a", "b", "c", "d"}, {}, &declared);
            return std::nullopt;
        } catch (const std::runtime_error& error) {
            std::string message = error.what();
            std::size_t offset = code.size();
            if (message.compare(0, std::char_traits<char>::length(kPrefix), kPrefix) == 0) {
                const std::size_t digits = std::char_traits<char>::length(kPrefix);
                const std::size_t colon = message.find(':', digits);
                if (colon != std::string::npos) {
                    offset = static_cast<std::size_t>(std::strtoull(message.c_str() + digits, nullptr, 10));
                    message = message.substr(colon + 1);
                    if (!message.empty() && message.front() == ' ') {
                        message.erase(0, 1);
                    }
                }
            }
            offset = (std::min)(offset, code.size());
            SyntaxProblem problem;
            problem.message = message;
            int line = 1;
            int column = 1;
            for (std::size_t i = 0; i < offset; ++i) {
                if (code[i] == '\n') {
                    ++line;
                    column = 1;
                } else if (!continuation(code[i])) {
                    ++column;
                }
            }
            problem.line = problem.endLine = line;
            problem.column = problem.endColumn = column;
            return problem;
        }
    }

}
