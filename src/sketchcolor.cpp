#include "curvy/sketchcolor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace curvy {

    namespace {
        float clamp01(float value) {
            return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        }

        std::uint32_t toByte(float value) {
            return std::uint32_t(std::lround(clamp01(value) * 255.0f));
        }
    }

    SketchColor SketchColor::rgb(float r, float g, float b, float alpha) {
        SketchColor color;
        color.space_ = ColorSpace::RGB;
        color.c_[0] = clamp01(r);
        color.c_[1] = clamp01(g);
        color.c_[2] = clamp01(b);
        color.alpha_ = clamp01(alpha);
        return color;
    }

    SketchColor SketchColor::cmyk(float c, float m, float y, float k, float alpha) {
        SketchColor color;
        color.space_ = ColorSpace::CMYK;
        color.c_[0] = clamp01(c);
        color.c_[1] = clamp01(m);
        color.c_[2] = clamp01(y);
        color.c_[3] = clamp01(k);
        color.alpha_ = clamp01(alpha);
        return color;
    }

    SketchColor SketchColor::gray(float g, float alpha) {
        SketchColor color;
        color.space_ = ColorSpace::Gray;
        color.c_[0] = clamp01(g);
        color.alpha_ = clamp01(alpha);
        return color;
    }

    SketchColor SketchColor::hsb(float h, float s, float b, float alpha) {
        SketchColor color;
        color.space_ = ColorSpace::HSB;
        color.c_[0] = h - std::floor(h);
        color.c_[1] = clamp01(s);
        color.c_[2] = clamp01(b);
        color.alpha_ = clamp01(alpha);
        return color;
    }

    SketchColor SketchColor::fromHex(const std::string& hex) {
        std::string digits = (!hex.empty() && hex[0] == '#') ? hex.substr(1) : hex;
        if (digits.size() != 6 && digits.size() != 8) {
            throw std::runtime_error("SketchColor::fromHex: expected #rrggbb or #rrggbbaa, got \"" + hex + "\"");
        }
        unsigned int value = 0;
        for (char ch : digits) {
            unsigned int nibble = 0;
            if (ch >= '0' && ch <= '9') {
                nibble = unsigned(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                nibble = unsigned(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                nibble = unsigned(ch - 'A' + 10);
            } else {
                throw std::runtime_error("SketchColor::fromHex: bad hex digit in \"" + hex + "\"");
            }
            value = (value << 4) | nibble;
        }
        float a = 1.0f;
        if (digits.size() == 8) {
            a = float(value & 0xFFu) / 255.0f;
            value >>= 8;
        }
        return rgb(float((value >> 16) & 0xFFu) / 255.0f, float((value >> 8) & 0xFFu) / 255.0f,
            float(value & 0xFFu) / 255.0f, a);
    }

    float SketchColor::component(std::size_t index) const {
        if (index >= 4) {
            throw std::runtime_error("SketchColor::component: index out of range");
        }
        return c_[index];
    }

    void SketchColor::setAlpha(float alpha) {
        alpha_ = clamp01(alpha);
    }

    void SketchColor::toRgb(float& r, float& g, float& b) const {
        r = g = b = 0.0f;
        switch (space_) {
        case ColorSpace::RGB:
            r = c_[0];
            g = c_[1];
            b = c_[2];
            break;
        case ColorSpace::CMYK:
            r = (1.0f - c_[0]) * (1.0f - c_[3]);
            g = (1.0f - c_[1]) * (1.0f - c_[3]);
            b = (1.0f - c_[2]) * (1.0f - c_[3]);
            break;
        case ColorSpace::Gray:
            r = g = b = c_[0];
            break;
        case ColorSpace::HSB: {
            float h = c_[0] * 6.0f;
            float s = c_[1];
            float v = c_[2];
            int sector = int(std::floor(h)) % 6;
            float f = h - std::floor(h);
            float p = v * (1.0f - s);
            float q = v * (1.0f - s * f);
            float t = v * (1.0f - s * (1.0f - f));
            switch (sector) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
            }
            break;
        }
        }
    }

    SketchColor SketchColor::convertedTo(ColorSpace space) const {
        if (space == space_) {
            return *this;
        }
        float r, g, b;
        toRgb(r, g, b);
        switch (space) {
        case ColorSpace::RGB:
            return rgb(r, g, b, alpha_);
        case ColorSpace::Gray:
            return gray(0.299f * r + 0.587f * g + 0.114f * b, alpha_);
        case ColorSpace::CMYK: {
            float k = 1.0f - std::max(r, std::max(g, b));
            if (k >= 1.0f) {
                return cmyk(0.0f, 0.0f, 0.0f, 1.0f, alpha_);
            }
            float inv = 1.0f - k;
            return cmyk((inv - r) / inv, (inv - g) / inv, (inv - b) / inv, k, alpha_);
        }
        case ColorSpace::HSB:
        default: {
            float hi = std::max(r, std::max(g, b));
            float lo = std::min(r, std::min(g, b));
            float delta = hi - lo;
            float h = 0.0f;
            if (delta > 1e-6f) {
                if (hi == r) {
                    h = std::fmod((g - b) / delta, 6.0f);
                } else if (hi == g) {
                    h = (b - r) / delta + 2.0f;
                } else {
                    h = (r - g) / delta + 4.0f;
                }
                h /= 6.0f;
                if (h < 0.0f) {
                    h += 1.0f;
                }
            }
            return hsb(h, hi > 1e-6f ? delta / hi : 0.0f, hi, alpha_);
        }
        }
    }

    BLRgba32 SketchColor::toRgba32() const {
        float r, g, b;
        toRgb(r, g, b);
        return BLRgba32(toByte(r), toByte(g), toByte(b), toByte(alpha_));
    }

    std::string SketchColor::toHex() const {
        BLRgba32 rgba = toRgba32();
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", rgba.r(), rgba.g(), rgba.b());
        return buffer;
    }

    bool SketchColor::operator==(const SketchColor& other) const {
        if (space_ != other.space_ || alpha_ != other.alpha_) {
            return false;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (c_[i] != other.c_[i]) {
                return false;
            }
        }
        return true;
    }

}
