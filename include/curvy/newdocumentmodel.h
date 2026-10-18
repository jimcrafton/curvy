#pragma once

#include "curvy/sketchcolor.h"

#include <string>
#include <vector>

namespace curvy {

    enum class LengthUnit { Points, Pixels, Millimeters, Centimeters, Inches, Picas };  // the order of the dialog's unit list

    enum class PageBackground { White, Transparent, CurrentFill };  // the order of the dialog's background list

    // One starting shape in the New Document dialog's list.
    // @reflect ignore=true
    struct DocumentPreset {
        std::string category;  // Print / Web / Mobile / Art
        std::string name;
        double width;
        double height;
        LengthUnit unit;
        double ppi;
        ColorSpace colorMode;  // RGB or CMYK
    };

    // The logic behind the New Document dialog, free of any UI (the dialog just mirrors it into controls):
    // the size in the unit being shown, unit conversion that keeps the physical size, orientation, "keep
    // proportions", resolution, color mode and background - and what a new document should be created with.
    // Mirrors the JavaScript in mockup/new-document-dialog.html.
    // @reflect ignore=true
    class NewDocumentModel {
    public:
        NewDocumentModel();

        // The starting shapes (same list as PRESETS in tools/gen_newui.py).
        static const std::vector<DocumentPreset>& presets();
        static std::vector<std::string> categories();
        // Indices into presets() of one category, in order.
        static std::vector<std::size_t> presetsIn(const std::string& category);
        // "Letter   8.5 x 11 in   CMYK, 300 ppi" - the list's row text.
        static std::string rowText(const DocumentPreset& preset);
        static const char* unitName(LengthUnit unit);  // "in", "px", ... for labels
        static double pointsPer(LengthUnit unit, double ppi);

        // Starts from an existing document's page (Document Setup): shown in points, name and proportions untouched.
        void loadSetup(double widthPoints, double heightPoints, double ppi, ColorSpace colorMode, PageBackground background);

        // Takes all of a preset's settings (name resets to "Untitled-1", the link ratio is recaptured).
        void loadPreset(const DocumentPreset& preset);

        const std::string& name() const { return name_; }
        void setName(const std::string& name) { name_ = name; }

        LengthUnit unit() const { return unit_; }
        // Changing the unit converts width and height in place so the page stays the same physical size.
        void setUnit(LengthUnit unit);

        double width() const { return width_; }
        double height() const { return height_; }
        // With keepProportions() the other side follows at the captured ratio. Values <= 0 are ignored.
        void setWidth(double width);
        void setHeight(double height);

        bool keepProportions() const { return linked_; }
        void setKeepProportions(bool linked);  // switching on captures the current ratio

        bool isLandscape() const { return width_ > height_; }
        // Swaps width and height when they do not already match; the proportions ratio is recaptured.
        void setLandscape(bool landscape);

        double ppi() const { return ppi_; }
        void setPpi(double ppi);  // values <= 0 are ignored

        ColorSpace colorMode() const { return colorMode_; }
        void setColorMode(ColorSpace mode) { colorMode_ = mode == ColorSpace::CMYK ? ColorSpace::CMYK : ColorSpace::RGB; }

        PageBackground background() const { return background_; }
        void setBackground(PageBackground background) { background_ = background; }

        // The page in points (1/72 in) - what SketchDocument stores.
        double widthPoints() const;
        double heightPoints() const;
        long pixelWidth() const;
        long pixelHeight() const;

        // "8.5 in" / "11 in" for the preview's edge labels.
        std::string widthLabel() const;
        std::string heightLabel() const;
        // Two lines under the form: the size in pt/in/mm, then pixels at the ppi, mode and orientation.
        std::string summary() const;

        // Shortest decimal text for a number (at most 2 decimals): 8.5, 11, 215.9.
        static std::string format(double value);

    private:
        double toPoints(double value, LengthUnit unit) const;
        double fromPoints(double points, LengthUnit unit) const;
        void captureRatio();

        std::string name_ = "Untitled-1";
        LengthUnit unit_ = LengthUnit::Inches;
        double width_ = 8.5;
        double height_ = 11.0;
        double ppi_ = 300.0;
        ColorSpace colorMode_ = ColorSpace::CMYK;
        PageBackground background_ = PageBackground::White;
        bool linked_ = false;
        double ratio_ = 8.5 / 11.0;
    };

}
