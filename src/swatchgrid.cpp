#include "curvy/swatchgrid.h"

#include <newui/color.h>
#include <newui/mouse_constants.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    SwatchGrid::SwatchGrid() {
        setName("SwatchGrid");
        setVisible(true);
        selectedIndex_ = 0;
        onMouseDown.add(this, &SwatchGrid::handleMouseDown);
    }

    SyncReturn SwatchGrid::handleMouseDown(View& /*sender*/, const Point& pt, std::uint32_t buttons, std::uint32_t /*keys*/) {
        if ((buttons & mbmLeftButton) == 0 || tileCount() == 0) {
            return SyncReturn::Ignored;
        }
        Rect client = getClientBounds();
        double tile = (double(client.width()) - double(gap_) * double(columns_ - 1)) / double(columns_);
        if (tile < 2.0) {
            return SyncReturn::Ignored;
        }
        double stride = tile + double(gap_);
        double col = std::floor((double(pt.x) - client.left()) / stride);
        double row = std::floor((double(pt.y) - client.top()) / stride);
        if (col < 0 || row < 0 || col >= double(columns_)) {
            return SyncReturn::Ignored;
        }
        std::size_t index = std::size_t(row) * columns_ + std::size_t(col);
        if (index >= tileCount()) {
            return SyncReturn::Ignored;
        }
        setSelectedIndex(index);
        onSwatchPicked(*this);
        return SyncReturn::Handled;
    }

    void SwatchGrid::addSwatch(const std::string& color) {
        swatches_.push_back(color);
        style().markDirty();
    }

    void SwatchGrid::setSwatches(const std::vector<std::string>& colors) {
        if (colors == swatches_) {
            return;
        }
        swatches_ = colors;
        if (selectedIndex_ > swatches_.size()) {
            selectedIndex_ = swatches_.size();
        }
        style().markDirty();
    }

    void SwatchGrid::removeSwatch(const std::string& color) {
        auto it = std::find(swatches_.begin(), swatches_.end(), color);
        if (it != swatches_.end()) {
            swatches_.erase(it);
            style().markDirty();
        }
    }

    void SwatchGrid::setTilePainter(TilePainter painter, std::size_t count) {
        painter_ = std::move(painter);
        painterCount_ = count;
        style().markDirty();
    }

    float SwatchGrid::heightFor(std::size_t count) const {
        Rect client = getClientBounds();
        double tile = (double(client.width()) - double(gap_) * double(columns_ - 1)) / double(columns_);
        if (tile < 2.0 || count == 0) {
            return 0.0f;
        }
        std::size_t rows = (count + columns_ - 1) / columns_;
        return float(double(rows) * tile + double(rows - 1) * double(gap_));
    }

    void SwatchGrid::setColumns(std::size_t columns) {
        if (columns == 0) {
            throw std::runtime_error("SwatchGrid::setColumns: columns must be at least 1");
        }
        columns_ = columns;
        style().markDirty();
    }

    void SwatchGrid::setGap(float gap) {
        gap_ = gap < 0.0f ? 0.0f : gap;
        style().markDirty();
    }

    void SwatchGrid::setSelectedIndex(std::size_t index) {
        selectedIndex_ = index;
        style().markDirty();
    }

    // Tiles are square, sized so `columns` of them (plus gaps) exactly span the view's width.
    void SwatchGrid::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f || tileCount() == 0) {
            return;
        }

        double tile = (double(client.width()) - double(gap_) * double(columns_ - 1)) / double(columns_);
        if (tile < 2.0) {
            return;
        }

        ctx.save();
        for (std::size_t i = 0; i < tileCount(); ++i) {
            double x = client.left() + double(i % columns_) * (tile + double(gap_));
            double y = client.top() + double(i / columns_) * (tile + double(gap_));
            if (y > double(client.top() + client.height())) {
                break;
            }
            if (painter_) {
                ctx.save();
                ctx.clip_to_rect(BLRect(x, y, tile, tile));
                painter_(ctx, Rect(float(x), float(y), float(tile), float(tile)), i);
                ctx.restore();
            } else {
                Color color;
                if (!Color::fromString(swatches_[i], color)) {
                    continue;
                }
                ctx.set_fill_style(color.toBLRgba32());
                ctx.fill_rect(BLRect(x, y, tile, tile));
            }
            bool selected = (i == selectedIndex_);
            ctx.set_stroke_style(selected
                ? UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32()
                : BLRgba32(0x59000000u));
            ctx.set_stroke_width(selected ? 2.0 : 1.0);
            ctx.stroke_rect(BLRect(x + 0.5, y + 0.5, tile - 1.0, tile - 1.0));
        }
        ctx.restore();
    }

}
