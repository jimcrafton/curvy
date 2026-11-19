#include "curvy/units.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace curvy {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        std::string lower(std::string_view text) {
            std::string result(text);
            for (char& c : result) {
                c = char(std::tolower(static_cast<unsigned char>(c)));
            }
            return result;
        }

        std::string trimmed(std::string_view text) {
            std::size_t first = 0;
            std::size_t last = text.size();
            while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) {
                ++first;
            }
            while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
                --last;
            }
            return std::string(text.substr(first, last - first));
        }

        // Splits "12.5 mm" into the number and what follows it; false when there is no number.
        bool splitNumber(std::string_view text, double& value, std::string& suffix) {
            const std::string body = trimmed(text);
            if (body.empty()) {
                return false;
            }
            const char* begin = body.c_str();
            char* end = nullptr;
            value = std::strtod(begin, &end);
            if (end == begin || !std::isfinite(value)) {
                return false;
            }
            suffix = lower(trimmed(std::string_view(end)));
            return true;
        }
    }

    double Units::pointsPer(LengthUnit unit, double ppi) {
        switch (unit) {
            case LengthUnit::Points: return 1.0;
            case LengthUnit::Pixels: return 72.0 / (ppi > 0.0 ? ppi : 72.0);
            case LengthUnit::Millimeters: return 72.0 / 25.4;
            case LengthUnit::Centimeters: return 72.0 / 2.54;
            case LengthUnit::Inches: return 72.0;
            case LengthUnit::Picas: return 12.0;
            case LengthUnit::Meters: return 72.0 / 0.0254;
            case LengthUnit::Feet: return 72.0 * 12.0;
            case LengthUnit::DisplayPoints: return 72.0 / 96.0;
        }
        return 1.0;
    }

    double Units::toPoints(double value, LengthUnit unit, double ppi) {
        return value * pointsPer(unit, ppi);
    }

    double Units::fromPoints(double points, LengthUnit unit, double ppi) {
        return points / pointsPer(unit, ppi);
    }

    double Units::toDegrees(double value, AngleUnit unit) {
        return unit == AngleUnit::Radians ? value * 180.0 / kPi : value;
    }

    double Units::fromDegrees(double degrees, AngleUnit unit) {
        return unit == AngleUnit::Radians ? degrees * kPi / 180.0 : degrees;
    }

    const char* Units::name(LengthUnit unit) {
        switch (unit) {
            case LengthUnit::Points: return "pt";
            case LengthUnit::Pixels: return "px";
            case LengthUnit::Millimeters: return "mm";
            case LengthUnit::Centimeters: return "cm";
            case LengthUnit::Inches: return "in";
            case LengthUnit::Picas: return "pc";
            case LengthUnit::Meters: return "m";
            case LengthUnit::Feet: return "ft";
            case LengthUnit::DisplayPoints: return "dip";
        }
        return "px";
    }

    const char* Units::name(AngleUnit unit) {
        return unit == AngleUnit::Radians ? "rad" : "deg";
    }

    std::optional<LengthUnit> Units::lengthFromName(std::string_view text) {
        const std::string key = lower(trimmed(text));
        struct Entry {
            const char* name;
            LengthUnit unit;
        };
        static const Entry table[] = {
            {"pt", LengthUnit::Points},          {"point", LengthUnit::Points},          {"points", LengthUnit::Points},
            {"px", LengthUnit::Pixels},          {"pixel", LengthUnit::Pixels},          {"pixels", LengthUnit::Pixels},
            {"mm", LengthUnit::Millimeters},     {"millimeter", LengthUnit::Millimeters}, {"millimeters", LengthUnit::Millimeters},
            {"cm", LengthUnit::Centimeters},     {"centimeter", LengthUnit::Centimeters}, {"centimeters", LengthUnit::Centimeters},
            {"in", LengthUnit::Inches},          {"inch", LengthUnit::Inches},           {"inches", LengthUnit::Inches},
            {"pc", LengthUnit::Picas},           {"pica", LengthUnit::Picas},            {"picas", LengthUnit::Picas},
            {"m", LengthUnit::Meters},           {"meter", LengthUnit::Meters},          {"meters", LengthUnit::Meters},
            {"ft", LengthUnit::Feet},            {"foot", LengthUnit::Feet},             {"feet", LengthUnit::Feet},
            {"dip", LengthUnit::DisplayPoints},  {"display points", LengthUnit::DisplayPoints},
        };
        for (const Entry& entry : table) {
            if (key == entry.name) {
                return entry.unit;
            }
        }
        return std::nullopt;
    }

    std::optional<AngleUnit> Units::angleFromName(std::string_view text) {
        const std::string key = lower(trimmed(text));
        if (key == "deg" || key == "degree" || key == "degrees") {
            return AngleUnit::Degrees;
        }
        if (key == "rad" || key == "radian" || key == "radians") {
            return AngleUnit::Radians;
        }
        return std::nullopt;
    }

    std::string Units::number(double value, int decimals) {
        if (decimals < 0) {
            decimals = 0;
        }
        const double scale = std::pow(10.0, decimals);
        double rounded = std::round(value * scale) / scale;
        if (rounded == 0.0) {
            rounded = 0.0;   // drops the sign of -0
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, rounded);
        std::string text = buffer;
        if (text.find('.') != std::string::npos) {
            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
        }
        return text;
    }

    std::string Units::format(double value, LengthUnit unit, int decimals) {
        return number(value, decimals) + " " + name(unit);
    }

    std::string Units::format(double value, AngleUnit unit, int decimals) {
        return number(value, decimals) + (unit == AngleUnit::Radians ? " rad" : "\xC2\xB0");
    }

    bool Units::parseLength(std::string_view text, LengthUnit fallback, double ppi, double& points) {
        double value = 0.0;
        std::string suffix;
        if (!splitNumber(text, value, suffix)) {
            return false;
        }
        LengthUnit unit = fallback;
        if (!suffix.empty()) {
            const std::optional<LengthUnit> named = lengthFromName(suffix);
            if (!named) {
                return false;
            }
            unit = *named;
        }
        points = toPoints(value, unit, ppi);
        return true;
    }

    bool Units::parseAngle(std::string_view text, AngleUnit fallback, double& degrees) {
        double value = 0.0;
        std::string suffix;
        if (!splitNumber(text, value, suffix)) {
            return false;
        }
        AngleUnit unit = fallback;
        if (!suffix.empty()) {
            if (suffix == "\xC2\xB0") {
                unit = AngleUnit::Degrees;
            } else {
                const std::optional<AngleUnit> named = angleFromName(suffix);
                if (!named) {
                    return false;
                }
                unit = *named;
            }
        }
        degrees = toDegrees(value, unit);
        return true;
    }

    Quantity Units::quantityFromName(std::string_view text) {
        if (text == "length") {
            return Quantity::Length;
        }
        if (text == "angle") {
            return Quantity::Angle;
        }
        return Quantity::None;
    }

    double Units::toDisplay(double stored, Quantity quantity, const DocumentUnits& units, double ppi) {
        if (quantity == Quantity::Length) {
            return fromPoints(stored, units.length, ppi);
        }
        if (quantity == Quantity::Angle) {
            return fromDegrees(stored, units.angle);
        }
        return stored;
    }

    bool Units::parseQuantity(std::string_view text, Quantity quantity, const DocumentUnits& units, double ppi, double& stored) {
        if (quantity == Quantity::Length) {
            return parseLength(text, units.length, ppi, stored);
        }
        if (quantity == Quantity::Angle) {
            return parseAngle(text, units.angle, stored);
        }
        const std::string copy(text);
        char* end = nullptr;
        const double number = std::strtod(copy.c_str(), &end);
        if (end == copy.c_str()) {
            return false;
        }
        stored = number;
        return true;
    }

    double Units::displayScale(Quantity quantity, const DocumentUnits& units, double ppi) {
        return toDisplay(1.0, quantity, units, ppi) - toDisplay(0.0, quantity, units, ppi);
    }

}
