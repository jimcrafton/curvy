#include "curvy/colorpickercard.h"

#include "curvy/graphpanel.h"
#include "curvy/theme.h"

#include <newui/mouse_constants.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kWidth = 230.0f, kMargin = 8.0f, kRow = 22.0f, kWheel = 190.0f;

        struct ColorMath {
            static double grayOf(double r, double g, double b) { return 0.299 * r + 0.587 * g + 0.114 * b; }
            static void toCmyk(double r, double g, double b, double out[4]) {
                const double k = 1.0 - std::max({r, g, b});
                const double rest = 1.0 - k;
                out[0] = rest > 1e-9 ? (1.0 - r - k) / rest : 0.0;
                out[1] = rest > 1e-9 ? (1.0 - g - k) / rest : 0.0;
                out[2] = rest > 1e-9 ? (1.0 - b - k) / rest : 0.0;
                out[3] = k;
            }
            static void fromCmyk(const double in[4], double& r, double& g, double& b) {
                r = (1.0 - in[0]) * (1.0 - in[3]);
                g = (1.0 - in[1]) * (1.0 - in[3]);
                b = (1.0 - in[2]) * (1.0 - in[3]);
            }
        };
    }

    ColorPickerCard::ColorPickerCard() {
        setName("graphColorPicker");
        setVisible(true);
        style().setBackgroundColor(BLRgba32(Theme::argb(Theme::current().bgSurface, 245)));
        style().setBorderFill(Color(0.29f, 0.29f, 0.29f));
        style().setBorderWidth(1.0f);

        wheel_ = new ColorWheel();
        addChild(wheel_);
        wheel_->onDragStarted += [this](ColorWheel&) {
            gesture_ = true;
            onDragStarted(*this);
            return SyncReturn::Handled;
        };
        wheel_->onChanged += [this](ColorWheel&) {
            hue_ = wheel_->hue();
            ColorWheel::hsbToRgb(wheel_->hue(), wheel_->saturation(), wheel_->brightness(), r_, g_, b_);
            updating_ = true;
            slidersFromColor();
            refreshHex();
            updating_ = false;
            onChanged(*this);
            return SyncReturn::Handled;
        };
        wheel_->onDragFinished += [this](ColorWheel&) {
            gestureEnded();
            return SyncReturn::Handled;
        };

        modes_ = new SegmentedControl();
        modes_->setSegments({"RGB", "HSB", "CMYK", "Gray"});
        addChild(modes_);
        modes_->onSelectionChanged += [this](SegmentedControl&) {
            mode_ = static_cast<Mode>(modes_->selectedIndex());
            refreshAll();
            return SyncReturn::Handled;
        };

        for (Row& row : rows_) {
            row = makeRow();
        }
        alphaRow_ = makeRow();

        hex_ = new TextField();
        hex_->setTextColor(BLRgba32(Theme::argb(Theme::current().textPrimary)));
        hex_->caret().setColor(Theme::current().textColor());
        addChild(hex_);
        hex_->onReturnPressed += [this](TextField&) {
            const std::wstring text = hex_->text();
            setHex(std::string(text.begin(), text.end()));
            return SyncReturn::Handled;
        };

        strip_ = new SwatchStrip();
        strip_->setVisible(false);
        addChild(strip_);
        strip_->onHeightChanged = [this]() { layoutCard(); };

        refreshAll();
    }

    void ColorPickerCard::setSwatchesVisible(bool visible) {
        if (swatchesVisible_ != visible) {
            swatchesVisible_ = visible;
            refreshAll();
        }
    }

    void ColorPickerCard::applyColor(double red, double green, double blue, double alpha) {
        beginGesture();
        r_ = std::clamp(red, 0.0, 1.0);
        g_ = std::clamp(green, 0.0, 1.0);
        b_ = std::clamp(blue, 0.0, 1.0);
        if (hasAlpha_) {
            a_ = std::clamp(alpha, 0.0, 1.0);
        }
        refreshAll();
        onChanged(*this);
        gestureEnded();
    }

    void ColorPickerCard::setHexVisible(bool visible) {
        if (hexVisible_ != visible) {
            hexVisible_ = visible;
            refreshAll();
        }
    }

    ColorPickerCard::Row ColorPickerCard::makeRow() {
        Row row;
        row.caption = new Label();
        row.caption->setTextAlignment(TextAlignment::Left);
        row.caption->setTextColor(BLRgba32(Theme::argb(Theme::current().textMuted)));
        addChild(row.caption);
        row.slider = new Slider();
        row.slider->setHorizontal(true);
        addChild(row.slider);
        row.value = new Label();
        row.value->setTextAlignment(TextAlignment::Right);
        row.value->setTextColor(BLRgba32(Theme::argb(Theme::current().textPrimary)));
        addChild(row.value);

        Slider* slider = row.slider;
        slider->onValueChanged += [this](Slider&) {
            if (!updating_) {
                sliderMoved();
            }
            return SyncReturn::Handled;
        };
        const auto released = [this](View&, const Point&, std::uint32_t, std::uint32_t) {
            gestureEnded();
            return SyncReturn(SyncReturn::Ignored);
        };
        slider->onMouseUp += released;
        if (slider->thumb() != nullptr) {
            slider->thumb()->onMouseUp += released;
        }
        return row;
    }

    std::size_t ColorPickerCard::sliderCount() const {
        return mode_ == Mode::Cmyk ? 4u : mode_ == Mode::Gray ? 1u : 3u;
    }

    void ColorPickerCard::setMode(Mode mode) {
        modes_->setSelectedIndex(static_cast<std::size_t>(mode));
        mode_ = mode;
        refreshAll();
    }

    void ColorPickerCard::setColor(double red, double green, double blue, double alpha, bool hasAlpha) {
        r_ = std::clamp(red, 0.0, 1.0);
        g_ = std::clamp(green, 0.0, 1.0);
        b_ = std::clamp(blue, 0.0, 1.0);
        a_ = std::clamp(alpha, 0.0, 1.0);
        hasAlpha_ = hasAlpha;
        refreshAll();
    }

    void ColorPickerCard::showRow(const Row& row, bool shown, float y, const char* caption, float low, float high) {
        row.caption->setVisible(shown);
        row.slider->setVisible(shown);
        row.value->setVisible(shown);
        if (!shown) {
            return;
        }
        row.caption->setText(caption);
        row.slider->setRange(low, high);
        row.caption->setBounds(Rect(kMargin, y + 2.0f, 36.0f, 18.0f));
        row.slider->setBounds(Rect(kMargin + 40.0f, y, kWidth - 2.0f * kMargin - 40.0f - 44.0f, 20.0f));
        row.value->setBounds(Rect(kWidth - kMargin - 40.0f, y + 2.0f, 40.0f, 18.0f));
    }

    void ColorPickerCard::layoutCard() {
        static const char* const captions[4][4] = {{"R", "G", "B", ""}, {"H", "S", "B", ""}, {"C", "M", "Y", "K"}, {"Gray", "", "", ""}};
        const std::size_t mode = static_cast<std::size_t>(mode_);
        wheel_->setBounds(Rect((kWidth - kWheel) * 0.5f, kMargin, kWheel, kWheel));
        float y = kMargin + kWheel + 6.0f;
        modes_->setBounds(Rect(kMargin, y, kWidth - 2.0f * kMargin, kRow));
        y += kRow + 8.0f;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const bool shown = i < sliderCount();
            float high = 100.0f;
            if (mode_ == Mode::Rgb) {
                high = 255.0f;
            } else if (mode_ == Mode::Hsb && i == 0) {
                high = 360.0f;
            }
            showRow(rows_[i], shown, y, captions[mode][i], 0.0f, high);
            if (shown) {
                y += kRow;
            }
        }
        showRow(alphaRow_, hasAlpha_, y, "A", 0.0f, 100.0f);
        if (hasAlpha_) {
            y += kRow;
        }
        hex_->setVisible(hexVisible_);
        if (hexVisible_) {
            y += 4.0f;
            hex_->setBounds(Rect(kMargin, y, kWidth - 2.0f * kMargin, 22.0f));
            y += 22.0f;
        }
        strip_->setVisible(swatchesVisible_);
        if (swatchesVisible_) {
            y += 10.0f;
            strip_->setBounds(Rect(kMargin, y, kWidth - 2.0f * kMargin, 1.0f));
            y += strip_->layoutFor(kWidth - 2.0f * kMargin);
        }
        y += kMargin;
        setBounds(Rect(bounds().left(), bounds().top(), kWidth, y));
    }

    void ColorPickerCard::slidersFromColor() {
        double values[4] = {0.0, 0.0, 0.0, 0.0};
        if (mode_ == Mode::Rgb) {
            values[0] = r_ * 255.0;
            values[1] = g_ * 255.0;
            values[2] = b_ * 255.0;
        } else if (mode_ == Mode::Hsb) {
            double hue = 0.0, saturation = 0.0, brightness = 0.0;
            bool defined = false;
            ColorWheel::rgbToHsb(r_, g_, b_, hue, saturation, brightness, defined);
            values[0] = hue_ * 360.0;
            values[1] = saturation * 100.0;
            values[2] = brightness * 100.0;
        } else if (mode_ == Mode::Cmyk) {
            ColorMath::toCmyk(r_, g_, b_, values);
            for (double& v : values) {
                v *= 100.0;
            }
        } else {
            values[0] = ColorMath::grayOf(r_, g_, b_) * 100.0;
        }
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            rows_[i].slider->setValue(float(values[i]));
            char text[16];
            std::snprintf(text, sizeof(text), "%d", int(std::lround(values[i])));
            rows_[i].value->setText(text);
        }
        alphaRow_.slider->setValue(float(a_ * 100.0));
        char text[16];
        std::snprintf(text, sizeof(text), "%d", int(std::lround(a_ * 100.0)));
        alphaRow_.value->setText(text);
    }

    void ColorPickerCard::colorFromSliders() {
        const auto value = [this](std::size_t i) { return double(rows_[i].slider->value()); };
        if (mode_ == Mode::Rgb) {
            r_ = value(0) / 255.0;
            g_ = value(1) / 255.0;
            b_ = value(2) / 255.0;
        } else if (mode_ == Mode::Hsb) {
            hue_ = value(0) / 360.0;
            ColorWheel::hsbToRgb(hue_, value(1) / 100.0, value(2) / 100.0, r_, g_, b_);
        } else if (mode_ == Mode::Cmyk) {
            const double cmyk[4] = {value(0) / 100.0, value(1) / 100.0, value(2) / 100.0, value(3) / 100.0};
            ColorMath::fromCmyk(cmyk, r_, g_, b_);
        } else {
            r_ = g_ = b_ = value(0) / 100.0;
        }
        if (hasAlpha_) {
            a_ = double(alphaRow_.slider->value()) / 100.0;
        }
    }

    std::string ColorPickerCard::hexText() const {
        return hasAlpha_ ? GraphPanel::colorText(float(r_), float(g_), float(b_), float(a_)) : GraphPanel::colorText(float(r_), float(g_), float(b_));
    }

    void ColorPickerCard::refreshHex() {
        const std::string text = hexText();
        hex_->setText(std::wstring(text.begin(), text.end()));
    }

    void ColorPickerCard::refreshAll() {
        const bool was = updating_;
        updating_ = true;
        double hue = 0.0, saturation = 0.0, brightness = 0.0;
        bool defined = false;
        ColorWheel::rgbToHsb(r_, g_, b_, hue, saturation, brightness, defined);
        if (defined) {
            hue_ = hue;
        }
        wheel_->setHsb(hue_, saturation, brightness, true);
        layoutCard();
        slidersFromColor();
        refreshHex();
        updating_ = was;
        style().markDirty();
    }

    void ColorPickerCard::beginGesture() {
        if (!gesture_) {
            gesture_ = true;
            onDragStarted(*this);
        }
    }

    void ColorPickerCard::sliderMoved() {
        beginGesture();
        colorFromSliders();
        updating_ = true;
        double hue = 0.0, saturation = 0.0, brightness = 0.0;
        bool defined = false;
        ColorWheel::rgbToHsb(r_, g_, b_, hue, saturation, brightness, defined);
        if (defined && mode_ != Mode::Hsb) {
            hue_ = hue;
        }
        wheel_->setHsb(hue_, saturation, brightness, true);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d", int(std::lround(double(rows_[i].slider->value()))));
            rows_[i].value->setText(text);
        }
        char text[16];
        std::snprintf(text, sizeof(text), "%d", int(std::lround(double(alphaRow_.slider->value()))));
        alphaRow_.value->setText(text);
        refreshHex();
        updating_ = false;
        onChanged(*this);
    }

    void ColorPickerCard::gestureEnded() {
        if (gesture_) {
            gesture_ = false;
            onDragFinished(*this);
        }
    }

    bool ColorPickerCard::setHex(const std::string& text) {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        bool withAlpha = false;
        if (!GraphPanel::parseColor(text, r, g, b, a, withAlpha)) {
            refreshHex();
            return false;
        }
        applyColor(r, g, b, withAlpha ? a : a_);
        return true;
    }

}
