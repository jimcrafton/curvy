#include "curvy/curvyfile.h"
#include "curvy/sketchdocument.h"
#include "curvy/sketchexport.h"
#include "curvy/sketchobject.h"
#include "curvy/sketchsvg.h"
#include "curvy/shapetools.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace curvy;

constexpr double kPi = 3.14159265358979323846;

SketchColor hex(const char* text) { return SketchColor::fromHex(text); }

SketchGradient gradient(double angle, std::initializer_list<std::pair<double, const char*>> stops) {
    SketchGradient g;
    g.angleDegrees = angle;
    for (const auto& s : stops) {
        GradientStop stop;
        stop.offset = s.first;
        stop.color = hex(s.second);
        g.stops.push_back(stop);
    }
    g.sortStops();
    return g;
}

SketchPaint solid(const char* text) { return SketchPaint::solid(hex(text)); }
SketchPaint linear(double angle, std::initializer_list<std::pair<double, const char*>> stops) { return SketchPaint::linear(gradient(angle, stops)); }
SketchPaint radial(std::initializer_list<std::pair<double, const char*>> stops) { return SketchPaint::radial(gradient(0.0, stops)); }

AppearanceItem fill(const SketchPaint& paint, double opacity = 1.0, BlendMode blend = BlendMode::Normal) {
    AppearanceItem item = AppearanceItem::makeFill(paint);
    item.opacity = opacity;
    item.blendMode = blend;
    return item;
}

AppearanceItem line(const char* color, double width, bool round = true, std::vector<WidthPoint> widths = {}) {
    StrokeOptions options;
    if (round) {
        options.cap = StrokeCap::Round;
        options.join = StrokeJoin::Round;
    }
    options.widths = std::move(widths);
    return AppearanceItem::makeStroke(solid(color), width, options);
}

AppearanceItem glow(const char* color, double softness, double amount) {
    AppearanceItem item = AppearanceItem::makeEffect(EffectKind::OuterGlow);
    item.color = hex(color);
    item.softness = softness;
    item.opacity = amount;
    return item;
}

AppearanceItem blur(double softness) {
    AppearanceItem item = AppearanceItem::makeEffect(EffectKind::GaussianBlur);
    item.softness = softness;
    return item;
}

AppearanceItem shadow(const char* color, double dx, double dy, double softness, double amount) {
    AppearanceItem item = AppearanceItem::makeEffect(EffectKind::DropShadow);
    item.color = hex(color);
    item.offsetX = dx;
    item.offsetY = dy;
    item.softness = softness;
    item.opacity = amount;
    return item;
}

AppearanceItem innerShadow(const char* color, double dx, double dy, double softness, double amount) {
    AppearanceItem item = AppearanceItem::makeEffect(EffectKind::InnerShadow);
    item.color = hex(color);
    item.offsetX = dx;
    item.offsetY = dy;
    item.softness = softness;
    item.opacity = amount;
    return item;
}

// M L C Q Z with absolute coordinates, as in an SVG path.
BLPath svgPath(const std::string& d) {
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < d.size();) {
        const char c = d[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++i;
        } else if (std::isalpha(static_cast<unsigned char>(c))) {
            tokens.emplace_back(1, c);
            ++i;
        } else {
            std::size_t end = i;
            while (end < d.size() && (std::isdigit(static_cast<unsigned char>(d[end])) || d[end] == '.' || d[end] == '-' || d[end] == '+')) {
                ++end;
            }
            if (end == i) {
                ++i;
                continue;
            }
            tokens.push_back(d.substr(i, end - i));
            i = end;
        }
    }
    BLPath path;
    std::size_t at = 0;
    const auto number = [&]() { return std::atof(tokens[at++].c_str()); };
    const auto isNumber = [&]() { return at < tokens.size() && !std::isalpha(static_cast<unsigned char>(tokens[at][0])); };
    while (at < tokens.size()) {
        const char cmd = tokens[at++][0];
        switch (cmd) {
            case 'M': { const double x = number(), y = number(); path.move_to(x, y); while (isNumber()) { const double a = number(), b = number(); path.line_to(a, b); } break; }
            case 'L': while (isNumber()) { const double a = number(), b = number(); path.line_to(a, b); } break;
            case 'C': while (isNumber()) { const double a = number(), b = number(), c2 = number(), d2 = number(), e = number(), f = number(); path.cubic_to(a, b, c2, d2, e, f); } break;
            case 'Q': while (isNumber()) { const double a = number(), b = number(), c2 = number(), d2 = number(); path.quad_to(a, b, c2, d2); } break;
            case 'Z': path.close(); break;
            default: break;
        }
    }
    return path;
}

// A smooth curve through the points (Catmull-Rom as cubic segments), starting at the path's current point when `moveFirst` is false.
void smoothThrough(BLPath& path, const std::vector<BLPoint>& pts, bool moveFirst) {
    if (pts.size() < 2) {
        return;
    }
    if (moveFirst) {
        path.move_to(pts[0].x, pts[0].y);
    }
    const long n = static_cast<long>(pts.size());
    const auto at = [&](long i) { return pts[static_cast<std::size_t>(std::clamp(i, 0L, n - 1))]; };
    for (long i = 0; i + 1 < n; ++i) {
        const BLPoint p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        path.cubic_to(p1.x + (p2.x - p0.x) / 6.0, p1.y + (p2.y - p0.y) / 6.0, p2.x - (p3.x - p1.x) / 6.0, p2.y - (p3.y - p1.y) / 6.0, p2.x, p2.y);
    }
}

