#pragma once

#include "curvy/holdscrub.h"

#include <newui/models.h>

#include <map>
#include <string>

namespace curvy {

    // The state of the workspace shell that is not the artwork: whether a document is open, the tool, what the pointer is over, a
    // scrub in progress, the ring that is open, and the text of the hint bar. The controller changes it; the hint bar and the canvas
    // follow it (onChanged and Model::updateAllViews). Everything the hint bar says is worked out here, so it can be tested without a window.
    // @reflect ignore=true
    class WorkspaceModel : public newui::Model {
    public:
        enum class ScrubAxis { Vertical, Horizontal };

        bool documentOpen() const { return documentOpen_; }
        const std::string& documentName() const { return documentName_; }
        void setDocument(bool open, const std::string& name);

        const std::string& tool() const { return tool_; }
        void setTool(const std::string& tool, const std::string& heldKey = std::string());
        const std::string& heldToolKey() const { return heldKey_; }

        struct Hover {
            std::size_t object = 0;
            ScrubPart part = ScrubPart::None;
        };
        const Hover& hover() const { return hover_; }
        void setHover(const Hover& hover);

        // What is being scrubbed (null prop: nothing).
        void setScrub(bool active, ScrubPart part, const ScrubProp* prop, double value, double anchorX, double anchorY);
        bool scrubbing() const { return scrubActive_; }
        ScrubPart scrubPart() const { return scrubPart_; }
        const ScrubProp* scrubProp() const { return scrubProp_; }
        double scrubValue() const { return scrubValue_; }
        double scrubAnchorX() const { return anchorX_; }
        double scrubAnchorY() const { return anchorY_; }
        ScrubAxis scrubAxis() const { return axis_; }
        void setScrubAxis(ScrubAxis axis);

        // The optional docks (both off by default).
        bool toolbarVisible() const { return toolbarVisible_; }
        void setToolbarVisible(bool visible);
        bool propertiesVisible() const { return propertiesVisible_; }
        void setPropertiesVisible(bool visible);

        void setRing(const std::string& title);   // "" when no ring is open
        void setSpaceDown(bool down);
        void setSelectionCount(std::size_t count);
        void setPointer(bool known, double x, double y, double zoomPercent);
        void say(const std::string& text);

        // A hint shows until the user has done its thing a few times.
        bool fresh(const std::string& feature) const;
        void used(const std::string& feature);
        void setHintsFade(bool value) { hintsFade_ = value; touch(); }

        std::string hintTool() const;
        std::string hintKeys() const;
        std::string hintInfo() const;

    private:
        void touch();
        std::string idleHint() const;

        bool documentOpen_ = false;
        std::string documentName_;
        std::string tool_ = "Select";
        std::string heldKey_;
        Hover hover_;
        bool scrubActive_ = false;
        ScrubPart scrubPart_ = ScrubPart::None;
        const ScrubProp* scrubProp_ = nullptr;
        double scrubValue_ = 0.0, anchorX_ = 0.0, anchorY_ = 0.0;
        ScrubAxis axis_ = ScrubAxis::Vertical;
        bool toolbarVisible_ = false, propertiesVisible_ = false;
        std::string ring_;
        bool spaceDown_ = false;
        std::size_t selectionCount_ = 0;
        bool pointerKnown_ = false;
        double pointerX_ = 0.0, pointerY_ = 0.0, zoomPercent_ = 100.0;
        std::string message_ = "Tap Space to start";
        std::map<std::string, int> uses_;
        bool hintsFade_ = true;
    };

}
