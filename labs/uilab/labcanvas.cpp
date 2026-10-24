#include "labcanvas.h"

#include <newui/font.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lab {

    using namespace newui;

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kRuler = 18.0;
        constexpr auto kTapLimit = std::chrono::milliseconds(300);

        BLRgba32 rgb(std::uint32_t value) { return BLRgba32(0xFF000000u | value); }

        BLRgba32 hsla(double hue, double saturation, double lightness, double alpha) {
            hue = std::fmod(hue, 360.0);
            if (hue < 0.0) { hue += 360.0; }
            const double c = (1.0 - std::fabs(2.0 * lightness - 1.0)) * saturation;
            const double x = c * (1.0 - std::fabs(std::fmod(hue / 60.0, 2.0) - 1.0));
            const double m = lightness - c * 0.5;
            double r = 0.0, g = 0.0, b = 0.0;
            switch (int(hue / 60.0) % 6) {
                case 0: r = c; g = x; break;
                case 1: r = x; g = c; break;
                case 2: g = c; b = x; break;
                case 3: g = x; b = c; break;
                case 4: r = x; b = c; break;
                default: r = c; b = x; break;
            }
            auto byte = [](double v) { return std::uint32_t(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); };
            return BLRgba32((byte(alpha) << 24) | (byte(r + m) << 16) | (byte(g + m) << 8) | byte(b + m));
        }

        double distanceToSegment(const BLPoint& p, const BLPoint& a, const BLPoint& b) {
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double lengthSq = dx * dx + dy * dy;
            const double t = lengthSq > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq, 0.0, 1.0) : 0.0;
            return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
        }

        bool insidePolygon(const std::vector<BLPoint>& pts, const BLPoint& p) {
            bool inside = false;
            for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                if ((pts[i].y > p.y) != (pts[j].y > p.y) && p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x) {
                    inside = !inside;
                }
            }
            return inside;
        }

        curvy::RadialRing newRing() {
            return {"New", "Create...", "dialog.new", {
                {"Letter", "1", "new.letter"}, {"A4", "2", "new.a4"}, {"Full HD", "3", "new.fullhd"}, {"Square", "4", "new.square"},
                {"Open", "O", "file.open"}, {"Recent", "R", "file.recent"}}};
        }
        curvy::RadialRing createRing() {
            return {"Create", "Shape options...", "dialog.shape", {
                {"Rectangle", "R", "tool.rect"}, {"Ellipse", "E", "tool.ellipse"}, {"Polygon", "P", "tool.polygon"}, {"Star", "S", "tool.star"},
                {"Line", "L", "tool.line"}, {"Freehand", "B", "tool.freehand"}, {"Text", "T", "tool.text"}, {"File", "F", "ring.document"}}};
        }
        curvy::RadialRing documentRing() {
            return {"Document", "Setup...", "dialog.setup", {
                {"New artboard", "A", "doc.newArtboard"}, {"New document", "N", "ring.new"}, {"Open", "O", "file.open"}, {"Save", "S", "file.save"},
                {"Export", "E", "file.export"}, {"Place image", "I", "file.place"}, {"Fit", "0", "doc.fit"}, {"Rulers", "G", "doc.rulers"}}};
        }
        curvy::RadialRing objectRing() {
            return {"Shape", "Properties...", "dialog.object", {
                {"Arrange", "1", "obj.arrange"}, {"Group", "2", "obj.group"}, {"Fill", "3", "obj.fill"}, {"Stroke", "4", "obj.stroke"},
                {"Duplicate", "D", "obj.duplicate"}, {"Delete", "X", "obj.delete"}, {"Edit graph", "G", "obj.graph"}, {"Lock", "K", "obj.lock"}}};
        }
    }

    LabCanvas::LabCanvas() {
        setName("canvas");
        setVisible(true);
        setAcceptsFocus(true);
        menu_ = new curvy::RadialMenu();
        addChild(menu_);
        menu_->onCommand.add(this, &LabCanvas::handleCommand);
        onMouseDown.add(this, &LabCanvas::handleMouseDown);
        onMouseMove.add(this, &LabCanvas::handleMouseMove);
        onMouseUp.add(this, &LabCanvas::handleMouseUp);
        onKeyDown.add(this, &LabCanvas::handleKeyDown);
        onKeyUp.add(this, &LabCanvas::handleKeyUp);
        message_ = "Tap Space to start";
    }

    void LabCanvas::setBounds(const Rect& bounds) {
        SubView::setBounds(bounds);
        if (menu_ != nullptr) {
            menu_->setBounds(Rect(0.0f, 0.0f, bounds.size().width, bounds.size().height));
        }
    }

    // ---- Coordinates and shapes ----

    BLPoint LabCanvas::toWorld(const Point& pt) const {
        return BLPoint((double(pt.x) - panX_) / zoom_, (double(pt.y) - panY_) / zoom_);
    }

    BLPoint LabCanvas::toView(const BLPoint& world) const {
        return BLPoint(world.x * zoom_ + panX_, world.y * zoom_ + panY_);
    }

    namespace {
        BLBox boxOf(const BLPoint& a, const BLPoint& b) {
            return BLBox(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
        }
    }

    BLPath LabCanvas::outline(const Shape& shape) const {
        const BLBox box = boxOf(shape.a, shape.b);
        const double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        const double rx = (box.x1 - box.x0) * 0.5, ry = (box.y1 - box.y0) * 0.5;
        BLPath path;
        switch (shape.kind) {
            case Kind::Rect:
                path.add_rect(BLRect(box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0));
                break;
            case Kind::Ellipse:
                path.add_ellipse(BLEllipse(cx, cy, std::max(rx, 0.5), std::max(ry, 0.5)));
                break;
            case Kind::Polygon:
            case Kind::Star: {
                const bool star = shape.kind == Kind::Star;
                const int corners = std::max(shape.sides, 3) * (star ? 2 : 1);
                for (int i = 0; i < corners; ++i) {
                    const double angle = -kPi / 2.0 + 2.0 * kPi * double(i) / double(corners);
                    const double k = star && (i % 2 == 1) ? 0.45 : 1.0;
                    const BLPoint p(cx + rx * k * std::cos(angle), cy + ry * k * std::sin(angle));
                    if (i == 0) { path.move_to(p); } else { path.line_to(p); }
                }
                path.close();
                break;
            }
            case Kind::Line:
                path.move_to(shape.a);
                path.line_to(shape.b);
                break;
        }
        return path;
    }

    std::vector<BLPoint> LabCanvas::outlinePoints(const Shape& shape) const {
        const BLBox box = boxOf(shape.a, shape.b);
        const double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        const double rx = (box.x1 - box.x0) * 0.5, ry = (box.y1 - box.y0) * 0.5;
        std::vector<BLPoint> pts;
        switch (shape.kind) {
            case Kind::Rect:
                pts = {BLPoint(box.x0, box.y0), BLPoint(box.x1, box.y0), BLPoint(box.x1, box.y1), BLPoint(box.x0, box.y1)};
                break;
            case Kind::Ellipse:
                for (int i = 0; i < 72; ++i) {
                    const double angle = 2.0 * kPi * double(i) / 72.0;
                    pts.push_back(BLPoint(cx + rx * std::cos(angle), cy + ry * std::sin(angle)));
                }
                break;
            case Kind::Polygon:
            case Kind::Star: {
                const bool star = shape.kind == Kind::Star;
                const int corners = std::max(shape.sides, 3) * (star ? 2 : 1);
                for (int i = 0; i < corners; ++i) {
                    const double angle = -kPi / 2.0 + 2.0 * kPi * double(i) / double(corners);
                    const double k = star && (i % 2 == 1) ? 0.45 : 1.0;
                    pts.push_back(BLPoint(cx + rx * k * std::cos(angle), cy + ry * k * std::sin(angle)));
                }
                break;
            }
            case Kind::Line:
                pts = {shape.a, shape.b};
                break;
        }
        return pts;
    }

    LabCanvas::Hover LabCanvas::contextAt(const BLPoint& world) const {
        for (int i = int(shapes_.size()) - 1; i >= 0; --i) {
            const Shape& shape = shapes_[std::size_t(i)];
            const std::vector<BLPoint> pts = outlinePoints(shape);
            const bool closed = shape.kind != Kind::Line;
            double nearest = 1e30;
            for (std::size_t k = 0; k + (closed ? 0 : 1) < pts.size(); ++k) {
                nearest = std::min(nearest, distanceToSegment(world, pts[k], pts[(k + 1) % pts.size()]));
            }
            if (nearest <= shape.strokeW * 0.5 + 6.0 / zoom_) {
                return {i, Part::Stroke};
            }
            if (closed && insidePolygon(pts, world)) {
                return {i, Part::Fill};
            }
        }
        return {};
    }

    const std::vector<LabCanvas::Prop>& LabCanvas::propsFor(Part part) {
        static const std::vector<double> hues = {0, 60, 120, 180, 240, 300, 360};
        static const std::vector<double> percents = {0, 25, 50, 75, 100};
        static const std::vector<Prop> stroke = {
            {'W', "Width", &Shape::strokeW, 0.5, 40.0, 0.08, " px", 1, {1, 2, 4, 8, 16, 32}},
            {'O', "Opacity", &Shape::strokeOp, 0.0, 100.0, 0.4, "%", 0, percents},
            {'H', "Hue", &Shape::strokeHue, 0.0, 360.0, 1.0, "\xC2\xB0", 0, hues}};
        static const std::vector<Prop> fill = {
            {'O', "Opacity", &Shape::fillOp, 0.0, 100.0, 0.4, "%", 0, percents},
            {'H', "Hue", &Shape::fillHue, 0.0, 360.0, 1.0, "\xC2\xB0", 0, hues},
            {'B', "Brightness", &Shape::fillL, 15.0, 97.0, 0.3, "%", 0, {25, 50, 75, 90}}};
        static const std::vector<Prop> none;
        return part == Part::Stroke ? stroke : part == Part::Fill ? fill : none;
    }

    bool LabCanvas::startScrub(const std::string& key) {
        if (hover_.shape < 0 || key.size() != 1) {
            return false;
        }
        for (const Prop& prop : propsFor(hover_.part)) {
            if (prop.key == key[0]) {
                scrub_ = Scrub{};
                scrub_.active = true;
                scrub_.shape = hover_.shape;
                scrub_.part = hover_.part;
                scrub_.prop = &prop;
                scrub_.key = key;
                scrub_.anchor = BLPoint(double(pointer_.x), double(pointer_.y));
                scrub_.start = scrubTravel();
                scrub_.startValue = shapes_[std::size_t(scrub_.shape)].*prop.field;
                selected_ = scrub_.shape;
                reportHint();
                style().markDirty();
                return true;
            }
        }
        return false;
    }

    double LabCanvas::scrubTravel() const {
        return scrubAxis_ == ScrubAxis::Vertical ? -double(pointer_.y) : double(pointer_.x);
    }

    void LabCanvas::updateScrub() {
        const Prop& prop = *scrub_.prop;
        double value = scrub_.startValue + (scrubTravel() - scrub_.start) * prop.rate;
        if (shiftDown_) {
            value = *std::min_element(prop.snaps.begin(), prop.snaps.end(), [&](double a, double b) { return std::fabs(a - value) < std::fabs(b - value); });
        }
        shapes_[std::size_t(scrub_.shape)].*prop.field = std::clamp(value, prop.min, prop.max);
    }

    void LabCanvas::endScrub(bool keep) {
        if (!scrub_.active) {
            return;
        }
        if (keep) {
            used(("scrub." + scrub_.key).c_str());
        } else {
            shapes_[std::size_t(scrub_.shape)].*scrub_.prop->field = scrub_.startValue;
        }
        scrub_.active = false;
        say(keep ? "Kept" : "Cancelled");
    }

    int LabCanvas::shapeAt(const BLPoint& world) const {
        for (int i = int(shapes_.size()) - 1; i >= 0; --i) {
            const Shape& shape = shapes_[std::size_t(i)];
            const BLBox box = boxOf(shape.a, shape.b);
            const double slack = 4.0 / zoom_;
            if (world.x >= box.x0 - slack && world.x <= box.x1 + slack && world.y >= box.y0 - slack && world.y <= box.y1 + slack) {
                return i;
            }
        }
        return -1;
    }

    int LabCanvas::boardAt(const BLPoint& world) const {
        for (int i = int(boards_.size()) - 1; i >= 0; --i) {
            const BLRect& r = boards_[std::size_t(i)].rect;
            if (world.x >= r.x && world.x <= r.x + r.w && world.y >= r.y && world.y <= r.y + r.h) {
                return i;
            }
        }
        return -1;
    }

    LabCanvas::Shape LabCanvas::draftShape(bool constrain, bool fromCenter) const {
        Shape shape;
        shape.kind = drawKind_;
        shape.sides = drawSides_;
        BLPoint a = drawStart_, b = drawNow_;
        double dx = b.x - a.x, dy = b.y - a.y;
        if (constrain && drawKind_ != Kind::Line) {
            const double m = std::max(std::fabs(dx), std::fabs(dy));
            dx = dx < 0 ? -m : m;
            dy = dy < 0 ? -m : m;
        }
        if (fromCenter && drawKind_ != Kind::Line) {
            shape.a = BLPoint(a.x - dx, a.y - dy);
            shape.b = BLPoint(a.x + dx, a.y + dy);
        } else {
            shape.a = a;
            shape.b = BLPoint(a.x + dx, a.y + dy);
        }
        return shape;
    }

    void LabCanvas::addBoard(const std::string& name, double width, double height, const BLPoint* at) {
        Board board;
        board.name = name;
        board.rect = at != nullptr ? BLRect(at->x, at->y, width, height) : BLRect(0.0, 0.0, width, height);
        if (at == nullptr && !boards_.empty()) {
            const BLRect& last = boards_.back().rect;
            board.rect.x = last.x + last.w + 40.0;
        }
        boards_.push_back(board);
    }

    void LabCanvas::fitBoards() {
        if (boards_.empty()) {
            return;
        }
        BLBox all(boards_[0].rect.x, boards_[0].rect.y, boards_[0].rect.x + boards_[0].rect.w, boards_[0].rect.y + boards_[0].rect.h);
        for (const Board& b : boards_) {
            all.x0 = std::min(all.x0, b.rect.x);
            all.y0 = std::min(all.y0, b.rect.y);
            all.x1 = std::max(all.x1, b.rect.x + b.rect.w);
            all.y1 = std::max(all.y1, b.rect.y + b.rect.h);
        }
        const Rect client = getClientBounds();
        const double w = double(client.width()) - kRuler - 80.0, h = double(client.height()) - kRuler - 80.0;
        zoom_ = std::clamp(std::min(w / (all.x1 - all.x0), h / (all.y1 - all.y0)), 0.05, 1.5);
        panX_ = kRuler + (double(client.width()) - kRuler - (all.x1 - all.x0) * zoom_) * 0.5 - all.x0 * zoom_;
        panY_ = kRuler + (double(client.height()) - kRuler - (all.y1 - all.y0) * zoom_) * 0.5 - all.y0 * zoom_;
    }

    // ---- Messages to the hint bar ----

    void LabCanvas::say(const std::string& text) {
        message_ = text;
        reportHint();
        style().markDirty();
    }

    bool LabCanvas::fresh(const char* feature) const {
        const auto it = uses_.find(feature);
        return it == uses_.end() || it->second < 3;
    }

    void LabCanvas::used(const char* feature) {
        ++uses_[feature];
    }

    std::string LabCanvas::idleHint() const {
        std::vector<std::string> parts;
        if (hover_.shape >= 0) {
            std::string text = hover_.part == Part::Stroke ? "Outline: hold" : "Fill: hold";
            for (const Prop& prop : propsFor(hover_.part)) {
                text += std::string(" ") + prop.key + " " + prop.name;
            }
            return text + (scrubAxis_ == ScrubAxis::Vertical ? " and move up/down" : " and move sideways");
        }
        if (selected_ >= 0) {
            if (fresh("duplicate")) { parts.push_back("D duplicate"); }
            if (fresh("delete")) { parts.push_back("Del delete"); }
            if (fresh("rightClick")) { parts.push_back("right-click or pen button: object ring"); }
        } else {
            if (fresh("holdDraw")) { parts.push_back("hold R E P S L and drag to draw"); }
            if (fresh("pan")) { parts.push_back("hold Space and drag to pan"); }
        }
        if (fresh("ring")) { parts.push_back("tap Space for the menu"); }
        if (parts.empty()) {
            return "Space menu";
        }
        std::string text = parts[0];
        for (std::size_t i = 1; i < parts.size(); ++i) {
            text += " - " + parts[i];
        }
        return text;
    }

    void LabCanvas::reportHint() {
        std::string keys;
        if (!hasDocument()) {
            keys = message_;
        } else if (scrub_.active) {
            char value[48];
            std::snprintf(value, sizeof(value), "%.*f%s", scrub_.prop->digits, shapes_[std::size_t(scrub_.shape)].*scrub_.prop->field, scrub_.prop->unit);
            keys = std::string(scrub_.part == Part::Stroke ? "Outline " : "Fill ") + scrub_.prop->name + " " + value + (scrubAxis_ == ScrubAxis::Vertical ? " - move up/down" : " - move sideways") + " - Shift snap - Esc cancel";
        } else if (spaceDown_) {
            keys = "drag to pan, or release for the menu";
        } else if (drawing_) {
            keys = "Shift square - Alt from center" + std::string(drawKind_ == Kind::Polygon || drawKind_ == Kind::Star ? " - Up/Down sides: " + std::to_string(drawSides_) : "");
        } else if (tool_ != "Select") {
            keys = heldToolKey_.empty() ? "drag to draw - Esc to cancel" : "drag to draw - Shift square - Alt from center - release " + heldToolKey_ + " to stop";
        } else {
            keys = idleHint();
        }
        char info[96];
        if (pointerKnown_ && hasDocument()) {
            const BLPoint w = toWorld(pointer_);
            std::snprintf(info, sizeof(info), "x %.0f  y %.0f  %.0f%%", w.x, w.y, zoom_ * 100.0);
        } else {
            info[0] = '\0';
        }
        onHint(*this, hasDocument() ? tool_ : std::string("Nothing open"), hasDocument() && !message_.empty() && !drawing_ && tool_ == "Select" && selected_ < 0 && !spaceDown_ ? message_ + "  |  " + keys : keys, info);
    }

    // ---- Rings ----

    Point LabCanvas::ringCenter() const {
        if (pointerKnown_) {
            return pointer_;
        }
        const Rect client = getClientBounds();
        return Point(client.width() * 0.5f, client.height() * 0.5f);
    }

    void LabCanvas::openRing(bool byPress) {
        const Point at = ringCenter();
        const BLPoint world = toWorld(at);
        curvy::RadialRing ring;
        if (!hasDocument()) {
            ring = newRing();
        } else if (shapeAt(world) >= 0) {
            selected_ = shapeAt(world);
            ring = objectRing();
        } else if (boardAt(world) >= 0) {
            ring = createRing();
        } else {
            ring = documentRing();
        }
        hover_ = Hover{};
        used("ring");
        if (byPress) { used("rightClick"); }
        menu_->open(ring, at, byPress);
        say("Ring: " + ring.title);
    }

    SyncReturn LabCanvas::handleCommand(curvy::RadialMenu&, const std::string& command) {
        const Point at = ringCenter();
        auto toolCommand = [&](const char* tool) {
            tool_ = tool;
            toolSticky_ = true;
            say(std::string(tool) + " tool armed");
        };
        auto board = [&](const char* name, double w, double h) {
            boards_.clear();
            shapes_.clear();
            selected_ = -1;
            addBoard(name, w, h, nullptr);
            fitBoards();
            onDocumentName(*this, std::string("Untitled - ") + name);
            say(std::string("New ") + name);
        };
        if (command == "new.letter") { board("Letter", 612, 792); }
        else if (command == "new.a4") { board("A4", 595, 842); }
        else if (command == "new.fullhd") { board("Full HD", 1920, 1080); }
        else if (command == "new.square") { board("Square 1080", 1080, 1080); }
        else if (command == "tool.rect") { toolCommand("Rectangle"); }
        else if (command == "tool.ellipse") { toolCommand("Ellipse"); }
        else if (command == "tool.polygon") { toolCommand("Polygon"); }
        else if (command == "tool.star") { toolCommand("Star"); }
        else if (command == "tool.line") { toolCommand("Line"); }
        else if (command == "ring.document") { menu_->open(documentRing(), at, false); say("Ring: Document"); }
        else if (command == "ring.new") { menu_->open(newRing(), at, false); say("Ring: New"); }
        else if (command == "doc.newArtboard") {
            const BLPoint world = toWorld(at);
            addBoard("Artboard " + std::to_string(boards_.size() + 1), 400, 300, &world);
            say("New artboard at the pen");
        }
        else if (command == "doc.fit") { fitBoards(); say("Fit"); }
        else if (command == "doc.rulers") { showRulers_ = !showRulers_; say(showRulers_ ? "Rulers on" : "Rulers off"); }
        else if (command == "obj.duplicate" && selected_ >= 0) {
            Shape copy = shapes_[std::size_t(selected_)];
            used("duplicate");
            copy.a.x += 20; copy.a.y += 20; copy.b.x += 20; copy.b.y += 20;
            shapes_.push_back(copy);
            selected_ = int(shapes_.size()) - 1;
            say("Duplicated");
        }
        else if (command == "obj.delete" && selected_ >= 0) {
            used("delete");
            shapes_.erase(shapes_.begin() + selected_);
            selected_ = -1;
            say("Deleted");
        }
        else if (command.compare(0, 7, "dialog.") == 0) { say("Dialog would open: " + command.substr(7) + " (the hub)"); }
        else { say("(" + command + " is not in the lab)"); }
        return SyncReturn::Handled;
    }

    // ---- Mouse ----

    void LabCanvas::notePen() {
        if (rootView() != nullptr) {
            pen_ = rootView()->penState();
            penSeen_ = penSeen_ || pen_.inRange;
        }
    }

    bool LabCanvas::eraseUnderPen(const BLPoint& world) {
        const Hover hit = contextAt(world);
        if (hit.shape < 0) {
            return false;
        }
        shapes_.erase(shapes_.begin() + hit.shape);
        selected_ = -1;
        hover_ = Hover{};
        say("Erased with the pen's eraser");
        return true;
    }

    SyncReturn LabCanvas::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (rootView() != nullptr) {
            rootView()->setFocusedSubView(this);
        }
        notePen();
        pointer_ = pt;
        pointerKnown_ = true;
        shiftDown_ = (keys & kmShift) != 0;
        altDown_ = (keys & kmAlt) != 0;
        if ((buttons & mbmRightButton) != 0) {
            openRing(true);
            return SyncReturn::Handled;
        }
        if ((buttons & mbmLeftButton) == 0) {
            return SyncReturn::Ignored;
        }
        if (!hasDocument()) {
            say("Tap Space to start");
            return SyncReturn::Handled;
        }
        if (pen_.eraser && pen_.inContact) {
            eraseUnderPen(toWorld(pt));
            return SyncReturn::Handled;
        }
        if (spaceDown_) {
            panning_ = true;
            used("pan");
            spaceUsed_ = true;
            panFrom_ = BLPoint(pt.x, pt.y);
            return SyncReturn::Handled;
        }
        if (tool_ != "Select") {
            drawing_ = true;
            drawKind_ = tool_ == "Ellipse" ? Kind::Ellipse : tool_ == "Polygon" ? Kind::Polygon : tool_ == "Star" ? Kind::Star : tool_ == "Line" ? Kind::Line : Kind::Rect;
            drawSides_ = drawKind_ == Kind::Polygon ? 6 : 5;
            drawStart_ = drawNow_ = toWorld(pt);
            drawPressure_ = pen_.inContact && pen_.hasPressure ? double(pen_.pressure) : 0.0;
            reportHint();
            return SyncReturn::Handled;
        }
        selected_ = shapeAt(toWorld(pt));
        reportHint();
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn LabCanvas::handleMouseMove(View&, const Point& pt, std::uint32_t, std::uint32_t keys) {
        notePen();
        if (drawing_ && pen_.inContact && pen_.hasPressure) {
            drawPressure_ = std::max(drawPressure_, double(pen_.pressure));
        }
        if (pen_.eraser && pen_.inContact && hasDocument() && !drawing_) {
            eraseUnderPen(toWorld(pt));
        }
        pointer_ = pt;
        pointerKnown_ = true;
        shiftDown_ = (keys & kmShift) != 0;
        altDown_ = (keys & kmAlt) != 0;
        if (panning_) {
            panX_ += double(pt.x) - panFrom_.x;
            panY_ += double(pt.y) - panFrom_.y;
            panFrom_ = BLPoint(pt.x, pt.y);
        } else if (drawing_) {
            drawNow_ = toWorld(pt);
        }
        if (scrub_.active) {
            updateScrub();
        } else if (hasDocument() && !panning_ && !drawing_ && !spaceDown_ && !menu_->isOpen() && tool_ == "Select") {
            hover_ = contextAt(toWorld(pt));
        } else {
            hover_ = Hover{};
        }
        reportHint();
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn LabCanvas::handleMouseUp(View&, const Point&, std::uint32_t, std::uint32_t) {
        if (panning_) {
            panning_ = false;
        }
        if (drawing_) {
            finishDraw(true);
        }
        return SyncReturn::Handled;
    }

    void LabCanvas::finishDraw(bool commit) {
        if (drawing_ && commit) {
            Shape shape = draftShape(shiftDown_, altDown_);
            if (drawPressure_ > 0.0) {
                shape.strokeW = 0.5 + drawPressure_ * 12.0;
            }
            const BLBox box = boxOf(shape.a, shape.b);
            if ((box.x1 - box.x0) * zoom_ >= 4.0 || (box.y1 - box.y0) * zoom_ >= 4.0) {
                shapes_.push_back(shape);
                selected_ = int(shapes_.size()) - 1;
                if (!heldToolKey_.empty()) { used("holdDraw"); }
                say("Created " + tool_);
            }
        }
        drawing_ = false;
        if (!toolSticky_ && heldToolKey_.empty()) {
            tool_ = "Select";
        }
        reportHint();
        style().markDirty();
    }

    // ---- Keys ----

    std::string LabCanvas::keyName(std::uint32_t vk) {
        if (vk >= vkLetterA && vk <= vkLetterZ) {
            return std::string(1, char('A' + (vk - vkLetterA)));
        }
        if (vk >= vkNumber0 && vk <= vkNumber9) {
            return std::string(1, char('0' + (vk - vkNumber0)));
        }
        return std::string();
    }

    SyncReturn LabCanvas::handleKeyDown(View&, std::uint32_t keyMask, int, int repeatCount, std::uint32_t vk) {
        shiftDown_ = (keyMask & kmShift) != 0;
        altDown_ = (keyMask & kmAlt) != 0;
        if (scrub_.active) {
            if (vk == vkEscape) {
                endScrub(false);
            }
            return SyncReturn::Handled;
        }
        if (menu_->isOpen()) {
            if (vk == vkEscape || vk == vkSpaceBar) {
                menu_->handleEscape();
                say("Ring closed");
            } else {
                menu_->handleKey(keyName(vk));
            }
            return SyncReturn::Handled;
        }
        if (vk == vkSpaceBar) {
            if (!spaceDown_) {
                spaceDown_ = true;
                spaceUsed_ = false;
                spaceStart_ = std::chrono::steady_clock::now();
                reportHint();
            }
            return SyncReturn::Handled;
        }
        if (spaceDown_) {
            spaceUsed_ = true;
        }
        if (vk == vkEscape) {
            if (drawing_) {
                finishDraw(false);
            }
            tool_ = "Select";
            toolSticky_ = false;
            selected_ = -1;
            say("Select");
            return SyncReturn::Handled;
        }
        if (drawing_ && (vk == vkUpArrow || vk == vkDownArrow) && (drawKind_ == Kind::Polygon || drawKind_ == Kind::Star)) {
            drawSides_ = std::clamp(drawSides_ + (vk == vkUpArrow ? 1 : -1), 3, 12);
            reportHint();
            style().markDirty();
            return SyncReturn::Handled;
        }
        if (hasDocument() && !drawing_ && !spaceDown_ && repeatCount <= 1 && startScrub(keyName(vk))) {
            return SyncReturn::Handled;
        }
        if (vk == vkDelete && selected_ >= 0) {
            used("delete");
            shapes_.erase(shapes_.begin() + selected_);
            selected_ = -1;
            say("Deleted");
            return SyncReturn::Handled;
        }
        if (!hasDocument() || drawing_ || repeatCount > 1) {
            return SyncReturn::Ignored;
        }
        const std::string key = keyName(vk);
        const char* tool = key == "R" ? "Rectangle" : key == "E" ? "Ellipse" : key == "P" ? "Polygon" : key == "S" ? "Star" : key == "L" ? "Line" : nullptr;
        if (tool != nullptr) {
            tool_ = tool;
            toolSticky_ = false;
            heldToolKey_ = key;
            reportHint();
            return SyncReturn::Handled;
        }
        if (key == "D" && selected_ >= 0) {
            handleCommand(*menu_, "obj.duplicate");
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    SyncReturn LabCanvas::handleKeyUp(View&, std::uint32_t keyMask, int, int, std::uint32_t vk) {
        shiftDown_ = (keyMask & kmShift) != 0;
        altDown_ = (keyMask & kmAlt) != 0;
        if (scrub_.active) {
            if (keyName(vk) == scrub_.key) {
                endScrub(true);
            }
            return SyncReturn::Handled;
        }
        if (vk == vkSpaceBar && spaceDown_) {
            spaceDown_ = false;
            panning_ = false;
            const bool tap = !spaceUsed_ && std::chrono::steady_clock::now() - spaceStart_ < kTapLimit;
            if (tap) {
                openRing(false);
            } else {
                reportHint();
            }
            return SyncReturn::Handled;
        }
        const std::string key = keyName(vk);
        if (!key.empty() && key == heldToolKey_) {
            heldToolKey_.clear();
            if (!drawing_ && !toolSticky_) {
                tool_ = "Select";
            }
            reportHint();
        }
        return SyncReturn::Handled;
    }

    // ---- Painting ----

    void LabCanvas::paintRulers(BLContext& ctx, const Rect& client) const {
        const double width = client.width(), height = client.height();
        ctx.set_fill_style(rgb(0x26272B));
        ctx.fill_rect(BLRect(0.0, 0.0, width, kRuler));
        ctx.fill_rect(BLRect(0.0, 0.0, kRuler, height));
        ctx.set_fill_style(rgb(0x1E1F22));
        ctx.fill_rect(BLRect(0.0, 0.0, kRuler, kRuler));

        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(9.0f);
        BLFont* blFont = font.blFont();
        double step = 10.0;
        for (double candidate : {10.0, 50.0, 100.0, 500.0, 1000.0}) {
            step = candidate;
            if (candidate * zoom_ >= 40.0) { break; }
        }
        ctx.set_stroke_style(rgb(0x9A9EA8));
        ctx.set_stroke_width(1.0);
        ctx.set_fill_style(rgb(0x9A9EA8));
        BLPath ticks;
        for (long i = long(std::floor((kRuler - panX_) / zoom_ / step)); ; ++i) {
            const double x = std::floor(double(i) * step * zoom_ + panX_) + 0.5;
            if (x > width) { break; }
            if (x < kRuler) { continue; }
            ticks.move_to(x, kRuler - 7.0);
            ticks.line_to(x, kRuler);
            if (blFont != nullptr && blFont->is_valid()) {
                const std::string label = std::to_string(long(double(i) * step));
                ctx.fill_utf8_text(BLPoint(x + 2.0, 9.0), *blFont, label.c_str(), label.size());
            }
        }
        for (long i = long(std::floor((kRuler - panY_) / zoom_ / step)); ; ++i) {
            const double y = std::floor(double(i) * step * zoom_ + panY_) + 0.5;
            if (y > height) { break; }
            if (y < kRuler) { continue; }
            ticks.move_to(kRuler - 7.0, y);
            ticks.line_to(kRuler, y);
            if (blFont != nullptr && blFont->is_valid()) {
                const std::string label = std::to_string(long(double(i) * step));
                ctx.save();
                ctx.translate(9.0, y - 3.0);
                ctx.rotate(-1.5707963267948966);
                ctx.fill_utf8_text(BLPoint(0.0, 0.0), *blFont, label.c_str(), label.size());
                ctx.restore();
            }
        }
        ctx.stroke_path(ticks);
        if (pointerKnown_) {
            ctx.set_fill_style(rgb(0xFFB454));
            BLPath marks;
            marks.move_to(double(pointer_.x) - 4.0, kRuler);
            marks.line_to(double(pointer_.x) + 4.0, kRuler);
            marks.line_to(double(pointer_.x), kRuler - 6.0);
            marks.close();
            marks.move_to(kRuler, double(pointer_.y) - 4.0);
            marks.line_to(kRuler, double(pointer_.y) + 4.0);
            marks.line_to(kRuler - 6.0, double(pointer_.y));
            marks.close();
            ctx.fill_path(marks);
        }
    }

    void LabCanvas::paintIcon(BLContext& ctx, char key, double cx, double cy, double hue) {
        const double r = 6.0;
        ctx.set_stroke_style(rgb(0xE6E7EA));
        ctx.set_stroke_width(1.4);
        ctx.set_fill_style(rgb(0xE6E7EA));
        switch (key) {
            case 'W': {
                BLPath path;
                path.move_to(cx - r, cy);
                path.line_to(cx + r, cy);
                path.move_to(cx - r, cy);
                path.line_to(cx - r + 3.0, cy - 3.0);
                path.move_to(cx - r, cy);
                path.line_to(cx - r + 3.0, cy + 3.0);
                path.move_to(cx + r, cy);
                path.line_to(cx + r - 3.0, cy - 3.0);
                path.move_to(cx + r, cy);
                path.line_to(cx + r - 3.0, cy + 3.0);
                ctx.stroke_path(path);
                break;
            }
            case 'O':
                ctx.stroke_circle(BLCircle(cx, cy, r));
                ctx.save();
                ctx.clip_to_rect(BLRect(cx - r, cy - r, r, 2.0 * r));
                ctx.fill_circle(BLCircle(cx, cy, r));
                ctx.restore();
                break;
            case 'H':
                ctx.set_fill_style(hsla(hue, 0.7, 0.55, 1.0));
                ctx.fill_circle(BLCircle(cx, cy, r));
                break;
            default: {
                ctx.fill_circle(BLCircle(cx, cy, r * 0.5));
                BLPath rays;
                for (int i = 0; i < 8; ++i) {
                    const double angle = kPi * double(i) / 4.0;
                    rays.move_to(cx + std::cos(angle) * r * 0.8, cy + std::sin(angle) * r * 0.8);
                    rays.line_to(cx + std::cos(angle) * r * 1.2, cy + std::sin(angle) * r * 1.2);
                }
                ctx.stroke_path(rays);
                break;
            }
        }
    }

    void LabCanvas::paintScrubCard(BLContext& ctx) const {
        const Prop& prop = *scrub_.prop;
        const Shape& shape = shapes_[std::size_t(scrub_.shape)];
        const double value = shape.*prop.field;
        char valueText[48];
        std::snprintf(valueText, sizeof(valueText), "%.*f%s", prop.digits, value, prop.unit);

        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(12.0f);
        font.setBold(true);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }
        const Rect client = getClientBounds();
        const double w = 250.0, h = 58.0;
        const double x = std::clamp(scrub_.anchor.x - w * 0.5, 8.0, std::max(8.0, double(client.width()) - w - 8.0));
        double y = scrub_.anchor.y - 24.0 - h;
        if (y < kRuler + 8.0) {
            y = scrub_.anchor.y + 24.0;
        }
        ctx.set_fill_style(BLRgba32(0xF217181Bu));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, 10.0));
        ctx.set_stroke_style(rgb(0x5A5D66));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(x + 0.5, y + 0.5, w - 1.0, h - 1.0, 10.0));

        const bool named = fresh(("scrub." + scrub_.key).c_str());
        ctx.set_fill_style(rgb(0xE6E7EA));
        if (named) {
            const std::string title = std::string(scrub_.part == Part::Stroke ? "Outline - " : "Fill - ") + prop.name;
            ctx.fill_utf8_text(BLPoint(x + 12.0, y + 22.0), *blFont, title.c_str(), title.size());
        } else {
            ctx.set_fill_style(rgb(0x3A3D43));
            ctx.fill_round_rect(BLRoundRect(x + 10.0, y + 7.0, 44.0, 22.0, 6.0));
            paintIcon(ctx, prop.key, x + 24.0, y + 18.0, shape.fillHue);
            ctx.set_fill_style(rgb(0xE6E7EA));
            ctx.fill_utf8_text(BLPoint(x + 36.0, y + 22.0), *blFont, scrub_.key.c_str(), scrub_.key.size());
        }
        const float valueWidth = font.measureText(valueText).width;
        ctx.set_fill_style(rgb(0xFFB454));
        ctx.fill_utf8_text(BLPoint(x + w - 12.0 - double(valueWidth), y + 22.0), *blFont, valueText, std::char_traits<char>::length(valueText));

        const double fraction = (value - prop.min) / (prop.max - prop.min);
        const double tx = x + 16.0, tw = w - 32.0, ty = y + h - 18.0;
        ctx.set_fill_style(rgb(0x3A3D43));
        ctx.fill_round_rect(BLRoundRect(tx, ty, tw, 6.0, 3.0));
        ctx.set_fill_style(rgb(0xFFB454));
        ctx.fill_round_rect(BLRoundRect(tx, ty, std::max(6.0, tw * fraction), 6.0, 3.0));
        ctx.set_fill_style(rgb(0x17181B));
        ctx.fill_circle(BLCircle(tx + tw * fraction, ty + 3.0, 9.0));
        ctx.set_fill_style(rgb(0xFFB454));
        ctx.fill_circle(BLCircle(tx + tw * fraction, ty + 3.0, 7.0));
    }

    void LabCanvas::paintPenReadout(BLContext& ctx) const {
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(11.0f);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }
        const Rect client = getClientBounds();
        const double w = 190.0, h = 66.0;
        const double x = double(client.width()) - w - 10.0, y = double(client.height()) - h - 10.0;
        ctx.set_fill_style(BLRgba32(0xE617181Bu));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, 8.0));
        ctx.set_stroke_style(rgb(0x5A5D66));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(x + 0.5, y + 0.5, w - 1.0, h - 1.0, 8.0));

        std::string status = !penSeen_ ? "No pen seen yet" : pen_.eraser ? (pen_.inContact ? "Eraser touching" : "Eraser hovering")
                           : pen_.inContact ? "Pen touching" : pen_.inRange ? "Pen hovering" : "Pen out of range";
        ctx.set_fill_style(penSeen_ && pen_.inRange ? rgb(0xFFB454) : rgb(0x9A9EA8));
        ctx.fill_utf8_text(BLPoint(x + 10.0, y + 17.0), *blFont, status.c_str(), status.size());

        ctx.set_fill_style(rgb(0x3A3D43));
        ctx.fill_round_rect(BLRoundRect(x + 10.0, y + 26.0, w - 20.0, 6.0, 3.0));
        if (pen_.hasPressure || pen_.inContact) {
            ctx.set_fill_style(rgb(0xFFB454));
            ctx.fill_round_rect(BLRoundRect(x + 10.0, y + 26.0, std::max(4.0, (w - 20.0) * double(pen_.pressure)), 6.0, 3.0));
        }
        char line[96];
        std::snprintf(line, sizeof(line), "pressure %s%.2f   tilt %d, %d", pen_.hasPressure ? "" : "n/a ", double(pen_.pressure), pen_.tiltX, pen_.tiltY);
        ctx.set_fill_style(rgb(0xE6E7EA));
        ctx.fill_utf8_text(BLPoint(x + 10.0, y + 47.0), *blFont, line, std::char_traits<char>::length(line));
        const std::string buttons = std::string("barrel ") + (pen_.barrel ? "down" : "up") + "   twist " + std::to_string(pen_.twist);
        ctx.set_fill_style(rgb(0x9A9EA8));
        ctx.fill_utf8_text(BLPoint(x + 10.0, y + 61.0), *blFont, buttons.c_str(), buttons.size());
    }

    void LabCanvas::paintHoverChip(BLContext& ctx) const {
        const std::vector<Prop>& props = propsFor(hover_.part);
        if (props.empty()) {
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(12.0f);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }
        bool named = false;
        for (const Prop& prop : props) {
            named = named || fresh((std::string("scrub.") + prop.key).c_str());
        }
        std::string text = hover_.part == Part::Stroke ? "Outline" : "Fill";
        for (const Prop& prop : props) {
            text += std::string("   ") + prop.key + " " + prop.name;
        }
        const double w = named ? double(font.measureText(text).width) + 20.0 : 12.0 + 38.0 * double(props.size());
        const double h = 28.0;
        const Rect client = getClientBounds();
        double x = double(pointer_.x) - 24.0 - w;
        double y = double(pointer_.y) - 24.0 - h;
        if (x < kRuler + 4.0) { x = double(pointer_.x) + 24.0; }
        if (y < kRuler + 4.0) { y = double(pointer_.y) + 24.0; }
        x = std::min(x, double(client.width()) - w - 4.0);
        ctx.set_fill_style(BLRgba32(0xF217181Bu));
        ctx.fill_round_rect(BLRoundRect(x, y, w, h, 8.0));
        ctx.set_stroke_style(rgb(0x5A5D66));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(x + 0.5, y + 0.5, w - 1.0, h - 1.0, 8.0));
        ctx.set_fill_style(rgb(0xE6E7EA));
        if (named) {
            ctx.fill_utf8_text(BLPoint(x + 10.0, y + 18.0), *blFont, text.c_str(), text.size());
            return;
        }
        double cx = x + 18.0;
        for (const Prop& prop : props) {
            paintIcon(ctx, prop.key, cx, y + h * 0.5, shapes_[std::size_t(hover_.shape)].fillHue);
            ctx.set_fill_style(rgb(0xE6E7EA));
            ctx.fill_utf8_text(BLPoint(cx + 11.0, y + 18.0), *blFont, &prop.key, 1);
            cx += 38.0;
        }
    }

    void LabCanvas::paint(BLContext& ctx) {
        const Rect client = getClientBounds();
        ctx.set_fill_style(rgb(0x2B2D31));
        ctx.fill_rect(BLRect(0.0, 0.0, client.width(), client.height()));

        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(13.0f);
        BLFont* blFont = font.blFont();
        if (!hasDocument()) {
            if (blFont != nullptr && blFont->is_valid()) {
                const std::string text = "Tap Space to start";
                const float w = font.measureText(text).width;
                ctx.set_fill_style(rgb(0x9A9EA8));
                ctx.fill_utf8_text(BLPoint(double(client.width() - w) * 0.5, double(client.height()) * 0.5), *blFont, text.c_str(), text.size());
            }
        } else {
            ctx.save();
            ctx.translate(panX_, panY_);
            ctx.scale(zoom_);
            for (const Board& board : boards_) {
                ctx.set_fill_style(rgb(0xFFFFFF));
                ctx.fill_rect(board.rect);
            }
            for (std::size_t i = 0; i < shapes_.size(); ++i) {
                const Shape& shape = shapes_[i];
                const BLPath path = outline(shape);
                if (shape.kind != Kind::Line) {
                    ctx.set_fill_style(hsla(shape.fillHue, 0.8, shape.fillL / 100.0, shape.fillOp / 100.0));
                    ctx.fill_path(path);
                }
                ctx.set_stroke_style(hsla(shape.strokeHue, 0.65, 0.42, shape.strokeOp / 100.0));
                ctx.set_stroke_width(shape.strokeW);
                ctx.stroke_path(path);
            }
            const Hover target = scrub_.active ? Hover{scrub_.shape, scrub_.part} : hover_;
            if (target.shape >= 0 && target.shape < int(shapes_.size())) {
                const Shape& shape = shapes_[std::size_t(target.shape)];
                const BLPath path = outline(shape);
                if (target.part == Part::Fill) {
                    ctx.set_fill_style(BLRgba32(0x38FFB454u));
                    ctx.fill_path(path);
                } else {
                    ctx.set_stroke_style(BLRgba32(0xA0FFB454u));
                    ctx.set_stroke_width(shape.strokeW + 6.0 / zoom_);
                    ctx.stroke_path(path);
                }
            }
            if (selected_ >= 0 && selected_ < int(shapes_.size())) {
                const Shape& shape = shapes_[std::size_t(selected_)];
                const BLBox box = boxOf(shape.a, shape.b);
                ctx.set_stroke_style(rgb(0x4C8DFF));
                ctx.set_stroke_width(1.0 / zoom_);
                ctx.stroke_rect(BLRect(box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0));
                ctx.set_fill_style(rgb(0xFFFFFF));
                const double h = 4.0 / zoom_;
                const double xs[3] = {box.x0, (box.x0 + box.x1) * 0.5, box.x1}, ys[3] = {box.y0, (box.y0 + box.y1) * 0.5, box.y1};
                for (int ix = 0; ix < 3; ++ix) {
                    for (int iy = 0; iy < 3; ++iy) {
                        if (ix == 1 && iy == 1) { continue; }
                        ctx.fill_rect(BLRect(xs[ix] - h, ys[iy] - h, 2 * h, 2 * h));
                        ctx.stroke_rect(BLRect(xs[ix] - h, ys[iy] - h, 2 * h, 2 * h));
                    }
                }
            }
            if (drawing_) {
                const Shape draft = draftShape(shiftDown_, altDown_);
                const BLPath path = outline(draft);
                ctx.set_fill_style(BLRgba32(0x554C8DFFu));
                if (draft.kind != Kind::Line) { ctx.fill_path(path); }
                ctx.set_stroke_style(rgb(0x4C8DFF));
                ctx.set_stroke_width(drawPressure_ > 0.0 ? 0.5 + drawPressure_ * 12.0 : 1.5 / zoom_);
                ctx.stroke_path(path);
            }
            ctx.restore();

            if (blFont != nullptr && blFont->is_valid()) {
                Font label = FontManager::getSystemFont(SystemUIFont::Message);
                label.setSize(11.0f);
                BLFont* blLabel = label.blFont();
                if (blLabel != nullptr && blLabel->is_valid()) {
                    ctx.set_fill_style(rgb(0x9A9EA8));
                    for (const Board& board : boards_) {
                        const BLPoint p = toView(BLPoint(board.rect.x, board.rect.y));
                        ctx.fill_utf8_text(BLPoint(p.x, p.y - 6.0), *blLabel, board.name.c_str(), board.name.size());
                    }
                }
            }
            if (showRulers_) {
                paintRulers(ctx, client);
            }
            if (scrub_.active) {
                paintScrubCard(ctx);
            } else if (hover_.shape >= 0) {
                paintHoverChip(ctx);
            }
        }
        paintPenReadout(ctx);
        // The ring (a child) paints after this.
    }

}
