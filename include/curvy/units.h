#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace curvy {

    // The order is the order of the New Document dialog's unit list (a saved index never leaves the process; files store the short name).
    enum class LengthUnit { Points, Pixels, Millimeters, Centimeters, Inches, Picas, Meters, Feet, DisplayPoints };

    enum class AngleUnit { Degrees, Radians };

    // What a number means to a person: a plain number, a length (stored in points) or an angle (stored in degrees). Set on a parameter by the
    // tag "quantity:length" or "quantity:angle" (the tag "unit:" is the scrub step, a different thing).
    enum class Quantity { None, Length, Angle };

    // The units a document is worked in: what rulers, dimension labels, parameter rows and typed bare numbers use. Saved with the document.
    // Geometry is always stored in points (1/72 in) and degrees; this only says how numbers are shown and read.
    // @reflect ignore=true
    struct DocumentUnits {
        LengthUnit length = LengthUnit::Pixels;
        AngleUnit angle = AngleUnit::Degrees;
        bool operator==(const DocumentUnits& other) const { return length == other.length && angle == other.angle; }
        bool operator!=(const DocumentUnits& other) const { return !(*this == other); }
    };

    // Conversions, names, parsing and formatting of lengths and angles. Static and stateless so scripts and tests reach it the same way.
    // DisplayPoints is newui's DisplayUnit::Dip (1/96 in); it is a fixed ratio here, the screen's DPI scale is a view concern.
    // Pixels are 1/ppi inch.
    // @reflect ignore=true
    class Units {
    public:
        static constexpr int kLengthCount = 9;

        // Points in one unit (pixels need the document's ppi; a ppi <= 0 counts as 72).
        static double pointsPer(LengthUnit unit, double ppi);
        static double toPoints(double value, LengthUnit unit, double ppi);
        static double fromPoints(double points, LengthUnit unit, double ppi);

        static double toDegrees(double value, AngleUnit unit);
        static double fromDegrees(double degrees, AngleUnit unit);

        // "px", "pt", "mm", "cm", "in", "pc", "m", "ft", "dip"; "deg", "rad". The name is what files and scripts use.
        static const char* name(LengthUnit unit);
        static const char* name(AngleUnit unit);
        // The unit for a name, case-insensitive; a few spellings are accepted ("inch", "inches", "millimeters", "degrees", "radians").
        static std::optional<LengthUnit> lengthFromName(std::string_view text);
        static std::optional<AngleUnit> angleFromName(std::string_view text);

        // Shortest text for a number with at most `decimals` decimals (8.5, 11, 215.9); no exponent, no "-0".
        static std::string number(double value, int decimals = 3);
        // "12.5 mm": the value (already in `unit`) and the unit's name.
        static std::string format(double value, LengthUnit unit, int decimals = 3);
        static std::string format(double value, AngleUnit unit, int decimals = 3);

        // A number with an optional unit suffix ("12.5mm", "3 in", "1ft", "20 dip"); a bare number is in `fallback`. On success `points` gets the length
        // in points. Nothing else may follow (no silent partial parse); an empty or non-numeric text is false and leaves `points` alone.
        static bool parseLength(std::string_view text, LengthUnit fallback, double ppi, double& points);
        static bool parseAngle(std::string_view text, AngleUnit fallback, double& degrees);

        // "length" or "angle" (the value of a "quantity:" tag); anything else is None.
        static Quantity quantityFromName(std::string_view text);
        // A stored number as the document's units show it, and typed text (a bare number is in the document's unit) back to the stored number.
        // None passes the number through (and parseQuantity reads a plain number).
        static double toDisplay(double stored, Quantity quantity, const DocumentUnits& units, double ppi);
        static bool parseQuantity(std::string_view text, Quantity quantity, const DocumentUnits& units, double ppi, double& stored);
        // How many stored units one displayed unit is (1 for None); scales a scrub step.
        static double displayScale(Quantity quantity, const DocumentUnits& units, double ppi);
    };

}
