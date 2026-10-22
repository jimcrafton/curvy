#include "curvy/dockcontroller.h"

#include "curvy/theme.h"

#include <newui/bundle.h>

#include <algorithm>

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
        if (propertyDock_ != nullptr) {
            transformPanel_ = std::make_unique<TransformPanel>(frame_.rootView(), document_);
        }
        bindTools();
        bindToggles();
        model_.onChanged += [this](Model&) {
            sync();
            return SyncReturn::Handled;
        };
        canvas_.onSizeChanged += [this](View&, const Size&) {
            layout();
            return SyncReturn::Handled;
        };
        restyle();
        sync();
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
            button->setIconSize(20.0f);
            button->setToggleButton(true);
            tools_.push_back({button, row.tool});
            const SketchTool tool = row.tool;
            button->onClick += [this, tool](Control&) {
                canvas_.setTool(tool);
                syncTool();
                return SyncReturn::Handled;
            };
        }
        syncTool();
    }

    void DockController::bindToggles() {
        toolbarToggle_ = find<Button>(frame_.rootView(), "toolbarToggle");
        propertiesToggle_ = find<Button>(frame_.rootView(), "propertiesToggle");
        if (toolbarToggle_ != nullptr) {
            toolbarToggle_->setToggleButton(true);
            toolbarToggle_->onClick += [this](Control&) {
                model_.setToolbarVisible(!model_.toolbarVisible());
                return SyncReturn::Handled;
            };
        }
        if (propertiesToggle_ != nullptr) {
            propertiesToggle_->setToggleButton(true);
            propertiesToggle_->onClick += [this](Control&) {
                model_.setPropertiesVisible(!model_.propertiesVisible());
                return SyncReturn::Handled;
            };
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
    }

    void DockController::layout() {
        const Rect client = canvas_.getClientBounds();
        const float top = canvas_.rulerSize(), left = canvas_.rulerSize();
        const float height = std::max(0.0f, client.height() - top);
        auto place = [&](SubView* dock, float x, float width, const char* innerName) {
            if (dock == nullptr) {
                return;
            }
            dock->setBounds(Rect(x, top, width, height));
            if (View* inner = dock->findView(innerName)) {
                inner->setBounds(Rect(0.0f, 0.0f, width, height));
            }
        };
        if (toolDock_ != nullptr) {
            place(toolDock_, left, toolDock_->bounds().size().width, "toolDockBox");
        }
        if (propertyDock_ != nullptr) {
            const float width = propertyDock_->bounds().size().width;
            place(propertyDock_, client.width() - width, width, "propertyDockBox");
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
