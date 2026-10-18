#include "curvy/sketchdocument.h"

#include "curvy/sketchsvg.h"

#include <algorithm>
#include <cmath>

// The brush library (see brush.h): calligraphic brushes, and brushes made of artwork that is stretched or scattered along a stroke.

namespace curvy {

    namespace {
        using Library = std::vector<std::shared_ptr<const BrushDef>>;

        // `items` with every stroke that names brush `id` pointing at `to` instead (null = the plain stroke). True when anything changed.
        bool rebind(std::vector<AppearanceItem>& items, std::size_t id, const std::shared_ptr<const BrushDef>& to) {
            bool any = false;
            for (AppearanceItem& item : items) {
                if (item.strokeOptions.brush && item.strokeOptions.brush->id == id) {
                    item.strokeOptions.brush = to;
                    any = true;
                }
            }
            return any;
        }

        std::shared_ptr<SymbolDef> artFrom(const std::vector<SketchObject*>& objects, std::size_t id, const BLBox& box) {
            auto def = std::make_shared<SymbolDef>();
            def->id = id;
            def->box = box;
            def->content = std::make_shared<SketchDocument>();
            for (SketchObject* object : objects) {
                def->content->add(object);
            }
            return def;
        }

        SketchObject* filled(SketchObject* object, const char* color) {
            object->setFill(SketchPaint::solid(SketchColor::fromHex(color)));
            object->setStroke(SketchPaint::none());
            return object;
        }
    }

    std::shared_ptr<const BrushDef> SketchDocument::findBrush(std::size_t id) const {
        for (const std::shared_ptr<const BrushDef>& def : brushes_) {
            if (def->id == id) {
                return def;
            }
        }
        return nullptr;
    }

    std::size_t SketchDocument::activeBrush() const {
        if (findBrush(activeBrush_)) {
            return activeBrush_;
        }
        return brushes_.empty() ? 0 : brushes_.front()->id;
    }

    void SketchDocument::setActiveBrush(std::size_t id) {
        if (id != activeBrush_ && findBrush(id)) {
            activeBrush_ = id;
            onChanged(*this);
        }
    }

