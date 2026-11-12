#pragma once

#include <newui/models.h>

#include <string>

namespace curvy {

    // The state of the workspace shell: the open ring, the held Space key and the text of the hint bar. The controller changes it;
    // the hint bar follows it.
    // @reflect ignore=true
    class WorkspaceModel : public newui::Model {
    public:
        bool documentOpen() const { return documentOpen_; }
        const std::string& documentName() const { return documentName_; }
        void setDocument(bool open, const std::string& name);

        void setRing(const std::string& title);   // "" when no ring is open
        void setSpaceDown(bool down);
        void say(const std::string& text);
        void setZoom(double percent);   // shown at the right of the hint bar while a document is open

        std::string hintTool() const;
        std::string hintKeys() const;
        std::string hintInfo() const;

    private:
        void touch();

        bool documentOpen_ = false;
        std::string documentName_;
        std::string ring_;
        bool spaceDown_ = false;
        double zoomPercent_ = 100.0;
        std::string message_ = "Tap Space to start";
    };

}