BLPath ellipsePath(double cx, double cy, double rx, double ry) {
    BLPath path;
    path.add_ellipse(BLEllipse(cx, cy, rx, ry));
    return path;
}

bool insidePolygon(const std::vector<BLPoint>& poly, double x, double y) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if (((poly[i].y > y) != (poly[j].y > y)) && (x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) {
            in = !in;
        }
    }
    return in;
}

// A crescent from a to b, bowed by `bend` and `width` wide in the middle, with sharp tips.
BLPath swoosh(BLPoint a, BLPoint b, double bend, double width, double ripple) {
    const BLPoint mid((a.x + b.x) / 2, (a.y + b.y) / 2);
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::max(1.0, std::hypot(dx, dy));
    const BLPoint control(mid.x - dy / len * bend, mid.y + dx / len * bend);
    std::vector<BLPoint> upper, lower;
    const int steps = 10;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double u = 1.0 - t;
        const BLPoint p(u * u * a.x + 2 * u * t * control.x + t * t * b.x, u * u * a.y + 2 * u * t * control.y + t * t * b.y);
        const double tx = 2 * u * (control.x - a.x) + 2 * t * (b.x - control.x), ty = 2 * u * (control.y - a.y) + 2 * t * (b.y - control.y);
        const double tl = std::max(1e-6, std::hypot(tx, ty));
        const double w = width * std::pow(std::sin(kPi * t), 0.8) * (1.0 + ripple * std::sin(2.6 * kPi * t + bend));
        upper.emplace_back(p.x - ty / tl * w, p.y + tx / tl * w);
        lower.emplace_back(p.x + ty / tl * w, p.y - tx / tl * w);
    }
    BLPath path;
    smoothThrough(path, upper, true);
    std::reverse(lower.begin(), lower.end());
    smoothThrough(path, lower, false);
    path.close();
    return path;
}

struct Builder {
    SketchDocument& doc;

    SketchObject& put(const std::string& name, const BLPath& path, std::vector<AppearanceItem> look) {
        SketchObject* object = SketchObject::makePath(path);
        object->setName(name);
        object->setAppearance(look);
        return doc.add(object);
    }

    SketchObject& putSvg(const std::string& name, const std::string& d, std::vector<AppearanceItem> look) { return put(name, svgPath(d), std::move(look)); }

    void layer(const std::string& name, bool first = false) {
        if (first) {
            doc.renameLayer(doc.activeLayer(), name);
        } else {
            doc.addLayer(0, name);
        }
    }
};

const char* const kOutline = "#1b2744";

void background(Builder& b) {
    b.layer("Background", true);
    SketchGradient points;
    const auto point = [&](double x, double y, const char* color, double radius) {
        GradientPoint p;
        p.x = x;
        p.y = y;
        p.color = hex(color);
        p.radius = radius;
        points.points.push_back(p);
    };
    point(0.15, 0.95, "#f2d9bd", 0.9);
    point(0.85, 0.95, "#ecc9a3", 0.8);
    point(0.5, 0.55, "#f6e6d2", 0.8);
    point(0.5, 0.05, "#f4dcc0", 0.7);
    b.put("Paper", svgPath("M 0 0 L 1000 0 L 1000 1000 L 0 1000 Z"), {fill(SketchPaint::points(points))});
    b.put("Warm glow", svgPath("M 140 640 C 140 500 330 440 470 480 C 640 520 780 640 720 800 C 660 960 330 1000 200 880 C 150 830 140 740 140 640 Z"),
          {fill(radial({{0.0, "#fff6e6"}, {1.0, "#fff6e600"}}), 0.9, BlendMode::Screen), blur(30)});
    std::mt19937 rng(21);
    for (int i = 0; i < 9; ++i) {
        const double x = std::uniform_real_distribution<double>(40, 960)(rng), y = std::uniform_real_distribution<double>(660, 980)(rng);
        const double r = std::uniform_real_distribution<double>(14, 46)(rng);
        SketchObject& circle = b.doc.addParametric(SketchObject::makeEllipse(x, y, r, r), "Draw");
        circle.setName("Bokeh " + std::to_string(i + 1));
        circle.setAppearance({fill(radial({{0.0, "#ffffff00"}, {0.8, "#ffffff55"}, {1.0, "#ffffffaa"}}), 0.35, BlendMode::Screen)});
    }
}

std::vector<BLPoint> skyMass() {
    // The visible edge of the hair-sky, from the crown out to the left, then off the page, and back along the right under-edge.
    return {{405, 590}, {340, 554}, {270, 574}, {198, 522}, {128, 548}, {66, 476}, {-30, 440},
            {-30, -30}, {1030, -30}, {1030, 292},
            {972, 338}, {915, 318}, {858, 372}, {800, 366}, {752, 424}, {700, 398}, {660, 446}, {618, 424}, {596, 482}, {552, 478}, {516, 534}, {470, 548}};
}

