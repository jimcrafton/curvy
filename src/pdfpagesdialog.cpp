#include "curvy/pdfpagesdialog.h"
#include "curvy/io/pdf/pdfimport.h"

#ifdef CURVY_GUI_AUTOMATION
#include <newui/automation.h>
#endif
#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/models.h>
#include <newui/utils.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

namespace curvy {

    using namespace newui;

    namespace {
        class Session {
        public:
            Session(RootView& root, const std::string& fileName, std::size_t pageCount) : root_(root), pageCount_(pageCount) {
                if (auto* name = dynamic_cast<Label*>(root_.findView("pagesFileLabel"))) { name->setText(fileName); }
                if (auto* count = dynamic_cast<Label*>(root_.findView("pagesCountLabel"))) { count->setText(std::to_string(pageCount)); }
                mode_ = dynamic_cast<DropDownList*>(root_.findView("pagesModeCombo"));
                value_ = dynamic_cast<TextField*>(root_.findView("pagesValueField"));
                caption_ = dynamic_cast<Label*>(root_.findView("pagesValueCaption"));
                hint_ = dynamic_cast<Label*>(root_.findView("pagesHint"));
                if (mode_ != nullptr) {
                    auto model = std::make_unique<StringListModel>();
                    for (const char* item : {"All pages", "First pages", "Specific pages"}) { model->addItem(item); }
                    mode_->setModel(std::move(model));
                    mode_->setSelectedIndex(0);
                }
                follow();
            }

            PdfPagesDialog::Mode mode() const {
                const std::optional<std::size_t> index = mode_ != nullptr ? mode_->selectedIndex() : std::nullopt;
                return index.has_value() && *index <= 2 ? PdfPagesDialog::Mode(*index) : PdfPagesDialog::All;
            }

            // Shows what the chosen mode asks for in the caption, the field and the hint.
            void follow() {
                const PdfPagesDialog::Mode m = mode();
                if (caption_ != nullptr) { caption_->setText(m == PdfPagesDialog::Listed ? "Pages" : "Number of pages"); }
                if (value_ != nullptr) {
                    value_->setEnabled(m != PdfPagesDialog::All);
                    if (m == PdfPagesDialog::First && std::string(wideToUtf8(value_->text())).find_first_not_of("0123456789") != std::string::npos) {
                        value_->setText(L"1");
                    }
                    if (m == PdfPagesDialog::Listed && wideToUtf8(value_->text()).find_first_not_of("0123456789") == std::string::npos) {
                        value_->setText(L"1");
                    }
                }
                if (hint_ != nullptr) {
                    hint_->setText(m == PdfPagesDialog::All ? "Every page of the file becomes an artboard."
                        : m == PdfPagesDialog::First ? "The first pages of the file, from page 1."
                        : "List page numbers and ranges: 1,5,10 or 2-4,9.");
                }
            }

            bool choice(std::vector<int>& pages, std::string& problem) const {
                return PdfPagesDialog::pagesFor(mode(), value_ != nullptr ? wideToUtf8(value_->text()) : std::string(), pageCount_, pages, problem);
            }

            void wire(const std::function<void(DialogResult)>& closeWith, const std::function<void(const std::string&)>& complain, std::vector<int>& pages) {
                if (mode_ != nullptr) {
                    mode_->onSelectionChanged += [this](DropDownList&) { follow(); return SyncReturn::Handled; };
                }
                if (auto* ok = dynamic_cast<Button*>(root_.findView("pagesImportButton"))) {
                    ok->onClick += [this, closeWith, complain, &pages](Control&) {
                        std::string problem;
                        if (choice(pages, problem)) {
                            closeWith(DialogResult::Ok);
                        } else {
                            complain(problem);
                        }
                        return SyncReturn::Handled;
                    };
                }
                if (auto* cancel = dynamic_cast<Button*>(root_.findView("pagesCancelButton"))) {
                    cancel->onClick += [closeWith](Control&) { closeWith(DialogResult::Cancel); return SyncReturn::Handled; };
                }
            }

            RootView& root() { return root_; }
            DropDownList* modeBox() { return mode_; }
            TextField* valueField() { return value_; }

        private:
            RootView& root_;
            std::size_t pageCount_;
            DropDownList* mode_ = nullptr;
            TextField* value_ = nullptr;
            Label* caption_ = nullptr;
            Label* hint_ = nullptr;
        };
    }

