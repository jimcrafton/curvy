#include "curvy/sketchdocument.h"

#include "curvy/sketchsvg.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

// The symbol library: artwork defined once, placed as Symbol objects (see SymbolDef in sketchobject.h).

namespace curvy {

    namespace {
        using Library = std::vector<std::shared_ptr<const SymbolDef>>;

        // The selected objects (stacking order) copied into a new definition `id`: their bounds are its box. Null when nothing is selected.
        std::shared_ptr<SymbolDef> buildDefinition(const SketchDocument& document, std::size_t id, const std::string& name, std::vector<const SketchObject*>& picked) {
            for (std::size_t i = 0; i < document.objectCount(); ++i) {   // stacking order
                if (document.isSelected(document.object(i).id())) {
                    picked.push_back(&document.object(i));
                }
            }
            if (picked.empty()) {
                return nullptr;
            }
            auto def = std::make_shared<SymbolDef>();
            def->id = id;
            def->name = name;
            def->content = std::make_shared<SketchDocument>();
            BLBox box = picked.front()->paintBounds();
            Library used;
            for (const SketchObject* object : picked) {
                BLBox b = object->paintBounds();
                box = BLBox(std::min(box.x0, b.x0), std::min(box.y0, b.y0), std::max(box.x1, b.x1), std::max(box.y1, b.y1));
                SketchObject* copy = object->clone();
                def->content->setLayerOf(*copy, 0);
                def->content->add(copy);
                for (std::size_t g : def->content->groupsOf(*copy)) {
                    def->content->setGroupProps(g, document.groupProps(g));
                }
                if (copy->isSymbol() && std::find(used.begin(), used.end(), copy->symbol()) == used.end()) {
                    used.push_back(copy->symbol());
                }
            }
            def->content->setSymbols(used);
            std::vector<std::shared_ptr<const BrushDef>> brushesUsed;
            for (const SketchObject* object : picked) {
                for (const AppearanceItem& item : object->appearance()) {
                    const std::shared_ptr<const BrushDef>& brush = item.strokeOptions.brush;
                    if (brush && std::find(brushesUsed.begin(), brushesUsed.end(), brush) == brushesUsed.end()) {
                        brushesUsed.push_back(brush);
                    }
                }
            }
            def->content->setBrushes(brushesUsed);
            // A tile never has no size.
            if (!(box.x1 - box.x0 >= 1.0)) { box.x1 = box.x0 + 1.0; }
            if (!(box.y1 - box.y0 >= 1.0)) { box.y1 = box.y0 + 1.0; }
            def->box = box;
            return def;
        }
    }

    std::shared_ptr<SymbolDef> SketchDocument::selectionArtwork(std::size_t id, const std::string& name) const {
        std::vector<const SketchObject*> picked;
        return buildDefinition(*this, id, name, picked);
    }

    std::shared_ptr<const SymbolDef> SketchDocument::findSymbol(std::size_t id) const {
        for (const std::shared_ptr<const SymbolDef>& def : symbols_) {
            if (def->id == id) {
                return def;
            }
        }
        return nullptr;
    }

    std::size_t SketchDocument::activeSymbol() const {
        if (findSymbol(activeSymbol_)) {
            return activeSymbol_;
        }
        return symbols_.empty() ? 0 : symbols_.front()->id;
    }

    void SketchDocument::setActiveSymbol(std::size_t id) {
        if (id != activeSymbol_ && findSymbol(id)) {
            activeSymbol_ = id;
            onChanged(*this);
        }
    }

    std::size_t SketchDocument::activePattern() const {
        if (findPattern(activePattern_)) {
            return activePattern_;
        }
        return patterns_.empty() ? 0 : patterns_.front()->id;
    }

    void SketchDocument::setActivePattern(std::size_t id) {
        if (id != activePattern_ && findPattern(id)) {
            activePattern_ = id;
            onChanged(*this);
        }
    }

    std::size_t SketchDocument::placeActiveSymbol() {
        const Artboard& board = activeArtboardRect();
        return placeSymbol(activeSymbol(), BLPoint(board.x + board.width * 0.5, board.y + board.height * 0.5));
    }