void skyHair(Builder& b) {
    b.layer("Sky hair");
    const std::vector<BLPoint> mass = skyMass();
    BLPath outline;
    {
        std::vector<BLPoint> left(mass.begin(), mass.begin() + 7);
        smoothThrough(outline, left, true);
        outline.line_to(mass[7].x, mass[7].y);
        outline.line_to(mass[8].x, mass[8].y);
        outline.line_to(mass[9].x, mass[9].y);
        std::vector<BLPoint> right(mass.begin() + 9, mass.end());
        right.push_back(mass[0]);
        smoothThrough(outline, right, false);
        outline.close();
    }
    b.put("Night sky", outline,
          {fill(linear(90, {{0.0, "#10173f"}, {0.28, "#1d2c6c"}, {0.55, "#2f4c96"}, {0.8, "#4f86bf"}, {1.0, "#63b6d0"}}))});

    // Soft atmosphere under the ribbons.
    b.put("Haze", svgPath("M 140 330 C 260 250 420 280 520 380 C 600 450 560 520 470 540 C 340 560 210 470 140 330 Z"),
          {fill(radial({{0.0, "#9be0d8"}, {1.0, "#9be0d800"}}), 0.8, BlendMode::Screen), blur(26)});
    b.put("Haze high", svgPath("M 640 120 C 760 60 920 90 960 190 C 980 250 880 290 780 270 C 690 250 590 200 640 120 Z"),
          {fill(radial({{0.0, "#5fb3de"}, {1.0, "#5fb3de00"}}), 0.7, BlendMode::Screen), blur(30)});

    // Flowing ribbons: placed by hand-made rules, kept inside the sky, light near the head and hand, dark at the top.
    std::mt19937 rng(11);
    const auto uniform = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    struct Tone { const char* dark; const char* light; };
    const Tone top[] = {{"#10173f", "#1f2f74"}, {"#172359", "#2b4590"}, {"#1d2c6c", "#3a5fae"}};
    const Tone middle[] = {{"#2b4590", "#4f86bf"}, {"#34559f", "#5fa3d3"}, {"#3f6db3", "#7fc3df"}};
    const Tone low[] = {{"#4f86bf", "#9be0d8"}, {"#5fb3de", "#b8ece6"}, {"#62c4cf", "#d7f4ee"}, {"#7fc3df", "#e9faf5"}};
    int placed = 0;
    for (int tries = 0; placed < 46 && tries < 4000; ++tries) {
        const double x = uniform(-20, 1020), y = uniform(-20, 560);
        if (!insidePolygon(mass, x, y) || std::hypot(x - 430, y - 575) < 150) {
            continue;
        }
        const double fromHeadX = x - 430, fromHeadY = y - 640;
        double angle = std::atan2(fromHeadY, fromHeadX) + 0.7 * std::sin(y / 85.0) + uniform(-0.35, 0.35);
        const double length = uniform(120, 360);
        const BLPoint a(x, y), c(x + std::cos(angle) * length, y + std::sin(angle) * length);
        if (!insidePolygon(mass, c.x, c.y) || !insidePolygon(mass, (a.x + c.x) / 2, (a.y + c.y) / 2)) {
            continue;
        }
        const double width = 7 + std::pow(uniform(0, 1), 1.6) * 36;
        const double bend = uniform(-90, 90);
        const Tone& tone = y < 170 ? top[rng() % 3] : y < 340 ? middle[rng() % 3] : low[rng() % 4];
        const double deg = angle * 180.0 / kPi;
        std::vector<AppearanceItem> look = {fill(linear(deg, {{0.0, tone.dark}, {1.0, tone.light}}), uniform(0.82, 1.0))};
        if (y > 330 && rng() % 4 == 0) {
            look.push_back(glow("#bff3ee", 7, 0.5));
        }
        b.put("Ribbon " + std::to_string(placed + 1), swoosh(a, c, bend, width, 0.25), look);
        ++placed;
    }

    // Locks of hair rising from the head into the sky: dark at the crown, taking on the sky's colours as they climb.
    struct Lock { BLPoint from, to; double bend, width; };
    const Lock locks[] = {
        {{385, 575}, {240, 365}, -55, 36}, {{405, 562}, {300, 240}, 60, 34}, {{432, 552}, {410, 190}, -45, 38},
        {{458, 550}, {540, 165}, 55, 36}, {{484, 556}, {660, 262}, -60, 34}, {{500, 566}, {775, 372}, 50, 30},
        {{365, 610}, {120, 410}, 45, 30}, {{345, 650}, {50, 575}, -40, 22},
    };
    int lockIndex = 0;
    for (const Lock& lock : locks) {
        const double deg = std::atan2(lock.to.y - lock.from.y, lock.to.x - lock.from.x) * 180.0 / kPi;
        b.put("Hair lock " + std::to_string(++lockIndex), swoosh(lock.from, lock.to, lock.bend, lock.width, 0.12),
              {fill(linear(deg, {{0.0, "#141c4c"}, {0.55, "#27408c"}, {1.0, "#4f86bf"}}))});
    }

    // Bright cloud flecks on top.
    for (int i = 0; i < 16; ++i) {
        const double x = uniform(120, 940), y = uniform(40, 420);
        if (!insidePolygon(mass, x, y)) {
            continue;
        }
        const double angle = uniform(-0.5, 0.5) + (x < 500 ? -0.3 : 0.3);
        const BLPoint a(x, y), c(x + std::cos(angle) * uniform(60, 140), y + std::sin(angle) * uniform(20, 60));
        b.put("Fleck " + std::to_string(i + 1), swoosh(a, c, uniform(-25, 25), uniform(3, 8), 0.2),
              {fill(solid(i % 3 == 0 ? "#e9faf5" : "#9be0d8"), 0.9, BlendMode::Screen)});
    }

    // Wisps of hair trailing off to the left, curling.
    const std::vector<std::vector<BLPoint>> wisps = {
        {{300, 556}, {230, 566}, {160, 590}, {110, 640}, {100, 700}, {140, 730}, {185, 705}, {180, 672}},
        {{350, 580}, {290, 612}, {235, 660}, {215, 720}, {240, 770}, {285, 770}, {298, 735}},
        {{250, 548}, {170, 540}, {96, 560}, {50, 610}, {48, 665}, {86, 690}},
        {{205, 540}, {140, 520}, {80, 492}, {30, 440}}};
    int wispIndex = 0;
    for (const auto& points : wisps) {
        BLPath path;
        smoothThrough(path, points, true);
        b.put("Wisp " + std::to_string(++wispIndex), path,
              {AppearanceItem::makeFill(SketchPaint::none()), line("#233a84", 10, true, {{0.0, 0.1, 0.1}, {0.35, 1.0, 1.0}, {1.0, 0.1, 0.1}})});
    }
}

