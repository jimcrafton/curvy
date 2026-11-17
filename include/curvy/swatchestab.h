#pragma once

#include "curvy/noderef.h"
#include "curvy/panelsmenu.h"
#include "curvy/sketchdocument.h"
#include "curvy/swatchpalettes.h"

#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/subview.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The Swatches tab of the Document panel (Resources/swatchesTab.newui, from docs/mockups/swatchesTabPanel.html, layout C): every palette as a row with
    // its colours as a strip (Recent last), the colours of the palette that is open as tiles, the details of the selected swatch (name, hex, linked, how
    // many shapes in how many groups use it, Select them, Use as fill) and the colours in use that are in no palette (+ to add one, Add all). It is a view
    // of the document's palettes: every edit goes through the document (one undo step) and it reads them again when they change. The details and the
    // colours in use read the graph, so they are worked out when something is selected or when refresh() is called (the owner calls it when the tab is
    // shown), not while the graph changes. The tab needs `parent` to outlive it and the document to outlive it.
    // @reflect ignore=true
    class SwatchesTab {
    public:
        // Loads the content into a scroll view that fills `parent`; the owner sizes it with layoutFor().
        SwatchesTab(SketchDocument& document, newui::SubView& parent);
        ~SwatchesTab();
        SwatchesTab(const SwatchesTab&) = delete;
        SwatchesTab& operator=(const SwatchesTab&) = delete;

        // Reads the palettes and the graph again.
        void refresh();
        // Palettes belong to a document: without one open the tab shows a note instead of a palette that New or Open would replace. The tab starts
        // without one.
        void setDocumentOpen(bool open);
        bool documentOpen() const { return documentOpen_; }
        // Fills the parent (`width` by `height`), laying the parts out for that width; the content scrolls when it is taller.
        void layoutFor(float width, float height);

        // The swatch's colour was asked for ("Use as fill", "Use" in the menu): the owner applies it.
        std::function<void(const Swatch& swatch)> onUse;
        // What needs a file dialog or the selection, which the owner has.
        std::function<void()> onImportPalette;
        std::function<void()> onPaletteFromImage;
        std::function<void(const std::vector<NodeRef>& users)> onSelectUsers;
        // Something to tell the user (a refused edit, what was added).
        std::function<void(const std::string& message)> onMessage;
        // The content became taller or shorter (a palette grew, a document opened): the owner can ask its panel for the height it needs.
        std::function<void()> onContentHeightChanged;
        // Shows a menu at a screen point and returns when it is done (the owner's popup; a test runs the entries itself).
        std::function<void(const std::vector<PanelMenuEntry>& entries, const newui::Point& screen)> presentMenu;

        // The menus (what the palette button, the "..." button and a right-click on a tile show), as data.
        std::vector<PanelMenuEntry> paletteListMenu();
        std::vector<PanelMenuEntry> paletteMoreMenu();
        std::vector<PanelMenuEntry> swatchMenu(std::size_t index);

        // What a click or a typed entry does.
        void selectPalette(std::size_t row);   // 0 is the first of the document's palettes; palettes().size() is Recent
        void selectSwatch(std::size_t index);
        void setName(const std::string& text);
        void setHex(const std::string& text);
        void setLinked(bool linked);
        void selectUsers();
        void useSelected();
        void addInUse(std::size_t index);
        void addAllInUse();
        void renameActivePalette(const std::string& name);

        // Dragging a tile onto another moves the swatch there (one undo step) and keeps it selected; points are in the tile grid's coordinates. A drag
        // does nothing in Recent, which is automatic. They are what the tiles' mouse handlers call, so a test can drive a drag without a window.
        void pressTile(std::size_t index, const newui::Point& gridPoint);
        void dragTileTo(const newui::Point& gridPoint);
        void releaseTileAt(const newui::Point& gridPoint);
        newui::Point tileCenter(std::size_t index) const;
        bool tileDragging() const { return dragging_; }
        int dropTarget() const { return dropTarget_; }

        // What is on show, for tests.
        std::size_t paletteRowCount() const;   // the document's palettes and Recent
        std::size_t activeRow() const { return showingRecent_ ? document_.palettes().size() : document_.activePalette(); }
        std::size_t tileCount() const;
        int selectedIndex() const { return selected_; }
        std::string paletteName() const;
        std::string nameText() const;
        std::string hexText() const;
        bool linkedChecked() const;
        std::string usedByText() const;
        bool selectThemEnabled() const;
        std::size_t inUseCount() const;
        // Whether the colour of a tile, of a palette row's strip and of a colour in use is really drawn (every view on the way is visible, and has a size).
        bool tileDrawn(std::size_t index) const;
        bool paletteStripDrawn(std::size_t row) const;
        bool inUseChipDrawn(std::size_t index) const;
        // The scrolling area that holds the content has a size, so something can be seen.
        bool hasSize() const;
        std::string emptyNoteText() const;
        bool emptyNoteShown() const;
        std::string statusText() const;
        float contentHeight() const { return contentHeight_; }
        float scrollableHeight() const { return scroll_->contentSize().height; }

    private:
        struct PaletteRow {
            newui::SubView* view = nullptr;
            newui::Label* name = nullptr;
            newui::Label* count = nullptr;
            newui::SubView* strip = nullptr;
            std::vector<newui::SubView*> segments;
        };
        struct Tile {
            newui::SubView* view = nullptr;
            newui::SubView* chip = nullptr;
        };
        struct UseRow {
            newui::SubView* view = nullptr;
            newui::SubView* chip = nullptr;
            newui::Label* hex = nullptr;
            newui::Label* info = nullptr;
            newui::Button* add = nullptr;
        };

        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_->findView(name));
        }
        const std::vector<Swatch>& shown() const;
        void say(const std::string& message);
        void guarded(const std::function<void()>& edit);
        void clearBuiltIn(newui::SubView* container);
        void wire();
        void refreshPalettes();
        void refreshTiles();
        void refreshDetails();
        void refreshInUse();
        void layoutAll(float width);
        void popup(const std::vector<PanelMenuEntry>& entries, newui::View& at, const newui::Point& local);
        void paintSelection();
        int tileIndexAt(const newui::Point& gridPoint) const;
        newui::Point toGrid(std::size_t tile, const newui::Point& local) const;

        PaletteRow& paletteRowAt(std::size_t index);
        Tile& tileAt(std::size_t index);
        UseRow& useRowAt(std::size_t index);

        SketchDocument& document_;
        newui::SubView& parent_;
        newui::ScrollView* scroll_ = nullptr;
        newui::SubView* root_ = nullptr;
        newui::Connection connection_;
        newui::Button* paletteButton_ = nullptr;
        newui::Button* moreButton_ = nullptr;
        newui::TextField* renameField_ = nullptr;
        newui::SubView* paletteRows_ = nullptr;
        newui::SubView* grid_ = nullptr;
        newui::SubView* details_ = nullptr;
        newui::SubView* detailColor_ = nullptr;
        newui::TextField* detailName_ = nullptr;
        newui::TextField* detailHex_ = nullptr;
        newui::Toggle* detailLinked_ = nullptr;
        newui::Label* usedBy_ = nullptr;
        newui::Button* selectThem_ = nullptr;
        newui::Button* useAsFill_ = nullptr;
        newui::Label* gridTitle_ = nullptr;
        newui::Label* inUseTitle_ = nullptr;
        newui::SubView* inUseList_ = nullptr;
        newui::Button* addAll_ = nullptr;
        newui::Label* status_ = nullptr;
        newui::Label* emptyNote_ = nullptr;
        bool documentOpen_ = false;
        std::vector<PaletteRow> paletteViews_;
        std::vector<Tile> tiles_;
        std::vector<UseRow> useRows_;
        std::vector<ColorUse> inUse_;
        std::size_t shownTiles_ = 0;
        std::size_t shownRows_ = 0;
        std::size_t shownUses_ = 0;
        bool showingRecent_ = false;
        int selected_ = -1;
        std::size_t columns_ = 1;
        int pressedTile_ = -1;
        int dropTarget_ = -1;
        bool dragging_ = false;
        newui::Point pressPoint_;
        bool updating_ = false;
        float contentHeight_ = 0.0f;
        float detailsHeight_ = 0.0f;
        float lastWidth_ = 268.0f;
    };

}
