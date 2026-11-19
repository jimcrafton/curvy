#include "curvy/rulers.h"

#include <cmath>

namespace curvy {

    double Rulers::majorStep(double pixelsPerUnit, double minPixels) {
        if (!(pixelsPerUnit > 0.0)) {
            return 1.0;
        }
        const double wanted = minPixels / pixelsPerUnit;
        const double power = std::pow(10.0, std::floor(std::log10(wanted)));
        for (const double mantissa : {1.0, 2.0, 5.0, 10.0}) {
            if (mantissa * power >= wanted * (1.0 - 1e-9)) {
                return mantissa * power;
            }
        }
        return 10.0 * power;
    }

    int Rulers::subdivisions(double majorStep) {
        const double power = std::pow(10.0, std::floor(std::log10(majorStep) + 1e-9));
        const double mantissa = majorStep / power;
        if (mantissa > 4.5) {
            return 5;
        }
        if (mantissa > 1.5) {
            return 4;
        }
        return 10;
    }

    std::vector<Rulers::Tick> Rulers::ticks(double fromPoints, double toPoints, double zoom, LengthUnit unit, double ppi, double minMinorPixels, double originPoints) {
        std::vector<Tick> result;
        const double perUnit = Units::pointsPer(unit, ppi);
        if (!(perUnit > 0.0) || !(zoom > 0.0) || toPoints < fromPoints) {
            return result;
        }
        const double major = majorStep(perUnit * zoom);
        const int parts = subdivisions(major);
        double minor = major / double(parts);
        if (minor * perUnit * zoom < minMinorPixels) {
            minor = major;   // too dense: only the major ticks
        }
        const int stride = minor == major ? 1 : parts;
        const double minorPoints = minor * perUnit;
        const long long first = static_cast<long long>(std::ceil((fromPoints - originPoints) / minorPoints - 1e-9));
        const long long last = static_cast<long long>(std::floor((toPoints - originPoints) / minorPoints + 1e-9));
        if (last - first > 100000) {
            return result;
        }
        for (long long i = first; i <= last; ++i) {
            Tick tick;
            tick.points = double(i) * minorPoints + originPoints;
            tick.value = double(i) * minor;
            tick.major = (i % stride) == 0;
            result.push_back(tick);
        }
        return result;
    }

    Rulers::Region Rulers::regionAt(float x, float y, bool visible) {
        if (!visible || x < 0.0f || y < 0.0f) {
            return Region::None;
        }
        const bool top = y < kThickness, left = x < kThickness;
        if (top && left) {
            return Region::Corner;
        }
        return top ? Region::Top : left ? Region::Left : Region::None;
    }

}