BLPath birdBody() {
    return svgPath("M 52 0 C 44 -8 32 -11 20 -9 C 8 -7 -12 -7 -32 -2 L -60 6 L -46 10 L -64 17 L -30 13 C -10 15 10 13 24 8 C 34 6 44 4 52 0 Z");
}

BLPath birdWing(double flip) {
    BLPath wing = svgPath("M 22 -6 C 12 -32 -8 -62 -50 -88 L -38 -62 L -56 -66 L -40 -45 L -60 -46 L -38 -29 L -52 -27 L -27 -14 L -8 -8 Z");
    BLPath out;
    out.add_path(wing, BLMatrix2D::make_scaling(1.0, flip));
    return out;
}

void birds(Builder& b) {
    b.layer("Birds");
    struct Flight { double x, y, size, turn, flip; bool wingsDown; };
    const Flight flights[] = {
        {430, 330, 1.55, -0.55, 1, false}, {620, 200, 1.9, -0.18, 1, false}, {810, 120, 1.0, 0.1, -1, true},
        {535, 410, 1.15, -0.8, 1, true}, {330, 175, 0.9, -0.35, 1, false}, {700, 330, 1.25, 0.25, -1, false}, {900, 250, 0.7, 0.05, 1, true},
    };
    int index = 0;
    for (const Flight& f : flights) {
        BLMatrix2D place = BLMatrix2D::make_identity();
        place.rotate(f.turn);
        place.post_scale(f.size * f.flip, f.size);
        place.post_translate(f.x, f.y);
        BLPath shape;
        shape.add_path(birdBody(), place);
        shape.add_path(birdWing(f.wingsDown ? -1.0 : 1.0), place);
        BLMatrix2D behind = place;
        behind.post_translate(-6 * f.size * f.flip, 5 * f.size);
        BLPath farWing;
        farWing.add_path(birdWing(f.wingsDown ? -0.7 : 0.7), behind);
        b.put("Bird " + std::to_string(++index) + " far wing", farWing, {fill(linear(90, {{0.0, "#bcd9ee"}, {1.0, "#7fa8d4"}}), 0.9)});
        b.put("Bird " + std::to_string(index), shape,
              {fill(linear(90, {{0.0, "#ffffff"}, {1.0, "#dcecf8"}})), glow("#bfeaff", 10, 0.6)});
    }
}

