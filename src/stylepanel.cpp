#include "curvy/stylepanel.h"

#include "curvy/numberscrubber.h"

#include <newui/color.h>
#include <newui/menus.h>
#include <newui/models.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        // Full weight at the start fading to a thin tip; and a lens: pointed ends swelling to the middle.
        std::vector<WidthPoint> taperProfile() { return {WidthPoint{0.0, 1.0, 1.0}, WidthPoint{1.0, 0.1, 0.1}}; }
        std::vector<WidthPoint> bulgeProfile() { return {WidthPoint{0.0, 0.05, 0.05}, WidthPoint{0.5, 1.5, 1.5}, WidthPoint{1.0, 0.05, 0.05}}; }
        std::string narrow(const std::wstring& text) {
            std::string result;
            for (wchar_t ch : text) {
                result.push_back(ch < 128 ? char(ch) : '?');
            }
            return result;
        }

        std::wstring widen(const std::string& text) {
            return std::wstring(text.begin(), text.end());
        }

        // The leading number in text such as "4 pt" or "55%"; false when there is none.
        bool leadingNumber(const std::wstring& text, double& out) {
            std::string s = narrow(text);
            char* end = nullptr;
            double value = std::strtod(s.c_str(), &end);
            if (end == s.c_str()) {
                return false;
            }
            out = value;
            return true;
        }

        Color toColor(const SketchColor& color) {
            BLRgba32 rgba = color.toRgba32();
            return Color(float(rgba.r()) / 255.0f, float(rgba.g()) / 255.0f, float(rgba.b()) / 255.0f, float(rgba.a()) / 255.0f);
        }

        const char* const kColorSliders[] = {"comp0Slider", "comp1Slider", "comp2Slider", "comp3Slider", "alphaSlider"};

        // How each color space is laid out in the four component sliders.
        struct ModeLayout {
            std::size_t count;
            const char* captions[4];
            float maxima[4];
        };
        const ModeLayout kLayouts[] = {
            {1, {"K", "", "", ""}, {100.0f, 0.0f, 0.0f, 0.0f}},               // Gray: ink coverage like Illustrator (0 = white)
            {3, {"R", "G", "B", ""}, {255.0f, 255.0f, 255.0f, 0.0f}},         // RGB
            {4, {"C", "M", "Y", "K"}, {100.0f, 100.0f, 100.0f, 100.0f}},      // CMYK
            {3, {"H", "S", "B", ""}, {360.0f, 100.0f, 100.0f, 0.0f}},         // HSB
        };

        // Index into kLayouts / the segmented control (Gray, RGB, CMYK, HSB).
        std::size_t modeIndex(ColorSpace space) {
            switch (space) {
                case ColorSpace::Gray: return 0;
                case ColorSpace::CMYK: return 2;
                case ColorSpace::HSB: return 3;
                default: return 1;
            }
        }
        ColorSpace spaceAt(std::size_t index) {
            static const ColorSpace kSpaces[] = {ColorSpace::Gray, ColorSpace::RGB, ColorSpace::CMYK, ColorSpace::HSB};
            return kSpaces[index < 4 ? index : 1];
        }

        // The slider positions for a color in its own space.
        void sliderValues(const SketchColor& color, float out[4]) {
            const ModeLayout& layout = kLayouts[modeIndex(color.space())];
            for (std::size_t i = 0; i < 4; ++i) {
                out[i] = i < layout.count ? color.component(i) * layout.maxima[i] : 0.0f;
            }
            if (color.space() == ColorSpace::Gray) {
                out[0] = (1.0f - color.component(0)) * 100.0f;
            }
        }

        SketchColor colorFromSliders(ColorSpace space, const float v[4], float alpha) {
            const ModeLayout& layout = kLayouts[modeIndex(space)];
            float n[4];
            for (std::size_t i = 0; i < 4; ++i) {
                n[i] = layout.maxima[i] > 0.0f ? v[i] / layout.maxima[i] : 0.0f;
            }
            switch (space) {
                case ColorSpace::CMYK: return SketchColor::cmyk(n[0], n[1], n[2], n[3], alpha);
                case ColorSpace::Gray: return SketchColor::gray(1.0f - n[0], alpha);
                case ColorSpace::HSB: return SketchColor::hsb(n[0], n[1], n[2], alpha);
                default: return SketchColor::rgb(n[0], n[1], n[2], alpha);
            }
        }
    }

    StylePanel::StylePanel(RootView& root, SketchDocument& document)
        : root_(root), document_(document) {

        // The hue ring and the gradient strip are drawn by views of their own; the layout holds a host for each (and the tabs that swap the ring for
        // the sliders).
        if (auto* host = find<SubView>("colorWheelHost")) {
            wheel_ = new ColorWheel();
            host->addChild(wheel_);
            wheel_->setBounds(Rect(Point(0.0f, 0.0f), host->bounds().size()));
            host->onSizeChanged += [this](View&, const Size& size) {
                wheel_->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
                return SyncReturn::Handled;
            };
        }
        if (auto* host = find<SubView>("gradientBarHost"); host != nullptr && find<GradientBar>("gradientBar") == nullptr) {
            auto* bar = new GradientBar();
            bar->setName("gradientBar");
            host->addChild(bar);
            bar->setBounds(Rect(Point(0.0f, 0.0f), host->bounds().size()));
            host->onSizeChanged += [bar](View&, const Size& size) {
                bar->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
                return SyncReturn::Handled;
            };
        }
        if (auto* tabs = find<SegmentedControl>("pickerTabs")) {
            tabs->setSegments({"Wheel", "Sliders"});
            tabs->setSelectedIndex(0);
            auto show = [this](bool wheel) {
                if (auto* area = find<View>("colorWheelHost")) {
                    area->setVisible(wheel);
                }
                if (auto* area = find<View>("pickerSliderArea")) {
                    area->setVisible(!wheel);
                }
                root_.updateLayout();
            };
            tabs->onSelectionChanged += [show](SegmentedControl& changed) {
                show(changed.selectedIndex() != std::optional<std::size_t>(1));
                return SyncReturn::Handled;
            };
            show(true);
        }

        // Fill / Stroke wells in the tool palette choose what the Color panel edits.
        if (auto* well = find<SubView>("fillWell")) {
            well->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                setTarget(Target::Fill);
                return SyncReturn::Handled;
            };
        }
        if (auto* well = find<SubView>("strokeWell")) {
            well->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                setTarget(Target::Stroke);
                return SyncReturn::Handled;
            };
        }

        // The "Fill / Stroke" text in the Color panel header toggles the target too (the wells sit at the very
        // bottom of the tool palette, which is easy to miss or to have clipped by a short window).
        if (auto* info = find<View>("colorPanelHeaderInfo")) {
            info->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                setTarget(target_ == Target::Fill ? Target::Stroke : Target::Fill);
                return SyncReturn::Handled;
            };
        }

        // Sliders: a drag is live while the button is down and one undo step on release. The four component
        // sliders change meaning and range with the color mode (see kLayouts) - refresh() sets them; alpha is 0..100.
        for (const char* name : kColorSliders) {
            if (auto* slider = find<Slider>(name)) {
                slider->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                    beginDrag();
                    return SyncReturn::Ignored;  // the slider still needs the press
                };
                slider->onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                    endDrag();
                    return SyncReturn::Ignored;
                };
                slider->onValueChanged += [this, name](Slider& changed) {
                    if (updating_) {
                        return SyncReturn::Ignored;
                    }
                    if (auto* value = find<TextField>((std::string(name) + "Value").c_str())) {
                        updating_ = true;
                        value->setText(widen(std::to_string(int(changed.value() + 0.5f))));
                        updating_ = false;
                    }
                    applySlidersAsColor();
                    return SyncReturn::Handled;
                };
            }
            // Typing a number in the value box moves the slider (and so applies the color).
            if (auto* field = find<TextField>((std::string(name) + "Value").c_str())) {
                field->onReturnPressed += [this, name](TextField& typed) {
                    double number = 0.0;
                    if (auto* slider = find<Slider>(name)) {
                        if (leadingNumber(typed.text(), number)) {
                            slider->setValue(float(number));
                        } else {
                            refresh();
                        }
                    }
                    return SyncReturn::Handled;
                };
            }
        }

        if (auto* opacity = find<Slider>("opacitySlider")) {
            opacity->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                beginDrag();
                return SyncReturn::Ignored;
            };
            opacity->onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                endDrag();
                return SyncReturn::Ignored;
            };
            opacity->onValueChanged += [this](Slider& changed) {
                if (updating_) {
                    return SyncReturn::Ignored;
                }
                if (auto* value = find<TextField>("opacitySliderValue")) {
                    updating_ = true;
                    value->setText(widen(std::to_string(int(changed.value() + 0.5f))));
                    updating_ = false;
                }
                applyOpacity();
                return SyncReturn::Handled;
            };
        }
        if (auto* field = find<TextField>("opacitySliderValue")) {
            field->onReturnPressed += [this](TextField& typed) {
                double number = 0.0;
                if (auto* slider = find<Slider>("opacitySlider")) {
                    if (leadingNumber(typed.text(), number)) {
                        slider->setValue(float(number));
                    } else {
                        refresh();
                    }
                }
                return SyncReturn::Handled;
            };
        }

        if (auto* hex = find<TextField>("hexField")) {
            hex->onReturnPressed += [this](TextField&) {
                applyHex();
                return SyncReturn::Handled;
            };
            hex->onLostFocus += [this](View&) {
                applyHex();
                return SyncReturn::Handled;
            };
        }
        if (auto* weight = find<TextField>("strokeWeightField")) {
            weight->onReturnPressed += [this](TextField&) {
                applyStrokeWeight();
                return SyncReturn::Handled;
            };
            weight->onLostFocus += [this](View&) {
                applyStrokeWeight();
                return SyncReturn::Handled;
            };
        }

        // Cap and join are radio groups of toggle buttons: checking one applies it, unchecking the active one snaps back.
        auto wireOption = [this](const char* name, std::function<void(StrokeOptions&)> set, const char* what) {
            if (auto* button = find<ToolbarButton>(name)) {
                button->onCheckedChanged += [this, set, what](ToolbarButton& changed) {
                    if (updating_) {
                        return SyncReturn::Ignored;
                    }
                    if (changed.isChecked()) {
                        applyStrokeOptions(set, what);
                    }
                    refresh();
                    return SyncReturn::Handled;
                };
            }
        };
        wireOption("capButtButton", [](StrokeOptions& o) { o.cap = StrokeCap::Butt; }, "Butt Cap");
        wireOption("capRoundButton", [](StrokeOptions& o) { o.cap = StrokeCap::Round; }, "Round Cap");
        wireOption("capSquareButton", [](StrokeOptions& o) { o.cap = StrokeCap::Square; }, "Square Cap");
        wireOption("joinMiterButton", [](StrokeOptions& o) { o.join = StrokeJoin::Miter; }, "Miter Join");
        wireOption("joinRoundButton", [](StrokeOptions& o) { o.join = StrokeJoin::Round; }, "Round Join");
        wireOption("joinBevelButton", [](StrokeOptions& o) { o.join = StrokeJoin::Bevel; }, "Bevel Join");
        // Align: where the stroke of a closed outline sits.
        wireOption("alignCenterStroke", [](StrokeOptions& o) { o.align = StrokeAlign::Center; }, "Align Stroke to Center");
        wireOption("alignInsideStroke", [](StrokeOptions& o) { o.align = StrokeAlign::Inside; }, "Align Stroke to Inside");
        wireOption("alignOutsideStroke", [](StrokeOptions& o) { o.align = StrokeAlign::Outside; }, "Align Stroke to Outside");
        // Arrows: the heads on the ends of an open path, and their size.
        for (bool atEnd : {false, true}) {
            if (auto* combo = find<DropDownList>(atEnd ? "arrowEndCombo" : "arrowStartCombo")) {
                combo->onSelectionChanged += [this, atEnd](DropDownList& changed) {
                    std::optional<std::size_t> index = changed.selectedIndex();
                    if (updating_ || !index.has_value()) {
                        return SyncReturn::Ignored;
                    }
                    const ArrowStyle style = *index < 4 ? ArrowStyle(*index) : ArrowStyle::None;
                    applyStrokeOptions([atEnd, style](StrokeOptions& o) { (atEnd ? o.arrowEnd : o.arrowStart) = style; }, atEnd ? "End Arrow" : "Start Arrow");
                    return SyncReturn::Handled;
                };
            }
        }
        if (auto* scale = find<TextField>("arrowScaleField")) {
            scale->onReturnPressed += [this](TextField&) {
                applyArrowScale();
                return SyncReturn::Handled;
            };
            scale->onLostFocus += [this](View&) {
                applyArrowScale();
                return SyncReturn::Handled;
            };
        }
        // Width profiles: presets for the variable-width stroke (the Width tool edits the points by hand).
        wireOption("profileUniformButton", [](StrokeOptions& o) { o.widths.clear(); }, "Uniform Width");
        wireOption("profileTaperButton", [](StrokeOptions& o) { o.widths = taperProfile(); }, "Taper Width");
        wireOption("profileBulgeButton", [](StrokeOptions& o) { o.widths = bulgeProfile(); }, "Bulge Width");
        if (auto* miter = find<TextField>("miterLimitField")) {
            miter->onReturnPressed += [this](TextField&) {
                applyMiterLimit();
                return SyncReturn::Handled;
            };
            miter->onLostFocus += [this](View&) {
                applyMiterLimit();
                return SyncReturn::Handled;
            };
        }

        // Dashes: the toggle turns the pattern on (from the four fields, 12/6 when blank); editing a field re-applies it.
        if (auto* toggle = find<Toggle>("dashedToggle")) {
            toggle->onCheckedChanged += [this](Toggle&) {
                applyDashes();
                return SyncReturn::Handled;
            };
        }
        for (const char* name : {"dashField", "gapField", "dash2Field", "gap2Field"}) {
            if (auto* field = find<TextField>(name)) {
                field->onReturnPressed += [this](TextField&) {
                    applyDashes();
                    return SyncReturn::Handled;
                };
                field->onLostFocus += [this](View&) {
                    applyDashes();
                    return SyncReturn::Handled;
                };
            }
        }

        // Appearance stack: choose a row, then add / duplicate / delete / reorder / hide / clear.
        if (auto* stack = find<ListView>("appearanceStack")) {
            stack->onSelectionChanged += [this](ListView& list) {
                std::optional<std::size_t> row = list.selectedIndex();
                if (!updating_ && row.has_value()) {
                    stackRowChosen(*row);
                }
                return SyncReturn::Handled;
            };
        }
        auto onTool = [this](const char* name, std::function<void()> action) {
            if (auto* button = find<Control>(name)) {
                button->onClick += [action](Control&) {
                    action();
                    return SyncReturn::Handled;
                };
            }
        };
        onTool("newEffectButton", [this]() { showEffectMenu(); });
        onTool("newStrokeButton", [this]() { baseRowSelected_ = false; effectRowSelected_ = false; target_ = Target::Stroke; document_.addAppearanceItem(AppearanceKind::Stroke); refresh(); onTargetChanged(*this); });
        onTool("newFillButton", [this]() { baseRowSelected_ = false; effectRowSelected_ = false; target_ = Target::Fill; document_.addAppearanceItem(AppearanceKind::Fill); refresh(); onTargetChanged(*this); });
        onTool("duplicateItemButton", [this]() { if (!baseRowSelected_) { document_.duplicateActiveItem(targetKind()); } });
        onTool("deleteItemButton", [this]() { if (!baseRowSelected_) { document_.deleteActiveItem(targetKind()); } });
        onTool("moveItemUpButton", [this]() { if (!baseRowSelected_) { document_.moveActiveItem(targetKind(), 1); } });
        onTool("moveItemDownButton", [this]() { if (!baseRowSelected_) { document_.moveActiveItem(targetKind(), -1); } });
        onTool("clearAppearanceButton", [this]() { baseRowSelected_ = false; effectRowSelected_ = false; document_.clearAppearance(); });
        // Effect options: Enter or leaving a field applies it.
        for (auto [name, field] : std::initializer_list<std::pair<const char*, EffectField>>{
                {"effectXField", EffectField::X}, {"effectYField", EffectField::Y},
                {"effectSizeField", EffectField::Size}, {"effectColorField", EffectField::Color}}) {
            if (auto* edit = find<TextField>(name)) {
                EffectField which = field;
                edit->onReturnPressed += [this, which](TextField&) {
                    applyEffectField(which);
                    return SyncReturn::Handled;
                };
                edit->onLostFocus += [this, which](View&) {
                    applyEffectField(which);
                    return SyncReturn::Handled;
                };
            }
        }
        if (auto* eye = find<ToolbarButton>("itemVisibleButton")) {
            eye->onCheckedChanged += [this](ToolbarButton& changed) {
                if (updating_ || baseRowSelected_ || document_.selection().empty()) {
                    return SyncReturn::Ignored;
                }
                bool visible = changed.isChecked();
                AppearanceKind kind = targetKind();
                edit(visible ? "Show Item" : "Hide Item", [this, kind, visible]() { document_.setSelectionItemVisible(kind, visible); }, []() {});
                return SyncReturn::Handled;
            };
        }

        if (auto* styles = find<SwatchGrid>("graphicStyleGrid")) {
            styles->onSwatchPicked += [this](SwatchGrid&) {
                styleTilePicked();
                return SyncReturn::Handled;
            };
        }
        onTool("newGraphicStyleButton", [this]() {
            if (document_.addGraphicStyleFromSelection()) {
                if (auto* grid = find<SwatchGrid>("graphicStyleGrid")) {
                    grid->setSelectedIndex(document_.graphicStyles().size() - 1);   // the new style
                }
            }
            refreshGraphicStyles();
        });
        onTool("deleteGraphicStyleButton", [this]() {
            if (auto* grid = find<SwatchGrid>("graphicStyleGrid")) {
                if (document_.deleteGraphicStyle(grid->selectedIndex())) {
                    grid->setSelectedIndex(document_.graphicStyles().size());   // none
                }
            }
            refreshGraphicStyles();
        });
        onTool("addSwatchButton", [this]() {
            document_.addSwatch(displayedColor().convertedTo(ColorSpace::RGB).toHex());
            refreshSwatches();
        });
        onTool("deleteSwatchButton", [this]() {
            if (auto* grid = find<SwatchGrid>("swatchGrid")) {
                if (document_.deleteSwatch(grid->selectedIndex())) {
                    grid->setSelectedIndex(document_.swatches().size());   // none chosen
                }
            }
            refreshSwatches();
        });
        if (auto* swatches = find<SwatchGrid>("swatchGrid")) {
            swatches->onSwatchPicked += [this](SwatchGrid& grid) {
                std::size_t index = grid.selectedIndex();
                if (index < grid.swatches().size()) {
                    try {
                        // Swatches are hex (RGB); the document's color mode decides the space they are applied in.
                        SketchColor color = SketchColor::fromHex(grid.swatches()[index]).convertedTo(document_.colorMode());
                        applyColor(color, "Apply Swatch");
                    } catch (const std::runtime_error&) {
                    }
                }
                return SyncReturn::Handled;
            };
        }

        // Fill type: none / solid / linear / radial. Switching between kinds keeps what it can (a gradient's
        // selected stop becomes the solid color, a solid becomes the gradient's first stop).
        auto wireType = [this](const char* name, int kind) {
            if (auto* button = find<ToolbarButton>(name)) {
                button->onCheckedChanged += [this, kind](ToolbarButton& changed) {
                    if (updating_) {
                        return SyncReturn::Ignored;
                    }
                    if (changed.isChecked()) {
                        SketchPaint current = targetPaint();
                        if (kind == 0) {
                            applyPaint(SketchPaint::none(), "No Paint");
                        } else if (kind == 1) {
                            applyPaint(SketchPaint::solid(current.isNone() ? lastSolid_ : displayedColor()), "Solid Paint");
                        } else if (kind == 4) {
                            if (!current.isPoints()) {
                                applyPaint(SketchPaint::points(SketchGradient::pointsFromColor(current.isNone() ? lastSolid_ : displayedColor())), "Point Gradient");
                            }
                            selectedStop_ = 0;
                            refresh();
                            onGradientChosen(*this);
                            return SyncReturn::Handled;
                        } else {
                            SketchGradient gradient = current.isGradient() ? current.gradient()
                                : SketchGradient::fromColor(current.isNone() ? lastSolid_ : current.color());
                            applyGradient(gradient, kind == 2 ? SketchPaint::Kind::Linear : SketchPaint::Kind::Radial,
                                kind == 2 ? "Linear Gradient" : "Radial Gradient");
                            refresh();
                            onGradientChosen(*this);
                            return SyncReturn::Handled;
                        }
                    }
                    refresh();
                    return SyncReturn::Handled;
                };
            }
        };
        wireType("fillNoneButton", 0);
        wireType("fillSolidButton", 1);
        wireType("fillLinearButton", 2);
        wireType("fillRadialButton", 3);
        wireType("fillPointsButton", 4);

        // The gradient bar: click/drag stops. Dragging is live and one undo step on release, like a slider.
        if (auto* bar = find<GradientBar>("gradientBar")) {
            bar->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                beginDrag();
                return SyncReturn::Ignored;
            };
            bar->onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                endDrag();
                return SyncReturn::Ignored;
            };
            bar->onStopSelected += [this](GradientBar& changed) {
                selectedStop_ = changed.selectedStop();
                refresh();
                onStopSelected(*this);
                return SyncReturn::Handled;
            };
            bar->onStopsEdited += [this](GradientBar& changed) {
                SketchPaint paint = targetPaint();
                if (!paint.isGradient()) {
                    refresh();  // the bar only edits a gradient paint
                    return SyncReturn::Handled;
                }
                SketchGradient gradient = paint.gradient();
                gradient.stops.clear();
                for (const GradientBar::Stop& stop : changed.parsedStops()) {
                    gradient.stops.push_back({stop.offset, SketchColor::rgb(stop.color.r, stop.color.g, stop.color.b, stop.color.a)});
                }
                selectedStop_ = changed.selectedStop();
                applyGradient(gradient, paint.kind(), "Edit Gradient");
                return SyncReturn::Handled;
            };
        }
        for (const char* name : {"stopOffsetField", "gradientAngleField"}) {
            if (auto* field = find<TextField>(name)) {
                field->onReturnPressed += [this](TextField&) {
                    applyGradientFields();
                    return SyncReturn::Handled;
                };
                field->onLostFocus += [this](View&) {
                    applyGradientFields();
                    return SyncReturn::Handled;
                };
            }
        }
        auto wireButton = [this](const char* name, std::function<void()> action) {
            if (auto* button = find<ToolbarButton>(name)) {
                button->onClick += [action](Control&) {
                    action();
                    return SyncReturn::Handled;
                };
            }
        };
        wireButton("addStopButton", [this]() {
            SketchPaint paint = targetPaint();
            if (!paint.isGradient() || paint.gradient().stops.size() < 1) {
                return;
            }
            SketchGradient gradient = paint.gradient();
            std::size_t index = std::min(selectedStop_, gradient.stops.size() - 1);
            // Halfway between the selected stop and the next one (or the previous one at the end).
            double here = gradient.stops[index].offset;
            double other = index + 1 < gradient.stops.size() ? gradient.stops[index + 1].offset
                : (index > 0 ? gradient.stops[index - 1].offset : 1.0);
            double offset = (here + other) * 0.5;
            gradient.stops.push_back({offset, gradient.colorAt(offset)});
            gradient.sortStops();
            for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
                if (gradient.stops[i].offset == offset) {
                    selectedStop_ = i;
                }
            }
            applyGradient(gradient, paint.kind(), "Add Gradient Stop");
            refresh();
            onStopSelected(*this);
        });
        wireButton("deleteStopButton", [this]() {
            SketchPaint paint = targetPaint();
            if (!paint.isGradient() || paint.gradient().stops.size() <= 2) {
                return;  // a gradient keeps at least two stops
            }
            SketchGradient gradient = paint.gradient();
            std::size_t index = std::min(selectedStop_, gradient.stops.size() - 1);
            gradient.stops.erase(gradient.stops.begin() + std::ptrdiff_t(index));
            selectedStop_ = std::min(index, gradient.stops.size() - 1);
            applyGradient(gradient, paint.kind(), "Delete Gradient Stop");
            refresh();
            onStopSelected(*this);
        });
        wireButton("reverseGradientButton", [this]() {
            SketchPaint paint = targetPaint();
            if (!paint.isGradient()) {
                return;
            }
            SketchGradient gradient = paint.gradient();
            gradient.reverse();
            selectedStop_ = gradient.stops.empty() ? 0 : gradient.stops.size() - 1 - std::min(selectedStop_, gradient.stops.size() - 1);
            applyGradient(gradient, paint.kind(), "Reverse Gradient");
            refresh();
        });
        if (auto* spread = find<DropDownList>("spreadCombo")) {
            spread->onSelectionChanged += [this](DropDownList& combo) {
                SketchPaint paint = targetPaint();
                std::optional<std::size_t> index = combo.selectedIndex();
                if (updating_ || !paint.isGradient() || !index.has_value()) {
                    return SyncReturn::Ignored;
                }
                SketchGradient gradient = paint.gradient();
                gradient.spread = *index == 1 ? GradientSpread::Repeat : (*index == 2 ? GradientSpread::Reflect : GradientSpread::Pad);
                if (gradient != paint.gradient()) {
                    applyGradient(gradient, paint.kind(), "Gradient Spread");
                }
                return SyncReturn::Handled;
            };
        }

        // Blend mode of the selected objects (the Appearance panel's list; it follows the list's order).
        if (auto* blend = find<DropDownList>("blendModeCombo")) {
            blend->onSelectionChanged += [this](DropDownList& combo) {
                std::optional<std::size_t> index = combo.selectedIndex();
                if (updating_ || !index.has_value() || document_.selection().empty()) {
                    return SyncReturn::Ignored;
                }
                applyBlendMode(*index);
                return SyncReturn::Handled;
            };
        }

        // Apply: across the object (default) or, for a stroke, along its length.
        if (auto* apply = find<DropDownList>("gradientApplyCombo")) {
            apply->onSelectionChanged += [this](DropDownList& combo) {
                SketchPaint paint = targetPaint();
                std::optional<std::size_t> index = combo.selectedIndex();
                if (updating_ || !paint.isGradient() || !index.has_value()) {
                    return SyncReturn::Ignored;
                }
                SketchGradient gradient = paint.gradient();
                gradient.alongStroke = *index == 1 && target_ == Target::Stroke;
                if (gradient != paint.gradient()) {
                    applyGradient(gradient, paint.kind(), "Gradient Along Stroke");
                }
                return SyncReturn::Handled;
            };
        }

        // The color-mode control shows the space of the color being edited; clicking another converts that
        // color into it (one undo step). With no paint there is nothing to convert, so it just snaps back.
        if (auto* mode = find<SegmentedControl>("colorModeControl")) {
            mode->onSelectionChanged += [this](SegmentedControl& changed) {
                if (updating_) {
                    return SyncReturn::Ignored;
                }
                std::optional<std::size_t> index = changed.selectedIndex();
                if (index.has_value() && !targetPaint().isNone()) {
                    SketchColor color = displayedColor();
                    ColorSpace space = spaceAt(*index);
                    if (color.space() != space) {
                        applyColor(color.convertedTo(space), "Convert Color Mode");
                    }
                }
                refresh();
                return SyncReturn::Handled;
            };
        }

        if (wheel_ != nullptr) {
            wheel_->onDragStarted += [this](ColorWheel&) {
                beginDrag();
                return SyncReturn::Handled;
            };
            wheel_->onDragFinished += [this](ColorWheel&) {
                endDrag();
                refresh();
                return SyncReturn::Handled;
            };
            wheel_->onChanged += [this](ColorWheel& changed) {
                if (updating_) {
                    return SyncReturn::Ignored;
                }
                const SketchColor shown = displayedColor();
                SketchColor color = SketchColor::hsb(float(changed.hue()), float(changed.saturation()), float(changed.brightness()), shown.alpha());
                applyColor(color.convertedTo(shown.space()), target_ == Target::Fill ? "Change Fill" : "Change Stroke");
                return SyncReturn::Handled;
            };
        }

        document_.onSelectionChanged += [this](SketchDocument&) {
            refresh();
            return SyncReturn::Handled;
        };
        document_.onChanged += [this](SketchDocument&) {
            if (!dragging_) {
                refresh();
            }
            return SyncReturn::Handled;
        };
        // Dragging a caption scrubs its number. The plain step is the unit; Shift makes it a tenth, Ctrl+Shift a hundredth.
        bindScrub("strokeWeightLabel", "strokeWeightField", 0.5, 0.0, 1000.0);
        bindScrub("miterLimitLabel", "miterLimitField", 1.0, 1.0, 500.0);
        bindScrub("stopOffsetLabel", "stopOffsetField", 1.0, 0.0, 100.0);
        bindScrub("gradientAngleLabel", "gradientAngleField", 1.0, -360.0, 360.0);
        bindScrub("effectXLabel", "effectXField", 1.0, -1000.0, 1000.0);
        bindScrub("effectYLabel", "effectYField", 1.0, -1000.0, 1000.0);
        bindScrub("effectSizeLabel", "effectSizeField", 1.0, -1000.0, 1000.0);   // Offset Path may go negative; the others clamp at 0 themselves
        refresh();
    }

    void StylePanel::bindScrub(const char* labelName, const char* fieldName, double unit, double minimum, double maximum) {
        auto* label = find<Label>(labelName);
        if (label == nullptr || find<TextField>(fieldName) == nullptr) {
            return;
        }
        auto suffix = std::make_shared<std::string>();   // what follows the number in the field ("pt", "%", the degree sign)
        auto scrubber = NumberScrubber::attach(*label, unit, minimum, maximum);
        scrubber->onBegin += [this, fieldName, suffix](NumberScrubber&, double& value) {
            auto* field = find<TextField>(fieldName);
            if (field == nullptr || !field->isEnabled() || !field->isVisible() || !leadingNumber(field->text(), value)) {
                return SyncReturn::Ignored;
            }
            std::string text = narrow(field->text());
            std::size_t at = 0;
            while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '.' || text[at] == '-' || text[at] == '+')) { ++at; }
            *suffix = text.substr(at);
            beginDrag();
            return SyncReturn::Handled;
        };
        scrubber->onChange += [this, fieldName, suffix](NumberScrubber&, double value) {
            if (auto* field = find<TextField>(fieldName)) {
                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "%g", value);
                field->setText(widen(buffer + *suffix));
                field->onReturnPressed(*field);
            }
            return SyncReturn::Handled;
        };
        scrubber->onEnd += [this](NumberScrubber&, bool) {
            endDrag();
            return SyncReturn::Handled;
        };
    }

    namespace {
        // One row of the Appearance stack: "Stroke  4 pt  Solid #1b3a6b  50%  Multiply  (hidden)".
        std::string appearanceRowText(const AppearanceItem& item) {
            if (item.kind == AppearanceKind::Effect) {
                // "Drop Shadow   Multiply   75%   x 4 y 4   blur 6"
                char buffer[64];
                std::string text = std::string(effectName(item.effect)) + "   ";
                if (isTintedEffect(item.effect)) {
                    if (item.blendMode != BlendMode::Normal) {
                        text += std::string(blendModeName(item.blendMode)) + "   ";
                    }
                    text += std::to_string(int(item.opacity * 100.0 + 0.5)) + "%   ";
                }
                if (item.effect == EffectKind::DropShadow || item.effect == EffectKind::InnerShadow) {
                    std::snprintf(buffer, sizeof(buffer), "x %g y %g   ", item.offsetX, item.offsetY);
                    text += buffer;
                }
                switch (item.effect) {
                    case EffectKind::RoundCorners: std::snprintf(buffer, sizeof(buffer), "radius %g", item.radius); break;
                    case EffectKind::OffsetPath: std::snprintf(buffer, sizeof(buffer), "offset %g", item.radius); break;
                    case EffectKind::Roughen: std::snprintf(buffer, sizeof(buffer), "size %g   detail %g", item.radius, item.offsetX); break;
                    default: std::snprintf(buffer, sizeof(buffer), "blur %g", item.softness); break;
                }
                text += buffer;
                if (!item.visible) {
                    text += "   (hidden)";
                }
                return text;
            }
            std::string text = item.kind == AppearanceKind::Fill ? "Fill   " : "Stroke   ";
            if (item.kind == AppearanceKind::Stroke) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%g pt   ", item.strokeWidth);
                text += buffer;
            }
            switch (item.paint.kind()) {
                case SketchPaint::Kind::None: text += "None"; break;
                case SketchPaint::Kind::Solid: text += "Solid " + item.paint.color().toHex(); break;
                case SketchPaint::Kind::Linear: text += "Linear gradient"; break;
                case SketchPaint::Kind::Radial: text += "Radial gradient"; break;
                case SketchPaint::Kind::Pattern: text += "Pattern"; break;
                case SketchPaint::Kind::Points: text += "Point gradient"; break;
            }
            if (item.opacity < 1.0) {
                text += "   " + std::to_string(int(item.opacity * 100.0 + 0.5)) + "%";
            }
            if (item.blendMode != BlendMode::Normal) {
                text += std::string("   ") + blendModeName(item.blendMode);
            }
            if (!item.visible) {
                text += "   (hidden)";
            }
            return text;
        }
    }

    void StylePanel::refreshAppearance(const SketchObject* first) {
        auto* list = find<ListView>("appearanceStack");
        if (list == nullptr) {
            return;
        }
        std::vector<std::string> rows;
        if (first != nullptr) {
            const std::vector<AppearanceItem>& items = first->appearance();
            for (std::size_t i = items.size(); i-- > 0;) {  // the top of the stack first
                rows.push_back(appearanceRowText(items[i]));
            }
            std::size_t group = document_.selectedGroup();
            GroupProps props = document_.groupProps(group);
            double opacity = group != 0 ? props.opacity : first->opacity();
            BlendMode mode = group != 0 ? props.blendMode : first->blendMode();
            std::string base = std::string(group != 0 ? "Group Opacity   " : "Opacity   ") + std::to_string(int(opacity * 100.0 + 0.5)) + "%";
            if (mode != BlendMode::Normal) {
                base += std::string("   ") + blendModeName(mode);
            }
            rows.push_back(base);
        }
        auto* model = dynamic_cast<StringListModel*>(list->model());
        if (model == nullptr || model->items() != rows) {
            auto fresh = std::make_unique<StringListModel>();
            fresh->items() = rows;
            list->setModel(std::move(fresh));
        }
        if (first == nullptr) {
            list->clearSelection();
        } else {
            std::size_t itemCount = rows.size() - 1;
            std::size_t row = baseRowSelected_ ? itemCount : itemCount - 1 - std::min(itemCount - 1, first->activeIndex(targetKind()));
            if (list->selectedIndex() != std::optional<std::size_t>(row)) {
                list->setSelectedIndex(row);
            }
        }
        bool itemRow = first != nullptr && !baseRowSelected_;
        if (auto* eye = find<ToolbarButton>("itemVisibleButton")) {
            eye->setEnabled(itemRow);
            bool visible = itemRow ? first->activeItem(targetKind()).visible : true;
            if (eye->isChecked() != visible) {
                eye->setChecked(visible);
            }
        }
        for (const char* name : {"duplicateItemButton", "deleteItemButton", "moveItemUpButton", "moveItemDownButton"}) {
            if (auto* button = find<Control>(name)) {
                button->setEnabled(itemRow);
            }
        }
        for (const char* name : {"newStrokeButton", "newFillButton", "newEffectButton", "clearAppearanceButton"}) {
            if (auto* button = find<Control>(name)) {
                button->setEnabled(first != nullptr);
            }
        }
    }

    void StylePanel::stackRowChosen(std::size_t row) {
        const SketchObject* first = document_.selection().empty() ? nullptr : document_.findById(document_.selection().front());
        if (first == nullptr) {
            return;
        }
        std::size_t count = first->appearance().size();
        if (row >= count) {  // the base "Opacity" row
            baseRowSelected_ = true;
            refresh();
            return;
        }
        std::size_t index = count - 1 - row;
        baseRowSelected_ = false;
        AppearanceKind chosen = first->appearance()[index].kind;
        if (chosen == AppearanceKind::Effect) {
            // An effect row leaves the fill / stroke target alone: the Color and Stroke panels keep their paint.
            effectRowSelected_ = true;
            document_.setActiveItem(chosen, index);
            refresh();
            return;
        }
        effectRowSelected_ = false;
        Target before = target_;
        target_ = chosen == AppearanceKind::Fill ? Target::Fill : Target::Stroke;
        document_.setActiveItem(chosen, index);
        refresh();
        if (target_ != before) {
            onTargetChanged(*this);
        }
    }

    void StylePanel::setTarget(Target target) {
        target_ = target;
        baseRowSelected_ = false;
        effectRowSelected_ = false;
        refresh();
        onTargetChanged(*this);
    }

    void StylePanel::setSelectedStop(std::size_t index) {
        selectedStop_ = index;
        refresh();
    }

    SketchPaint StylePanel::targetPaint() const {
        if (!document_.selection().empty()) {
            if (const SketchObject* object = document_.findById(document_.selection().front())) {
                return target_ == Target::Fill ? object->fill() : object->stroke();
            }
        }
        return target_ == Target::Fill ? document_.defaultFill() : document_.defaultStroke();
    }

    void StylePanel::beginDrag() {
        if (!dragging_) {
            dragging_ = true;
            dragBefore_ = document_.snapshotStyle();
            dragDescription_.clear();
        }
    }

    void StylePanel::endDrag() {
        if (!dragging_) {
            return;
        }
        dragging_ = false;
        if (!dragBefore_.empty() && !dragDescription_.empty()) {
            document_.commitStyle(dragBefore_, dragDescription_);
        }
        dragBefore_.clear();
        refresh();
    }

    void StylePanel::edit(const char* description, const std::function<void()>& onSelection, const std::function<void()>& onDefaults) {
        if (document_.selection().empty()) {
            onDefaults();
            return;
        }
        std::vector<StyleState> before;
        if (!dragging_) {
            before = document_.snapshotStyle();
        }
        onSelection();
        if (dragging_) {
            dragDescription_ = description;
        } else {
            document_.commitStyle(before, description);
        }
    }

    void StylePanel::applyPaint(const SketchPaint& paint, const char* description) {
        bool fill = target_ == Target::Fill;
        edit(description,
            [this, fill, paint]() { if (fill) { document_.setSelectionFill(paint); } else { document_.setSelectionStroke(paint); } },
            [this, fill, paint]() { if (fill) { document_.setDefaultFill(paint); } else { document_.setDefaultStroke(paint); } });
        if (paint.kind() == SketchPaint::Kind::Solid) {
            lastSolid_ = paint.color();
        }
    }

    void StylePanel::applySlidersAsColor() {
        auto* a = find<Slider>("alphaSlider");
        float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 4; ++i) {
            if (auto* slider = find<Slider>(kColorSliders[i])) {
                v[i] = slider->value();
            }
        }
        // Edit in the space the color already has (what the mode control shows).
        SketchColor color = colorFromSliders(displayedColor().space(), v, a != nullptr ? a->value() / 100.0f : displayedColor().alpha());
        applyColor(color, target_ == Target::Fill ? "Change Fill" : "Change Stroke");
    }

    SketchColor StylePanel::displayedColor() const {
        SketchPaint paint = targetPaint();
        if (paint.isPoints() && !paint.gradient().points.empty()) {
            return paint.gradient().points[std::min(selectedStop_, paint.gradient().points.size() - 1)].color;
        }
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            return paint.gradient().stops[std::min(selectedStop_, paint.gradient().stops.size() - 1)].color;
        }
        // No paint: show black in the document's color mode (what choosing a color would start from).
        return paint.isNone() ? SketchColor::rgb(0.0f, 0.0f, 0.0f).convertedTo(document_.colorMode()) : paint.color();
    }

    void StylePanel::applyColor(const SketchColor& color, const char* description) {
        SketchPaint paint = targetPaint();
        if (paint.isPoints() && !paint.gradient().points.empty()) {
            SketchGradient gradient = paint.gradient();
            gradient.points[std::min(selectedStop_, gradient.points.size() - 1)].color = color;
            applyPaint(SketchPaint::points(gradient), description);
            return;
        }
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            SketchGradient gradient = paint.gradient();
            gradient.stops[std::min(selectedStop_, gradient.stops.size() - 1)].color = color;
            applyGradient(gradient, paint.kind(), description);
        } else {
            applyPaint(SketchPaint::solid(color), description);
        }
    }

    void StylePanel::applyGradient(const SketchGradient& gradient, SketchPaint::Kind kind, const char* description) {
        applyPaint(kind == SketchPaint::Kind::Radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient), description);
    }

    // Stop offset (%) and angle (degrees) typed into their fields.
    void StylePanel::applyGradientFields() {
        if (updating_) {
            return;
        }
        SketchPaint paint = targetPaint();
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            SketchGradient gradient = paint.gradient();
            std::size_t index = std::min(selectedStop_, gradient.stops.size() - 1);
            double number = 0.0;
            if (auto* field = find<TextField>("stopOffsetField")) {
                if (leadingNumber(field->text(), number)) {
                    gradient.stops[index].offset = std::min(1.0, std::max(0.0, number / 100.0));
                    // Keep the edited stop selected after the sort.
                    GradientStop edited = gradient.stops[index];
                    gradient.sortStops();
                    for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
                        if (gradient.stops[i] == edited) {
                            selectedStop_ = i;
                        }
                    }
                }
            }
            if (auto* field = find<TextField>("gradientAngleField")) {
                if (leadingNumber(field->text(), number)) {
                    // One object: turn its explicit axis about its midpoint; several (different boxes): back to
                    // the angle-derived axis.
                    if (gradient.hasAxis && document_.selection().size() == 1) {
                        const SketchObject* object = document_.findById(document_.selection().front());
                        gradient.setAngle(object != nullptr ? object->paintFrameBounds() : BLBox(0, 0, 1, 1), number);
                    } else {
                        gradient.hasAxis = false;
                        gradient.angleDegrees = number;
                    }
                }
            }
            if (gradient != paint.gradient()) {
                applyGradient(gradient, paint.kind(), "Edit Gradient");
            }
        }
        refresh();
    }

    void StylePanel::applyHex() {
        if (updating_) {
            return;
        }
        auto* hex = find<TextField>("hexField");
        auto* a = find<Slider>("alphaSlider");
        if (hex == nullptr) {
            return;
        }
        try {
            SketchColor parsed = SketchColor::fromHex(narrow(hex->text()));
            // A 6-digit hex has no alpha of its own - keep the current one.
            if (narrow(hex->text()).size() <= 7 && a != nullptr) {
                parsed.setAlpha(a->value() / 100.0f);
            }
            // Hex is RGB; the color keeps the space it is being edited in (hex of a CMYK color converts back).
            SketchColor current = displayedColor();
            bool same = parsed.toHex() == current.toHex() && parsed.alpha() == current.alpha();
            if (!same || targetPaint().isNone()) {
                applyColor(parsed.convertedTo(current.space()), target_ == Target::Fill ? "Change Fill" : "Change Stroke");
            }
        } catch (const std::runtime_error&) {
        }
        refresh();
    }

    void StylePanel::applyStrokeWeight() {
        if (updating_) {
            return;
        }
        auto* field = find<TextField>("strokeWeightField");
        double width = 0.0;
        if (field != nullptr && leadingNumber(field->text(), width) && width >= 0.0) {
            edit("Stroke Weight",
                [this, width]() { document_.setSelectionStrokeWidth(width); },
                [this, width]() { document_.setDefaultStrokeWidth(width); });
        }
        refresh();
    }

    void StylePanel::applyStrokeOptions(const std::function<void(StrokeOptions&)>& change, const char* description) {
        if (!document_.selection().empty()) {
            std::vector<StyleState> before = document_.snapshotStyle();
            document_.changeSelectionStrokeOptions(change);
            document_.commitStyle(before, description);
        } else {
            StrokeOptions options = document_.defaultStrokeOptions();
            change(options);
            document_.setDefaultStrokeOptions(options);
        }
    }

    void StylePanel::applyDashes() {
        if (updating_) {
            return;
        }
        std::vector<double> dashes;
        auto* toggle = find<Toggle>("dashedToggle");
        if (toggle != nullptr && toggle->isChecked()) {
            double values[4] = {12.0, 6.0, 0.0, 0.0};
            bool given[4] = {false, false, false, false};
            const char* names[4] = {"dashField", "gapField", "dash2Field", "gap2Field"};
            for (int i = 0; i < 4; ++i) {
                auto* field = find<TextField>(names[i]);
                double value = 0.0;
                if (field != nullptr && leadingNumber(field->text(), value) && value >= 0.0) {
                    values[i] = value;
                    given[i] = true;
                }
            }
            // The second pair is optional; both halves are needed to use it.
            size_t count = given[2] && given[3] ? 4 : 2;
            if (values[0] + values[1] <= 0.0 || (count == 4 && values[0] + values[1] + values[2] + values[3] <= 0.0)) {
                values[0] = 12.0;
                values[1] = 6.0;
                count = 2;
            }
            dashes.assign(values, values + count);
        }
        applyStrokeOptions([dashes](StrokeOptions& o) { o.dashes = dashes; }, dashes.empty() ? "Solid Stroke" : "Dashed Stroke");
        refresh();
    }

    void StylePanel::applyMiterLimit() {
        if (updating_) {
            return;
        }
        double limit = 0.0;
        auto* field = find<TextField>("miterLimitField");
        if (field != nullptr && leadingNumber(field->text(), limit) && limit >= 1.0) {
            applyStrokeOptions([limit](StrokeOptions& o) { o.miterLimit = limit; }, "Miter Limit");
        }
        refresh();
    }

    void StylePanel::applyArrowScale() {
        if (updating_) {
            return;
        }
        double percent = 0.0;
        auto* field = find<TextField>("arrowScaleField");
        if (field != nullptr && leadingNumber(field->text(), percent) && percent >= 1.0) {
            applyStrokeOptions([percent](StrokeOptions& o) { o.arrowScale = std::min(percent, 1000.0); }, "Arrow Scale");
        }
        refresh();
    }

    void StylePanel::applyBlendMode(std::size_t index) {
        std::vector<StyleState> before = document_.snapshotStyle();
        if (baseRowSelected_ && document_.selectedGroup() != 0) {
            document_.setSelectionGroupBlendMode(BlendMode(index));
        } else if (baseRowSelected_) {
            document_.setSelectionBlendMode(BlendMode(index));
        } else {
            document_.setSelectionItemBlendMode(targetKind(), BlendMode(index));
        }
        document_.commitStyle(before, "Blend Mode");
    }

    void StylePanel::applyOpacity() {
        auto* slider = find<Slider>("opacitySlider");
        if (slider == nullptr) {
            return;
        }
        double opacity = std::min(1.0, std::max(0.0, double(slider->value()) / 100.0));
        bool base = baseRowSelected_;
        AppearanceKind kind = targetKind();
        edit("Opacity",
            [this, opacity, base, kind]() {
                if (base && document_.selectedGroup() != 0) { document_.setSelectionGroupOpacity(opacity); }
                else if (base) { document_.setSelectionOpacity(opacity); }
                else { document_.setSelectionItemOpacity(kind, opacity); }
            },
            []() {});
    }

    void StylePanel::syncSlider(const char* sliderName, float value) {
        if (auto* slider = find<Slider>(sliderName)) {
            slider->setValue(value);
        }
        if (auto* field = find<TextField>((std::string(sliderName) + "Value").c_str())) {
            field->setText(widen(std::to_string(int(value + 0.5f))));
        }
    }

    void StylePanel::syncWells() {
        auto paintWell = [this](const char* name, const SketchPaint& paint, bool active) {
            if (auto* well = find<SubView>(name)) {
                gfx::Fill& fill = well->style().backgroundFill();
                fill.setKind(gfx::PaintKind::Color);
                fill.setColor(paint.isNone() ? Color(1.0f, 1.0f, 1.0f, 1.0f) : toColor(paint.color()));
                well->style().setBorderWidth(active ? 3.0f : 1.0f);
                well->style().markDirty();
            }
        };
        const SketchObject* first = document_.selection().empty() ? nullptr : document_.findById(document_.selection().front());
        SketchPaint fill = first != nullptr ? first->fill() : document_.defaultFill();
        SketchPaint stroke = first != nullptr ? first->stroke() : document_.defaultStroke();
        paintWell("fillWell", fill, target_ == Target::Fill);
        // The stroke well is a ring in the stroke's colour, so it reads as the outline next to the fill's swatch.
        if (auto* well = find<SubView>("strokeWell")) {
            gfx::Fill& background = well->style().backgroundFill();
            background.setKind(gfx::PaintKind::Color);
            background.setColor(toColor(SketchColor::rgb(0.16f, 0.16f, 0.16f)));
            well->style().setBorderFill(stroke.isNone() ? Color(0.45f, 0.45f, 0.45f, 1.0f) : toColor(stroke.color()));
            well->style().setBorderWidth(5.0f);
            well->style().markDirty();
        }
        if (auto* info = find<Label>("colorPanelHeaderInfo")) {
            info->setText(target_ == Target::Fill ? "Fill \xE2\x97\x8F   Stroke \xE2\x97\x8B" : "Fill \xE2\x97\x8B   Stroke \xE2\x97\x8F");
        }
        // What the Appearance panel is showing: the kind of the selected object ("3 objects" for several).
        if (auto* info = find<Label>("appearancePanelHeaderInfo")) {
            static const char* kKinds[] = {"Line", "Rectangle", "Rounded Rectangle", "Ellipse", "Polygon", "Polyline", "Path", "Text", "Symbol", "Image"};
            std::string kind;
            if (document_.selection().size() > 1) {
                kind = std::to_string(document_.selection().size()) + " objects";
            } else if (first != nullptr) {
                kind = kKinds[std::size_t(first->type())];
            }
            info->setText(kind);
        }
    }

    void StylePanel::refresh() {
        if (updating_) {
            return;
        }
        updating_ = true;

        SketchPaint paint = targetPaint();
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            selectedStop_ = std::min(selectedStop_, paint.gradient().stops.size() - 1);
        } else if (paint.isPoints() && !paint.gradient().points.empty()) {
            selectedStop_ = std::min(selectedStop_, paint.gradient().points.size() - 1);
        } else {
            selectedStop_ = 0;
        }
        // Sliders and hex show the solid color, or the selected stop of a gradient.
        SketchColor shown = displayedColor();
        const ModeLayout& layout = kLayouts[modeIndex(shown.space())];
        float values[4];
        sliderValues(shown, values);
        for (std::size_t i = 0; i < 4; ++i) {
            std::string sliderName = std::string("comp") + char('0' + int(i)) + "Slider";
            bool used = i < layout.count;
            if (auto* row = find<View>((sliderName + "Row").c_str())) {
                row->setVisible(used);
            }
            if (!used) {
                continue;
            }
            if (auto* caption = find<Label>((sliderName + "Caption").c_str())) {
                caption->setText(layout.captions[i]);
            }
            if (auto* slider = find<Slider>(sliderName.c_str())) {
                slider->setRange(0.0f, layout.maxima[i]);
            }
            syncSlider(sliderName.c_str(), values[i]);
        }
        syncSlider("alphaSlider", paint.isNone() ? 100.0f : shown.alpha() * 100.0f);
        if (wheel_ != nullptr && !wheel_->isDragging()) {
            const SketchColor hsb = shown.convertedTo(ColorSpace::HSB);
            const bool hueMeaningful = hsb.component(1) > 0.002f && hsb.component(2) > 0.002f;   // a grey or black has no hue: keep the one shown
            wheel_->setHsb(double(hsb.component(0)), double(hsb.component(1)), double(hsb.component(2)), hueMeaningful);
        }
        if (auto* row = find<View>("gradientRow")) {
            if (row->isVisible() != paint.isGradient()) {
                row->setVisible(paint.isGradient());
                root_.updateLayout();
                onContentChanged(*this);
            }
        }
        if (auto* hex = find<TextField>("hexField")) {
            hex->setText(widen(paint.isNone() ? std::string("none") : shown.toHex()));
        }

        // The gradient editor row: the bar always previews (the gradient, or the color fading to white).
        if (auto* bar = find<GradientBar>("gradientBar")) {
            SketchGradient preview = paint.isGradient() ? paint.gradient() : SketchGradient::fromColor(shown);
            std::vector<GradientBar::Stop> stops;
            for (const GradientStop& stop : preview.stops) {
                stops.push_back({stop.offset, toColor(stop.color)});
            }
            bar->setStops(stops);
            bar->setSelectedStop(paint.isGradient() ? selectedStop_ : stops.size());
        }
        if (auto* field = find<TextField>("stopOffsetField")) {
            char buffer[32];
            double offset = paint.isGradient() && !paint.gradient().stops.empty() ? paint.gradient().stops[selectedStop_].offset * 100.0 : 0.0;
            std::snprintf(buffer, sizeof(buffer), "%g%%", std::round(offset * 10.0) / 10.0);
            field->setText(widen(paint.isGradient() ? buffer : ""));
        }
        if (auto* field = find<TextField>("gradientAngleField")) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", std::round((paint.isGradient() ? paint.gradient().angleDegrees : 0.0) * 10.0) / 10.0);
            field->setText(paint.isGradient() ? widen(buffer) + L"\u00b0" : std::wstring());
        }
        if (auto* label = find<Label>("gradientTypeLabel")) {
            label->setText(paint.kind() == SketchPaint::Kind::Radial ? "Radial" : (paint.kind() == SketchPaint::Kind::Linear ? "Linear" : "-"));
        }
        if (auto* spread = find<DropDownList>("spreadCombo")) {
            std::size_t index = !paint.isGradient() ? 0
                : (paint.gradient().spread == GradientSpread::Repeat ? 1 : (paint.gradient().spread == GradientSpread::Reflect ? 2 : 0));
            if (spread->selectedIndex() != std::optional<std::size_t>(index)) {
                spread->setSelectedIndex(index);
            }
        }

        if (auto* apply = find<DropDownList>("gradientApplyCombo")) {
            // Only a stroke can run its colors along its length.
            apply->setEnabled(paint.isGradient() && target_ == Target::Stroke);
            std::size_t index = paint.isGradient() && paint.gradient().alongStroke && target_ == Target::Stroke ? 1 : 0;
            if (apply->selectedIndex() != std::optional<std::size_t>(index)) {
                apply->setSelectedIndex(index);
            }
        }

        auto setChecked = [this](const char* name, bool checked) {
            if (auto* button = find<ToolbarButton>(name)) {
                if (button->isChecked() != checked) {
                    button->setChecked(checked);
                }
            }
        };
        setChecked("fillNoneButton", paint.isNone());
        setChecked("fillSolidButton", paint.kind() == SketchPaint::Kind::Solid);
        setChecked("fillLinearButton", paint.kind() == SketchPaint::Kind::Linear);
        setChecked("fillRadialButton", paint.kind() == SketchPaint::Kind::Radial);
        setChecked("fillPointsButton", paint.isPoints());

        if (auto* mode = find<SegmentedControl>("colorModeControl")) {
            std::size_t index = modeIndex(shown.space());
            if (mode->selectedIndex() != std::optional<std::size_t>(index)) {
                mode->setSelectedIndex(index);
            }
        }

        const SketchObject* first = document_.selection().empty() ? nullptr : document_.findById(document_.selection().front());
        double weight = first != nullptr ? first->strokeWidth() : document_.defaultStrokeWidth();
        if (auto* field = find<TextField>("strokeWeightField")) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g pt", weight);
            field->setText(widen(buffer));
        }
        const StrokeOptions& options = first != nullptr ? first->strokeOptions() : document_.defaultStrokeOptions();
        for (auto [name, on] : std::initializer_list<std::pair<const char*, bool>>{
                {"capButtButton", options.cap == StrokeCap::Butt}, {"capRoundButton", options.cap == StrokeCap::Round},
                {"capSquareButton", options.cap == StrokeCap::Square}, {"joinMiterButton", options.join == StrokeJoin::Miter},
                {"joinRoundButton", options.join == StrokeJoin::Round}, {"joinBevelButton", options.join == StrokeJoin::Bevel},
                {"alignCenterStroke", options.align == StrokeAlign::Center}, {"alignInsideStroke", options.align == StrokeAlign::Inside},
                {"alignOutsideStroke", options.align == StrokeAlign::Outside},
                {"profileUniformButton", options.widths.empty()}, {"profileTaperButton", options.widths == taperProfile()},
                {"profileBulgeButton", options.widths == bulgeProfile()}}) {
            if (auto* button = find<ToolbarButton>(name)) {
                if (button->isChecked() != on) {
                    button->setChecked(on);
                }
            }
        }
        if (auto* toggle = find<Toggle>("dashedToggle")) {
            if (toggle->isChecked() != !options.dashes.empty()) {
                toggle->setChecked(!options.dashes.empty());
            }
        }
        if (!options.dashes.empty()) {
            const char* names[4] = {"dashField", "gapField", "dash2Field", "gap2Field"};
            for (size_t i = 0; i < 4; ++i) {
                if (auto* field = find<TextField>(names[i])) {
                    char buffer[32] = "";
                    if (i < options.dashes.size()) {
                        std::snprintf(buffer, sizeof(buffer), "%g", options.dashes[i]);
                    }
                    field->setText(widen(buffer));
                }
            }
        }
        for (bool atEnd : {false, true}) {
            if (auto* combo = find<DropDownList>(atEnd ? "arrowEndCombo" : "arrowStartCombo")) {
                const std::size_t index = std::size_t(atEnd ? options.arrowEnd : options.arrowStart);
                if (combo->selectedIndex() != std::optional<std::size_t>(index)) {
                    combo->setSelectedIndex(index);
                }
            }
        }
        if (auto* field = find<TextField>("arrowScaleField")) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g%%", options.arrowScale);
            field->setText(widen(buffer));
        }
        if (auto* field = find<TextField>("miterLimitField")) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", options.miterLimit);
            field->setText(widen(buffer));
        }
        // An effect row stays chosen only while the object still has an active effect (it was deleted, or another
        // object without effects was selected).
        if (effectRowSelected_ && (first == nullptr || first->activeIndex(AppearanceKind::Effect) >= first->appearance().size())) {
            effectRowSelected_ = false;
        }
        const AppearanceItem* shownItem = first != nullptr ? &first->activeItem(targetKind()) : nullptr;
        bool showBase = first == nullptr || baseRowSelected_;
        std::size_t group = document_.selectedGroup();
        GroupProps groupProps = document_.groupProps(group);
        double shownOpacity = first == nullptr ? 1.0 : (showBase ? (group != 0 ? groupProps.opacity : first->opacity()) : shownItem->opacity);
        syncSlider("opacitySlider", float(shownOpacity * 100.0));
        if (auto* blend = find<DropDownList>("blendModeCombo")) {
            blend->setEnabled(first != nullptr);
            std::size_t index = first == nullptr ? 0 : std::size_t(showBase ? (group != 0 ? groupProps.blendMode : first->blendMode()) : shownItem->blendMode);
            if (blend->selectedIndex() != std::optional<std::size_t>(index)) {
                blend->setSelectedIndex(index);
            }
        }
        syncWells();
        refreshAppearance(first);
        refreshEffectOptions(first);
        refreshGraphicStyles();
        refreshSwatches();

        updating_ = false;
    }

    void StylePanel::refreshSwatches() {
        if (auto* grid = find<SwatchGrid>("swatchGrid")) {
            grid->setSwatches(document_.swatches());
            if (auto* button = find<Control>("deleteSwatchButton")) {
                button->setEnabled(grid->selectedIndex() < document_.swatches().size());
            }
        }
    }

    // ---- Effects ----

    void StylePanel::addEffect(EffectKind kind) {
        if (document_.selection().empty()) {
            return;
        }
        effectRowSelected_ = true;   // set first: the document's change refreshes the panel, which must already show the new row chosen
        baseRowSelected_ = false;
        if (!document_.addEffect(kind)) {
            effectRowSelected_ = false;
        }
        refresh();
    }

    void StylePanel::showEffectMenu() {
        if (document_.selection().empty()) {
            return;
        }
        // A throwaway menu: the parent item only holds the children the popup is built from.
        MenuItem menu;
        static const EffectKind kKinds[] = {EffectKind::DropShadow, EffectKind::OuterGlow, EffectKind::InnerShadow, EffectKind::GaussianBlur,
            EffectKind::RoundCorners, EffectKind::OffsetPath, EffectKind::Roughen};
        for (EffectKind kind : kKinds) {
            auto* item = new MenuItem(effectName(kind));
            item->onClick += [this, kind](MenuItem&) {
                addEffect(kind);
                return SyncReturn::Handled;
            };
            menu.addChild(item);
        }
        POINT cursor{0, 0};
        ::GetCursorPos(&cursor);
        ContextMenu popup;
        popup.show(root_.windowHandle(), menu, cursor.x, cursor.y);
    }

    void StylePanel::refreshEffectOptions(const SketchObject* first) {
        const AppearanceItem* effect = (first != nullptr && effectRowSelected_) ? &first->activeItem(AppearanceKind::Effect) : nullptr;
        // X / Y: a shadow's offset, or (Roughen) the detail and the seed. Only the effects with a color show the color row.
        bool shadow = effect != nullptr && (effect->effect == EffectKind::DropShadow || effect->effect == EffectKind::InnerShadow);
        bool rough = effect != nullptr && effect->effect == EffectKind::Roughen;
        bool tinted = effect != nullptr && isTintedEffect(effect->effect);
        if (auto* row = find<View>("effectRow")) {
            row->setVisible(effect != nullptr);
        }
        if (auto* row = find<View>("effectColorRow")) {
            row->setVisible(tinted);
        }
        for (const char* name : {"effectXLabel", "effectXField", "effectYLabel", "effectYField"}) {
            if (auto* view = find<View>(name)) {
                view->setVisible(shadow || rough);
            }
        }
        // Each options row left out frees its height plus the gap above it.
        constexpr float kRow = 24.0f + 6.0f;
        fitAppearancePanel(effect == nullptr ? 2.0f * kRow : (tinted ? 0.0f : kRow));
        if (effect == nullptr) {
            return;
        }
        auto number = [](double value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", value);
            return widen(buffer);
        };
        // The size field is the radius of Round Corners / the distance of Offset Path / the size of Roughen, else the blur.
        bool uses_radius = effect->effect == EffectKind::RoundCorners || effect->effect == EffectKind::OffsetPath || rough;
        if (auto* label = find<Label>("effectSizeLabel")) {
            label->setText(effect->effect == EffectKind::OffsetPath ? "Offset" : (rough ? "Size" : (uses_radius ? "Radius" : "Blur")));
        }
        if (auto* label = find<Label>("effectXLabel")) {
            label->setText(rough ? "Detail" : "X");
        }
        if (auto* label = find<Label>("effectYLabel")) {
            label->setText(rough ? "Seed" : "Y");
        }
        // Roughen's labels are longer than a shadow's "X" / "Y" / "Blur": give them the room (and take it back for the others).
        auto labelWidth = [this](const char* name, float width) {
            if (auto* label = find<Label>(name)) {
                if (label->desiredSize().width != width) {
                    label->setDesiredSize(Size(width, label->desiredSize().height));
                }
            }
        };
        labelWidth("effectXLabel", rough ? 38.0f : 14.0f);
        labelWidth("effectYLabel", rough ? 30.0f : 14.0f);
        labelWidth("effectSizeLabel", rough ? 28.0f : 34.0f);
        if (auto* row = find<SubView>("effectRow")) {
            row->updateLayout();
        }
        if (auto* field = find<TextField>("effectXField")) {
            field->setText(number(effect->offsetX));
        }
        if (auto* field = find<TextField>("effectYField")) {
            field->setText(number(effect->offsetY));
        }
        if (auto* field = find<TextField>("effectSizeField")) {
            field->setText(number(uses_radius ? effect->radius : effect->softness));
        }
        if (tinted) {
            if (auto* field = find<TextField>("effectColorField")) {
                field->setText(widen(effect->color.toHex()));
            }
            if (auto* well = find<SubView>("effectColorWell")) {
                gfx::Fill& fill = well->style().backgroundFill();
                fill.setKind(gfx::PaintKind::Color);
                fill.setColor(toColor(effect->color));
                well->style().markDirty();
            }
        }
    }

    void StylePanel::fitAppearancePanel(float hidden) {
        fitSection("appearancePanel", "appearancePanelBody", hidden);
    }

    void StylePanel::fitSection(const char* panelName, const char* bodyName, float shrink, const char* childName) {
        auto* panel = find<SubView>(panelName);
        auto* body = find<SubView>(bodyName);
        auto* content = find<SubView>("sidebarContent");
        auto* scroll = find<ScrollView>("sidebar");
        auto* child = childName != nullptr ? find<SubView>(childName) : nullptr;
        if (panel == nullptr || body == nullptr || content == nullptr || scroll == nullptr) {
            return;
        }
        if (sidebarContentHeight_ == 0.0f) {
            sidebarContentHeight_ = content->bounds().height();
        }
        Section& section = sections_[panelName];
        if (section.panelHeight == 0.0f) {
            section.panelHeight = panel->desiredSize().height;
            section.bodyHeight = body->desiredSize().height;
            section.childHeight = child != nullptr ? child->desiredSize().height : 0.0f;
        }
        if (section.shrink == shrink && panel->desiredSize().height == section.panelHeight - shrink) {
            return;
        }
        section.shrink = shrink;
        panel->setDesiredSize(Size(panel->desiredSize().width, section.panelHeight - shrink));
        body->setDesiredSize(Size(body->desiredSize().width, section.bodyHeight - shrink));
        if (child != nullptr) {
            child->setDesiredSize(Size(child->desiredSize().width, section.childHeight - shrink));
        }
        float total = 0.0f;
        for (const auto& entry : sections_) {
            total += entry.second.shrink;
        }
        float contentHeight = sidebarContentHeight_ - total;
        content->setBounds(Rect(content->bounds().left(), content->bounds().top(), content->bounds().width(), contentHeight));
        scroll->setContentSize(Size(scroll->contentSize().width, contentHeight));
        content->updateLayout();
    }

    // ---- Graphic Styles ----

    void StylePanel::refreshGraphicStyles() {
        auto* grid = find<SwatchGrid>("graphicStyleGrid");
        if (grid == nullptr) {
            return;
        }
        const std::vector<GraphicStyle>& styles = document_.graphicStyles();
        // Each tile: a white square (so a Ghost style's transparency reads), then a rounded rectangle wearing the style.
        std::vector<GraphicStyle> copy = styles;
        grid->setTilePainter([copy](BLContext& ctx, const Rect& tile, std::size_t index) {
            if (index >= copy.size()) {
                return;
            }
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            ctx.fill_rect(BLRect(tile.left(), tile.top(), tile.width(), tile.height()));
            double inset = std::max(5.0, double(tile.width()) * 0.18);
            std::unique_ptr<SketchObject> sample(SketchObject::makeRoundRect(
                tile.left() + inset, tile.top() + inset, tile.width() - 2.0 * inset, tile.height() - 2.0 * inset, 4.0));
            sample->setAppearance(copy[index].items);
            sample->setOpacity(copy[index].opacity);
            sample->setBlendMode(copy[index].blendMode);
            sample->paint(ctx);
        }, copy.size());
        if (styleTiles_ == static_cast<std::size_t>(-1) || grid->selectedIndex() > copy.size()) {
            grid->setSelectedIndex(copy.size());   // none chosen yet
        }
        // The grid grows by rows as styles are added (its height and the panel's are fixed sizes in the layout).
        if (styleTiles_ != copy.size()) {
            styleTiles_ = copy.size();
            float wanted = std::max(grid->heightFor(copy.size()), grid->heightFor(1));
            float base = 40.0f;   // the layout's one-row height
            fitSection("stylesPanel", "stylesPanelBody", base - wanted, "graphicStyleGrid");
        }
        bool haveSelection = !document_.selection().empty();
        if (auto* button = find<Control>("newGraphicStyleButton")) {
            button->setEnabled(haveSelection);
        }
        if (auto* button = find<Control>("deleteGraphicStyleButton")) {
            button->setEnabled(grid->selectedIndex() < copy.size());
        }
    }

    // A click on a tile applies that style to the selection.
    void StylePanel::styleTilePicked() {
        auto* grid = find<SwatchGrid>("graphicStyleGrid");
        if (grid != nullptr && !document_.selection().empty()) {
            document_.applyGraphicStyle(grid->selectedIndex());
        }
        refreshGraphicStyles();
    }

    void StylePanel::applyEffectField(EffectField which) {
        if (updating_ || !effectRowSelected_ || document_.selection().empty()) {
            return;
        }
        const char* names[] = {"effectXField", "effectYField", "effectSizeField", "effectColorField"};
        auto* field = find<TextField>(names[int(which)]);
        if (field == nullptr) {
            return;
        }
        if (which == EffectField::Color) {
            try {
                SketchColor parsed = SketchColor::fromHex(narrow(field->text())).convertedTo(document_.colorMode());
                document_.changeActiveEffect([parsed](AppearanceItem& item) { item.color = parsed; }, "Effect Color");
            } catch (const std::runtime_error&) {
            }
        } else {
            double value = 0.0;
            if (leadingNumber(field->text(), value)) {
                switch (which) {
                    case EffectField::X:
                        document_.changeActiveEffect([value](AppearanceItem& item) { item.offsetX = value; }, "Effect Offset");
                        break;
                    case EffectField::Y:
                        document_.changeActiveEffect([value](AppearanceItem& item) { item.offsetY = value; }, "Effect Offset");
                        break;
                    default:
                        document_.changeActiveEffect([value](AppearanceItem& item) {
                            if (item.effect == EffectKind::OffsetPath) {
                                item.radius = value;   // negative moves the outline inward
                            } else if (item.effect == EffectKind::RoundCorners || item.effect == EffectKind::Roughen) {
                                item.radius = std::max(0.0, value);
                            } else {
                                item.softness = std::max(0.0, value);
                            }
                        }, "Effect Size");
                        break;
                }
            }
        }
        refresh();   // reads back the clamped / normalized value (and restores the field when the text was not valid)
    }

}
