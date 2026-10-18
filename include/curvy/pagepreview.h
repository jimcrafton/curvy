#pragma once

#include <newui/subview.h>

#include <string>

namespace curvy {

    // The New Document dialog's preview: a to-scale page on a gray pasteboard, with its width and
    // height written beside it ("New Document dialog" in demos/sketch/mockup/new-document-dialog.html).
    //
    // Only the page's proportions matter here - pageWidth()/pageHeight() are in whatever unit the
    // dialog is showing - so the dialog sets them (and the two labels) whenever the size, unit or
    // orientation changes. Not interactive.
    // @reflect category=basic
    class PagePreview : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        PagePreview();
        virtual ~PagePreview() {}

        // Both must be positive (they only set the page's aspect ratio).
        float pageWidth() const { return pageWidth_; }
        void setPageWidth(float width);
        float pageHeight() const { return pageHeight_; }
        void setPageHeight(float height);

        // Text drawn along the bottom edge / right edge of the page, e.g. "8.5 in" and "11 in".
        const std::string& widthLabel() const { return widthLabel_; }
        void setWidthLabel(const std::string& text);
        const std::string& heightLabel() const { return heightLabel_; }
        void setHeightLabel(const std::string& text);

        void paint(BLContext& ctx) override;

    private:
        static constexpr float kMargin = 34.0f;   // pasteboard left free around the page for the labels

        void paintLabel(BLContext& ctx, const std::string& text, double centerX, double centerY) const;

        float pageWidth_ = 8.5f;
        float pageHeight_ = 11.0f;
        std::string widthLabel_;
        std::string heightLabel_;
    };

}