void figure(Builder& b) {
    b.layer("Figure");
    // Torso and collar.
    b.putSvg("Shirt", "M 290 1010 C 300 930 350 876 428 846 L 524 842 C 596 856 650 886 676 934 L 712 1010 Z",
             {fill(linear(100, {{0.0, "#b6d3f7"}, {0.5, "#79aef0"}, {1.0, "#4f85d1"}})), line(kOutline, 3.5)});
    b.putSvg("Shirt shade", "M 290 1010 C 300 940 340 892 400 862 C 392 920 400 970 440 1010 Z",
             {fill(linear(0, {{0.0, "#3a68b8"}, {1.0, "#3a68b800"}}), 0.8, BlendMode::Multiply)});
    // Neck.
    b.putSvg("Neck", "M 436 756 C 442 790 450 822 452 856 C 470 864 500 864 520 856 C 520 826 518 796 512 770 Z",
             {fill(linear(90, {{0.0, "#d9a487"}, {0.45, "#efc9aa"}, {1.0, "#f4d7bc"}})), line(kOutline, 3)});
    b.putSvg("Chest", "M 452 852 C 470 864 500 864 520 852 L 500 948 L 476 948 Z", {fill(linear(90, {{0.0, "#efc9aa"}, {1.0, "#e0a98a"}})), line(kOutline, 2.5)});
    b.put("Chin shadow", svgPath("M 436 764 C 462 792 500 792 514 770 L 516 806 C 486 826 452 816 442 796 Z"),
          {fill(solid("#b9735a"), 0.55, BlendMode::Multiply), blur(5)});
    b.putSvg("Collar left", "M 452 846 C 436 868 414 896 388 926 C 420 944 450 950 476 946 C 476 910 470 880 462 856 Z",
             {fill(linear(60, {{0.0, "#eaf3fe"}, {1.0, "#9fc0f0"}})), shadow("#1b2744", 2, 4, 5, 0.3), line(kOutline, 3)});
    b.putSvg("Collar right", "M 520 846 C 540 868 566 898 592 924 C 560 944 526 950 498 946 C 500 910 506 880 512 858 Z",
             {fill(linear(120, {{0.0, "#f4f9ff"}, {1.0, "#b3cef4"}})), shadow("#1b2744", 2, 4, 5, 0.3), line(kOutline, 3)});

    // Face (profile, looking up and to the right).
    b.putSvg("Face",
             "M 468 556 C 500 566 524 592 528 624 C 529 638 536 648 548 660 C 553 666 550 673 541 675 C 536 678 536 682 540 686 C 543 690 541 695 536 697 "
             "C 541 700 539 706 533 708 C 536 716 534 726 526 734 C 512 756 486 772 456 770 C 436 768 420 758 414 744 L 398 640 C 408 598 434 568 468 556 Z",
             {fill(linear(70, {{0.0, "#f6dcc2"}, {0.6, "#f0cfb1"}, {1.0, "#e2ac8f"}})), innerShadow("#c98a72", -4, 6, 8, 0.55), line(kOutline, 3.5)});
    b.putSvg("Ear", "M 404 690 C 396 674 412 660 428 668 C 440 676 438 704 424 724 C 412 726 404 710 404 690 Z",
             {fill(linear(90, {{0.0, "#f3d3b6"}, {1.0, "#dca184"}})), line(kOutline, 3)});
    b.putSvg("Ear inner", "M 414 684 C 420 680 428 684 428 696 C 426 706 420 712 416 708", {AppearanceItem::makeFill(SketchPaint::none()), line("#b9806a", 2.5)});
    b.put("Blush", svgPath("M 462 712 C 478 700 504 706 508 722 C 504 738 476 744 462 732 Z"),
          {fill(radial({{0.0, "#ff8f8f"}, {1.0, "#ff8f8f00"}}), 0.75, BlendMode::Multiply), blur(5)});

    // Eye.
    b.putSvg("Eye white", "M 480 648 C 490 634 516 634 530 650 C 520 666 492 666 480 648 Z", {fill(solid("#ffffff")), line(kOutline, 1.5)});
    b.put("Iris", ellipsePath(507, 650, 12.5, 13.5), {fill(radial({{0.0, "#9fdcff"}, {0.5, "#2c6fb3"}, {1.0, "#17285f"}}))});
    b.put("Pupil", ellipsePath(508, 650, 5.5, 6.8), {fill(solid("#0c1233"))});
    b.put("Eye glint", ellipsePath(513, 644, 3.6, 3.6), {fill(solid("#ffffff"))});
    b.put("Eye glint small", ellipsePath(501, 658, 1.9, 1.9), {fill(solid("#ffffff"), 0.9)});
    b.putSvg("Eye lid", "M 475 648 C 488 629 518 627 538 648", {AppearanceItem::makeFill(SketchPaint::none()), line(kOutline, 6, true, {{0.0, 0.3, 0.3}, {0.5, 1.0, 1.0}, {1.0, 0.8, 0.8}})});
    b.putSvg("Lashes", "M 531 643 C 540 639 546 634 552 626", {AppearanceItem::makeFill(SketchPaint::none()), line(kOutline, 4.5, true, {{0.0, 1.0, 1.0}, {1.0, 0.1, 0.1}})});
    b.putSvg("Eyebrow", "M 480 616 C 498 604 522 606 540 618", {AppearanceItem::makeFill(SketchPaint::none()), line("#1f2d6b", 4.5, true, {{0.0, 0.3, 0.3}, {0.5, 1.0, 1.0}, {1.0, 0.4, 0.4}})});
    b.putSvg("Nose line", "M 545 668 C 549 671 548 675 543 676", {AppearanceItem::makeFill(SketchPaint::none()), line("#b9806a", 2)});
    b.putSvg("Mouth", "M 540 703 C 534 705 529 706 524 705", {AppearanceItem::makeFill(SketchPaint::none()), line("#a95a52", 2.8)});

    // Hair on the head: bangs, the side lock behind the ear, and a few lighter strands.
    b.putSvg("Hair bangs",
             "M 338 650 C 334 596 372 548 440 534 C 486 528 530 548 540 590 C 514 584 496 592 488 614 C 468 604 448 614 436 640 C 424 676 414 720 396 770 "
             "C 366 730 340 696 338 650 Z",
             {fill(linear(60, {{0.0, "#141c4c"}, {0.6, "#27408c"}, {1.0, "#4a74b8"}}))});
    b.putSvg("Hair side lock", "M 398 700 C 404 740 396 786 372 826 C 350 790 344 740 360 696 Z",
             {fill(linear(90, {{0.0, "#233a84"}, {1.0, "#4f86bf"}})), line(kOutline, 3)});
    b.putSvg("Hair strand 1", "M 372 600 C 410 566 460 552 508 566", {AppearanceItem::makeFill(SketchPaint::none()), line("#8fb6e6", 3, true, {{0.0, 0.1, 0.1}, {0.5, 1.0, 1.0}, {1.0, 0.1, 0.1}})});
    b.putSvg("Hair strand 2", "M 360 650 C 366 622 384 600 408 588", {AppearanceItem::makeFill(SketchPaint::none()), line("#8fb6e6", 2.5, true, {{0.0, 0.1, 0.1}, {0.5, 1.0, 1.0}, {1.0, 0.1, 0.1}})});

    // The raised arm: sleeve, cuff, then the hand reaching into the sky.
    b.putSvg("Sleeve",
             "M 586 887 L 758 752 C 764 748 768 744 766 738 L 655 598 L 725 542 C 800 610 890 690 918 752 C 930 778 922 796 904 810 L 654 973 Z",
             {fill(linear(35, {{0.0, "#b3cef4"}, {0.45, "#6fa8ee"}, {1.0, "#3f78c6"}})), shadow("#1b2744", 8, 10, 12, 0.35), line(kOutline, 3.5)});
    b.putSvg("Sleeve fold 1", "M 770 744 C 760 790 728 840 676 888", {AppearanceItem::makeFill(SketchPaint::none()), line("#2f5fae", 3.5, true, {{0.0, 0.2, 0.2}, {0.5, 1.0, 1.0}, {1.0, 0.1, 0.1}})});
    b.putSvg("Sleeve fold 2", "M 712 640 C 740 670 778 706 790 740", {AppearanceItem::makeFill(SketchPaint::none()), line("#2f5fae", 3, true, {{0.0, 0.2, 0.2}, {0.5, 1.0, 1.0}, {1.0, 0.1, 0.1}})});
    b.putSvg("Sleeve highlight", "M 640 880 C 700 840 760 790 830 730", {AppearanceItem::makeFill(SketchPaint::none()), line("#dcebfb", 5, true, {{0.0, 0.1, 0.1}, {0.5, 1.0, 1.0}, {1.0, 0.1, 0.1}})});
    b.putSvg("Cuff", "M 646 604 L 735 536 L 748 556 L 660 626 Z", {fill(linear(60, {{0.0, "#eaf3fe"}, {1.0, "#a3c4f2"}})), line(kOutline, 3)});

    // Hand: palm and five fingers, each drawn as an outlined round stroke.
    const char* skin = "#f1cfae";
    const struct { const char* d; double w; } fingers[] = {
        {"M 676 556 C 662 530 646 506 624 482", 17},   // index
        {"M 692 548 C 686 520 676 492 664 462", 18},   // middle
        {"M 708 548 C 710 522 708 496 704 470", 17},   // ring
        {"M 722 554 C 732 532 742 512 750 494", 15},   // little
        {"M 722 586 C 746 574 764 560 778 540", 17},   // thumb
    };
    b.putSvg("Palm", "M 650 600 C 650 566 664 546 690 540 C 716 538 736 548 742 570 C 744 590 734 606 718 616 C 694 626 664 622 650 600 Z",
             {fill(linear(60, {{0.0, "#f6dcc2"}, {1.0, "#e0a98a"}})), line(kOutline, 3.5)});
    int finger = 0;
    for (const auto& f : fingers) {
        BLPath path = svgPath(f.d);
        b.put("Finger outline " + std::to_string(++finger), path, {AppearanceItem::makeFill(SketchPaint::none()), line(kOutline, f.w + 7)});
    }
    finger = 0;
    for (const auto& f : fingers) {
        b.put("Finger " + std::to_string(++finger), svgPath(f.d),
              {AppearanceItem::makeFill(SketchPaint::none()), line(skin, f.w, true, {{0.0, 1.0, 1.0}, {1.0, 0.82, 0.82}})});
    }
    b.putSvg("Palm shade", "M 664 604 C 690 618 722 612 736 596", {AppearanceItem::makeFill(SketchPaint::none()), line("#d08f74", 4, true, {{0.0, 0.2, 0.2}, {0.5, 1.0, 1.0}, {1.0, 0.2, 0.2}})});
}

