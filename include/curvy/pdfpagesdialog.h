#pragma once

#include <newui/frame.h>

#include <string>
#include <vector>

namespace curvy {

    // Asked before a PDF with more than one page is imported (Resources/pdfpagesdialog.newui, from mockup/pdfpagesdialog.html): all pages,
    // the first N, or a list such as "1,5,10" or "2-4, 9". Runs modally over `owner`.
    // @reflect ignore=true
    class PdfPagesDialog {
    public:
        enum Mode { All = 0, First = 1, Listed = 2 };

        // True when the user pressed Import; `pages` then holds the page numbers (empty for all pages). False on Cancel / close or if the
        // dialog file cannot be loaded.
        static bool run(newui::Frame& owner, const std::string& fileName, std::size_t pageCount, std::vector<int>& pages);

        // What a choice in the dialog means: the page numbers for `mode` and the text of its field (empty for All). False, with `problem`
        // saying what is wrong, when the text cannot be used.
        static bool pagesFor(Mode mode, const std::string& text, std::size_t pageCount, std::vector<int>& pages, std::string& problem);

        // Headless self-test (curvy.exe --check-newdoc runs it too): loads the dialog without showing it and drives the controls.
        // Returns the number of failed checks.
        static int check();
    };

}
