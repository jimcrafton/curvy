#include "curvy/graphcrumbs.h"

#include "curvy/theme.h"

#include <newui/viewstyle.h>

#include <cmath>

namespace curvy {

    using namespace newui;

    GraphCrumbs::GraphCrumbs(SubView& container, Label& current) : container_(container), current_(current) {}

    void GraphCrumbs::fit(Label& label, const std::string& text) const {
        const auto* style = dynamic_cast<const LabelStyle*>(&label.style());
        const float height = current_.bounds().size().height;
        if (style != nullptr) {
            label.setDesiredSize(Size(std::ceil(style->font().measureText(text).width) + 4.0f, height));
        }
    }

    void GraphCrumbs::addSegment() {
        const Theme& theme = Theme::current();
        const auto* currentStyle = dynamic_cast<const LabelStyle*>(&current_.style());
        Segment segment;
        segment.name = new Label();
        segment.separator = new Label();
        for (Label* label : {segment.name, segment.separator}) {
            label->setTextAlignment(TextAlignment::Left);
            if (auto* style = dynamic_cast<LabelStyle*>(&label->style()); style != nullptr && currentStyle != nullptr) {
                style->setFont(currentStyle->font());
                style->setWordWrap(false);
            }
        }
        segment.separator->setTextColor(BLRgba32(Theme::argb(theme.textMuted)));
        segment.separator->setText(">");
        fit(*segment.separator, ">");
        segment.name->setHotLink(true);
        segment.name->setLinkColor(BLRgba32(Theme::argb(theme.textMuted)));
        segment.name->setHoveredLinkColor(BLRgba32(Theme::argb(theme.accent)));

        const std::size_t level = segments_.size();
        segment.name->onLinkClicked += [this, level](Label&) {
            if (onNavigate) {
                onNavigate(level);
            }
            return SyncReturn(SyncReturn::Handled);
        };
        container_.addChild(segment.name);
        container_.addChild(segment.separator);
        container_.reorderChild(segment.name, level * 2);
        container_.reorderChild(segment.separator, level * 2 + 1);
        segments_.push_back(segment);
    }

    void GraphCrumbs::show(const std::vector<std::string>& names) {
        const std::size_t links = names.empty() ? 0 : names.size() - 1;
        while (segments_.size() < links) {
            addSegment();
        }
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            const bool on = i < links;
            segments_[i].name->setVisible(on);
            segments_[i].separator->setVisible(on);
            if (on) {
                segments_[i].name->setText(names[i]);
                fit(*segments_[i].name, names[i]);
            }
        }
        used_ = links;
        const std::string last = names.empty() ? std::string() : names.back();
        current_.setText(last);
        fit(current_, last);
        container_.updateLayout();
    }

}
