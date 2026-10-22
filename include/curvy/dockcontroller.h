#pragma once

#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"
#include "curvy/transformpanel.h"
#include "curvy/workspacemodel.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/frame.h>

#include <memory>
#include <vector>

namespace curvy {

    // The optional docks over the canvas (docs/ui-design.md, "Optional docked toolbar and property boxes"): a toolbar flush to the left edge
    // and property boxes flush to the right, both full height, translucent, both off by default. Their layout comes from the mockups
    // (docs/mockups/toolDock.html, propertyDock.html -> Resources/toolDock.newui, propertyDock.newui); this adds the behaviour:
    //
    //   - the tool buttons (the old palette's order and icons) pick the canvas tool and follow it
    //   - the property dock hosts the existing TransformPanel (one box so far) on the views the layout names
    //   - the "Tools" and "Panels" buttons of the top bar, and the View ring, flip WorkspaceModel's two flags; the docks follow the model
    //   - colours come from Theme::current() and are retaken by restyle() when the theme changes
    //
    // A newui::Controller, as the other controllers here: it observes the model it is given (not owned) and the views, and holds no state of
    // its own besides the views.
    // @reflect ignore=true
    class DockController : public newui::Controller {
    public:
        DockController(newui::Frame& frame, CanvasView& canvas, WorkspaceModel& model, SketchDocument& document);
        ~DockController() override = default;
        DockController(const DockController&) = delete;
        DockController& operator=(const DockController&) = delete;

        // Visibility and checked state from the model, and bounds from the canvas (below its rulers, full height).
        void sync();
        // The tool the canvas now has, shown on the buttons.
        void syncTool();
        // Colours from Theme::current().
        void restyle();

        newui::SubView* toolDock() const { return toolDock_; }
        newui::SubView* propertyDock() const { return propertyDock_; }
        newui::ToolbarButton* toolButton(SketchTool tool) const;

    private:
        struct ToolButton {
            newui::ToolbarButton* button = nullptr;
            SketchTool tool = SketchTool::Select;
        };
        void bindTools();
        void bindToggles();
        void layout();

        newui::Frame& frame_;
        CanvasView& canvas_;
        WorkspaceModel& model_;
        SketchDocument& document_;
        newui::SubView* toolDock_ = nullptr;
        newui::SubView* propertyDock_ = nullptr;
        newui::Button* toolbarToggle_ = nullptr;
        newui::Button* propertiesToggle_ = nullptr;
        std::vector<ToolButton> tools_;
        bool shownTools_ = false, shownProperties_ = false;
        float shownRuler_ = -1.0f;
        std::unique_ptr<TransformPanel> transformPanel_;
    };

}
