#include "curvy/cursorset.h"

#include <cctype>
#include <cstdlib>

namespace curvy {

    namespace {
        // A cursor over `text`: just enough JSON for { "name": { "key": number, ... }, ... }.
        struct Scanner {
            const std::string& text;
            std::size_t pos = 0;

            void skipSpace() {
                while (pos < text.size() && std::isspace((unsigned char)text[pos])) {
                    ++pos;
                }
            }
            bool eat(char c) {
                skipSpace();
                if (pos < text.size() && text[pos] == c) {
                    ++pos;
                    return true;
                }
                return false;
            }
            bool readString(std::string& out) {
                skipSpace();
                if (pos >= text.size() || text[pos] != '"') {
                    return false;
                }
                std::size_t end = text.find('"', pos + 1);
                if (end == std::string::npos) {
                    return false;
                }
                out = text.substr(pos + 1, end - pos - 1);
                pos = end + 1;
                return true;
            }
            bool readNumber(double& out) {
                skipSpace();
                char* end = nullptr;
                out = std::strtod(text.c_str() + pos, &end);
                if (end == text.c_str() + pos) {
                    return false;
                }
                pos = std::size_t(end - text.c_str());
                return true;
            }
        };
    }

    std::map<std::string, CursorHotspot> parseCursorHotspots(const std::string& json) {
        std::map<std::string, CursorHotspot> result;
        Scanner in{json};
        if (!in.eat('{')) {
            return result;
        }
        while (true) {
            std::string name;
            if (!in.readString(name) || !in.eat(':') || !in.eat('{')) {
                break;
            }
            CursorHotspot spot;
            bool good = true;
            while (!in.eat('}')) {
                std::string key;
                double value = 0.0;
                if (!in.readString(key) || !in.eat(':') || !in.readNumber(value)) {
                    good = false;
                    break;
                }
                if (key == "x") { spot.x = int(value); }
                else if (key == "y") { spot.y = int(value); }
                else if (key == "size") { spot.size = int(value); }
                in.eat(',');
            }
            if (!good) {
                break;
            }
            result[name] = spot;
            if (!in.eat(',')) {
                break;
            }
        }
        return result;
    }

}