void sparkles(Builder& b, std::vector<std::pair<std::size_t, std::string>>& twinkles) {
    b.layer("Sparkles");
    struct Spark { double x, y, size, phase; };
    const Spark sparks[] = {
        {250, 240, 22, 0.0}, {160, 110, 12, 1.3}, {590, 150, 17, 2.1}, {850, 70, 14, 0.7}, {960, 300, 10, 3.0},
        {735, 255, 24, 4.1}, {300, 395, 15, 5.2}, {470, 90, 11, 2.7}, {905, 180, 19, 1.9}, {120, 430, 12, 3.6},
        {600, 505, 13, 0.4}, {375, 505, 11, 2.2}, {760, 470, 17, 4.6},
    };
    int index = 0;
    for (const Spark& s : sparks) {
        SketchObject& star = b.doc.addParametric(SketchObject::makePolygon(starPoints(BLPoint(s.x, s.y), s.size, 0.2, 4, -kPi / 2)), "Draw");
        star.setName("Sparkle " + std::to_string(++index));
        star.setAppearance({fill(radial({{0.0, "#fff7c2"}, {0.55, "#ffd23f"}, {1.0, "#ffb300"}})), glow("#ffd23f", 9, 0.75)});
        twinkles.emplace_back(star.id(), std::to_string(s.size) + "|" + std::to_string(s.phase));
    }
    // Specks of light, plain circles.
    std::mt19937 rng(5);
    for (int i = 0; i < 26; ++i) {
        const double x = std::uniform_real_distribution<double>(30, 970)(rng), y = std::uniform_real_distribution<double>(20, 640)(rng);
        const double r = std::uniform_real_distribution<double>(2.2, 6.5)(rng);
        SketchObject& dot = b.doc.addParametric(SketchObject::makeEllipse(x, y, r, r), "Draw");
        dot.setName("Speck " + std::to_string(i + 1));
        dot.setAppearance({fill(solid(i % 4 == 0 ? "#ffffff" : "#ffcc00")), glow("#ffd23f", 5, 0.5)});
    }
}

}  // namespace

