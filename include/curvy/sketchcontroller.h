#pragma once

#include "curvy/imagetrace.h"
#include "curvy/newdocumentmodel.h"
#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/delegate.h>
#include <newui/frame.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // sketch's DocumentController: the File menu's New / Open / Save / Save As and the "save changes?"
    // question, bound to the one SketchDocument and the window that shows it. newui's DocumentController
    // supplies the logic; this adds SVG file filters, the owner window for the dialogs, and events the
    // window listens to (status text, title, a replaced document).
    // @reflect ignore=true
    class SketchDocumentController : public newui::DocumentController {
    public:
        SketchDocumentController(SketchDocument& document, newui::Frame& frame);

        SketchDocument& document() { return document_; }

        // Each returns false when cancelled or failed (the user has already been told about a failure).
        // New: asks to save changes, then shows the New Document dialog and starts a document with the chosen
        // page size, resolution, color mode and background.
        bool newDocument();
        // The same without the dialog: a document of this page size (points), 72 ppi, RGB, white. The workspace's New ring uses it.
        bool newDocument(const std::string& name, double widthPoints, double heightPoints);
        // An empty path asks with the Open dialog.
        bool open(const std::string& path = std::string());
        bool save();
        bool saveAs();
        // Opens an autosaved copy as a new, unsaved, modified document called `name` (Save then asks where).
        bool recoverFrom(const std::string& path, const std::string& name);
        // File > Document Setup: change the page size, resolution, color mode and background of this document
        // (the color mode converts the artwork, as one undo step).
        bool documentSetup();
        // File > Export As Image: asks where, then writes the page as a PNG or BMP at the document's resolution (sketchexport.h).
        bool exportImage();
        // File > Export All Artboards: one image per artboard, named after the chosen file ("poster.png" -> "poster_1_Artboard-1.png"...).
        bool exportArtboards();
        // File > Export As PDF: every artboard as a page of one PDF file (pdfexport.h).
        bool exportPdf();
        // File > Trace Image: asks for a picture (PNG, BMP or JPEG), traces it into vector shapes (imagetrace.h) and adds them to the page.
        bool traceImage();
        // File > Place Image: asks for a picture (PNG, JPEG, BMP or QOI) and puts it on the active artboard as an Image object.
        bool placeImage();
        // The same for a picture that is already chosen (what the dialog leads to; also for tests).
        bool traceImageFile(const std::string& path, const TraceOptions& options = TraceOptions());
        // True when it is fine to close the window / leave the app: nothing unsaved, or saved/discarded.
        bool canClose();

        // What an unsaved document is called ("Untitled-1" from the New Document dialog) - the title bar,
        // the "save changes?" question and Save As's suggested file name use it.
        const std::string& untitledName() const { return untitledName_; }

        // The files opened or saved most recently, newest first (at most kMaxRecent), kept in a text file in the
        // user's roaming data folder between runs. A file that cannot be opened is dropped from the list.
        static constexpr std::size_t kMaxRecent = 8;
        const std::vector<std::string>& recentFiles() const { return recent_; }
        void setMaxRecent(std::size_t count);
        // Where the export dialogs start; empty leaves it to Windows.
        void setExportFolder(const std::string& folder) { exportFolder_ = folder; }
        void clearRecentFiles();
        // Where the list is kept (for tests); loads the list from there.
        void setRecentFilesPath(const std::string& path);
        newui::Delegate<SketchDocumentController> onRecentChanged;

        // The file name (or Untitled) with a * while modified - what the title bar shows after "sketch - ".
        std::string displayName() const;

        // Short progress text for the status bar ("Saved C:\a.svg").
        typedef newui::Delegate<SketchDocumentController, std::string> StatusDelegate;
        StatusDelegate onStatus;
        // After any command that may change displayName() (the title should refresh).
        typedef newui::Delegate<SketchDocumentController> StateDelegate;
        StateDelegate onStateChanged;
        // After New or Open replaced the document's content (the view should refit, etc.).
        StateDelegate onDocumentReplaced;

        // Replaces the error message box (for tests and headless runs); an empty function restores it.
        void setErrorReporter(std::function<void(const std::string&)> reporter) { errorReporter_ = std::move(reporter); }

    protected:
        newui::UnsavedChangesChoice askUnsavedChanges(newui::Document& document) override;
        std::string chooseSavePath(newui::Document& document) override;
        std::string chooseOpenPath() override;
        void reportError(const std::string& message) override;

    private:
        TraceOptions traceOptions_;   // what the last Trace Image dialog chose
        SketchDocument& document_;
        newui::Frame& frame_;
        std::string untitledName_ = "Untitled";
        std::function<void(const std::string&)> errorReporter_;
        void startDocument(const NewDocumentModel& settings);
        void remember(const std::string& path);
        void forget(const std::string& path);
        void loadRecent();
        void saveRecent() const;
        std::vector<std::string> recent_;
        std::size_t maxRecent_ = kMaxRecent;
        std::string exportFolder_;
        std::string recentPath_;
    };

}
