#include "curvy/dockcontroller.h"

#include "curvy/theme.h"

#include <newui/bundle.h>
#include <newui/segmentedcontrol.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace curvy {

    using namespace newui;

    namespace {

        // The old palette's order (Resources/curvy.newui), with each tool's button name and icon.
        struct ToolRow {
            const char* name;
            SketchTool tool;
            const char* icon;
        };
        const ToolRow kToolRows[] = {
            {"selectTool", SketchTool::Select, "icons/tool-select.svg"},
            {"directSelectTool", SketchTool::DirectSelect, "icons/tool-direct-select.svg"},
            {"rectangleTool", SketchTool::Rectangle, "icons/tool-rectangle.svg"},
            {"ellipseTool", SketchTool::Ellipse, "icons/tool-ellipse.svg"},
            {"circleTool", SketchTool::Circle, "icons/tool-circle.svg"},
            {"roundRectTool", SketchTool::RoundRect, "icons/tool-roundrect.svg"},
            {"polygonTool", SketchTool::Polygon, "icons/tool-polygon.svg"},
            {"starTool", SketchTool::Star, "icons/tool-star.svg"},
            {"arcTool", SketchTool::Arc, "icons/tool-arc.svg"},
            {"lineTool", SketchTool::Line, "icons/tool-line.svg"},
            {"penTool", SketchTool::Pen, "icons/tool-pen.svg"},
            {"pencilTool", SketchTool::Pencil, "icons/tool-pencil.svg"},
            {"paintbrushTool", SketchTool::Paintbrush, "icons/tool-paintbrush.svg"},
            {"textTool", SketchTool::Text, "icons/tool-text.svg"},
            {"gradientTool", SketchTool::Gradient, "icons/tool-gradient.svg"},
            {"widthTool", SketchTool::Width, "icons/tool-width.svg"},
            {"scissorsTool", SketchTool::Scissors, "icons/tool-scissors.svg"},
            {"knifeTool", SketchTool::Knife, "icons/tool-knife.svg"},
            {"eraserTool", SketchTool::Eraser, "icons/tool-eraser.svg"},
            {"shapeBuilderTool", SketchTool::ShapeBuilder, "icons/tool-shapebuilder.svg"},
            {"eyedropperTool", SketchTool::Eyedropper, "icons/tool-eyedropper.svg"},
            {"magicWandTool", SketchTool::MagicWand, "icons/tool-magicwand.svg"},
            {"lassoTool", SketchTool::Lasso, "icons/tool-lasso.svg"},
            {"artboardTool", SketchTool::Artboard, "icons/tool-artboard.svg"},
            {"rotateTool", SketchTool::Rotate, "icons/tool-rotate.svg"},
            {"scaleTool", SketchTool::Scale, "icons/tool-scale.svg"},
            {"reflectTool", SketchTool::Reflect, "icons/tool-reflect.svg"},
            {"handTool", SketchTool::Hand, "icons/tool-hand.svg"},
            {"zoomTool", SketchTool::Zoom, "icons/tool-zoom.svg"}};

        template <typename T>
        T* find(View& root, const char* name) {
            return dynamic_cast<T*>(root.findView(name));
        }

        Color tint(std::uint32_t rgb, std::uint32_t alpha = 255) { return Color(Theme::argb(rgb, alpha), true); }
    }

    DockController::DockController(Frame& frame, CanvasView& canvas, WorkspaceModel& model, SketchDocument& document)
        : frame_(frame), canvas_(canvas), model_(model), document_(document) {
        toolDock_ = dynamic_cast<SubView*>(Bundle::instance().loadView("toolDock"));
        propertyDock_ = dynamic_cast<SubView*>(Bundle::instance().loadView("propertyDock"));
        for (SubView* dock : {toolDock_, propertyDock_}) {
            if (dock != nullptr) {
                dock->setVisible(false);
                canvas_.addChild(dock);
            }
        }
        if (auto* mode = find<SegmentedControl>(frame_.rootView(), "colorModeControl")) {
            mode->setSegments({"Gray", "RGB", "CMYK", "HSB"});   // the layout file only has the placeholder; StylePanel follows the colour's space
            mode->setSelectedIndex(1);
        }
        if (propertyDock_ != nullptr) {
            transformPanel_ = std::make_unique<TransformPanel>(frame_.rootView(), document_);
            stylePanel_ = std::make_unique<StylePanel>(frame_.rootView(), document_);   // binds the views it finds by name; the rest of its panel is not in the dock
            stylePanel_->onContentChanged += guarded(alive_, [this](StylePanel&) { fitCard(); return SyncReturn::Handled; });
        }
        if (propertyDock_ != nullptr) {
            cardHost_ = new SubView();
            cardHost_->setVisible(false);
            canvas_.addChild(cardHost_);
            cardClose_ = new ToolbarButton();
            cardClose_->setName("cardClose");
            cardClose_->setIcon("icons/close.svg");   // drawn, so the cross sits exactly in the middle (a glyph sits on the text baseline)
            cardClose_->setIconSize(14.0f);
            cardClose_->onClick += guarded(alive_, [this](Control&) {
                closeCard();
                if (onCardDismissed) {
                    onCardDismissed();
                }
                return SyncReturn::Handled;
            });
            cardHost_->addChild(cardClose_);
        }
        bindTools();
        bindToggles();
        model_.onChanged += guarded(alive_, [this](Model&) {
            sync();
            return SyncReturn::Handled;
        });
        canvas_.onSizeChanged += guarded(alive_, [this](View&, const Size&) {
            layout();
            return SyncReturn::Handled;
        });
        document_.onSelectionChanged += guarded(alive_, [this](SketchDocument&) {
            refreshContext();
            return SyncReturn::Handled;
        });
        restyle();
        refreshContext();
        sync();
    }

    DockController::~DockController() {
        *alive_ = false;
    }

    ToolbarButton* DockController::toolButton(SketchTool tool) const {
        for (const ToolButton& entry : tools_) {
            if (entry.tool == tool) {
                return entry.button;
            }
        }
        return nullptr;
    }

    void DockController::bindTools() {
        if (toolDock_ == nullptr) {
            return;
        }
        for (const ToolRow& row : kToolRows) {
            auto* button = find<ToolbarButton>(*toolDock_, row.name);
            if (button == nullptr) {
                continue;
            }
            button->setIcon(row.icon);
            button->setIconSize(18.0f);
            button->setToggleButton(true);
            tools_.push_back({button, row.tool});
            const SketchTool tool = row.tool;
            button->onClick += guarded(alive_, [this, tool](Control&) {
                canvas_.setTool(tool);
                syncTool();
                return SyncReturn::Handled;
            });
            button->onMouseDblClick += guarded(alive_, [this, tool, button](View&, const Point&, std::uint32_t, std::uint32_t) {
                if (onToolOptions) {
                    const Point corner = frame_.rootView().accumulatedOffset(button);
                    const Point origin = frame_.rootView().accumulatedOffset(&canvas_);
                    onToolOptions(tool, Point(corner.x - origin.x + button->bounds().size().width + 6.0f, corner.y - origin.y));
                }
                return SyncReturn::Handled;
            });
        }
        syncTool();
    }

    void DockController::bindToggles() {
        toolbarToggle_ = find<Button>(frame_.rootView(), "toolbarToggle");
        propertiesToggle_ = find<Button>(frame_.rootView(), "propertiesToggle");
        if (toolbarToggle_ != nullptr) {
            toolbarToggle_->setToggleButton(true);
            toolbarToggle_->onClick += guarded(alive_, [this](Control&) {
                model_.setToolbarVisible(!model_.toolbarVisible());
                return SyncReturn::Handled;
            });
        }
        if (propertiesToggle_ != nullptr) {
            propertiesToggle_->setToggleButton(true);
            propertiesToggle_->onClick += guarded(alive_, [this](Control&) {
                model_.setPropertiesVisible(!model_.propertiesVisible());
                return SyncReturn::Handled;
            });
        }
        static const struct { const char* name; const char* icon; } kStrokeIcons[] = {
            {"capButtButton", "icons/cap-butt.svg"}, {"capRoundButton", "icons/cap-round.svg"}, {"capSquareButton", "icons/cap-square.svg"},
            {"joinMiterButton", "icons/join-miter.svg"}, {"joinRoundButton", "icons/join-round.svg"}, {"joinBevelButton", "icons/join-bevel.svg"},
            {"fillNoneButton", "icons/fill-none.svg"}, {"fillSolidButton", "icons/fill-solid.svg"}, {"fillLinearButton", "icons/fill-linear.svg"},
            {"fillRadialButton", "icons/fill-radial.svg"}, {"fillPointsButton", "icons/fill-points.svg"}};
        for (const auto& row : kStrokeIcons) {
            if (auto* button = find<ToolbarButton>(frame_.rootView(), row.name)) {
                button->setIcon(row.icon);
                button->setIconSize(16.0f);
                button->setToggleButton(true);
            }
        }
        if (auto* link = find<ToolbarButton>(frame_.rootView(), "linkAspectButton")) {
            link->setIcon("icons/link-aspect.svg");
            link->setIconSize(15.0f);
            link->setToggleButton(true);
        }
    }

    void DockController::syncTool() {
        for (const ToolButton& entry : tools_) {
            entry.button->setChecked(entry.tool == canvas_.tool());
        }
        refreshContext();
    }

    void DockController::refreshContext() {
        if (propertyDock_ == nullptr) {
            return;
        }
        boxes_ = DockContext::boxesFor(document_, canvas_.tool());
        auto show = [&](const char* name, bool visible) {
            if (View* view = propertyDock_->findView(name)) {
                view->setVisible(visible);
            }
        };
        show("transformPanel", boxes_.transform);
        show("colorPanel", boxes_.colour);
        show("strokePanel", boxes_.stroke);
        show("opacityPanel", boxes_.opacity);
        show("dockCaption", !boxes_.caption.empty());
        if (auto* caption = dynamic_cast<Label*>(propertyDock_->findView("dockCaption"))) {
            caption->setText(boxes_.caption);
        }
        frame_.rootView().updateLayout();
        canvas_.style().markDirty();
    }

    void DockController::showCard(Card which, const Point& at, const Rect* avoid) {
        closeCard();
        if (cardHost_ == nullptr || which == Card::None) {
            return;
        }
        auto* box = dynamic_cast<SubView*>(propertyDock_->findView(which == Card::Fill ? "colorPanel" : "strokePanel"));
        if (box == nullptr) {
            return;
        }
        cardHome_ = dynamic_cast<SubView*>(box->parent());
        if (cardHome_ == nullptr) {
            return;
        }
        const auto& siblings = cardHome_->childViews();
        cardHomeIndex_ = std::size_t(std::find(siblings.begin(), siblings.end(), box) - siblings.begin());
        cardBox_ = box;
        card_ = which;
        if (which == Card::Fill) {
            stylePanel_->setTarget(StylePanel::Target::Fill);
        }
        stylePanel_->refresh();

        const Size size(propertyDock_->bounds().size().width, box->desiredSize().height);
        const Rect client = canvas_.getClientBounds();
        constexpr float kGap = 24.0f;   // room for a selected shape's handles and a gradient line's ends
        const float fullWidth = size.width;
        float x = at.x + 28.0f;
        float y = at.y - size.height * 0.5f;
        if (avoid != nullptr) {
            const float right = avoid->right() + kGap;
            const float left = avoid->left() - kGap - fullWidth;
            // The roomier side; with neither fitting, the card overlaps the shape rather than leave the canvas.
            if (right + fullWidth <= client.width() || client.width() - avoid->right() >= avoid->left()) {
                x = right;
            } else {
                x = left;
            }
            y = avoid->top() + avoid->height() * 0.5f - size.height * 0.5f;
        }
        x = std::clamp(x, 0.0f, std::max(0.0f, client.width() - size.width));
        y = std::clamp(y, canvas_.rulerSize(), std::max(canvas_.rulerSize(), client.height() - size.height));
        cardHome_->removeChild(box);
        cardHost_->addChild(box);
        box->setVisible(true);
        // The close button sits in the header's right end; a spacer there keeps the header's own content clear of it.
        constexpr float kCloseSize = 20.0f;
        constexpr float kHeaderHeight = 24.0f;
        cardHeader_ = box->findView(which == Card::Fill ? "colorPanelHeaderBar" : "strokePanelHeaderBar");
        if (auto* header = dynamic_cast<SubView*>(cardHeader_)) {
            cardSpacer_ = new SubView();
            cardSpacer_->setVisible(true);
            cardSpacer_->setDesiredSize(Size(kCloseSize + 2.0f, 1.0f));
            header->addChild(cardSpacer_);
            header->setBounds(Rect(0.0f, 0.0f, size.width, kHeaderHeight));
            header->updateLayout();
        }
        cardHost_->setBounds(Rect(x, y, size.width, size.height));
        box->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
        cardHost_->removeChild(cardClose_);
        cardHost_->addChild(cardClose_);   // on top of the box
        cardClose_->setBounds(Rect(size.width - kCloseSize - 4.0f, (kHeaderHeight - kCloseSize) * 0.5f, kCloseSize, kCloseSize));
        cardHost_->setVisible(true);
        restyle();
        frame_.rootView().updateLayout();
        fitCard();
        canvas_.style().markDirty();
    }

    void DockController::fitCard() {
        if (card_ != Card::Fill || cardBox_ == nullptr || cardHost_ == nullptr) {
            return;
        }
        auto* last = dynamic_cast<SubView*>(cardBox_->findView("alphaSliderRow"));
        if (last == nullptr) {
            return;
        }
        RootView& root = frame_.rootView();
        constexpr float kBottomPadding = 10.0f;
        const float bottom = root.accumulatedOffset(last).y + last->bounds().size().height - root.accumulatedOffset(cardBox_).y;
        const float height = bottom + kBottomPadding;
        const Rect now = cardHost_->bounds();
        const Rect client = canvas_.getClientBounds();
        const float y = std::clamp(now.top(), canvas_.rulerSize(), std::max(canvas_.rulerSize(), client.height() - height));
        cardHost_->setBounds(Rect(now.left(), y, now.width(), height));
        cardBox_->setBounds(Rect(0.0f, 0.0f, cardBox_->bounds().size().width, height));
        root.updateLayout();
        canvas_.style().markDirty();
    }

    void DockController::closeCard() {
        if (card_ == Card::None) {
            return;
        }
        card_ = Card::None;
        cardHost_->setVisible(false);
        if (auto* header = dynamic_cast<SubView*>(cardHeader_)) {
            header->removeChild(cardSpacer_);
        }
        delete cardSpacer_;
        cardSpacer_ = nullptr;
        cardHeader_ = nullptr;
        cardHost_->removeChild(cardBox_);
        cardHome_->addChild(cardBox_);
        cardHome_->reorderChild(cardBox_, cardHomeIndex_);
        cardBox_ = nullptr;
        cardHome_ = nullptr;
        refreshContext();   // the box is shown again only if the selection and tool call for it
        canvas_.style().markDirty();
    }

    bool DockController::cardContains(const Point& pt) const {
        return card_ != Card::None && cardHost_ != nullptr && cardHost_->bounds().contains(pt);
    }

    void DockController::setPropertyTextSize(int points) {
        propertyTextSize_ = points;
        if (propertyDock_ == nullptr) {
            return;
        }
        const float scale = float(points) / 11.0f;
        std::function<void(View*)> walk = [&](View* view) {
            auto scaled = [&](const Font& font) {
                const float base = baseFontSizes_.emplace(view, font.size()).first->second;
                Font out = font;
                out.setSize(std::max(6.0f, std::round(base * scale)));
                return out;
            };
            if (auto* label = dynamic_cast<Label*>(view)) {
                if (auto* style = dynamic_cast<LabelStyle*>(&label->style())) {
                    style->setFont(scaled(style->font()));
                }
            } else if (auto* field = dynamic_cast<TextField*>(view)) {
                field->setFont(scaled(field->font()));
            }
            for (SubView* child : view->childViews()) {
                walk(child);
            }
        };
        walk(propertyDock_);
        layout();
        frame_.rootView().updateLayout();
    }

    void DockController::layout() {
        const Rect client = canvas_.getClientBounds();
        const float top = canvas_.rulerSize();
        auto place = [&](SubView* dock, float x, float y, float width, const char* innerName) {
            if (dock == nullptr) {
                return;
            }
            const float height = std::max(0.0f, client.height() - y);
            dock->setBounds(Rect(x, y, width, height));
            if (View* inner = dock->findView(innerName)) {
                inner->setBounds(Rect(0.0f, 0.0f, width, height));
            }
        };
        if (toolDock_ != nullptr) {
            const float width = toolDock_->bounds().size().width;
            place(toolDock_, 0.0f, 0.0f, width, "toolDockBox");
            canvas_.setLeftInset(toolDock_->isVisible() ? width : 0.0f);
        }
        if (propertyDock_ != nullptr) {
            if (basePropertyWidth_ <= 0.0f) {
                basePropertyWidth_ = propertyDock_->bounds().size().width;
            }
            const float width = basePropertyWidth_ * float(propertyTextSize_) / 11.0f;
            place(propertyDock_, client.width() - width, top, width, "propertyDockBox");
        }
        canvas_.style().markDirty();
    }

    void DockController::sync() {
        const bool toolbar = model_.toolbarVisible(), properties = model_.propertiesVisible();
        const float ruler = canvas_.rulerSize();
        if (toolbar == shownTools_ && properties == shownProperties_ && ruler == shownRuler_) {
            return;   // the model changes on every pointer move; the docks only when one of these does
        }
        shownTools_ = toolbar;
        shownProperties_ = properties;
        shownRuler_ = ruler;
        if (toolDock_ != nullptr) {
            toolDock_->setVisible(toolbar);
        }
        if (propertyDock_ != nullptr) {
            propertyDock_->setVisible(properties);
        }
        if (toolbarToggle_ != nullptr && toolbarToggle_->isChecked() != toolbar) {
            toolbarToggle_->setChecked(toolbar);
        }
        if (propertiesToggle_ != nullptr && propertiesToggle_->isChecked() != properties) {
            propertiesToggle_->setChecked(properties);
        }
        layout();
    }

    void DockController::restyle() {
        const Theme& theme = Theme::current();
        auto fill = [](View* view, const Color& color) {
            if (view != nullptr) {
                view->style().setBackgroundColor(color);
            }
        };
        auto text = [](View* view, std::uint32_t rgb) {
            if (auto* label = dynamic_cast<Label*>(view)) {
                label->setTextColor(BLRgba32(Theme::argb(rgb)));
            }
        };
        fill(cardHost_, tint(theme.bgSurface));   // opaque: a translucent card shows the page edge through it as a stripe
        // The panels are translucent over the artwork: bgSurface at 60%.
        if (toolDock_ != nullptr) {
            fill(toolDock_->findView("toolDockBox"), tint(theme.bgSurface, 0x99));
        }
        if (propertyDock_ != nullptr) {
            fill(propertyDock_->findView("propertyDockBox"), tint(theme.bgSurface, 0x99));
            fill(propertyDock_->findView("transformPanelHeaderBar"), tint(theme.slot, 0x99));
            text(propertyDock_->findView("transformPanelHeader"), theme.textPrimary);
            text(propertyDock_->findView("transformPanelHeaderInfo"), theme.textMuted);
            for (const char* name : {"transformXLabel", "transformYLabel", "transformWLabel", "transformHLabel", "transformAngleLabel", "transformAngleUnit"}) {
                text(propertyDock_->findView(name), theme.textMuted);
            }
            for (const char* box : {"colorPanel", "strokePanel", "opacityPanel"}) {
                const std::string base = box;
                fill(propertyDock_->findView((base + "HeaderBar").c_str()), tint(theme.slot, 0x99));
                text(propertyDock_->findView((base + "Header").c_str()), theme.textPrimary);
            }
            text(propertyDock_->findView("colorPanelHeaderInfo"), theme.textMuted);
            text(propertyDock_->findView("dockCaption"), theme.textMuted);
            for (const char* name : {"comp0SliderCaption", "comp1SliderCaption", "comp2SliderCaption", "strokeWeightLabel", "opacitySliderCaption"}) {
                text(propertyDock_->findView(name), theme.textMuted);
            }
            for (const char* name : {"hexField", "comp0SliderValue", "comp1SliderValue", "comp2SliderValue", "strokeWeightField", "opacitySliderValue"}) {
                fill(propertyDock_->findView(name), tint(theme.bgApp));
            }
            for (const char* name : {"transformXField", "transformYField", "transformWField", "transformHField", "transformAngleField"}) {
                fill(propertyDock_->findView(name), tint(theme.bgApp));
            }
        }
        for (Button* toggle : {toolbarToggle_, propertiesToggle_}) {
            if (toggle != nullptr) {
                toggle->setTextColor(BLRgba32(Theme::argb(theme.textPrimary)));
            }
        }
    }

}