    bool PdfPagesDialog::pagesFor(Mode mode, const std::string& text, std::size_t pageCount, std::vector<int>& pages, std::string& problem) {
        problem.clear();
        if (mode == All) {
            pages.clear();
            return true;
        }
        if (mode == First) {
            std::vector<int> listed;
            if (!parsePageList("1-" + (text.empty() ? std::string("1") : text), pageCount, listed, nullptr)) {
                if (text.find_first_not_of(" 0123456789") != std::string::npos || text.find_first_of("0123456789") == std::string::npos) {
                    problem = "Enter how many pages to import, for example 5.";
                } else {
                    problem = "The file has " + std::to_string(pageCount) + " pages; enter a number from 1 to " + std::to_string(pageCount) + ".";
                }
                return false;
            }
            pages = std::move(listed);
            return true;
        }
        std::string why;
        if (!parsePageList(text, pageCount, pages, &why)) {
            problem = "The page list " + why + ".";
            return false;
        }
        return true;
    }

    bool PdfPagesDialog::run(Frame& owner, const std::string& fileName, std::size_t pageCount, std::vector<int>& pages) {
        Dialog dialog;
        dialog.setName("pdfpagesdialog");
        if (!Bundle::instance().loadDialog(dialog)) {
            return false;
        }
        std::vector<int> chosen;
        Session session(dialog.rootView(), fileName, pageCount);
        HWND window = owner.frameHandle();
        session.wire([&dialog](DialogResult result) { dialog.close(result); },
            [window](const std::string& problem) { Dialog::showMessageBox(window, problem, "Import PDF", MessageBoxButtons::Ok, MessageBoxIcon::Warning); }, chosen);
#ifdef CURVY_GUI_AUTOMATION
        dialog.rootView().setName("pdfPagesRoot");   // the workspace's own root is "rootview" too, and the driver finds the first
        Automation::expose(dialog.rootView());   // so a GUI test can reach the dialog's views while it is open
#endif
        if (dialog.showModal(&owner) != DialogResult::Ok) {
            return false;
        }
        pages = chosen;
        return true;
    }

    int PdfPagesDialog::check() {
        int failures = 0;
        auto expect = [&failures](bool ok, const char* what) {
            std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        std::vector<int> pages;
        std::string problem;
        expect(pagesFor(All, "", 20, pages, problem) && pages.empty(), "All pages means no list");
        expect(pagesFor(First, "3", 20, pages, problem) && pages == std::vector<int>({1, 2, 3}), "First 3 pages");
        expect(!pagesFor(First, "30", 20, pages, problem) && !problem.empty(), "More pages than the file has is refused");
        expect(!pagesFor(First, "abc", 20, pages, problem), "Text that is not a number is refused for First");
        expect(pagesFor(Listed, "1,5,10", 20, pages, problem) && pages == std::vector<int>({1, 5, 10}), "A list of pages");
        expect(pagesFor(Listed, "9, 2-4, 3", 20, pages, problem) && pages == std::vector<int>({2, 3, 4, 9}), "Ranges merge and sort");
        expect(!pagesFor(Listed, "21", 20, pages, problem), "A page past the end is refused");

        Dialog dialog;
        dialog.setName("pdfpagesdialog");
        expect(Bundle::instance().loadDialog(dialog), "pdfpagesdialog.newui loads into a Dialog");
        Session session(dialog.rootView(), "sample.pdf", 20);
        std::vector<int> chosen;
        bool closed = false;
        DialogResult result = DialogResult::Cancel;
        std::string complaint;
        session.wire([&](DialogResult r) { closed = true; result = r; }, [&](const std::string& p) { complaint = p; }, chosen);
        dialog.rootView().updateLayout();
        auto button = [&session](const char* name) { return dynamic_cast<Button*>(session.root().findView(name)); };
        expect(session.modeBox() != nullptr && session.valueField() != nullptr && button("pagesImportButton") && button("pagesCancelButton"), "all dialog controls found");
        if (session.modeBox() == nullptr || session.valueField() == nullptr || button("pagesImportButton") == nullptr || button("pagesCancelButton") == nullptr) {
            return failures;
        }
        button("pagesImportButton")->onClick(*button("pagesImportButton"));
        expect(closed && result == DialogResult::Ok && chosen.empty(), "Import with All pages closes with OK and no list");
        closed = false;
        session.modeBox()->setSelectedIndex(2);
        session.valueField()->setText(L"2, 7-8");
        button("pagesImportButton")->onClick(*button("pagesImportButton"));
        expect(closed && chosen == std::vector<int>({2, 7, 8}), "Specific pages are read from the field");
        closed = false;
        session.valueField()->setText(L"99");
        button("pagesImportButton")->onClick(*button("pagesImportButton"));
        expect(!closed && !complaint.empty(), "A bad list keeps the dialog open and says why");
        button("pagesCancelButton")->onClick(*button("pagesCancelButton"));
        expect(closed && result == DialogResult::Cancel, "Cancel closes it with Cancel");
        return failures;
    }

}
