#include "curvy/sketchdocument.h"

// Clipping masks: a group with GroupProps::clip set shows its members only inside the outline of its topmost member.

namespace curvy {

    const SketchObject* SketchDocument::clipMaskOf(std::size_t groupId) const {
        if (!groupProps(groupId).clip) {
            return nullptr;
        }
        const SketchObject* mask = nullptr;
        std::size_t members = 0;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            for (std::size_t g : object->groups()) {
                if (g == groupId) {
                    mask = object.get();
                    ++members;
                    break;
                }
            }
        }
        return members >= 2 ? mask : nullptr;
    }

    bool SketchDocument::canMakeClippingMask() const {
        if (selection_.size() < 2) {
            return false;
        }
        std::size_t group = selectedGroup();
        if (group != 0 && groupProps(group).clip) {
            return false;
        }
        // The mask is the topmost selected object (or the group's topmost member): it has to enclose an area, so a straight line or a
        // single point cannot be one. An open path is closed by a straight line for this, as when it is filled.
        const SketchObject* top = nullptr;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (isSelected(object->id())) {
                top = object.get();
            }
        }
        if (top == nullptr) {
            return false;
        }
        BLBox box = top->geometryBounds();
        return box.x1 - box.x0 > 1e-6 && box.y1 - box.y0 > 1e-6;
    }

    bool SketchDocument::canReleaseClippingMask() const {
        std::size_t group = selectedGroup();
        return group != 0 && groupProps(group).clip;
    }

    bool SketchDocument::makeClippingMask() {
        if (!canMakeClippingMask()) {
            return false;
        }
        std::size_t group = selectedGroup();
        if (group == 0) {
            return groupSelection(true);
        }
        // An existing group: flag it; its topmost member becomes the mask.
        GroupProps before = groupProps(group), after = before;
        after.clip = true;
        setGroupProps(group, after);
        undoStack_.push({"Make Clipping Mask",
            [this, group, after]() { setGroupProps(group, after); },
            [this, group, before]() { setGroupProps(group, before); }});
        return true;
    }

    bool SketchDocument::releaseClippingMask() {
        std::size_t group = selectedGroup();
        if (group == 0 || !groupProps(group).clip) {
            return false;
        }
        GroupProps before = groupProps(group), after = before;
        after.clip = false;
        setGroupProps(group, after);
        undoStack_.push({"Release Clipping Mask",
            [this, group, after]() { setGroupProps(group, after); },
            [this, group, before]() { setGroupProps(group, before); }});
        return true;
    }

}