TEST(SampleArt, DISABLED_WriteLayerStack) {
    namespace fs = std::filesystem;
    const fs::path out = fs::path(__FILE__).parent_path().parent_path() / "Resources" / "samples";
    fs::create_directories(out);

    SketchDocument doc;
    doc.setPageSize(800.0f, 600.0f);
    const auto block = [&](const std::string& name, double x, double y, double w, double h, const char* color) {
        SketchObject& object = doc.add(SketchObject::makeRect(x, y, w, h));
        object.setName(name);
        object.setAppearance({fill(solid(color)), line("#202020", 3.0)});
    };
    const auto disc = [&](const std::string& name, double x, double y, double r, const char* color) {
        SketchObject& object = doc.add(SketchObject::makeEllipse(x, y, r, r));
        object.setName(name);
        object.setAppearance({fill(solid(color)), line("#202020", 3.0)});
    };
    {
        SketchDocument::Batch batch(doc);
        block("Backdrop", 40, 40, 720, 520, "#d9d4c7");
        doc.addLayer(0, "Red");
        block("Red square", 100, 100, 260, 260, "#d94a3d");
        doc.addLayer(0, "Green");
        block("Green square", 200, 160, 260, 260, "#4aa564");
        doc.addLayer(0, "Blue");
        block("Blue square", 300, 220, 260, 260, "#3d6fd9");
        const std::size_t outer = doc.addLayer(0, "Discs");
        doc.addLayer(outer, "Yellow disc");
        disc("Yellow disc", 560, 170, 90, "#f2c230");
        doc.addLayer(outer, "Purple disc");
        disc("Purple disc", 620, 230, 90, "#8e44ad");
    }
    doc.undoStack().clear();

    const std::string native = curvyFromDocument(doc);
    {
        std::ofstream file(out / "layerStack.curvy", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << native;
    }
    SketchDocument loaded;
    std::string error;
    ASSERT_TRUE(documentFromCurvy(native, loaded, &error)) << error;
    EXPECT_EQ(loaded.objectCount(), doc.objectCount());
    EXPECT_EQ(loaded.layers().size(), doc.layers().size());
}

TEST(SampleArt, DISABLED_RenderLayerStackAfterGreenBack) {
    namespace fs = std::filesystem;
    const fs::path samples = fs::path(__FILE__).parent_path().parent_path() / "Resources" / "samples";
    std::ifstream in(samples / "layerStack.curvy", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    SketchDocument doc;
    std::string error;
    ASSERT_TRUE(documentFromCurvy(text, doc, &error)) << error;
    const char* dir = std::getenv("CURVY_RENDER_DIR");
    ASSERT_NE(dir, nullptr);
    ASSERT_TRUE(exportPageImage(doc, (fs::path(dir) / "before.png").string(), 1.0));
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        std::cout << "before " << i << " " << doc.object(i).name() << " layer " << doc.layerOf(doc.object(i)) << "\n";
    }
    {
        const jitnodes::Network& root = doc.network();
        const jitnodes::NodeId merge = root.findNode("paint");
        for (int i = 0; merge != jitnodes::invalidNodeId && i < root.node(merge).inputCount(); ++i) {
            const jitnodes::NodeId in = root.inputOf(merge, i);
            std::cout << "merge input " << i << " " << (in == jitnodes::invalidNodeId ? std::string("-") : root.node(in).name() + " (" + root.node(in).typeName() + ")") << "\n";
        }
        for (const Layer& layer : doc.layers()) {
            std::cout << "layer " << layer.id << " " << layer.name << " parent " << layer.parent << "\n";
        }
        for (std::uint32_t scope = 1; scope < doc.scopeCount(); ++scope) {
            const NodeRef at = doc.parentOfScope(scope);
            std::cout << "scope " << scope << " subnet " << (doc.hasNode(at) ? doc.node(at).getString("label") : std::string("?")) << " layerIdOfScope "
                      << doc.layerIdOfScope(scope) << " layerParent " << (doc.hasNode(at) ? doc.node(at).getFloat("layerParent") : -9.0f) << "\n";
        }
        for (std::size_t i = 0; i < doc.objectCount(); ++i) {
            std::cout << "obj " << doc.object(i).name() << " scope " << doc.object(i).graphScope() << " layer() " << doc.object(i).layer()
                      << " placed " << doc.object(i).placedLayer() << " layerOf " << doc.layerOf(doc.object(i)) << "\n";
        }
    }
    NodeRef green;
    for (std::uint32_t scope = 1; scope < doc.scopeCount(); ++scope) {
        const NodeRef at = doc.parentOfScope(scope);
        if (doc.hasNode(at) && doc.node(at).getString("label") == "Green") {
            green = at;
        }
    }
    ASSERT_TRUE(doc.hasNode(green));
    ASSERT_TRUE(doc.movePaintItem(green, -1));
    ASSERT_TRUE(exportPageImage(doc, (fs::path(dir) / "after.png").string(), 1.0));
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        std::cout << i << " " << doc.object(i).name() << " layer " << doc.layerOf(doc.object(i)) << " visible " << doc.object(i).isVisible()
                  << " opacity " << doc.object(i).opacity() << "\n";
    }
}

