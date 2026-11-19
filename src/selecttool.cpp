#include "curvy/selecttool.h"

#include "curvy/canvasview.h"
#include "curvy/selectionmodel.h"
#include "curvy/settings.h"
#include "curvy/shapepaths.h"
#include "curvy/sketchdocument.h"
#include "curvy/theme.h"

#include <newui/keyboard_constants.h>

#include <algorithm>

namespace curvy {

    using namespace newui;

    namespace {
        BLRgba32 rgb(std::uint32_t value) { return BLRgba32(Theme::argb(value)); }
    }

    void SelectTool::cancel() {
        marquee_.cancel();
        marqueeHits_.clear();
        context_.canvas.style().markDirty();
    }

    bool SelectTool::marqueeMayStart(const Point& pt) const {
        if (!context_.documentOpen() || context_.graphShown() || context_.menuOpen()) {
            return false;
        }
        return marquee_.polygonOpen() || !context_.canvas.shapeAt(pt).has_value();
    }

    bool SelectTool::inputDown(const ToolEvent& event) {
        const Point& pt = event.view;
        if (event.secondary) {
            if (context_.settings.selectTwoButton && marqueeMayStart(pt)) {
                beginMarquee(pt, true, event.keys);   // a click without a drag still opens the ring, on release
                return true;
            }
            return false;
        }
        if (marquee_.polygonOpen()) {
            beginMarquee(pt, false, event.keys);
            return true;
        }
        const bool mayStart = marqueeMayStart(pt);
        const bool subtract = (event.keys & kmAlt) != 0;
        const bool used = selectAt(pt, (event.keys & (kmShift | kmCtrl)) != 0 || (subtract && mayStart));
        if (mayStart) {
            beginMarquee(pt, false, event.keys);
        }
        return used;
    }

    void SelectTool::inputMove(const ToolEvent& event) {
        if (!marquee_.active()) {
            return;
        }
        marquee_.moveTo(event.view);
        if (marquee_.moved() || marquee_.polygonOpen()) {
            updateMarqueeHits();
        }
        context_.canvas.style().markDirty();
    }

    void SelectTool::inputUp(const ToolEvent&) {
        if (!marquee_.active() || marquee_.polygonOpen()) {
            return;
        }
        if (marquee_.moved()) {
            finishMarquee();
            return;
        }
        const bool ring = marquee_.ringOnClick();
        cancel();
        if (ring) {
            context_.openRing();
        }
    }

    bool SelectTool::keyDown(const ToolKeyEvent& event) {
        const bool shift = (event.mask & kmShift) != 0;
        if (marquee_.active()) {
            if (event.vk == vkEscape) {
                cancel();
                context_.say("Selection cancelled");
                return true;
            }
            if (event.vk == vkReturn && marquee_.canClose()) {
                finishMarquee();
                return true;
            }
        }
        if (event.vk == vkEscape && !shift && !context_.graphShown() && !context_.selection.empty()) {
            context_.selection.clear();
            context_.say("Selection cleared");
            return true;
        }
        if (event.vk == vkLetterM && !shift && !marquee_.active() && context_.documentOpen()) {
            cycleRegion();
            return true;
        }
        return false;
    }

    void SelectTool::beginMarquee(const Point& pt, bool secondary, std::uint32_t keys) {
        if (marquee_.polygonOpen()) {
            if (marquee_.addCorner(pt)) {
                finishMarquee();
            } else {
                updateMarqueeHits();
                context_.canvas.style().markDirty();
            }
            return;
        }
        const Settings& settings = context_.settings;
        RegionMode mode = settings.selectContainment == Settings::SelectContainment::Touches ? RegionMode::Touches : RegionMode::Inside;
        if (settings.selectTwoButton) {
            mode = secondary ? RegionMode::Touches : RegionMode::Inside;
        }
        SelectionModel::Combine how = SelectionModel::Combine::Replace;
        if ((keys & kmAlt) != 0) {
            how = SelectionModel::Combine::Subtract;
        } else if ((keys & (kmShift | kmCtrl)) != 0) {
            how = SelectionModel::Combine::Add;
        }
        marquee_.begin(settings.selectRegion, pt, mode, how, secondary);
        marqueeHits_.clear();
    }

    void SelectTool::updateMarqueeHits() {
        marqueeHits_.clear();
        const jitnodes::GeometryHandle artwork = context_.document.artwork();
        const std::vector<Point> outline = marquee_.outline();
        if (artwork == nullptr || outline.size() < 3) {
            return;
        }
        std::vector<BLPoint> region;
        region.reserve(outline.size());
        for (const Point& p : outline) {
            region.push_back(context_.canvas.toDocument(p));
        }
        marqueeHits_ = shapesInRegion(*artwork, region, marquee_.mode());
    }

    void SelectTool::finishMarquee() {
        if (!marquee_.active()) {
            return;
        }
        updateMarqueeHits();
        const std::size_t count = marqueeHits_.size();
        context_.selection.selectShapeKeys(marqueeHits_, marquee_.how());
        cancel();
        context_.say(std::to_string(count) + (count == 1 ? " shape" : " shapes"));
    }

    void SelectTool::cycleRegion() {
        static constexpr const char* names[] = {"Rectangle", "Ellipse", "Freehand", "Polygon"};
        Settings& settings = context_.settings;
        settings.selectRegion = Settings::SelectRegion((int(settings.selectRegion) + 1) % 4);
        context_.say(std::string("Select region: ") + names[int(settings.selectRegion)]);
        context_.saveSettings();
    }

