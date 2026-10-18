#include "curvy/sketchdocument.h"

#include "curvy/imagedata.h"

#include <algorithm>
#include <cmath>

// Expand Appearance: an object's fills, strokes and effects become ordinary objects.

namespace curvy {

    namespace {
        bool expandable(const SketchObject& object) {
            return object.isVisible() && !object.isLocked() && !object.isText() && !object.isSymbol() && !object.isImage();
        }

        // The object as it looks, drawn into a picture (at most 4096 pixels a side) and placed where it was.
        SketchObject* picture(const SketchObject& object) {
            const BLBox b = object.paintBounds();
            const double width = b.x1 - b.x0, height = b.y1 - b.y0;
            if (!(width > 0.0) || !(height > 0.0)) {
                return nullptr;
            }
            const double scale = std::min(2.0, 4096.0 / std::max(width, height));
            BLImage image(int(std::ceil(width * scale)), int(std::ceil(height * scale)), BL_FORMAT_PRGB32);
            {
                BLContext ctx(image);
                ctx.clear_all();
                ctx.set_transform(BLMatrix2D(scale, 0.0, 0.0, scale, -b.x0 * scale, -b.y0 * scale));
                object.paint(ctx);
                ctx.end();
            }
            BLImageCodec codec;
            codec.find_by_name("PNG");
            BLArray<uint8_t> buffer;
            if (image.write_to_data(buffer, codec) != BL_SUCCESS) {
                return nullptr;
            }
            std::shared_ptr<const ImageData> data = decodeImage(std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()), "Expanded Appearance");
            if (!data) {
                return nullptr;
            }
            BLMatrix2D placement = BLMatrix2D::make_scaling(1.0 / scale, 1.0 / scale);
            placement.post_translate(b.x0, b.y0);
            SketchObject* result = SketchObject::makeImageObject(data, placement);
            result->setLayer(object.layer());
            result->setGroups(object.groups());
            return result;
        }
    }

    bool SketchDocument::expandAppearance() {
        std::vector<Replacement> replacements;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!isSelected(object->id()) || !expandable(*object)) {
                continue;
            }
            Replacement replacement;
            replacement.remove.push_back(object->id());
            if (object->hasPixelEffects()) {
                if (SketchObject* flat = picture(*object)) {
                    replacement.add.emplace_back(flat);
                    replacements.push_back(std::move(replacement));
                }
                continue;
            }
            std::vector<std::unique_ptr<SketchObject>> parts;
            auto add = [&](const BLPath& outline, const std::vector<AppearanceItem>& items, const char* name) {
                if (outline.size() == 0) {
                    return;
                }
                SketchObject* part = object->cloneWithGeometry(SketchObjectType::Path, outline);
                part->setAppearance(items);
                part->setOpacity(1.0);
                part->setBlendMode(BlendMode::Normal);
                part->setName(name);
                parts.emplace_back(part);
            };
            const std::vector<AppearanceItem>& items = object->appearance();
            for (std::size_t i = 0; i < items.size(); ++i) {
                const AppearanceItem& item = items[i];
                if (!item.visible) {
                    continue;
                }
                if (item.kind == AppearanceKind::Fill && object->isClosed() && !item.paint.isNone()) {
                    AppearanceItem stroke = AppearanceItem::makeStroke(SketchPaint::none(), 0.0);
                    add(object->itemOutline(i), {item, stroke}, "Expanded Fill");
                } else if (item.kind == AppearanceKind::Stroke && !item.paint.isNone() && item.strokeWidth > 0.0) {
                    const BLPath shown = object->itemOutline(i);
                    if (item.strokeOptions.brush) {
                        // A brush stroke is its artwork: nib outline filled with the stroke paint, or the (bent / scattered) outlines of the artwork.
                        const BrushDef& brush = *item.strokeOptions.brush;
                        auto artwork = [&](const std::vector<BrushPart>& pieces) {
                            for (const BrushPart& piece : pieces) {
                                AppearanceItem fill = AppearanceItem::makeFill(piece.closed ? piece.fill : SketchPaint::none());
                                AppearanceItem line = AppearanceItem::makeStroke(piece.stroke, piece.strokeWidth);
                                fill.opacity = item.opacity;
                                line.opacity = item.opacity;
                                add(piece.path, {fill, line}, "Expanded Brush");
                            }
                        };
                        if (brush.kind == BrushKind::Calligraphic) {
                            AppearanceItem fill = AppearanceItem::makeFill(item.paint);
                            fill.opacity = item.opacity;
                            fill.blendMode = item.blendMode;
                            add(brush.nibOutline(shown, item.strokeWidth), {fill, AppearanceItem::makeStroke(SketchPaint::none(), 0.0)}, "Expanded Brush");
                        } else if (brush.kind == BrushKind::Art) {
                            artwork(brush.bendAlong(shown, item.strokeWidth));
                        } else {
                            for (const BLMatrix2D& matrix : brush.placements(shown, item.strokeWidth)) {
                                std::vector<BrushPart> moved = brush.parts();
                                for (BrushPart& piece : moved) {
                                    piece.path.transform(matrix);
                                    piece.strokeWidth *= std::sqrt(std::fabs(matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10));
                                }
                                artwork(moved);
                            }
                        }
                        continue;
                    }
                    BLPath outline;
                    if (object->strokeOutline(i, outline)) {
                        AppearanceItem fill = AppearanceItem::makeFill(item.paint);
                        fill.opacity = item.opacity;
                        fill.blendMode = item.blendMode;
                        add(outline, {fill, AppearanceItem::makeStroke(SketchPaint::none(), 0.0)}, "Expanded Stroke");
                    }
                }
            }
            if (parts.empty()) {
                continue;
            }
            // The whole-object opacity and blend mode: on the one result, or on the group of them.
            if (parts.size() == 1) {
                parts.front()->setOpacity(object->opacity());
                parts.front()->setBlendMode(object->blendMode());
            } else {
                const std::size_t group = nextGroupId_++;
                GroupProps props;
                props.opacity = object->opacity();
                props.blendMode = object->blendMode();
                if (!props.isDefault()) {
                    groupProps_[group] = props;
                }
                for (std::unique_ptr<SketchObject>& part : parts) {
                    std::vector<std::size_t> groups = part->groups();
                    groups.push_back(group);
                    part->setGroups(groups);
                }
            }
            for (std::unique_ptr<SketchObject>& part : parts) {
                replacement.add.push_back(std::move(part));
            }
            replacements.push_back(std::move(replacement));
        }
        if (replacements.empty()) {
            return false;
        }
        replaceUndoable(std::move(replacements), "Expand Appearance");
        return true;
    }

}
