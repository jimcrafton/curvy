#include "curvy/documentpanel.h"

#include "curvy/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kPad = 10.0f;
        constexpr float kLine = 22.0f;

        Label* makeLabel(SubView& parent, bool muted) {
            auto* label = new Label();
            label->setTextAlignment(TextAlignment::Left);
            const Theme& theme = Theme::current();
            label->setTextColor(BLRgba32(Theme::argb(muted ? theme.textMuted : theme.textPrimary)));
            parent.addChild(label);
            return label;
        }
    }

    DocumentPanel::DocumentPanel(SketchDocument& document) : Panel("document", "Document"), document_(document) {
        tabs_ = new TabControl();
        tabs_->setName("documentTabs");
        body().addChild(tabs_);

        auto* documentPage = new TabPage();
        documentPage->setTitle("Document");
        tabs_->addTab("Document", documentPage);
        const auto row = [&](const char* caption) {
            Label* title = makeLabel(*documentPage, true);
            title->setText(caption);
            rows_.push_back(title);
            Label* value = makeLabel(*documentPage, false);
            rows_.push_back(value);
            return value;
        };
        name_ = row("Name");
        page_ = row("Page");
        mode_ = row("Colour mode");

        auto* swatchTab = new TabPage();
        swatchTab->setTitle("Swatches");
        swatchPage_ = tabs_->addTab("Swatches", swatchTab);
        tabs_->onTabChanged += [this](TabControl&, std::size_t index) {
            if (index == 1) {
                layoutParts();   // the page gets its size as it is shown, after the panel last laid out, so it is laid out again now
                if (swatchesShown) {
                    swatchesShown();
                }
            }
            fitHeight();
            return SyncReturn::Handled;
        };

        body().onSizeChanged += [this](View&, const Size&) {
            layoutParts();
            return SyncReturn::Ignored;
        };
        refresh();
    }

    void DocumentPanel::fitHeight() {
        constexpr float kDocumentTab = 150.0f;   // the name, the page and the colour mode
        constexpr float kMostSwatches = 560.0f;   // beyond this the page scrolls
        float content = kDocumentTab;
        if (tabs_->selectedIndex() == 1 && swatchesContentHeight) {
            content = (std::min)(swatchesContentHeight(), kMostSwatches);
        }
        setPreferredBodyHeight(kTabStripHeight + content);
    }

    DocumentPanel::~DocumentPanel() {
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void DocumentPanel::refresh() {
        name_->setText(document_.name());
        char text[64];
        std::snprintf(text, sizeof(text), "%g x %g pt", document_.pageWidth(), document_.pageHeight());
        page_->setText(text);
        mode_->setText(document_.colorMode() == ColorSpace::CMYK ? "CMYK" : document_.colorMode() == ColorSpace::Gray ? "Gray" : "RGB");
        layoutParts();
    }

    std::string DocumentPanel::nameText() const {
        return name_->text();
    }

    std::string DocumentPanel::pageText() const {
        return page_->text();
    }

    void DocumentPanel::layoutParts() {
        const Rect own = body().bounds();
        body().setContentSize(Size(own.width(), own.height()));   // the tab control fills the body: nothing to scroll
        tabs_->setBounds(Rect(0.0f, 0.0f, own.width(), own.height()));
        const float width = (std::max)(40.0f, own.width() - 2.0f * kPad);
        if (layoutSwatches && swatchPage_ != nullptr) {
            const Rect page = swatchPage_->bounds();
            layoutSwatches(page.width(), page.height());
        }
        fitHeight();
        float y = kPad;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            rows_[i]->setBounds(Rect(kPad, y, width, kLine));
            y += (i % 2 == 0) ? kLine - 4.0f : kLine + 6.0f;
        }
        style().markDirty();
    }

}
