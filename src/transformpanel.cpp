#include "curvy/transformpanel.h"

#include <jitnodes/graph/network.h>

#include "curvy/theme.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kPad = 10.0f;
        constexpr float kLine = 26.0f;
        constexpr float kLabelWidth = 80.0f;

        struct Param {
            const char* name;
            const char* caption;
            double unit;
            Quantity quantity;
        };
        const Param kParams[] = {{"tx", "Move X", 1.0, Quantity::Length}, {"ty", "Move Y", 1.0, Quantity::Length}, {"rot", "Rotate", 1.0, Quantity::Angle},
                                 {"sx", "Scale X", 0.01, Quantity::None}, {"sy", "Scale Y", 0.01, Quantity::None}, {"skew", "Skew", 1.0, Quantity::Angle},
                                 {"px", "Pivot X", 1.0, Quantity::Length}, {"py", "Pivot Y", 1.0, Quantity::Length}};

        Quantity quantityOf(const std::string& param) {
            for (const Param& p : kParams) {
                if (param == p.name) {
                    return p.quantity;
                }
            }
            return Quantity::None;
        }
    }

    std::string TransformPanel::numberText(const std::string& param, double stored) const {
        char text[48];
        std::snprintf(text, sizeof(text), "%g", Units::toDisplay(stored, quantityOf(param), document_.units(), double(document_.ppi())));
        return text;
    }

    std::string TransformPanel::narrow(const std::wstring& text) {
        return std::string(text.begin(), text.end());
    }

    std::wstring TransformPanel::widen(const std::string& text) {
        return std::wstring(text.begin(), text.end());
    }

    TransformPanel::TransformPanel(SketchDocument& document, SelectionModel& selection) : Panel("transform", "Transform"), document_(document), selection_(selection) {
        tabs_ = new TabControl();
        tabs_->setName("transformTabs");
        body().addChild(tabs_);
        buildPage(Part::Object, "Object");
        buildPage(Part::Local, "Local");
        tabs_->onTabChanged += [this](TabControl&, std::size_t) {
            layoutParts();   // a page gets its size as it is shown, after the panel last laid out
            return SyncReturn::Handled;
        };
        body().onSizeChanged += [this](View&, const Size&) {
            layoutParts();
            return SyncReturn::Ignored;
        };
        // The tab strip, then a note, the Add button's line and the eight rows: what a page needs to show everything without scrolling.
        setPreferredBodyHeight(Panel::kTabStripHeight + 2.0f * kPad + (2.0f + float(sizeof(kParams) / sizeof(kParams[0]))) * kLine);
        refresh();
    }

    TransformPanel::~TransformPanel() {
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void TransformPanel::buildPage(Part part, const char* title) {
        Page& page = pages_[index(part)];
        page.tab = new TabPage();
        page.tab->setTitle(title);
        tabs_->addTab(title, page.tab);
        const Theme& theme = Theme::current();
        page.scroll = new ScrollView();
        page.tab->addChild(page.scroll);

        page.note = new Label();
        page.note->setTextAlignment(TextAlignment::Left);
        page.note->setTextColor(BLRgba32(Theme::argb(theme.textMuted)));
        page.scroll->addChild(page.note);

        if (part == Part::Local) {
            page.add = new Button();
            page.add->setName("addLocalTransform");
            page.add->setText("Add local transform");
            page.add->onClick += [this](Control&) {
                addLocalTransform();
                return SyncReturn::Handled;
            };
            page.scroll->addChild(page.add);
        }

        for (const Param& param : kParams) {
            Row row;
            row.param = param.name;
            row.caption = new Label();
            row.caption->setText(param.caption);
            row.caption->setTextAlignment(TextAlignment::Left);
            row.caption->setTextColor(BLRgba32(0xFFA0A0A0u));
            page.scroll->addChild(row.caption);
            row.field = new TextField();
            row.field->setName(std::string(part == Part::Object ? "objectXform_" : "localXform_") + param.name);
            row.field->setTextColor(BLRgba32(0xFFF0F0F0u));
            row.field->caret().setColor(theme.textColor());
            page.scroll->addChild(row.field);

            const std::size_t at = page.rows.size();
            row.field->onReturnPressed += [this, part, at](TextField&) {
                commit(part, at);
                return SyncReturn::Handled;
            };
            row.scrubber = NumberScrubber::attach(*row.caption, param.unit, -1e9, 1e9);
            row.scrubber->onBegin += [this, part, at](NumberScrubber&, double& value) {
                Page& p = pages_[index(part)];
                if (!p.shown || !document_.hasNode(p.node)) {
                    return SyncReturn(SyncReturn::Ignored);
                }
                scrubBefore_ = document_.snapshotGraph();
                value = Units::toDisplay(double(document_.node(p.node).getFloat(p.rows[at].param)), quantityOf(p.rows[at].param), document_.units(), double(document_.ppi()));
                return SyncReturn(SyncReturn::Handled);
            };
            row.scrubber->onChange += [this, part, at](NumberScrubber&, double value) {
                Page& p = pages_[index(part)];
                if (p.shown && document_.hasNode(p.node)) {
                    const double stored = value / Units::displayScale(quantityOf(p.rows[at].param), document_.units(), double(document_.ppi()));
                    document_.setGraphFloatLive(p.node, p.rows[at].param, float(stored));
                    p.rows[at].field->setText(widen(numberText(p.rows[at].param, stored)));
                    if (onEdited) {
                        onEdited();
                    }
                }
                return SyncReturn::Handled;
            };
            row.scrubber->onEnd += [this, part, at](NumberScrubber&, bool changed) {
                if (changed && scrubBefore_) {
                    document_.commitGraphEdit(*scrubBefore_, std::string("Change ") + pages_[index(part)].rows[at].param);
                }
                scrubBefore_.reset();
                return SyncReturn::Handled;
            };
            page.rows.push_back(std::move(row));
        }
    }

    NodeRef TransformPanel::nodeFor(Part part) const {
        return part == Part::Object ? selection_.objectXform() : selection_.localTransform();
    }

    void TransformPanel::refresh() {
        for (Part part : {Part::Object, Part::Local}) {
            Page& page = pages_[index(part)];
            const NodeRef node = nodeFor(part);
            page.shown = node.valid() && document_.hasNode(node);
            page.node = page.shown ? node : NodeRef();
            for (Row& row : page.rows) {
                if (page.shown) {
                    row.field->setText(widen(numberText(row.param, double(document_.node(node).getFloat(row.param)))));
                }
            }
            if (page.shown) {
                page.note->setText((part == Part::Object ? "Object transform: " : "Local transform: ") + document_.node(node).name());
            } else if (selection_.empty()) {
                page.note->setText("Nothing selected");
            } else if (part == Part::Object) {
                page.note->setText("This shape has no object around it");
            } else {
                page.note->setText(selection_.shape().valid() ? "This shape has no local transform" : "Select a shape to give it a local transform");
            }
        }
        layoutParts();
    }

    void TransformPanel::layoutParts() {
        const Rect own = body().bounds();
        body().setContentSize(Size(own.width(), own.height()));
        tabs_->setBounds(Rect(0.0f, 0.0f, own.width(), own.height()));
        for (Part part : {Part::Object, Part::Local}) {
            Page& page = pages_[index(part)];
            const Rect area = page.tab->bounds();
            const float pageWidth = area.width() > 0.0f ? area.width() : own.width();
            const float pageHeight = area.height() > 0.0f ? area.height() : own.height();
            const float width = (std::max)(pageWidth - 2.0f * kPad, 60.0f);
            page.scroll->setBounds(Rect(0.0f, 0.0f, pageWidth, pageHeight));
            float y = kPad;
            page.note->setBounds(Rect(kPad, y, width, kLine));
            y += kLine;
            const bool offerAdd = page.add != nullptr && !page.shown && selection_.shape().valid();
            if (page.add != nullptr) {
                page.add->setVisible(offerAdd);
                page.add->setBounds(Rect(kPad, y, width, kLine));
            }
            if (offerAdd) {
                y += kLine;
            }
            for (Row& row : page.rows) {
                row.caption->setVisible(page.shown);
                row.field->setVisible(page.shown);
                row.caption->setBounds(Rect(kPad, y, kLabelWidth, kLine));
                row.field->setBounds(Rect(kPad + kLabelWidth, y, (std::max)(width - kLabelWidth, 40.0f), kLine - 4.0f));
                y += kLine;
            }
            page.contentHeight = y + kPad;
            page.scroll->setContentSize(Size(pageWidth, (std::max)(page.contentHeight, pageHeight)));
        }
    }

    bool TransformPanel::scrollHasSize(Part part) const {
        const Rect box = pages_[index(part)].scroll->bounds();
        return box.width() > 0.0f && box.height() > 0.0f;
    }

    void TransformPanel::commit(Part part, std::size_t row) {
        Page& page = pages_[index(part)];
        if (!page.shown || row >= page.rows.size() || !document_.hasNode(page.node)) {
            return;
        }
        const std::string text = narrow(page.rows[row].field->text());
        if (text == numberText(page.rows[row].param, double(document_.node(page.node).getFloat(page.rows[row].param)))) {
            refresh();   // unchanged text: parsing it back would round the stored value
            return;
        }
        double number = 0.0;
        if (!Units::parseQuantity(text, quantityOf(page.rows[row].param), document_.units(), double(document_.ppi()), number)) {
            if (onRefused) {
                onRefused("'" + text + "' is not a number");
            }
            refresh();
            return;
        }
        try {
            document_.setGraphParam(page.node, page.rows[row].param, float(number));
            if (onEdited) {
                onEdited();
            }
        } catch (const std::exception& error) {
            if (onRefused) {
                onRefused(error.what());
            }
        }
        refresh();
    }

    bool TransformPanel::addButtonShown() const {
        const Page& page = pages_[index(Part::Local)];
        return page.add != nullptr && page.add->isVisible();
    }

    std::string TransformPanel::note(Part part) const {
        return pages_[index(part)].note->text();
    }

    std::string TransformPanel::value(Part part, const std::string& param) const {
        for (const Row& row : pages_[index(part)].rows) {
            if (param == row.param) {
                return narrow(row.field->text());
            }
        }
        return "";
    }

    void TransformPanel::setValue(Part part, const std::string& param, const std::string& text) {
        Page& page = pages_[index(part)];
        for (std::size_t i = 0; i < page.rows.size(); ++i) {
            if (param == page.rows[i].param) {
                page.rows[i].field->setText(widen(text));
                commit(part, i);
                return;
            }
        }
    }

    bool TransformPanel::addLocalTransform() {
        const NodeRef shape = selection_.shape();
        if (!shape.valid() || !document_.hasNode(shape) || selection_.localTransform().valid()) {
            return false;
        }
        document_.addLocalTransform(shape);
        if (onEdited) {
            onEdited();
        }
        refresh();
        return true;
    }

}