TEST(SampleArt, DISABLED_WriteSkyDreamer) {
    namespace fs = std::filesystem;
    const fs::path out = fs::path(__FILE__).parent_path().parent_path() / "Resources" / "samples";
    fs::create_directories(out);

    SketchDocument doc;
    doc.setPageSize(1000.0f, 1000.0f);
    std::vector<std::pair<std::size_t, std::string>> twinkles;
    {
        SketchDocument::Batch batch(doc);
        Builder b{doc};
        background(b);
        skyHair(b);
        birds(b);
        figure(b);
        sparkles(b, twinkles);
    }
    for (const auto& [id, spec] : twinkles) {
        const SketchObject* star = doc.findById(id);
        ASSERT_NE(star, nullptr);
        const std::string phase = spec.substr(spec.find('|') + 1);
        const NodeRef place = doc.transformRefOf(id);
        const BLBox box = star->geometryBounds();
        doc.setGraphParam(place, "pivotX", float((box.x0 + box.x1) / 2));
        doc.setGraphParam(place, "pivotY", float((box.y0 + box.y1) / 2));
        const std::string pulse = "0.82 + 0.18 * sin(time * 5 + " + phase + ")";
        EXPECT_EQ(doc.setGraphExpression(place, "scaleX", pulse), "");
        EXPECT_EQ(doc.setGraphExpression(place, "scaleY", pulse), "");
    }
    doc.undoStack().clear();

    // The twinkle is live: moving time changes a sparkle's size, about its centre.
    {
        const SketchObject* star = doc.findById(twinkles.front().first);
        ASSERT_NE(star, nullptr);
        const BLBox before = star->geometryBounds();
        doc.setTime(0.3);
        const BLBox later = star->geometryBounds();
        doc.setTime(0.0);
        EXPECT_NE(before.x1 - before.x0, later.x1 - later.x0);
        EXPECT_NEAR((before.x0 + before.x1) / 2, (later.x0 + later.x1) / 2, 1e-3);
    }

    const std::string text = svgFromDocument(doc);
    {
        std::ofstream file(out / "skyDreamer.svg", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << text;
    }
    const fs::path preview = fs::path(__FILE__).parent_path().parent_path() / "docs" / "samples";
    fs::create_directories(preview);
    ASSERT_TRUE(exportPageImage(doc, (preview / "skyDreamer.png").string(), 1.0));

    SketchDocument loaded;
    ASSERT_TRUE(documentFromSvg(text, loaded));
    EXPECT_EQ(loaded.objectCount(), doc.objectCount());
    std::size_t expressions = 0;
    const std::string graph = loaded.graphToText();
    for (std::size_t at = graph.find("sin(time"); at != std::string::npos; at = graph.find("sin(time", at + 1)) {
        ++expressions;
    }
    EXPECT_EQ(expressions, twinkles.size() * 2);

    // The native file, written beside the SVG: it must open, still pulse, and resave to itself.
    const std::string native = curvyFromDocument(doc);
    {
        std::ofstream file(out / "skyDreamer.curvy", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << native;
    }
    SketchDocument fromNative;
    std::string error;
    ASSERT_TRUE(documentFromCurvy(native, fromNative, &error)) << error;
    EXPECT_EQ(fromNative.objectCount(), doc.objectCount());
    std::size_t nativeExpressions = 0;
    const std::string nativeGraph = fromNative.graphToText();
    for (std::size_t at = nativeGraph.find("sin(time"); at != std::string::npos; at = nativeGraph.find("sin(time", at + 1)) {
        ++nativeExpressions;
    }
    EXPECT_EQ(nativeExpressions, twinkles.size() * 2);
    SketchDocument again;
    ASSERT_TRUE(documentFromCurvy(curvyFromDocument(fromNative), again, &error)) << error;
    EXPECT_EQ(curvyFromDocument(again), curvyFromDocument(fromNative)) << "a resave of the sample diffs to nothing";
}