    std::size_t SketchDocument::brushUseCount(std::size_t id) const {
        std::size_t uses = 0;
        auto uses_ = [id](const std::vector<AppearanceItem>& items) {
            for (const AppearanceItem& item : items) {
                if (item.strokeOptions.brush && item.strokeOptions.brush->id == id) {
                    return true;
                }
            }
            return false;
        };
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            uses += uses_(object->appearance()) ? 1 : 0;
        }
        for (const GraphicStyle& style : graphicStyles_) {
            uses += uses_(style.items) ? 1 : 0;
        }
        return uses;
    }

    void SketchDocument::setBrushes(std::vector<std::shared_ptr<const BrushDef>> brushes) {
        brushes_ = std::move(brushes);
        for (const std::shared_ptr<const BrushDef>& def : brushes_) {
            nextBrushId_ = std::max(nextBrushId_, def->id + 1);
        }
        bindBrushStrokes();
        onChanged(*this);
    }

    void SketchDocument::bindBrushStrokes() {
        auto bind = [this](std::vector<AppearanceItem>& items) {
            bool any = false;
            for (AppearanceItem& item : items) {
                if (item.strokeOptions.brushRef != 0) {
                    if (!item.strokeOptions.brush) {
                        item.strokeOptions.brush = findBrush(item.strokeOptions.brushRef);
                    }
                    item.strokeOptions.brushRef = 0;
                    any = true;
                }
            }
            return any;
        };
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            std::vector<AppearanceItem> items = object->appearance();
            if (bind(items)) {
                object->setAppearance(items);
            }
        }
        std::vector<GraphicStyle> styles = graphicStyles_;
        bool any = false;
        for (GraphicStyle& style : styles) {
            any = bind(style.items) || any;
        }
        if (any) {
            graphicStyles_ = std::move(styles);
        }
    }

    std::size_t SketchDocument::makeBrush(BrushKind kind, const std::string& name) {
        const std::size_t id = nextBrushId_;
        auto def = std::make_shared<BrushDef>();
        def->id = id;
        def->kind = kind;
        def->name = name.empty() ? std::string(brushKindName(kind)) + " Brush " + std::to_string(brushes_.size() + 1) : name;
        if (kind != BrushKind::Calligraphic) {
            std::shared_ptr<SymbolDef> art = selectionArtwork(id, def->name);
            if (!art) {
                return 0;
            }
            def->art = art;
            if (kind == BrushKind::Scatter) {
                def->spacing = 150.0;
                def->scatter = 30.0;
                def->sizeVariation = 20.0;
                def->rotation = 20.0;
            }
        }
        ++nextBrushId_;
        activeBrush_ = id;
        Library before = brushes_, after = brushes_;
        after.push_back(def);
        undoStack_.push({"Make Brush", [this, after]() { brushes_ = after; changed(); }, [this, before]() { brushes_ = before; changed(); }});
        return id;
    }

    std::size_t SketchDocument::addBrushPresets() {
        std::vector<std::shared_ptr<BrushDef>> presets;
        auto calligraphic = [&](const char* name, double angle, double roundness) {
            auto def = std::make_shared<BrushDef>();
            def->name = name;
            def->angle = angle;
            def->roundness = roundness;
            presets.push_back(def);
        };
        calligraphic("Calligraphic 45", 45.0, 25.0);
        calligraphic("Flat Chisel", 0.0, 15.0);
        calligraphic("Felt Tip", 20.0, 60.0);

        auto scatter = [&](const char* name, std::shared_ptr<SymbolDef> art, double spacing, double stray, double size, double turn, bool follow) {
            auto def = std::make_shared<BrushDef>();
            def->name = name;
            def->kind = BrushKind::Scatter;
            def->art = std::move(art);
            def->spacing = spacing;
            def->scatter = stray;
            def->sizeVariation = size;
            def->rotation = turn;
            def->followPath = follow;
            presets.push_back(def);
        };
        scatter("Dots", artFrom({filled(SketchObject::makeEllipse(4, 4, 4, 4), "#222222")}, 0, BLBox(0, 0, 8, 8)), 170.0, 25.0, 40.0, 0.0, true);
        {
            std::vector<BLPoint> star;
            for (int i = 0; i < 10; ++i) {
                const double a = -1.5707963267948966 + i * 0.6283185307179586, r = i % 2 == 0 ? 6.0 : 2.6;
                star.push_back(BLPoint(6.0 + r * std::cos(a), 6.0 + r * std::sin(a)));
            }
            scatter("Stars", artFrom({filled(SketchObject::makePolygon(star), "#f2b824")}, 0, BLBox(0, 0, 12, 12)), 220.0, 60.0, 40.0, 180.0, false);
        }

        {
            // A pointed leaf, 100 long and 6 across at the middle: weight 1 draws it at that size.
            BLPath lens;
            lens.move_to(0, 3);
            lens.quad_to(50, -3, 100, 3);
            lens.quad_to(50, 9, 0, 3);
            lens.close();
            auto def = std::make_shared<BrushDef>();
            def->name = "Tapered";
            def->kind = BrushKind::Art;
            def->art = artFrom({filled(SketchObject::makePath(lens), "#222222")}, 0, BLBox(0, 0, 100, 6));
            presets.push_back(def);
        }

        std::size_t added = 0;
        Library before = brushes_, after = brushes_;
        for (const std::shared_ptr<BrushDef>& preset : presets) {
            bool known = false;
            for (const std::shared_ptr<const BrushDef>& entry : after) {
                known = known || entry->name == preset->name;
            }
            if (!known) {
                preset->id = nextBrushId_++;
                if (preset->art) {
                    auto art = std::make_shared<SymbolDef>(*preset->art);
                    art->id = preset->id;
                    art->name = preset->name;
                    preset->art = art;
                }
                after.push_back(preset);
                ++added;
            }
        }
        if (added == 0) {
            return 0;
        }
        if (activeBrush_ == 0) {
            activeBrush_ = after.front()->id;
        }
        undoStack_.push({"Add Brush Presets", [this, after]() { brushes_ = after; changed(); }, [this, before]() { brushes_ = before; changed(); }});
        return added;
    }

    bool SketchDocument::applyBrush(std::size_t id) {
        std::shared_ptr<const BrushDef> def = id != 0 ? findBrush(id) : nullptr;
        if (id != 0 && !def) {
            return false;
        }
        std::vector<StyleState> before = snapshotStyle();
        bool any = false;
        for (std::size_t selected : selection_) {
            SketchObject* object = findById(selected);
            if (object == nullptr || object->isSymbol()) {
                continue;
            }
            StrokeOptions options = object->strokeOptions();
            if (options.brush == def && options.brushRef == 0) {
                continue;
            }
            options.brush = def;
            options.brushRef = 0;
            object->setStrokeOptions(options);
            if (def && object->stroke().isNone()) {
                object->setStroke(defaultStroke().isNone() ? SketchPaint::solid(SketchColor::gray(0.0f)) : defaultStroke());
                object->setStrokeWidth(def->kind == BrushKind::Calligraphic ? std::max(2.0, defaultStrokeWidth()) : 1.0);
            }
            any = true;
        }
        if (!any) {
            return false;
        }
        changed();
        return commitStyle(before, def ? "Apply Brush" : "Remove Brush");
    }

    bool SketchDocument::changeBrush(std::size_t id, const BrushDef& edited) {
        std::shared_ptr<const BrushDef> old = findBrush(id);
        if (!old) {
            return false;
        }
        auto fresh = std::make_shared<BrushDef>(*old);
        if (!edited.name.empty()) {
            fresh->name = edited.name;
        }
        fresh->setParams(edited.params());
        if (fresh->name == old->name && fresh->sameLook(*old)) {
            return false;
        }
        auto apply = [this, id](const std::shared_ptr<const BrushDef>& to) {
            for (std::shared_ptr<const BrushDef>& entry : brushes_) {
                if (entry->id == id) {
                    entry = to;
                }
            }
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                std::vector<AppearanceItem> items = object->appearance();
                if (rebind(items, id, to)) {
                    object->setAppearance(items);
                }
            }
            for (GraphicStyle& style : graphicStyles_) {
                rebind(style.items, id, to);
            }
            changed();
        };
        undoStack_.push({"Edit Brush", [apply, fresh]() { apply(fresh); }, [apply, old]() { apply(old); }});
        return true;
    }

    bool SketchDocument::deleteBrush(std::size_t id) {
        if (!findBrush(id) || brushUseCount(id) > 0) {
            return false;
        }
        Library before = brushes_, after;
        for (const std::shared_ptr<const BrushDef>& entry : brushes_) {
            if (entry->id != id) {
                after.push_back(entry);
            }
        }
        undoStack_.push({"Delete Brush", [this, after]() { brushes_ = after; changed(); }, [this, before]() { brushes_ = before; changed(); }});
        return true;
    }

    std::shared_ptr<const BrushDef> SketchDocument::importBrush(const std::shared_ptr<const BrushDef>& def) {
        if (!def) {
            return nullptr;
        }
        for (const std::shared_ptr<const BrushDef>& entry : brushes_) {
            if (entry == def) {
                return entry;
            }
        }
        const std::string text = def->art && def->art->content ? svgFromDocument(*def->art->content) : std::string();
        for (const std::shared_ptr<const BrushDef>& entry : brushes_) {
            if (entry->name == def->name && entry->sameLook(*def) &&
                (entry->art && entry->art->content ? svgFromDocument(*entry->art->content) : std::string()) == text) {
                return entry;
            }
        }
        auto copy = std::make_shared<BrushDef>(*def);
        copy->id = nextBrushId_++;
        brushes_.push_back(copy);
        return copy;
    }

    std::shared_ptr<const BrushDef> SketchDocument::selectedBrush() const {
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && object->strokeOptions().brush) {
                return object->strokeOptions().brush;
            }
        }
        return nullptr;
    }

}