    bool SelectTool::selectAt(const Point& pt, bool additive) {
        SelectionModel& selection = context_.selection;
        const std::optional<ShapeHit> hit = context_.canvas.shapeAt(pt);
        if (!hit.has_value() || hit->key.empty()) {   // an anonymous shape has no node to name
            return !additive && selection.clear();
        }
        // A click on what is already selected (or inside the selected group or layer) goes up one level; at the top it starts over at the shape.
        const bool steppingUp = !additive && selection.items().size() == 1 && selection.isShapeKeySelected(hit->key);
        const bool changed = steppingUp ? (selection.stepUp() || selection.selectShapeKey(hit->key, false)) : selection.selectShapeKey(hit->key, additive);
        if (changed && selection.primary().valid()) {
            context_.showSelection();
            context_.say(context_.document.node(selection.primary()).name());
        }
        return true;
    }

    void SelectTool::selectAll() {
        const jitnodes::GeometryHandle artwork = context_.documentOpen() ? context_.document.artwork() : nullptr;
        std::vector<std::string> keys;
        if (artwork != nullptr) {
            for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
                if (!shape.key.empty()) {
                    keys.push_back(std::string(shape.key));
                }
            }
        }
        context_.selection.selectShapeKeys(keys, SelectionModel::Combine::Replace);
        context_.say(std::to_string(keys.size()) + (keys.size() == 1 ? " shape" : " shapes"));
    }

    bool SelectTool::cycle(int step) {
        SelectionModel& selection = context_.selection;
        const jitnodes::GeometryHandle artwork = context_.documentOpen() && !selection.empty() ? context_.document.artwork() : nullptr;
        if (artwork == nullptr) {
            return false;
        }
        std::vector<std::string> keys;
        std::size_t first = 0, last = 0;
        bool found = false;
        for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
            if (shape.key.empty()) {
                continue;
            }
            keys.push_back(std::string(shape.key));
            if (selection.isShapeKeySelected(keys.back())) {
                if (!found) {
                    first = keys.size() - 1;
                }
                last = keys.size() - 1;
                found = true;
            }
        }
        if (keys.empty() || !found) {
            return false;   // what is selected is not a shape (a node of the graph): Tab keeps its other job
        }
        const std::size_t count = keys.size();
        const std::size_t next = step > 0 ? (last + 1) % count : (first + count - 1) % count;
        selection.selectShapeKey(keys[next], false);
        context_.say("Shape " + std::to_string(next + 1) + " of " + std::to_string(count));
        return true;
    }

    void SelectTool::paintOverlay(BLContext& ctx) const {
        if (!marquee_.active() || !(marquee_.moved() || marquee_.polygonOpen())) {
            return;
        }
        const jitnodes::GeometryHandle artwork = context_.document.artwork();
        if (artwork == nullptr) {
            return;
        }
        const BLPoint origin = context_.canvas.toView(BLPoint(0.0, 0.0));
        ctx.set_stroke_width(2.0);
        // Inside is the accent colour; touching is drawn in the highlight tone so the two read apart without a dash.
        const std::uint32_t tone = marquee_.mode() == RegionMode::Touches ? Theme::current().highlight : Theme::current().accent;
        for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
            if (std::find(marqueeHits_.begin(), marqueeHits_.end(), std::string(shape.key)) != marqueeHits_.end()) {
                ctx.stroke_path(shapePath(*artwork, shape, origin.x, origin.y, double(context_.canvas.zoom())), rgb(tone));
            }
        }
        const std::vector<Point> outline = marquee_.outline();
        if (outline.size() >= 2) {
            BLPath region;
            region.move_to(double(outline.front().x), double(outline.front().y));
            for (std::size_t i = 1; i < outline.size(); ++i) {
                region.line_to(double(outline[i].x), double(outline[i].y));
            }
            if (!marquee_.polygonOpen()) {
                region.close();
            }
            ctx.set_stroke_width(1.0);
            ctx.fill_path(region, BLRgba32((0x33u << 24) | (tone & 0xFFFFFFu)));
            ctx.stroke_path(region, rgb(tone));
        }
    }

    std::vector<ToolOption> SelectTool::options() {
        Settings& settings = context_.settings;
        const auto save = [this] { context_.saveSettings(); };
        std::vector<ToolOption> list;

        ToolOption region;
        region.id = "region";
        region.label = "Region";
        region.kind = ToolOption::Kind::Choice;
        region.choices = {"Rectangle", "Ellipse", "Freehand", "Polygon"};
        region.get = [&settings] { return double(int(settings.selectRegion)); };
        region.set = [&settings, save](double v) {
            settings.selectRegion = Settings::SelectRegion(std::clamp(int(v), 0, 3));
            save();
        };
        list.push_back(region);

        ToolOption two;
        two.id = "secondTouches";
        two.label = "Secondary button picks what it touches";
        two.kind = ToolOption::Kind::Toggle;
        two.reset = 1.0;
        two.get = [&settings] { return settings.selectTwoButton ? 1.0 : 0.0; };
        two.set = [&settings, save](double v) {
            settings.selectTwoButton = v >= 0.5;
            save();
        };
        list.push_back(two);

        ToolOption containment;
        containment.id = "containment";
        containment.label = "Containment";
        containment.kind = ToolOption::Kind::Choice;
        containment.choices = {"Inside", "Touches"};
        containment.get = [&settings] { return double(int(settings.selectContainment)); };
        containment.set = [&settings, save](double v) {
            settings.selectContainment = Settings::SelectContainment(std::clamp(int(v), 0, 1));
            save();
        };
        containment.enabled = [&settings] { return !settings.selectTwoButton; };   // the two buttons decide it otherwise
        list.push_back(containment);
        return list;
    }

}
