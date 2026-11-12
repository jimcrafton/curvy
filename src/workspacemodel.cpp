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

    void WorkspaceModel::setRing(const std::string& title) {
        ring_ = title;
        touch();
    }

    void WorkspaceModel::setSpaceDown(bool down) {
        spaceDown_ = down;
        touch();
    }

    void WorkspaceModel::say(const std::string& text) {
        message_ = text;
        touch();
    }

    std::string WorkspaceModel::hintTool() const {
        return documentOpen_ ? std::string("Select") : std::string("Nothing open");
    }

    std::string WorkspaceModel::hintKeys() const {
        if (!ring_.empty()) {
            return "Ring: " + ring_;
        }
        if (spaceDown_) {
            return "release for the menu";
        }
        return message_;
    }

    void WorkspaceModel::setZoom(double percent) {
        if (percent != zoomPercent_) {
            zoomPercent_ = percent;
            touch();
        }
    }

    std::string WorkspaceModel::hintInfo() const {
        if (!documentOpen_) {
            return std::string();
        }
        char text[32];
        std::snprintf(text, sizeof(text), zoomPercent_ < 10.0 ? "%.1f%%" : "%.0f%%", zoomPercent_);
        return text;
    }

}
