#include "curvy/gradientbar.h"

#include <newui/mouse_constants.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace curvy {

    using namespace newui;

    namespace {
        // "<offset> <color>" - an entry that doesn't parse is skipped rather than failing the
        // whole bar (a hand-edited .newui file may well contain a typo).
        bool parseStop(const std::string& text, GradientBar::Stop& out) {
            std::istringstream in(text);
            double offset = 0.0;
            std::string colorText;
            if (!(in >> offset >> colorText)) {
                return false;
            }
            Color color;
            if (!Color::fromString(colorText, color)) {
                return false;
            }
            out.offset = offset < 0.0 ? 0.0 : (offset > 1.0 ? 1.0 : offset);
            out.color = color;
            return true;
        }

        std::string formatStop(const GradientBar::Stop& stop) {
            BLRgba32 rgba = stop.color.toBLRgba32();
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.5f #%02x%02x%02x%02x", stop.offset,
                unsigned(rgba.r()), unsigned(rgba.g()), unsigned(rgba.b()), unsigned(rgba.a()));
            return buffer;
        }

        Color mixColors(const Color& a, const Color& b, double t) {
            float f = float(t);
            return Color(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f);
        }
    }

    GradientBar::GradientBar() {
        setName("GradientBar");
        setVisible(true);
        setDesiredSize(Size(0.0f, 28.0f));
        selectedStop_ = 0;
        onMouseDown.add(this, &GradientBar::handleMouseDown);
        onMouseMove.add(this, &GradientBar::handleMouseMove);
        onMouseUp.add(this, &GradientBar::handleMouseUp);
    }

    void GradientBar::addStop(const std::string& stop) {
        stops_.push_back(stop);
        style().markDirty();
    }

    void GradientBar::removeStop(const std::string& stop) {
        auto it = std::find(stops_.begin(), stops_.end(), stop);
        if (it != stops_.end()) {
            stops_.erase(it);
            style().markDirty();
        }
    }

    std::vector<GradientBar::Stop> GradientBar::parsedStops() const {
        std::vector<Stop> result;
        for (const std::string& text : stops_) {
            Stop stop;
            if (parseStop(text, stop)) {
                result.push_back(stop);
            }
        }
        std::stable_sort(result.begin(), result.end(), [](const Stop& a, const Stop& b) { return a.offset < b.offset; });
        return result;
    }

    void GradientBar::setStops(const std::vector<Stop>& stops) {
        stops_.clear();
        for (const Stop& stop : stops) {
            stops_.push_back(formatStop(stop));
        }
        style().markDirty();
    }

    void GradientBar::setSelectedStop(std::size_t index) {
        selectedStop_ = index;
        style().markDirty();
    }

    // The bar leaves room below for the markers; the margin keeps the first/last marker inside the view.
    bool GradientBar::metrics(Metrics& out) const {
        Rect client = getClientBounds();
        double margin = kMarkerSize * 0.5;
        out.barLeft = client.left() + margin;
        out.barWidth = client.width() - 2.0 * margin;
        out.barTop = client.top() + 2.0;
        out.barHeight = client.height() - kMarkerSize - 4.0;
        out.markerTop = out.barTop + out.barHeight + 2.0;
        return out.barWidth > 0.0 && out.barHeight > 0.0;
    }

    SyncReturn GradientBar::handleMouseDown(View& /*sender*/, const Point& pt, std::uint32_t buttons, std::uint32_t /*keys*/) {
        Metrics m;
        if ((buttons & mbmLeftButton) == 0 || !metrics(m)) {
            return SyncReturn::Ignored;
        }
        std::vector<Stop> stops = parsedStops();
        double margin = kMarkerSize * 0.5;

        // A marker under the pointer (the last one painted is on top)?
        for (std::size_t i = stops.size(); i > 0; --i) {
            double x = m.barLeft + stops[i - 1].offset * m.barWidth - margin;
            if (pt.x >= x && pt.x <= x + kMarkerSize && pt.y >= m.markerTop - 1.0 && pt.y <= m.markerTop + kMarkerSize + 1.0) {
                // The bar's own stop list is sorted by parsedStops(); store it that way so indices agree.
                setStops(stops);
                bool changed = selectedStop_ != i - 1;
                selectedStop_ = i - 1;
                dragging_ = true;
                style().markDirty();
                if (changed) {
                    onStopSelected(*this);
                }
                return SyncReturn::Handled;
            }
        }

        // Otherwise a click on the bar itself adds a stop there, colored as the gradient is at that point.
        if (pt.x >= m.barLeft && pt.x <= m.barLeft + m.barWidth && pt.y >= m.barTop && pt.y <= m.markerTop + kMarkerSize && stops.size() >= 2) {
            double offset = std::min(1.0, std::max(0.0, (double(pt.x) - m.barLeft) / m.barWidth));
            Stop added;
            added.offset = offset;
            added.color = stops.back().color;
            if (offset <= stops.front().offset) {
                added.color = stops.front().color;
            } else {
                for (std::size_t i = 1; i < stops.size(); ++i) {
                    if (offset <= stops[i].offset) {
                        double span = stops[i].offset - stops[i - 1].offset;
                        added.color = mixColors(stops[i - 1].color, stops[i].color, span > 1e-12 ? (offset - stops[i - 1].offset) / span : 0.0);
                        break;
                    }
                }
            }
            stops.push_back(added);
            std::stable_sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.offset < b.offset; });
            std::size_t index = 0;
            for (std::size_t i = 0; i < stops.size(); ++i) {
                if (stops[i].offset == added.offset) {
                    index = i;
                }
            }
            setStops(stops);
            selectedStop_ = index;
            dragging_ = true;
            onStopsEdited(*this);
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    SyncReturn GradientBar::handleMouseMove(View& /*sender*/, const Point& pt, std::uint32_t /*buttons*/, std::uint32_t /*keys*/) {
        Metrics m;
        if (!dragging_ || !metrics(m)) {
            return SyncReturn::Ignored;
        }
        std::vector<Stop> stops = parsedStops();
        if (selectedStop_ >= stops.size()) {
            return SyncReturn::Ignored;
        }
        double offset = std::min(1.0, std::max(0.0, (double(pt.x) - m.barLeft) / m.barWidth));
        if (offset == stops[selectedStop_].offset) {
            return SyncReturn::Handled;
        }
        stops[selectedStop_].offset = offset;
        // Keep the dragged stop selected as it passes its neighbours.
        std::size_t index = selectedStop_;
        while (index > 0 && stops[index].offset < stops[index - 1].offset) {
            std::swap(stops[index], stops[index - 1]);
            --index;
        }
        while (index + 1 < stops.size() && stops[index].offset > stops[index + 1].offset) {
            std::swap(stops[index], stops[index + 1]);
            ++index;
        }
        selectedStop_ = index;
        setStops(stops);
        onStopsEdited(*this);
        return SyncReturn::Handled;
    }

    SyncReturn GradientBar::handleMouseUp(View& /*sender*/, const Point& /*pt*/, std::uint32_t /*buttons*/, std::uint32_t /*keys*/) {
        if (!dragging_) {
            return SyncReturn::Ignored;
        }
        dragging_ = false;
        return SyncReturn::Handled;
    }

    void GradientBar::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }

        std::vector<Stop> stops = parsedStops();

        Metrics m;
        if (!metrics(m)) {
            return;
        }
        double margin = kMarkerSize * 0.5;

        ctx.save();
        if (stops.size() >= 2) {
            BLGradient gradient(BLLinearGradientValues(m.barLeft, 0.0, m.barLeft + m.barWidth, 0.0));
            for (const Stop& stop : stops) {
                gradient.add_stop(stop.offset, stop.color.toBLRgba32());
            }
            ctx.set_fill_style(gradient);
        } else if (stops.size() == 1) {
            ctx.set_fill_style(stops[0].color.toBLRgba32());
        } else {
            ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
        }
        ctx.fill_rect(BLRect(m.barLeft, m.barTop, m.barWidth, m.barHeight));
        ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(m.barLeft - 0.5, m.barTop - 0.5, m.barWidth + 1.0, m.barHeight + 1.0));

        // One marker per stop, in the stop's own color; the selected one gets an accent outline.
        for (std::size_t i = 0; i < stops.size(); ++i) {
            double x = m.barLeft + stops[i].offset * m.barWidth - margin;
            ctx.set_fill_style(stops[i].color.toBLRgba32());
            ctx.fill_rect(BLRect(x, m.markerTop, kMarkerSize, kMarkerSize));
            bool selected = (i == selectedStop_);
            ctx.set_stroke_style(selected
                ? UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32()
                : BLRgba32(0xFF333333u));
            ctx.set_stroke_width(selected ? 2.0 : 1.0);
            ctx.stroke_rect(BLRect(x, m.markerTop, kMarkerSize, kMarkerSize));
        }
        ctx.restore();
    }

}
