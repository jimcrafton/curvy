#pragma once

#include <newui/subview.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // A wrapping grid of small color tiles - used for the Color panel's swatch row and (with
    // larger tiles) the Graphic Styles panel. Each swatch is any newui::Color::fromString() text.
    //
    // Paints the tiles, tracks which one is selected and fires onSwatchPicked on a click; the style panel
    // applies the picked color.
    // @reflect category=basic
    class SwatchGrid : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        SwatchGrid();
        virtual ~SwatchGrid() {}

        // @reflect collection add=addSwatch remove=removeSwatch
        const std::vector<std::string>& swatches() const { return swatches_; }
        void addSwatch(const std::string& color);
        void removeSwatch(const std::string& color);
        // Replaces every swatch at once (the selection is kept while it still points at one).
        void setSwatches(const std::vector<std::string>& colors);

        // Tiles per row. Always at least 1.
        std::size_t columns() const { return columns_; }
        void setColumns(std::size_t columns);

        float gap() const { return gap_; }
        void setGap(float gap);

        // Tiles painted by the owner instead of filled with a color (the Graphic Styles panel draws each style on a small
        // shape): `painter` is called with the tile's rectangle and index for each of `count` tiles; the grid still
        // draws the selection border. Replaces swatches() until cleared with an empty painter.
        typedef std::function<void(BLContext&, const newui::Rect&, std::size_t)> TilePainter;
        void setTilePainter(TilePainter painter, std::size_t count);
        // Number of tiles, whichever way they are painted.
        std::size_t tileCount() const { return painter_ ? painterCount_ : swatches_.size(); }
        // Height of the grid when it shows `count` tiles at the current width (square tiles, gaps between).
        float heightFor(std::size_t count) const;

        // Index into swatches(); swatches().size() (the default) means none.
        std::size_t selectedIndex() const { return selectedIndex_; }
        void setSelectedIndex(std::size_t index);

        // Fired when the user clicks a tile (selectedIndex() already points at it).
        newui::Delegate<SwatchGrid> onSwatchPicked;

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);

        std::vector<std::string> swatches_;
        TilePainter painter_;
        std::size_t painterCount_ = 0;
        std::size_t columns_ = 12;
        float gap_ = 3.0f;
        std::size_t selectedIndex_ = 0;
    };

}
