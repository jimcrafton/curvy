#include <lex/python_language.h>

#include <lex/python_lexer.h>

namespace lex {

namespace {

class PythonLanguage final : public Language {
public:
    const wchar_t* name() const noexcept override { return L"Python"; }

    std::unique_ptr<LexerBase> createLexer(std::wstring_view text, LexerOptions options) const override {
        return std::make_unique<PythonLexer>(text, options);
    }

    void styleLine(std::wstring_view text, const Token* tokens, std::size_t count, StyleId* out) const override {
        Language::styleLine(text, tokens, count, out);
        bool afterDefinition = false;
        for (std::size_t i = 0; i < count; ++i) {
            const Token& token = tokens[i];
            if (isTrivia(token.cls)) {
                continue;
            }
            if (afterDefinition && token.kind == python::Identifier) {
                out[i] = StyleId::PropertyName;
            }
            const std::wstring_view word = text.substr(token.offset, token.length);
            afterDefinition = token.kind == python::Keyword && (word == L"def" || word == L"class");
        }
    }
};

}  // namespace

const Language& pythonLanguage() {
    static const PythonLanguage language;
    return language;
}

}  // namespace lex
