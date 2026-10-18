#pragma once

#include "curvy/canvasview.h"
#include "curvy/sketchcommands.h"
#include "curvy/sketchcontroller.h"
#include "curvy/sketchdocument.h"
#include "curvy/stylepanel.h"
#include "curvy/layerspanel.h"
#include "curvy/artboardpanel.h"
#include "curvy/brushpanel.h"
#include "curvy/librarypanel.h"
#include "curvy/textpanel.h"
#include "curvy/transformpanel.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/menus.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // The window controller: it connects the views that sketch.newui declares (the canvas, the menus, the toolbars, the tool
    // palette, the style panels, the status bar) to the model and to the other controllers. All the knowledge of "which widget does
    // what" lives here, as short bind methods and small tables - not in main(), and not in the widgets.
    //
    //   - a menu item or toolbar button only names a command id; SketchCommandController does the work (sketchcommands.h)
    //   - the views that show model state (the title, the Undo / Redo labels, the status bar, the check marks, the color mode) listen
    //     to the model's and the controllers' events and refresh themselves
    //
    // It is a newui::Controller that does not call Controller::setModel() (as SpreadsheetController in the manyrows demo): the
    // document belongs to the application, which must keep it alive for as long as the window. It registers handlers on the views
    // it is given, so it must be created after them and live as long as they do.
    // @reflect ignore=true
    class SketchWindowController : public newui::Controller {
    public:
        // Finds the views in `frame` (sketch.newui must have a canvas; the rest are optional) and wires everything up. The window
        // shows `document`.
        SketchWindowController(newui::Frame& frame, SketchDocument& document);
        ~SketchWindowController() override = default;
        SketchWindowController(const SketchWindowController&) = delete;
        SketchWindowController& operator=(const SketchWindowController&) = delete;

        // Opens the file at `path` (the one named on the command line), or - for an empty path - just titles the window "Untitled".
        void start(const std::string& path);

        newui::Frame& frame() { return frame_; }
        SketchDocument& document() { return document_; }
        CanvasView& canvas() { return *canvas_; }
        StylePanel& stylePanel() { return *stylePanel_; }
        SketchDocumentController& files() { return *files_; }
        SketchCommandController& commands() { return *commands_; }
        // The menu item at "File" / "Open..." or "View/Color Mode" / "RGB" (a slash path names a submenu); null when there is none.
        newui::MenuItem* menuItem(const std::string& menuPath, const std::string& text) const;

    private:
        // ---- Binding, one concern per method (the constructor calls them in this order) ----
        void bindFileEvents();       // title, status text, refit after New / Open, the Open Recent submenu
        void bindZoom();             // the zoom combo and the status bar's zoom pane follow the canvas
        void bindMenus();            // menu items -> commands (a table), About
        void bindToolbarButtons();   // toolbar buttons -> commands (a table), the Snap to Grid toggle
        void bindViewChecks();       // check marks of Show Grid / Show Rulers / Snap to Grid follow the canvas
        void bindEditState();        // Undo / Redo labels and enabled state follow the undo stack
        void bindColorMode();        // the document's color mode in the status bar and the Color Mode menu
        void bindStatusBar();        // selection count, size of the selection, pointer position, tool hints
        void bindToolPalette();      // the palette's radio buttons and the canvas tool follow each other
        void bindStylePanel();       // the Gradient tool and the Color panel share their target and selected stop
        void bindShortcuts();        // Ctrl+N / O / S inside the canvas, closing the window

        // Binds `action` to a menu item / a toolbar button; quietly nothing when the view is not in the layout.
        void onMenuItem(const std::string& menuPath, const std::string& text, std::function<void(newui::MenuItem&)> action);
        void onButton(const char* name, std::function<void()> action);
        void rebuildRecentMenu();
        void syncViewChecks();

        newui::Frame& frame_;
        SketchDocument& document_;
        CanvasView* canvas_ = nullptr;
        newui::StatusBar* status_ = nullptr;
        newui::MenuBar* menuBar_ = nullptr;
        newui::DropDownList* zoomCombo_ = nullptr;
        newui::ToolbarButton* snapButton_ = nullptr;

        std::unique_ptr<SketchDocumentController> files_;
        std::unique_ptr<SketchCommandController> commands_;
        std::unique_ptr<StylePanel> stylePanel_;
        std::unique_ptr<TransformPanel> transformPanel_;
        std::unique_ptr<LayersPanel> layersPanel_;
        std::unique_ptr<ArtboardPanel> artboardPanel_;
        std::unique_ptr<LibraryPanel> libraryPanel_;
        std::unique_ptr<BrushPanel> brushPanel_;
        std::unique_ptr<TextPanel> textPanel_;
        // Menu items whose text or state changes ("View/Color Mode/RGB", "Edit/Undo"), found once.
        std::map<std::string, newui::MenuItem*> menuItems_;
        // Items replaced by a rebuild of Open Recent: the entry that was just clicked is still running when it asks for the rebuild,
        // so what it replaces is only deleted by the next one.
        std::vector<std::unique_ptr<newui::MenuItem>> parkedMenuItems_;
    };

}
