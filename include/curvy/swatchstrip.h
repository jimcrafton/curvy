#pragma once

#include <newui/controls.h>
#include <newui/subview.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // One swatch as the strip shows it: a colour (red, green, blue, alpha in 0..1) and an optional name.
    // @reflect ignore=true
    struct StripSwatch {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        std::string name;
        bool linked = false;   // drawn with a corner mark

        bool sameColor(const StripSwatch& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }
    };

    // A palette as the strip's list shows it. An automatic one (Recent) fills itself: its swatches cannot be added to, renamed, replaced, deleted or
    // reordered.
    // @reflect ignore=true
    struct StripPalette {
        std::string name;
        bool automatic = false;
    };

    // One line of a menu the strip shows (the buttons and a right-click build them; tests run them directly).
    // @reflect ignore=true
    struct StripMenuEntry {
        std::string text;
        bool enabled = true;
        bool checked = false;
        bool separator = false;
        std::function<void()> run;
    };

    // The wrapping grid of small colour tiles, with a "+" tile last. Paints them, follows the pointer for the hover, and turns presses into
    // requests: a click uses a swatch, a right-click asks for its menu, a drag over another tile asks to move it there. It knows nothing of documents.
    // @reflect ignore=true
    class SwatchTiles : public newui::SubView {
    public:
        static constexpr float kTile = 18.0f;
        static constexpr float kGap = 3.0f;
        // What a hit test says besides a swatch index.
        static constexpr int kNone = -1;

        SwatchTiles();

        void setSwatches(std::vector<StripSwatch> swatches);
        // The colour on show: the swatches that match it get an outline.
        void setCurrent(float r, float g, float b, float a);
        // False for an automatic palette: no drag to reorder, and the "+" tile is dimmed and does nothing.
        void setEditable(bool editable);
        bool editable() const { return editable_; }

        // The index of the swatch at this point, swatches().size() for the "+" tile, kNone elsewhere.
        int tileAt(const newui::Point& pt) const;
        newui::Point tileCenter(std::size_t index) const;
        std::size_t tileCount() const { return swatches_.size() + 1; }
        // How tall the grid is at this width.
        float heightFor(float width) const;
        std::size_t columnsFor(float width) const;
        const std::vector<StripSwatch>& swatches() const { return swatches_; }
        int hovered() const { return hover_; }

        std::function<void(int index)> onHover;                       // the swatch under the pointer, kNone when it leaves
        std::function<void(std::size_t index)> onUse;                 // a click on a swatch
        std::function<void()> onAdd;                                  // a click on the "+" tile
        std::function<void(std::size_t index)> onMenu;                // a right-click on a swatch
        std::function<void(std::size_t from, std::size_t to)> onMove; // a drag from one swatch onto another

        void paint(BLContext& ctx) override;

        // The gestures as the mouse handlers run them, so a test can drive them without a window.
        void pressTile(int index, bool right, const newui::Point& pt);
        void moveTo(const newui::Point& pt, bool buttonDown);
        void releaseAt(const newui::Point& pt);

    private:
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseLeft(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        void setHover(int index);

        std::vector<StripSwatch> swatches_;
        StripSwatch current_;
        bool editable_ = true;
        int hover_ = kNone;
        int pressed_ = kNone;
        newui::Point pressPoint_;
        bool dragging_ = false;
        int dropTarget_ = kNone;
    };

    // A small down-pointing triangle drawn in code (the button font has no arrow glyph). Sits over the right end of the palette button.
    // @reflect ignore=true
    class DropArrow : public newui::SubView {
    public:
        DropArrow();
        void paint(BLContext& ctx) override;
    };

    // The swatches part of the colour card (docs/mockups/swatches.html): the active palette's name (a button that lists the palettes), a "..." button
    // for the palette menu, the tiles, a line of text for the hovered swatch, and an inline bar that asks to confirm a delete or to type a name.
    // The strip only shows what it is given and says what the user asked for; the owner applies it (SwatchPaletteController).
    // @reflect ignore=true
    class SwatchStrip : public newui::SubView {
    public:
        SwatchStrip();

        void setPalettes(std::vector<StripPalette> palettes, std::size_t active);
        void setSwatches(std::vector<StripSwatch> swatches);
        void setCurrent(float r, float g, float b, float a);
        const std::vector<StripPalette>& palettes() const { return palettes_; }
        std::size_t activePalette() const { return active_; }
        bool activeIsAutomatic() const { return active_ < palettes_.size() && palettes_[active_].automatic; }
        SwatchTiles& tiles() { return *tiles_; }

        // Lays the parts out for this width and returns the height used; the strip's own bounds follow (its top left stays).
        float layoutFor(float width);

        // Requests (all optional).
        std::function<void(std::size_t index)> onSelectPalette;
        std::function<void()> onNewPalette;
        std::function<void()> onDuplicatePalette;
        std::function<void(const std::string& name)> onRenamePalette;
        std::function<void()> onDeletePalette;
        std::function<void(std::size_t index)> onUse;
        std::function<void()> onAdd;
        std::function<void(std::size_t index)> onReplace;
        std::function<void(std::size_t index, const std::string& name)> onRenameSwatch;
        std::function<void(std::size_t index)> onDeleteSwatch;
        std::function<void(std::size_t index)> onSaveToPalette;
        // "Link" or "Unlink" in a swatch's menu: `linked` is what the swatch should become.
        std::function<void(std::size_t index, bool linked)> onSetLinked;
        std::function<void(std::size_t from, std::size_t to)> onMove;
        // Fired when the strip's height changed (a bar opened or closed, the swatches wrapped differently), so the owner can lay out again.
        std::function<void()> onHeightChanged;

        // The menus (what the buttons and a right-click show).
        std::vector<StripMenuEntry> paletteListMenu() const;
        std::vector<StripMenuEntry> paletteMoreMenu();
        std::vector<StripMenuEntry> swatchMenu(std::size_t index);

        // The inline bar: a question to confirm, or a name to type. Enter or OK answers it; Escape or Cancel dismisses it.
        void askConfirm(const std::string& question, std::function<void()> onYes);
        void askText(const std::string& title, const std::string& initial, std::function<void(const std::string&)> onOk);
        bool asking() const { return asking_; }
        const std::string& question() const;
        void answer(bool ok, const std::string& text = "");

        // What is on show, for tests.
        std::string hoverText() const;
        std::string paletteButtonText() const;

    private:
        void showMenu(const std::vector<StripMenuEntry>& entries);
        void refreshPaletteButton();
        void setHoverText(int index);
        void closeBar();
        void changedHeight();

        std::vector<StripPalette> palettes_;
        std::size_t active_ = 0;
        newui::Button* paletteButton_ = nullptr;
        DropArrow* arrow_ = nullptr;
        newui::Button* moreButton_ = nullptr;
        SwatchTiles* tiles_ = nullptr;
        newui::Label* foot_ = nullptr;
        newui::SubView* bar_ = nullptr;
        newui::Label* barText_ = nullptr;
        newui::TextField* barField_ = nullptr;
        newui::Button* barOk_ = nullptr;
        newui::Button* barCancel_ = nullptr;
        bool asking_ = false;
        bool askingText_ = false;
        std::function<void()> yes_;
        std::function<void(const std::string&)> typed_;
        std::string questionText_;
        float lastHeight_ = 0.0f;
        float width_ = 214.0f;
    };

}
