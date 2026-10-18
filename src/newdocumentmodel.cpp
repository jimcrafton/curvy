#include "curvy/newdocumentmodel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace curvy {

    namespace {
        using Unit = LengthUnit;
        using Mode = ColorSpace;

        const std::vector<DocumentPreset> kPresets = {
            {"Print", "Letter", 8.5, 11, Unit::Inches, 300, Mode::CMYK}, {"Print", "Legal", 8.5, 14, Unit::Inches, 300, Mode::CMYK},
            {"Print", "Tabloid", 11, 17, Unit::Inches, 300, Mode::CMYK}, {"Print", "A3", 297, 420, Unit::Millimeters, 300, Mode::CMYK},
            {"Print", "A4", 210, 297, Unit::Millimeters, 300, Mode::CMYK}, {"Print", "A5", 148, 210, Unit::Millimeters, 300, Mode::CMYK},
            {"Print", "B5", 176, 250, Unit::Millimeters, 300, Mode::CMYK}, {"Print", "Business card", 3.5, 2, Unit::Inches, 300, Mode::CMYK},
            {"Print", "Poster 18 x 24", 18, 24, Unit::Inches, 150, Mode::CMYK}, {"Print", "Postcard", 6, 4, Unit::Inches, 300, Mode::CMYK},
            {"Web", "Full HD", 1920, 1080, Unit::Pixels, 72, Mode::RGB}, {"Web", "HD", 1280, 720, Unit::Pixels, 72, Mode::RGB},
            {"Web", "Desktop 1440", 1440, 900, Unit::Pixels, 72, Mode::RGB}, {"Web", "Tablet landscape", 1024, 768, Unit::Pixels, 72, Mode::RGB},
            {"Web", "Social square", 1080, 1080, Unit::Pixels, 72, Mode::RGB}, {"Web", "Banner", 728, 90, Unit::Pixels, 72, Mode::RGB},
            {"Mobile", "Phone (1179 x 2556)", 1179, 2556, Unit::Pixels, 72, Mode::RGB}, {"Mobile", "Phone (1080 x 2400)", 1080, 2400, Unit::Pixels, 72, Mode::RGB},
            {"Mobile", "Tablet (2048 x 2732)", 2048, 2732, Unit::Pixels, 72, Mode::RGB}, {"Mobile", "App icon", 1024, 1024, Unit::Pixels, 72, Mode::RGB},
            {"Art", "Square 1000", 1000, 1000, Unit::Pixels, 72, Mode::RGB}, {"Art", "Square 12 in", 12, 12, Unit::Inches, 300, Mode::RGB},
            {"Art", "Illustration 8 x 10", 8, 10, Unit::Inches, 300, Mode::RGB}, {"Art", "Custom", 800, 600, Unit::Pixels, 72, Mode::RGB},
        };
    }

    NewDocumentModel::NewDocumentModel() {
        loadPreset(kPresets.front());
    }

    const std::vector<DocumentPreset>& NewDocumentModel::presets() {
        return kPresets;
    }

    std::vector<std::string> NewDocumentModel::categories() {
        std::vector<std::string> result;
        for (const DocumentPreset& preset : kPresets) {
            if (std::find(result.begin(), result.end(), preset.category) == result.end()) {
                result.push_back(preset.category);
            }
        }
        return result;
    }

    std::vector<std::size_t> NewDocumentModel::presetsIn(const std::string& category) {
        std::vector<std::size_t> result;
        for (std::size_t i = 0; i < kPresets.size(); ++i) {
            if (kPresets[i].category == category) {
                result.push_back(i);
            }
        }
        return result;
    }

    std::string NewDocumentModel::format(double value) {
        double rounded = std::round(value * 100.0) / 100.0;
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%g", rounded);
        return buffer;
    }

    const char* NewDocumentModel::unitName(LengthUnit unit) {
        switch (unit) {
            case LengthUnit::Points: return "pt";
            case LengthUnit::Pixels: return "px";
            case LengthUnit::Millimeters: return "mm";
            case LengthUnit::Centimeters: return "cm";
            case LengthUnit::Picas: return "pc";
            default: return "in";
        }
    }

    std::string NewDocumentModel::rowText(const DocumentPreset& preset) {
        return preset.name + "   " + format(preset.width) + " x " + format(preset.height) + " " + unitName(preset.unit) + "   " +
            (preset.colorMode == ColorSpace::CMYK ? "CMYK" : "RGB") + ", " + format(preset.ppi) + " ppi";
    }

    double NewDocumentModel::pointsPer(LengthUnit unit, double ppi) {
        switch (unit) {
            case LengthUnit::Points: return 1.0;
            case LengthUnit::Pixels: return 72.0 / (ppi > 0.0 ? ppi : 72.0);
            case LengthUnit::Millimeters: return 72.0 / 25.4;
            case LengthUnit::Centimeters: return 72.0 / 2.54;
            case LengthUnit::Picas: return 12.0;
            default: return 72.0;
        }
    }

    double NewDocumentModel::toPoints(double value, LengthUnit unit) const {
        return value * pointsPer(unit, ppi_);
    }

    double NewDocumentModel::fromPoints(double points, LengthUnit unit) const {
        return points / pointsPer(unit, ppi_);
    }

    void NewDocumentModel::captureRatio() {
        ratio_ = height_ > 0.0 ? width_ / height_ : 1.0;
    }

    void NewDocumentModel::loadSetup(double widthPoints, double heightPoints, double ppi, ColorSpace colorMode, PageBackground background) {
        unit_ = LengthUnit::Points;
        width_ = widthPoints;
        height_ = heightPoints;
        ppi_ = ppi > 0.0 ? ppi : 72.0;
        setColorMode(colorMode);
        background_ = background;
        linked_ = false;
        captureRatio();
    }

    void NewDocumentModel::loadPreset(const DocumentPreset& preset) {
        name_ = "Untitled-1";
        unit_ = preset.unit;
        width_ = preset.width;
        height_ = preset.height;
        ppi_ = preset.ppi;
        colorMode_ = preset.colorMode;
        captureRatio();
    }

    void NewDocumentModel::setUnit(LengthUnit unit) {
        if (unit == unit_) {
            return;
        }
        width_ = fromPoints(toPoints(width_, unit_), unit);
        height_ = fromPoints(toPoints(height_, unit_), unit);
        unit_ = unit;
    }

    void NewDocumentModel::setWidth(double width) {
        if (width <= 0.0) {
            return;
        }
        width_ = width;
        if (linked_ && ratio_ > 0.0) {
            height_ = width_ / ratio_;
        }
    }

    void NewDocumentModel::setHeight(double height) {
        if (height <= 0.0) {
            return;
        }
        height_ = height;
        if (linked_ && ratio_ > 0.0) {
            width_ = height_ * ratio_;
        }
    }

    void NewDocumentModel::setKeepProportions(bool linked) {
        linked_ = linked;
        if (linked_) {
            captureRatio();
        }
    }

    void NewDocumentModel::setLandscape(bool landscape) {
        if ((landscape && width_ < height_) || (!landscape && width_ > height_)) {
            std::swap(width_, height_);
        }
        captureRatio();
    }

    void NewDocumentModel::setPpi(double ppi) {
        if (ppi > 0.0) {
            ppi_ = ppi;
        }
    }

    double NewDocumentModel::widthPoints() const {
        return toPoints(width_, unit_);
    }

    double NewDocumentModel::heightPoints() const {
        return toPoints(height_, unit_);
    }

    long NewDocumentModel::pixelWidth() const {
        return std::lround(widthPoints() * ppi_ / 72.0);
    }

    long NewDocumentModel::pixelHeight() const {
        return std::lround(heightPoints() * ppi_ / 72.0);
    }

    std::string NewDocumentModel::widthLabel() const {
        return format(width_) + " " + unitName(unit_);
    }

    std::string NewDocumentModel::heightLabel() const {
        return format(height_) + " " + unitName(unit_);
    }

    std::string NewDocumentModel::summary() const {
        double wpt = widthPoints(), hpt = heightPoints();
        std::string text = format(wpt) + " x " + format(hpt) + " pt  =  " +
            format(wpt / 72.0) + " x " + format(hpt / 72.0) + " in  =  " +
            format(wpt * 25.4 / 72.0) + " x " + format(hpt * 25.4 / 72.0) + " mm\n" +
            std::to_string(pixelWidth()) + " x " + std::to_string(pixelHeight()) + " px at " + format(ppi_) + " ppi  -  " +
            (colorMode_ == ColorSpace::CMYK ? "CMYK" : "RGB") + "  -  " + (isLandscape() ? "Landscape" : "Portrait");
        return text;
    }

}
