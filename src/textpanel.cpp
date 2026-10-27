#include "curvy/textpanel.h"

#include "curvy/numberscrubber.h"

#include <newui/fontmanager.h>
#include <newui/models.h>
#include <newui/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace curvy {

    namespace {
        // 12.5 -> "12.5", 24 -> "24" (up to two decimals, trailing zeros dropped).
        std::wstring numberText(double value) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.2f", value);
            std::string text = buffer;
            while (!text.empty() && text.back() == '0') { text.pop_back(); }
            if (!text.empty() && text.back() == '.') { text.pop_back(); }
            if (text == "-0" || text.empty()) { text = "0"; }
            return newui::utf8ToWide(text);
        }

        bool parseNumber(const std::wstring& text, double& value) {
            std::string narrow = newui::wideToUtf8(text);
            char* end = nullptr;
            double parsed = std::strtod(narrow.c_str(), &end);
            if (end == narrow.c_str() || !std::isfinite(parsed)) {
                return false;
            }
            value = parsed;
            return true;
        }
    }

    TextPanel::TextPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        font_ = dynamic_cast<newui::DropDownList*>(root_.findView("textFontCombo"));
        size_ = dynamic_cast<newui::TextField*>(root_.findView("textSizeField"));
        tracking_ = dynamic_cast<newui::TextField*>(root_.findView("textTrackingField"));
        leading_ = dynamic_cast<newui::TextField*>(root_.findView("textLeadingField"));
        pathStart_ = dynamic_cast<newui::TextField*>(root_.findView("textPathStartField"));
        pathFlip_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("textPathFlipButton"));
        bold_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("textBoldButton"));
        italic_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("textItalicButton"));
        const char* alignNames[3] = {"textAlignLeftButton", "textAlignCenterButton", "textAlignRightButton"};
        for (int i = 0; i < 3; ++i) {
            align_[i] = dynamic_cast<newui::ToolbarButton*>(root_.findView(alignNames[i]));
        }

        // Every font the system offers, once each and in order.
        for (const newui::SystemFontInfo& info : newui::FontManager::listFonts()) {
            fonts_.push_back(info.name);
        }
        std::sort(fonts_.begin(), fonts_.end());
        fonts_.erase(std::unique(fonts_.begin(), fonts_.end()), fonts_.end());
        if (font_ != nullptr) {
            auto model = std::make_unique<newui::StringListModel>();
            model->items() = fonts_;
            font_->setModel(std::move(model));
            font_->onSelectionChanged += [this](newui::DropDownList&) {
                applyFont();
                return newui::SyncReturn::Handled;
            };
        }
        if (size_ != nullptr) {
            size_->onReturnPressed += [this](newui::TextField&) {
                applySize();
                return newui::SyncReturn::Handled;
            };
        }
        if (tracking_ != nullptr) {
            tracking_->onReturnPressed += [this](newui::TextField&) {
                double value;
                if (!updating_ && parseNumber(tracking_->text(), value)) {
                    applyStyle("Tracking", [value](TextStyle& style) { style.tracking = std::max(-50.0, std::min(500.0, value)); });
                }
                refresh();
                return newui::SyncReturn::Handled;
            };
        }
        if (leading_ != nullptr) {
            leading_->onReturnPressed += [this](newui::TextField&) {
                double value;
                if (!updating_ && parseNumber(leading_->text(), value)) {
                    applyStyle("Leading", [value](TextStyle& style) { style.leading = std::max(0.2, std::min(10.0, value / 100.0)); });
                }
                refresh();
                return newui::SyncReturn::Handled;
            };
        }
        auto toggleHandler = [this](newui::ToolbarButton* button, const char* what, std::function<void(TextStyle&, bool)> set) {
            if (button == nullptr) {
                return;
            }
            button->onCheckedChanged += [this, what, set](newui::ToolbarButton& changed) {
                if (updating_) {
                    return newui::SyncReturn::Ignored;
                }
                bool on = changed.isChecked();
                applyStyle(what, [set, on](TextStyle& style) { set(style, on); });
                refresh();
                return newui::SyncReturn::Handled;
            };
        };
        toggleHandler(bold_, "Bold", [](TextStyle& style, bool on) { style.bold = on; });
        toggleHandler(italic_, "Italic", [](TextStyle& style, bool on) { style.italic = on; });
        for (int i = 0; i < 3; ++i) {
            toggleHandler(align_[i], "Align Text", [i](TextStyle& style, bool on) {
                if (on) { style.align = i; }
            });
        }
        if (pathStart_ != nullptr) {
            pathStart_->onReturnPressed += [this](newui::TextField&) { applyPathOptions(); return newui::SyncReturn::Handled; };
            pathStart_->onLostFocus += [this](newui::View&) { applyPathOptions(); return newui::SyncReturn::Handled; };
        }
        if (pathFlip_ != nullptr) {
            pathFlip_->onCheckedChanged += [this](newui::ToolbarButton&) {
                if (!updating_) { applyPathOptions(); }
                return newui::SyncReturn::Handled;
            };
        }
        bindScrub("textSizeLabel", Scrubbed::Size, 1.0, 1.0, 1000.0);
        bindScrub("textTrackingLabel", Scrubbed::Tracking, 1.0, -50.0, 500.0);
        bindScrub("textLeadingLabel", Scrubbed::Leading, 1.0, 20.0, 1000.0);
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    const SketchObject* TextPanel::firstText() const {
        for (std::size_t id : document_.selection()) {
            const SketchObject* object = document_.findById(id);
            if (object != nullptr && object->isText()) {
                return object;
            }
        }
        return nullptr;
    }

    void TextPanel::refresh() {
        const SketchObject* text = firstText();
        updating_ = true;
        auto enable = [text](newui::Control* control) {
            if (control != nullptr) {
                control->setEnabled(text != nullptr);
            }
        };
        enable(font_);
        enable(size_);
        enable(tracking_);
        enable(leading_);
        enable(bold_);
        enable(italic_);
        for (newui::ToolbarButton* button : align_) {
            enable(button);
        }
        if (text != nullptr) {
            const TextStyle& style = text->textStyle();
            if (font_ != nullptr) {
                std::string name = text->fontName().empty() ? defaultTextFontName() : text->fontName();
                auto it = std::find(fonts_.begin(), fonts_.end(), name);
                if (it != fonts_.end()) {
                    std::optional<std::size_t> index = std::size_t(it - fonts_.begin());
                    if (font_->selectedIndex() != index) {
                        font_->setSelectedIndex(*index);
                    }
                }
            }
            if (size_ != nullptr) { size_->setText(numberText(text->fontSize())); }
            if (tracking_ != nullptr) { tracking_->setText(numberText(style.tracking)); }
            if (leading_ != nullptr) { leading_->setText(numberText(style.leading * 100.0)); }
            if (bold_ != nullptr) { bold_->setChecked(style.bold); }
            if (italic_ != nullptr) { italic_->setChecked(style.italic); }
            for (int i = 0; i < 3; ++i) {
                if (align_[i] != nullptr) { align_[i]->setChecked(style.align == i); }
            }
        }
        // Text on a path: where it starts and which side it is on.
        const SketchObject* warped = document_.selectedWarpedText();
        if (pathStart_ != nullptr) {
            pathStart_->setEnabled(warped != nullptr && warped->textWarp().kind == TextWarp::Kind::OnPath);
            pathStart_->setText(warped != nullptr ? numberText(warped->textWarp().start) : std::wstring());
        }
        if (pathFlip_ != nullptr) {
            pathFlip_->setEnabled(warped != nullptr);
            pathFlip_->setChecked(warped != nullptr && warped->textWarp().flip);
        }
        updating_ = false;
    }

    void TextPanel::bindScrub(const char* labelName, Scrubbed which, double unit, double minimum, double maximum) {
        auto* label = dynamic_cast<newui::Label*>(root_.findView(labelName));
        if (label == nullptr) {
            return;
        }
        // The value `which` of a text as the number the field shows, and a text with that number set.
        auto set = [which](SketchDocument& document, const Snap& snap, double value) {
            TextState state = snap.state;
            switch (which) {
                case Scrubbed::Size: state.size = value; break;
                case Scrubbed::Tracking: state.style.tracking = value; break;
                case Scrubbed::Leading: state.style.leading = value / 100.0; break;
            }
            document.setObjectText(snap.id, state);
        };
        auto scrubber = NumberScrubber::attach(*label, unit, minimum, maximum);
        scrubber->onBegin += [this, which](NumberScrubber&, double& value) {
            scrubBefore_.clear();
            scrubActive_ = false;
            const SketchObject* first = firstText();
            if (updating_ || first == nullptr) {
                return newui::SyncReturn::Ignored;
            }
            value = which == Scrubbed::Size ? first->fontSize() : (which == Scrubbed::Tracking ? first->textStyle().tracking : first->textStyle().leading * 100.0);
            for (std::size_t id : document_.selection()) {
                const SketchObject* object = document_.findById(id);
                if (object != nullptr && object->isText()) {
                    scrubBefore_.push_back({id, SketchDocument::textStateOf(*object)});
                }
            }
            scrubValue_ = value;
            scrubActive_ = true;
            return newui::SyncReturn::Handled;
        };
        scrubber->onChange += [this, set](NumberScrubber&, double value) {
            if (!scrubActive_) {
                return newui::SyncReturn::Ignored;
            }
            scrubValue_ = value;
            for (const Snap& snap : scrubBefore_) {
                set(document_, snap, value);
            }
            document_.touch();
            return newui::SyncReturn::Handled;
        };
        scrubber->onEnd += [this, set, which](NumberScrubber&, bool changed) {
            if (!scrubActive_) {
                return newui::SyncReturn::Ignored;
            }
            scrubActive_ = false;
            if (changed) {
                // Back to what the drag found, then the final number as one undo step.
                for (const Snap& snap : scrubBefore_) {
                    document_.setObjectText(snap.id, snap.state);
                }
                const double value = scrubValue_;
                document_.changeSelectionText([value, which](TextState& state) {
                    switch (which) {
                        case Scrubbed::Size: state.size = value; break;
                        case Scrubbed::Tracking: state.style.tracking = value; break;
                        case Scrubbed::Leading: state.style.leading = value / 100.0; break;
                    }
                }, which == Scrubbed::Size ? "Text Size" : (which == Scrubbed::Tracking ? "Tracking" : "Leading"));
            }
            scrubBefore_.clear();
            refresh();
            return newui::SyncReturn::Handled;
        };
    }

    // The Start and Flip controls of text on a path (Flip alone for text between paths).
    void TextPanel::applyPathOptions() {
        double start = 0.0;
        if (!updating_ && document_.canReleaseTextWarp() && pathStart_ != nullptr && pathFlip_ != nullptr && parseNumber(pathStart_->text(), start)) {
            document_.setTextPathOptions(start, pathFlip_->isChecked());
        }
        refresh();
    }

    void TextPanel::applyFont() {
        if (updating_ || font_ == nullptr || !font_->selectedIndex().has_value() || *font_->selectedIndex() >= fonts_.size()) {
            return;
        }
        const std::string name = fonts_[*font_->selectedIndex()];
        document_.changeSelectionText([name](TextState& state) { state.font = name; }, "Text Font");
    }

    void TextPanel::applySize() {
        double value;
        if (!updating_ && size_ != nullptr && parseNumber(size_->text(), value) && value > 0.0) {
            value = std::max(1.0, std::min(1000.0, value));
            document_.changeSelectionText([value](TextState& state) { state.size = value; }, "Text Size");
        }
        refresh();
    }

    void TextPanel::applyStyle(const char* what, const std::function<void(TextStyle&)>& change) {
        document_.changeSelectionText([&change](TextState& state) { change(state.style); }, what);
    }

}
