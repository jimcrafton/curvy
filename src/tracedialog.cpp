#include "curvy/tracedialog.h"

#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/models.h>
#include <newui/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace curvy {

    using namespace newui;

    namespace {
        bool parseNumber(const std::wstring& text, double& out) {
            const std::string narrow = wideToUtf8(text);
            char* end = nullptr;
            const double value = std::strtod(narrow.c_str(), &end);
            if (end == narrow.c_str() || !std::isfinite(value)) {
                return false;
            }
            out = value;
            return true;
        }

        std::wstring whole(int value) { return utf8ToWide(std::to_string(value)); }

        // One run of the dialog: finds the controls once, shows `options` in them and reads them back.
        class Session {
        public:
            Session(RootView& root, TraceOptions& options, const std::string& picture) : root_(root), options_(options) {
                if (auto* name = dynamic_cast<Label*>(root_.findView("tracePictureLabel"))) {
                    name->setText(picture);
                }
                colors_ = dynamic_cast<TextField*>(root_.findView("traceColorsField"));
                size_ = dynamic_cast<TextField*>(root_.findView("traceSizeField"));
                specks_ = dynamic_cast<TextField*>(root_.findView("traceSpecksField"));
                smoothness_ = dynamic_cast<DropDownList*>(root_.findView("traceSmoothnessCombo"));
                white_ = dynamic_cast<DropDownList*>(root_.findView("traceWhiteCombo"));
                show();
            }

            // Puts the options in the controls.
            void show() {
                if (colors_ != nullptr) { colors_->setText(whole(options_.colors)); }
                if (size_ != nullptr) { size_->setText(whole(options_.maxSize)); }
                if (specks_ != nullptr) { specks_->setText(whole(int(std::lround(options_.minArea)))); }
                if (smoothness_ != nullptr) { smoothness_->setSelectedIndex(std::size_t(TraceDialog::smoothnessFor(options_.fitTolerance))); }
                if (white_ != nullptr) { white_->setSelectedIndex(options_.ignoreWhite ? 1u : 0u); }
            }

            // Reads the controls back; a field that is not a number keeps its value, and every number is brought into range.
            void commit() {
                double value = 0.0;
                if (colors_ != nullptr && parseNumber(colors_->text(), value)) { options_.colors = int(std::lround(std::min(32.0, std::max(2.0, value)))); }
                if (size_ != nullptr && parseNumber(size_->text(), value)) { options_.maxSize = int(std::lround(std::min(2000.0, std::max(50.0, value)))); }
                if (specks_ != nullptr && parseNumber(specks_->text(), value)) { options_.minArea = std::min(1000.0, std::max(0.0, std::floor(value))); }
                if (smoothness_ != nullptr && smoothness_->selectedIndex().has_value()) {
                    options_.fitTolerance = TraceDialog::toleranceFor(int(*smoothness_->selectedIndex()));
                }
                if (white_ != nullptr && white_->selectedIndex().has_value()) { options_.ignoreWhite = *white_->selectedIndex() == 1u; }
                show();
            }

            void wire(const std::function<void(DialogResult)>& closeWith) {
                for (TextField* field : {colors_, size_, specks_}) {
                    if (field != nullptr) {
                        field->onReturnPressed += [this](TextField&) { commit(); return SyncReturn::Handled; };
                        field->onLostFocus += [this](View&) { commit(); return SyncReturn::Handled; };
                    }
                }
                if (auto* ok = dynamic_cast<Button*>(root_.findView("traceButton"))) {
                    ok->onClick += [this, closeWith](Control&) {
                        commit();
                        closeWith(DialogResult::Ok);
                        return SyncReturn::Handled;
                    };
                }
                if (auto* cancel = dynamic_cast<Button*>(root_.findView("traceCancelButton"))) {
                    cancel->onClick += [closeWith](Control&) {
                        closeWith(DialogResult::Cancel);
                        return SyncReturn::Handled;
                    };
                }
            }

            RootView& root() { return root_; }

        private:
            RootView& root_;
            TraceOptions& options_;
            TextField* colors_ = nullptr;
            TextField* size_ = nullptr;
            TextField* specks_ = nullptr;
            DropDownList* smoothness_ = nullptr;
            DropDownList* white_ = nullptr;
        };
    }

    double TraceDialog::toleranceFor(int smoothness) {
        static const double kTolerances[] = {0.35, 0.7, 1.4};
        return kTolerances[std::min(2, std::max(0, smoothness))];
    }

    int TraceDialog::smoothnessFor(double tolerance) {
        return tolerance < 0.5 ? 0 : (tolerance > 1.0 ? 2 : 1);
    }

    bool TraceDialog::run(Frame& owner, TraceOptions& options, const std::string& picture) {
        Dialog dialog;
        dialog.setName("tracedialog");
        if (!Bundle::instance().loadDialog(dialog)) {
            return false;
        }
        TraceOptions working = options;
        Session session(dialog.rootView(), working, picture);
        session.wire([&dialog](DialogResult result) { dialog.close(result); });
        if (dialog.showModal(&owner) != DialogResult::Ok) {
            return false;
        }
        options = working;
        return true;
    }

    int TraceDialog::check() {
        int failures = 0;
        auto expect = [&failures](bool ok, const char* what) {
            std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        Dialog dialog;
        dialog.setName("tracedialog");
        expect(Bundle::instance().loadDialog(dialog), "tracedialog.newui loads into a Dialog");
        TraceOptions options;
        options.colors = 12;
        options.maxSize = 600;
        options.minArea = 10.0;
        options.fitTolerance = 1.4;
        options.ignoreWhite = true;
        Session session(dialog.rootView(), options, "sample.png");
        bool closed = false;
        DialogResult result = DialogResult::Cancel;
        session.wire([&](DialogResult r) { closed = true; result = r; });
        dialog.rootView().updateLayout();
        auto field = [&session](const char* name) { return dynamic_cast<TextField*>(session.root().findView(name)); };
        auto combo = [&session](const char* name) { return dynamic_cast<DropDownList*>(session.root().findView(name)); };
        auto button = [&session](const char* name) { return dynamic_cast<Button*>(session.root().findView(name)); };
        expect(field("traceColorsField") && field("traceSizeField") && field("traceSpecksField") && combo("traceSmoothnessCombo") && combo("traceWhiteCombo") &&
            button("traceButton") && button("traceCancelButton"), "all dialog controls found");
        if (!(field("traceColorsField") && field("traceSizeField") && field("traceSpecksField") && combo("traceSmoothnessCombo") && combo("traceWhiteCombo") &&
            button("traceButton") && button("traceCancelButton"))) {
            return failures;
        }
        expect(wideToUtf8(field("traceColorsField")->text()) == "12" && wideToUtf8(field("traceSizeField")->text()) == "600" &&
            wideToUtf8(field("traceSpecksField")->text()) == "10", "it shows the options it was given");
        expect(combo("traceSmoothnessCombo")->selectedIndex() == std::optional<std::size_t>(2) && combo("traceWhiteCombo")->selectedIndex() == std::optional<std::size_t>(1),
            "and the smoothness and the white choice");
        field("traceColorsField")->setText(L"99");
        field("traceSizeField")->setText(L"10");
        field("traceSpecksField")->setText(L"abc");
        combo("traceSmoothnessCombo")->setSelectedIndex(0);
        combo("traceWhiteCombo")->setSelectedIndex(0);
        button("traceButton")->onClick(*button("traceButton"));
        expect(closed && result == DialogResult::Ok, "Trace closes the dialog with OK");
        expect(options.colors == 32 && options.maxSize == 50, "numbers are brought into range");
        expect(options.minArea == 10.0, "a field that is not a number keeps its value");
        expect(std::fabs(options.fitTolerance - 0.35) < 1e-9 && !options.ignoreWhite, "the choices are read back");
        closed = false;
        button("traceCancelButton")->onClick(*button("traceCancelButton"));
        expect(closed && result == DialogResult::Cancel, "Cancel closes it with Cancel");
        expect(TraceDialog::smoothnessFor(TraceDialog::toleranceFor(0)) == 0 && TraceDialog::smoothnessFor(TraceDialog::toleranceFor(1)) == 1 &&
            TraceDialog::smoothnessFor(TraceDialog::toleranceFor(2)) == 2, "smoothness and tolerance map both ways");
        return failures;
    }

}
