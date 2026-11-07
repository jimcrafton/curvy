#pragma once

#include "curvy/imagetrace.h"
#include "curvy/newdocumentmodel.h"
#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/delegate.h>
#include <newui/frame.h>
#include <newui/runloop.h>

#include "curvy/backgroundtask.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
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
        ~SketchDocumentController() override;

        SketchDocument& document() { return document_; }

        // Background file work. With it on (the app turns it on; it is off by default so tests and headless runs stay synchronous), Open,
        // Import PDF, Save and Save As do their reading / writing on a worker thread and return as soon as it has started (true = started).
        // The document is marked busy (SketchDocument::busy) from then until the worker is done: nothing else may touch it, and every command
        // here answers false. The worker posts its end to the UI thread through the RunLoop, which then finishes exactly as the synchronous
        // path does (recent files, status text, onDocumentReplaced...). While it runs, onBusyProgress fires about 30 times a second.
        void setBackgroundIo(bool on) { background_ = on; }
        bool backgroundIo() const { return background_; }
        bool busy() const { return document_.isBusy(); }
        // 0..1, or negative when the work cannot say how far it is.
        float busyFraction() const { return task_.progress().fraction(); }
        const std::string& busyLabel() const { return busyLabel_; }
        // Asks the worker to stop. A cancelled Open leaves an empty untitled document (the old one was already replaced); a cancelled Save
        // leaves the file on disk as it was.
        void cancelBusy();
        // A job that runs longer than this is cancelled by the poll; if the worker then ignores the cancel for `grace`, the UI is given
        // back (onBusyFinished) with an error while the document stays busy until the worker really ends.
        void setBusyLimits(std::chrono::seconds timeout, std::chrono::seconds grace) { timeout_ = timeout; grace_ = grace; }
        newui::Delegate<SketchDocumentController> onBusyStarted, onBusyProgress, onBusyFinished;
        // Runs `task` now when called on the UI thread, otherwise posts it there. The document fires onModifiedChanged from the worker
        // during a background Open / Save; handlers that touch the UI go through this.
        void onUiThread(std::function<void()> task);

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
        // File > Export As EPS: the active artboard as an Encapsulated PostScript file (epsexport.h).
        bool exportEps();
        // File > Import PDF / Illustrator: asks for a .pdf or .ai file and replaces the document with its artwork (pdfimport.h), one artboard per
        // page. The result is an untitled, modified document: Save asks where, so the PDF itself is never overwritten with another format.
        // An empty path asks with the Open dialog.
        bool importPdf(const std::string& path = std::string());
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
        enum class Outcome { Ok, Failed, Cancelled, TimedOut };
        // Marks the document busy and runs `work` on the worker; `finish` runs on the UI thread afterwards.
        bool startBusy(const std::string& label, SketchDocument::Busy kind, std::function<bool()> work, std::function<void(Outcome)> finish);
        void finishBusy(bool ok);
        bool tickBusy();
        bool refuseWhileBusy();
        void discardPartialLoad();
        void completeOpen(const std::string& path);
        void completeImport(const std::string& file);
        bool startOpen(const std::string& path);
        bool startImport(const std::string& file);
        bool startSave(const std::string& path);
        // Runs an export on the worker in the app, inline otherwise. failedFile, when set by `work`, names the file the message reports.
        bool startExport(const std::string& label, std::function<bool()> work, const std::string& doneStatus, const std::string& failMessage,
                         std::shared_ptr<std::string> failedFile = nullptr);

        bool background_ = false;
        BackgroundTask task_;
        newui::RunLoop* uiLoop_ = nullptr;
        std::thread::id uiThread_;
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
        std::optional<SketchDocument::Batch> batch_;   // holds back the document's announcements until the worker is done
        std::string busyLabel_;
        std::function<void(Outcome)> finish_;
        newui::RunLoop::TimerHandle busyTimer_ = newui::RunLoop::kInvalidTimerHandle;
        std::chrono::steady_clock::time_point busyStart_, cancelAsked_;
        bool cancelRequested_ = false, timedOut_ = false, released_ = false;
        std::chrono::seconds timeout_{600}, grace_{20};

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
