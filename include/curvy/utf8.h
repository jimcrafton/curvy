#pragma once

#include <string>

namespace curvy {

    // UTF-8 text (what files and Python use) to the UTF-16 the text controls hold, and back. Invalid input becomes U+FFFD, never an error.
    std::wstring utf8ToWide(const std::string& text);
    std::string wideToUtf8(const std::wstring& text);

}
