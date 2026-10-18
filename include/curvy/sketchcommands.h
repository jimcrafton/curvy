#pragma once

#include <newui/controllers.h>

#include <functional>
#include <map>
#include <string>

namespace curvy {

    class CanvasView;
    class SketchDocument;
    class SketchDocumentController;

    // The controller for everything the user can ask sketch to do: each command has a stable id ("edit.undo", "arrange.bringToFront",
    // "file.save"...) and is the ONE place that says what it does. Menu items, toolbar buttons and keyboard shortcuts are only
    // views that name a command id (see SketchWindowController's binding tables) - none of them knows how the work is done, and the same
    // command is never written twice.
    //
    //   model       SketchDocument (the artwork, its selection and its undo history)
    //   views       CanvasView, StylePanel, the menus / toolbars / status bar declared in sketch.newui
    //   controllers SketchDocumentController (files), SketchCommandController (everything else), SketchWindowController (binds views to them)
    //
    // It is a newui::Controller (the C between the views and the model) but, like SpreadsheetController in the manyrows demo, does not
    // call Controller::setModel(): the document is owned by the application and observed through a plain reference, so it must
    // outlive this object.
    //
    // Commands that work on the selection do nothing when it is empty or the operation does not apply; `enabled()` says whether
    // running one would currently do something, so a view can grey its item out.
    // @reflect ignore=true
    class SketchCommandController : public newui::Controller {
    public:
        SketchCommandController(SketchDocument& document, CanvasView& canvas, SketchDocumentController& files);
        ~SketchCommandController() override = default;
        SketchCommandController(const SketchCommandController&) = delete;
        SketchCommandController& operator=(const SketchCommandController&) = delete;

        bool has(const std::string& id) const { return commands_.count(id) != 0; }
        // Runs the command. False when there is no such command or it is not enabled right now.
        bool run(const std::string& id);
        bool enabled(const std::string& id) const;

        // "Undo Move" / "Redo Flip Horizontal" for the Edit menu: the plain word when there is nothing to undo / redo.
        std::string undoText() const;
        std::string redoText() const;

    private:
        struct Command {
            std::function<void()> run;
            std::function<bool()> enabled;   // empty: always enabled
        };
        void add(const std::string& id, std::function<void()> run, std::function<bool()> enabled = std::function<bool()>());
        void addFileCommands();
        void addEditCommands();
        void addObjectCommands();
        void addArrangeCommands();
        void addViewCommands();

        SketchDocument& document_;
        CanvasView& canvas_;
        SketchDocumentController& files_;
        std::map<std::string, Command> commands_;
    };

}
