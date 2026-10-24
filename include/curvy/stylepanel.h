#pragma once

#include "curvy/sketchdocument.h"

#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/rootview.h>
#include <newui/segmentedcontrol.h>

#include "curvy/colorwheel.h"
#include "curvy/gradientbar.h"
#include "curvy/swatchgrid.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace curvy {

    // Wires the Color / Stroke / Appearance panels of sketch.newui to the document: the panels show the
    // style of the selection (or the defaults new shapes get, with nothing selected) and edits flow back
    // as undoable style steps - a slider drag is one step, recorded on mouse-up.
    //
    // Solid/gradient/no fill and stroke in RGB, CMYK, Gray or HSB (the four component sliders follow the
    // color's own space; the mode buttons convert it), stroke weight, whole-object opacity, swatches.
    // Cap/join/dash and the appearance stack follow in later phases.
    // @reflect ignore=true
    class StylePanel {
    public:
        enum class Target { Fill, Stroke };

        StylePanel(newui::RootView& root, SketchDocument& document);
        StylePanel(const StylePanel&) = delete;
        StylePanel& operator=(const StylePanel&) = delete;

        Target target() const { return target_; }
        void setTarget(Target target);

        // Re-reads the selection (or defaults) into every control.
        void refresh();

        // Adds an effect on top of the selection's stack and chooses its row - what a pick in the fx popup does
        // (the popup itself blocks in a native menu loop, so the checks and any other UI call this directly).
        void addEffect(EffectKind kind);

        // The gradient stop the panel edits (index into the target gradient's stops). Setting it does not
        // fire onStopSelected - that is for changes the user makes in the panel.
        std::size_t selectedStop() const { return selectedStop_; }
        void setSelectedStop(std::size_t index);
        newui::Delegate<StylePanel> onStopSelected;
        // The user chose a Linear or Radial fill from the type buttons: the host can put the canvas on the Gradient tool so the line and its stops are
        // draggable on the shape.
        newui::Delegate<StylePanel> onGradientChosen;
        // Fired when the panel's content grows or shrinks (the gradient bar appearing), so a floating card can resize to fit.
        newui::Delegate<StylePanel> onContentChanged;
        // Fired when the fill/stroke target changes.
        newui::Delegate<StylePanel> onTargetChanged;

    private:
        SketchPaint targetPaint() const;
        // Applies `paint` to the target of the selection (or the defaults) and records an undo step unless a
        // drag is in progress.
        void applyPaint(const SketchPaint& paint, const char* description);
        // Applies the current slider values as a solid color.
        // Sets the color being edited: the solid color, or the selected stop of a gradient.
        void applyColor(const SketchColor& color, const char* description);
        // Replaces the target paint with a gradient of the given kind (Linear or Radial).
        void applyGradient(const SketchGradient& gradient, SketchPaint::Kind kind, const char* description);
        // The color the sliders and hex field show.
        SketchColor displayedColor() const;
        void applyGradientFields();
        void applySlidersAsColor();
        void applyHex();
        void applyStrokeWeight();
        // Cap / join / miter: `change` edits the options of the selection (one undo step) or of the defaults.
        void applyStrokeOptions(const std::function<void(StrokeOptions&)>& change, const char* description);
        void applyDashes();
        void applyMiterLimit();
        void applyArrowScale();
        void applyOpacity();
        void applyBlendMode(std::size_t index);

        // ---- Appearance stack ----
        // The list shows the first selected object's items top first, then the base "Opacity" row (object opacity and
        // blend mode). Choosing an item row makes it that kind's active item, so every other control edits it.
        void refreshAppearance(const SketchObject* first);
        void stackRowChosen(std::size_t row);
        // The kind the opacity slider, blend list, eye, duplicate / delete / move act on: the chosen effect row, else
        // the fill / stroke target. (The Color and Stroke panels keep editing target_ while an effect row is chosen.)
        AppearanceKind targetKind() const {
            return effectRowSelected_ ? AppearanceKind::Effect : (target_ == Target::Fill ? AppearanceKind::Fill : AppearanceKind::Stroke);
        }

        // ---- Effects ----
        // The fx button: a popup of the four effects at the cursor; the chosen one goes through addEffect().
        void showEffectMenu();
        // The options row under the blend list: shown only while an effect row is chosen, with the fields that effect has.
        void refreshEffectOptions(const SketchObject* first);
        // Reads one field of the options row and edits the chosen effect with it (one undo step).
        enum class EffectField { X, Y, Size, Color };
        void applyEffectField(EffectField field);
        // Shrinks the Appearance panel by `hidden` pixels (the rows the options row leaves out) - the panel, its body, the
        // sidebar's content and the sidebar's scrollable height are all fixed sizes in sketch.newui, so they are moved
        // together. 0 restores the full height. The first call remembers the layout's heights as the full ones.
        void fitAppearancePanel(float hidden);
        // Moves a sidebar section by `shrink` pixels (negative grows it): the panel, its body and - when `child` names one - the
        // child whose own fixed height changes, then the sidebar's content and scrollable height. Sections are tracked by
        // panel name so several can be adjusted independently.
        void fitSection(const char* panel, const char* body, float shrink, const char* child = nullptr);

        // ---- Graphic Styles ----
        // Redraws the style tiles (each style painted on a small shape) and sizes the grid to its rows.
        void refreshGraphicStyles();
        // The Color panel's swatch grid shows the document's swatches; the buttons add the shown color / delete the chosen swatch.
        void refreshSwatches();
        void styleTilePicked();

        void beginDrag();
        void endDrag();
        // Dragging the caption `labelName` sideways scrubs the number in `fieldName` (numberscrubber.h): every change goes through the
        // field's own Enter handler, and the whole drag is one undo step (beginDrag / endDrag). `unit` is the plain step.
        void bindScrub(const char* labelName, const char* fieldName, double unit, double minimum, double maximum);
        // Runs `edit` (which changes the selection live) and either records a step now or leaves it to endDrag().
        void edit(const char* description, const std::function<void()>& onSelection, const std::function<void()>& onDefaults);

        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_.findView(name));
        }
        void syncWells();
        void syncSlider(const char* sliderName, float value);

        newui::RootView& root_;
        SketchDocument& document_;
        Target target_ = Target::Fill;
        bool updating_ = false;
        ColorWheel* wheel_ = nullptr;
        bool baseRowSelected_ = false;  // the stack's base "Opacity" row is the one being edited
        // The layout's own heights of every section fitSection() has moved, and how far each is moved now.
        struct Section {
            float panelHeight = 0.0f;
            float bodyHeight = 0.0f;
            float childHeight = 0.0f;
            float shrink = 0.0f;
        };
        std::map<std::string, Section> sections_;
        float sidebarContentHeight_ = 0.0f;
        std::size_t styleTiles_ = static_cast<std::size_t>(-1);   // how many style tiles the grid was last sized for
        bool effectRowSelected_ = false;  // an effect row is the one being edited (never together with baseRowSelected_)
        bool dragging_ = false;
        std::string dragDescription_;
        std::vector<StyleState> dragBefore_;
        SketchColor lastSolid_ = SketchColor::fromHex("#e8452c");
        std::size_t selectedStop_ = 0;  // index into the target gradient's stops
    };

}