    std::size_t SketchDocument::symbolUseCount(std::size_t id) const {
        std::size_t uses = 0;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            uses += object->isSymbol() && object->symbol() && object->symbol()->id == id ? 1 : 0;
        }
        return uses;
    }

    void SketchDocument::setSymbols(std::vector<std::shared_ptr<const SymbolDef>> symbols) {
        symbols_ = std::move(symbols);
        for (const std::shared_ptr<const SymbolDef>& def : symbols_) {
            nextSymbolId_ = std::max(nextSymbolId_, def->id + 1);
        }
        onChanged(*this);
    }

    std::size_t SketchDocument::makeSymbol(const std::string& name) {
        std::vector<const SketchObject*> picked;
        const std::size_t id = nextSymbolId_;
        std::shared_ptr<SymbolDef> def = buildDefinition(*this, id, name.empty() ? "Symbol " + std::to_string(id) : name, picked);
        if (!def) {
            return 0;
        }
        ++nextSymbolId_;
        activeSymbol_ = id;
        symbols_.push_back(def);

        std::unique_ptr<SketchObject> instance(SketchObject::makeSymbolInstance(def, BLMatrix2D::make_identity()));
        setLayerOf(*instance, layerOf(*picked.back()));
        Replacement replacement;
        replacement.remove = selection_;
        replacement.add.push_back(std::move(instance));
        replacement.atTop = true;
        std::vector<Replacement> replacements;
        replacements.push_back(std::move(replacement));
        replaceUndoable(std::move(replacements), "Make Symbol");
        return def->id;
    }

    std::size_t SketchDocument::placeSymbol(std::size_t symbolId, const BLPoint& center) {
        std::shared_ptr<const SymbolDef> def = findSymbol(symbolId);
        if (!def) {
            return 0;
        }
        BLMatrix2D place = BLMatrix2D::make_translation(center.x - (def->box.x0 + def->box.x1) * 0.5, center.y - (def->box.y0 + def->box.y1) * 0.5);
        std::vector<std::unique_ptr<SketchObject>> objects;
        objects.emplace_back(SketchObject::makeSymbolInstance(def, place));
        std::vector<std::size_t> ids = addObjectsUndoable(std::move(objects), "Place Symbol");
        return ids.empty() ? 0 : ids.front();
    }

    bool SketchDocument::redefineSymbol(std::size_t symbolId) {
        std::shared_ptr<const SymbolDef> old = findSymbol(symbolId);
        if (!old || selection_.empty()) {
            return false;
        }
        for (std::size_t selected : selection_) {
            const SketchObject* object = findById(selected);
            if (object != nullptr && object->isSymbol() && object->symbol() && object->symbol()->id == symbolId) {
                return false;   // an instance of itself
            }
        }
        std::vector<const SketchObject*> picked;
        std::shared_ptr<SymbolDef> def = buildDefinition(*this, symbolId, old->name, picked);
        if (!def) {
            return false;
        }

        // Every instance keeps its place: the new artwork's box is carried by the matrix the old one had.
        struct Follow { std::size_t id; BLPath before, after; };
        std::vector<Follow> follow;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->isSymbol() && object->symbol() && object->symbol()->id == symbolId) {
                std::unique_ptr<SketchObject> fresh(SketchObject::makeSymbolInstance(def, object->symbolMatrix()));
                follow.push_back({object->id(), object->geometry(), fresh->geometry()});
            }
        }
        auto apply = [this, symbolId, follow](const std::shared_ptr<const SymbolDef>& to, bool forward) {
            for (std::shared_ptr<const SymbolDef>& entry : symbols_) {
                if (entry->id == symbolId) {
                    entry = to;
                }
            }
            for (const Follow& f : follow) {
                if (SketchObject* object = findById(f.id)) {
                    object->setSymbol(to);
                    setObjectOutline(*object, forward ? f.after : f.before);
                }
            }
            changed();
        };
        undoStack_.push({"Redefine Symbol",
            [apply, def]() { apply(def, true); },
            [apply, old]() { apply(old, false); }});
        return true;
    }

    bool SketchDocument::canBreakSymbolLink() const {
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && object->isSymbol()) {
                return true;
            }
        }
        return false;
    }

    bool SketchDocument::breakSymbolLink() {
        std::vector<Replacement> replacements;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!isSelected(object->id()) || !object->isSymbol() || !object->symbol() || !object->symbol()->content) {
                continue;
            }
            const SketchDocument& content = *object->symbol()->content;
            BLMatrix2D matrix = object->symbolMatrix();
            Replacement replacement;
            replacement.remove.push_back(object->id());
            std::map<std::size_t, std::size_t> fresh;   // the artwork's groups become groups of this document
            for (std::size_t i = 0; i < content.objectCount(); ++i) {
                std::unique_ptr<SketchObject> copy(content.object(i).clone());
                copy->transform(matrix);
                setLayerOf(*copy, layerOf(*object));
                std::vector<std::size_t> groups;
                for (std::size_t g : content.groupsOf(*copy)) {
                    auto it = fresh.find(g);
                    if (it == fresh.end()) {
                        std::size_t made = nextGroupId_++;
                        GroupProps props = content.groupProps(g);
                        if (!props.isDefault()) {
                            groupProps_[made] = props;
                        }
                        it = fresh.emplace(g, made).first;
                    }
                    groups.push_back(it->second);
                }
                // The instance's own look (opacity / blend) cannot be carried onto several objects; the artwork keeps its own.
                if (copy->isSymbol()) {
                    copy->setSymbol(importSymbol(copy->symbol()));
                }
                setGroupsOf(*copy, groups);
                replacement.add.push_back(std::move(copy));
            }
            replacement.atTop = true;
            replacements.push_back(std::move(replacement));
        }
        if (replacements.empty()) {
            return false;
        }
        replaceUndoable(std::move(replacements), "Break Link to Symbol");
        return true;
    }

    bool SketchDocument::renameSymbol(std::size_t symbolId, const std::string& name) {
        std::shared_ptr<const SymbolDef> old = findSymbol(symbolId);
        if (!old || name.empty() || name == old->name) {
            return false;
        }
        auto renamed = std::make_shared<SymbolDef>(*old);
        renamed->name = name;
        auto apply = [this, symbolId](const std::shared_ptr<const SymbolDef>& to) {
            for (std::shared_ptr<const SymbolDef>& entry : symbols_) {
                if (entry->id == symbolId) {
                    entry = to;
                }
            }
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                if (object->isSymbol() && object->symbol() && object->symbol()->id == symbolId) {
                    object->setSymbol(to);
                }
            }
            changed();
        };
        undoStack_.push({"Rename Symbol", [apply, renamed]() { apply(renamed); }, [apply, old]() { apply(old); }});
        return true;
    }

    bool SketchDocument::deleteSymbol(std::size_t symbolId) {
        std::shared_ptr<const SymbolDef> old = findSymbol(symbolId);
        if (!old || symbolUseCount(symbolId) > 0) {
            return false;
        }
        Library before = symbols_, after;
        for (const std::shared_ptr<const SymbolDef>& entry : symbols_) {
            if (entry->id != symbolId) {
                after.push_back(entry);
            }
        }
        undoStack_.push({"Delete Symbol",
            [this, after]() { symbols_ = after; changed(); },
            [this, before]() { symbols_ = before; changed(); }});
        return true;
    }

    std::shared_ptr<const SymbolDef> SketchDocument::importSymbol(const std::shared_ptr<const SymbolDef>& def) {
        if (!def) {
            return nullptr;
        }
        for (const std::shared_ptr<const SymbolDef>& entry : symbols_) {
            if (entry == def) {
                return entry;
            }
        }
        const std::string text = def->content ? svgFromDocument(*def->content) : std::string();
        for (const std::shared_ptr<const SymbolDef>& entry : symbols_) {
            if (entry->name == def->name && entry->box.x0 == def->box.x0 && entry->box.y0 == def->box.y0 && entry->box.x1 == def->box.x1 &&
                entry->box.y1 == def->box.y1 && entry->content && svgFromDocument(*entry->content) == text) {
                return entry;
            }
        }
        auto copy = std::make_shared<SymbolDef>(*def);
        copy->id = nextSymbolId_++;
        symbols_.push_back(copy);
        return copy;
    }

    // ---- patterns ----

    std::shared_ptr<const SymbolDef> SketchDocument::findPattern(std::size_t id) const {
        for (const std::shared_ptr<const SymbolDef>& def : patterns_) {
            if (def->id == id) {
                return def;
            }
        }
        return nullptr;
    }

    std::size_t SketchDocument::patternUseCount(std::size_t id) const {
        std::size_t uses = 0;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            bool used = false;
            for (const AppearanceItem& item : object->appearance()) {
                used = used || (item.paint.isPattern() && item.paint.patternId() == id);
            }
            uses += used ? 1 : 0;
        }
        return uses;
    }

    void SketchDocument::setPatterns(std::vector<std::shared_ptr<const SymbolDef>> patterns) {
        patterns_ = std::move(patterns);
        for (const std::shared_ptr<const SymbolDef>& def : patterns_) {
            nextSymbolId_ = std::max(nextSymbolId_, def->id + 1);
        }
        bindPatternPaints();
        onChanged(*this);
    }

    void SketchDocument::bindPatternPaints() {
        auto bind = [this](std::vector<AppearanceItem>& items) {
            bool any = false;
            for (AppearanceItem& item : items) {
                if (item.paint.isPattern() && !item.paint.patternDef()) {
                    item.paint.bindPattern(findPattern(item.paint.patternId()));
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

    std::size_t SketchDocument::makePattern(const std::string& name) {
        std::vector<const SketchObject*> picked;
        const std::size_t id = nextSymbolId_;
        std::shared_ptr<SymbolDef> def = buildDefinition(*this, id, name.empty() ? "Pattern " + std::to_string(patterns_.size() + 1) : name, picked);
        if (!def) {
            return 0;
        }
        ++nextSymbolId_;
        activePattern_ = id;
        Library before = patterns_, after = patterns_;
        after.push_back(def);
        undoStack_.push({"Make Pattern", [this, after]() { patterns_ = after; changed(); }, [this, before]() { patterns_ = before; changed(); }});
        return id;
    }

    bool SketchDocument::applyPattern(std::size_t patternId, bool toStroke) {
        std::shared_ptr<const SymbolDef> def = findPattern(patternId);
        if (!def || selection_.empty()) {
            return false;
        }
        std::vector<StyleState> before = snapshotStyle();
        bool any = false;
        for (std::size_t id : selection_) {
            SketchObject* object = findById(id);
            if (object == nullptr || object->isSymbol()) {
                continue;
            }
            const SketchPaint& current = toStroke ? object->stroke() : object->fill();
            double scale = current.isPattern() ? current.patternScale() : 1.0, angle = current.isPattern() ? current.patternAngle() : 0.0;
            if (toStroke) {
                object->setStroke(SketchPaint::pattern(def, scale, angle));
            } else {
                object->setFill(SketchPaint::pattern(def, scale, angle));
            }
            any = true;
        }
        if (!any) {
            return false;
        }
        changed();
        return commitStyle(before, toStroke ? "Pattern Stroke" : "Pattern Fill");
    }

    const SketchPaint* SketchDocument::selectedPatternPaint() const {
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            if (object->fill().isPattern()) {
                return &object->fill();
            }
            if (object->stroke().isPattern()) {
                return &object->stroke();
            }
        }
        return nullptr;
    }

    bool SketchDocument::setPatternTransform(double scale, double angleDegrees) {
        if (!(scale > 0.0) || !std::isfinite(scale) || !std::isfinite(angleDegrees)) {
            return false;
        }
        std::vector<StyleState> before = snapshotStyle();
        bool any = false;
        for (std::size_t id : selection_) {
            SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            if (object->fill().isPattern()) {
                object->setFill(SketchPaint::pattern(object->fill().patternDef(), scale, angleDegrees));
                any = true;
            }
            if (object->stroke().isPattern()) {
                object->setStroke(SketchPaint::pattern(object->stroke().patternDef(), scale, angleDegrees));
                any = true;
            }
        }
        if (!any) {
            return false;
        }
        changed();
        return commitStyle(before, "Pattern Transform");
    }

    bool SketchDocument::renamePattern(std::size_t patternId, const std::string& name) {
        std::shared_ptr<const SymbolDef> old = findPattern(patternId);
        if (!old || name.empty() || name == old->name) {
            return false;
        }
        auto renamed = std::make_shared<SymbolDef>(*old);
        renamed->name = name;
        Library before = patterns_, after = patterns_;
        for (std::shared_ptr<const SymbolDef>& entry : after) {
            if (entry->id == patternId) {
                entry = renamed;
            }
        }
        // The paints that name it keep pointing at the old definition (same artwork), which is only a different name.
        undoStack_.push({"Rename Pattern", [this, after]() { patterns_ = after; changed(); }, [this, before]() { patterns_ = before; changed(); }});
        return true;
    }

    bool SketchDocument::deletePattern(std::size_t patternId) {
        if (!findPattern(patternId) || patternUseCount(patternId) > 0) {
            return false;
        }
        Library before = patterns_, after;
        for (const std::shared_ptr<const SymbolDef>& entry : patterns_) {
            if (entry->id != patternId) {
                after.push_back(entry);
            }
        }
        undoStack_.push({"Delete Pattern", [this, after]() { patterns_ = after; changed(); }, [this, before]() { patterns_ = before; changed(); }});
        return true;
    }

    std::shared_ptr<const SymbolDef> SketchDocument::importPattern(const std::shared_ptr<const SymbolDef>& def) {
        if (!def) {
            return nullptr;
        }
        for (const std::shared_ptr<const SymbolDef>& entry : patterns_) {
            if (entry == def) {
                return entry;
            }
        }
        const std::string text = def->content ? svgFromDocument(*def->content) : std::string();
        for (const std::shared_ptr<const SymbolDef>& entry : patterns_) {
            if (entry->name == def->name && entry->box.x0 == def->box.x0 && entry->box.y0 == def->box.y0 && entry->box.x1 == def->box.x1 &&
                entry->box.y1 == def->box.y1 && entry->content && svgFromDocument(*entry->content) == text) {
                return entry;
            }
        }
        auto copy = std::make_shared<SymbolDef>(*def);
        copy->id = nextSymbolId_++;
        patterns_.push_back(copy);
        return copy;
    }

}
