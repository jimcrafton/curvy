#include "curvy/editorfonts.h"

#include <newui/fontmanager.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace curvy {

    namespace {

        std::string lowered(const std::string& text) {
            std::string out = text;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return out;
        }

        bool contains(const std::string& text, const char* part) {
            return text.find(part) != std::string::npos;
        }

        bool endsWith(const std::string& text, const char* suffix) {
            const std::string tail(suffix);
            return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
        }

        bool looksMonospace(const std::string& name) {
            const std::string lower = lowered(name);
            // Italics are no use for code ("...Italic", "...Oblique", and Source Code Pro's "...It"); Monotype Corsiva is a script face.
            if (contains(lower, "italic") || contains(lower, "oblique") || contains(lower, "monotype") || endsWith(lower, "it")) {
                return false;
            }
            for (const char* part : {"mono", "code", "consol", "courier", "hack", "fixed", "terminal", "menlo", "typewriter", "inconsolata"}) {
                if (contains(lower, part)) {
                    return true;
                }
            }
            return false;
        }

    }

    std::vector<std::string> installedMonospaceFonts() {
        std::set<std::string> names;
        for (const newui::SystemFontInfo& info : newui::FontManager::listFonts()) {
            if (looksMonospace(info.name)) {
                names.insert(info.name);
            }
        }
        return std::vector<std::string>(names.begin(), names.end());
    }

    std::string automaticEditorFont() {
        static const char* const preferred[] = {"JetBrainsMono-Medium", "Hack-Regular", "SourceCodePro-Medium", "FiraCode-Medium", "Fira Mono", "Consolas"};
        for (const char* name : preferred) {
            if (newui::FontManager::isAvailable(name)) {
                return name;
            }
        }
        return newui::FontManager::monospaceFont().name();
    }

    newui::Font scriptEditorFont(const std::string& chosen, int size) {
        const float points = float(size);
        if (!chosen.empty() && newui::FontManager::isAvailable(chosen)) {
            return newui::Font(chosen, points);
        }
        return newui::Font(automaticEditorFont(), points);
    }

}
