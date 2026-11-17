#include "curvy/codelanguage.h"

namespace curvy {

    std::shared_ptr<const newui::TextStyleSheet> CodeLanguage::styleSheet(bool, const std::map<std::string, std::uint32_t>&) const {
        return nullptr;
    }

    std::vector<newui::text::TextStyleRange> CodeLanguage::styleRanges(const std::wstring&) const {
        return {};
    }

    std::wstring CodeLanguage::newLineIndent(const std::wstring& lineBeforeCaret, std::size_t) const {
        const std::size_t first = lineBeforeCaret.find_first_not_of(L" \t");
        return lineBeforeCaret.substr(0, first == std::wstring::npos ? lineBeforeCaret.size() : first);
    }

    std::vector<newui::text::TextFold> CodeLanguage::folds(const std::wstring&) const {
        return {};
    }

}
