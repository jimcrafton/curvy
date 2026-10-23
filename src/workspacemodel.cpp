#include "curvy/workspacemodel.h"

#include <cstdio>

namespace curvy {

    void WorkspaceModel::touch() {
        onChanged(*this);
        updateAllViews();
    }

    void WorkspaceModel::setDocument(bool open, const std::string& name) {
        documentOpen_ = open;
        documentName_ = name;
        if (!open) {
            message_ = "Tap Space to start";
        }
        touch();
    }

    void WorkspaceModel::setTool(const std::string& tool, const std::string& heldKey) {
        tool_ = tool;
        heldKey_ = heldKey;
        touch();
    }

    void WorkspaceModel::setHover(const Hover& hover) {
        if (hover.object == hover_.object && hover.part == hover_.part) {
            return;
        }
        hover_ = hover;
        touch();
    }

    void WorkspaceModel::setScrub(bool active, ScrubPart part, const ScrubProp* prop, double value, double anchorX, double anchorY) {
        scrubActive_ = active;
        scrubPart_ = part;
        scrubProp_ = prop;
        scrubValue_ = value;
        anchorX_ = anchorX;
        anchorY_ = anchorY;
        touch();
    }

    void WorkspaceModel::setScrubAxis(ScrubAxis axis) {
        axis_ = axis;
        touch();
    }

    void WorkspaceModel::setToolbarVisible(bool visible) {
        toolbarVisible_ = visible;
        touch();
    }

    void WorkspaceModel::setPropertiesVisible(bool visible) {
        propertiesVisible_ = visible;
        touch();
    }

    void WorkspaceModel::setRing(const std::string& title) {
        ring_ = title;
        touch();
    }

    void WorkspaceModel::setSpaceDown(bool down) {
        spaceDown_ = down;
        touch();
    }

    void WorkspaceModel::setSelectionCount(std::size_t count) {
        selectionCount_ = count;
        touch();
    }

    void WorkspaceModel::setPointer(bool known, double x, double y, double zoomPercent) {
        pointerKnown_ = known;
        pointerX_ = x;
        pointerY_ = y;
        zoomPercent_ = zoomPercent;
        touch();
    }

    void WorkspaceModel::say(const std::string& text) {
        message_ = text;
        touch();
    }

    bool WorkspaceModel::fresh(const std::string& feature) const {
        const auto it = uses_.find(feature);
        return !hintsFade_ || it == uses_.end() || it->second < 3;
    }

    void WorkspaceModel::used(const std::string& feature) {
        ++uses_[feature];
    }

    std::string WorkspaceModel::idleHint() const {
        const char* travel = axis_ == ScrubAxis::Vertical ? " and move up/down" : " and move sideways";
        if (hover_.part != ScrubPart::None) {
            std::string text = hover_.part == ScrubPart::Stroke ? "Outline: hold" : "Fill: hold";
            for (const ScrubProp& prop : HoldScrub::props(hover_.part)) {
                text += std::string(" ") + prop.key + " " + prop.name;
            }
            return text + travel;
        }
        std::vector<std::string> parts;
        if (selectionCount_ > 0) {
            if (fresh("duplicate")) { parts.push_back("Ctrl+D duplicate"); }
            if (fresh("delete")) { parts.push_back("Del delete"); }
            if (fresh("rightClick")) { parts.push_back("right-click or pen button: object ring"); }
        } else {
            if (fresh("holdDraw")) { parts.push_back("hold R E P S L and drag to draw"); }
            if (fresh("pan")) { parts.push_back("hold Space and drag to pan"); }
        }
        if (fresh("ring")) { parts.push_back("tap Space for the menu"); }
        if (parts.empty()) {
            return "Space menu";
        }
        std::string text = parts[0];
        for (std::size_t i = 1; i < parts.size(); ++i) {
            text += " - " + parts[i];
        }
        return text;
    }

    std::string WorkspaceModel::hintTool() const {
        return documentOpen_ ? tool_ : std::string("Nothing open");
    }

    std::string WorkspaceModel::hintKeys() const {
        if (!documentOpen_) {
            return message_;
        }
        if (scrubActive_ && scrubProp_ != nullptr) {
            return std::string(scrubPart_ == ScrubPart::Stroke ? "Outline " : "Fill ") + scrubProp_->name + " " + HoldScrub::format(*scrubProp_, scrubValue_) +
                (axis_ == ScrubAxis::Vertical ? " - move up/down" : " - move sideways") + " - Shift snap - Esc cancel";
        }
        if (!ring_.empty()) {
            return "Ring: " + ring_;
        }
        if (spaceDown_) {
            return "drag to pan, or release for the menu";
        }
        if (tool_ != "Select") {
            return heldKey_.empty() ? "drag to draw - V or Esc for Select" : "drag to draw - Shift square - Alt from center - release " + heldKey_ + " to stop";
        }
        const std::string hint = idleHint();
        return !message_.empty() && selectionCount_ == 0 && hover_.part == ScrubPart::None ? message_ + "  |  " + hint : hint;
    }

    std::string WorkspaceModel::hintInfo() const {
        if (!documentOpen_ || !pointerKnown_) {
            return std::string();
        }
        char info[96];
        std::snprintf(info, sizeof(info), "x %.0f  y %.0f  %.0f%%", pointerX_, pointerY_, zoomPercent_);
        return info;
    }

}
