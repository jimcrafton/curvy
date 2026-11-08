#include "curvy/sketchcontroller.h"

#include "curvy/io/pdf/pdfexport.h"
#include "curvy/io/eps/epsexport.h"
#include "curvy/io/pdf/pdfimport.h"
#include "curvy/pdfpagesdialog.h"
#include "curvy/tracedialog.h"

#include "curvy/imagetrace.h"
#include "curvy/sketchexport.h"

#include "curvy/newdocumentdialog.h"

#include <newui/dialogs.h>

#include <newui/utils.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace curvy {

    SketchDocumentController::SketchDocumentController(SketchDocument& document, newui::Frame& frame)
        : document_(document), frame_(frame) {
        uiLoop_ = &newui::RunLoop::current();
        uiThread_ = std::this_thread::get_id();
        // A background Open / Save clears the modified flag on the worker thread.
        document_.onModifiedChanged += [this](newui::Document&) {
            onUiThread([this]() { onStateChanged(*this); });
            return newui::SyncReturn::Handled;
        };
        std::string folder = newui::SpecialFolders::roamingAppData();
        if (!folder.empty()) {
            setRecentFilesPath(folder + "/sketch/recent.txt");
        }
    }

    SketchDocumentController::~SketchDocumentController() {
        *alive_ = false;   // posts still on their way from the worker find this and do nothing
        task_.cancel();
        task_.waitFor(std::chrono::seconds(30));   // the task's own destructor joins; this only bounds how long a stuck job is waited on first
        if (busyTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            uiLoop_->cancelDelayed(busyTimer_);
        }
    }

    void SketchDocumentController::onUiThread(std::function<void()> task) {
        if (std::this_thread::get_id() == uiThread_) {
            task();
            return;
        }
        std::shared_ptr<bool> alive = alive_;
        uiLoop_->post([alive, task = std::move(task)]() {
            if (*alive) {
                task();
            }
        });
    }

    // ---- Background work ----

    bool SketchDocumentController::refuseWhileBusy() {
        if (!document_.isBusy()) {
            return false;
        }
        onStatus(*this, busyLabel_ + " - wait for it to finish, or press Esc to cancel");
        return true;
    }

    void SketchDocumentController::cancelBusy() {
        if (!document_.isBusy() || cancelRequested_) {
            return;
        }
        cancelRequested_ = true;
        cancelAsked_ = std::chrono::steady_clock::now();
        task_.cancel();
    }

    bool SketchDocumentController::startBusy(const std::string& label, SketchDocument::Busy kind, std::function<bool()> work, std::function<void(Outcome)> finish) {
        if (document_.isBusy() || task_.running()) {
            return false;
        }
        busyLabel_ = label;
        finish_ = std::move(finish);
        cancelRequested_ = timedOut_ = released_ = false;
        busyStart_ = std::chrono::steady_clock::now();
        document_.setBusy(kind);
        batch_.emplace(document_);
        document_.setTaskProgress(&task_.progress());
        std::shared_ptr<bool> alive = alive_;
        const bool started = task_.start([this, alive, work = std::move(work)](TaskProgress&) {
            bool ok = false;
            try {
                ok = work();
            } catch (...) {
            }
            uiLoop_->post([this, alive, ok]() {
                if (*alive) {
                    finishBusy(ok);
                }
            });
        });
        if (!started) {
            batch_.reset();
            document_.setTaskProgress(nullptr);
            document_.setBusy(SketchDocument::Busy::None);
            return false;
        }
        busyTimer_ = uiLoop_->postDelayed(std::chrono::milliseconds(33), [this]() { return tickBusy(); });
        onBusyStarted(*this);
        return true;
    }

    bool SketchDocumentController::tickBusy() {
        if (!document_.isBusy()) {
            busyTimer_ = newui::RunLoop::kInvalidTimerHandle;
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!cancelRequested_ && now - busyStart_ > timeout_) {
            timedOut_ = true;
            cancelBusy();
        }
        if (cancelRequested_ && !released_ && now - cancelAsked_ > grace_) {
            released_ = true;   // the worker ignores the cancel: the UI is given back, the document stays busy until it ends
            onBusyFinished(*this);
            reportError("The file is still being processed in the background and does not respond to being cancelled. It will be "
                        "available again when that ends; quitting curvy stops it.");
        }
        if (!released_) {
            onBusyProgress(*this);
        }
        return false;
    }

    void SketchDocumentController::finishBusy(bool ok) {
        task_.waitFor(std::chrono::seconds(10));   // the worker posted this as its last act: it is ending
        const Outcome outcome = ok ? Outcome::Ok : timedOut_ ? Outcome::TimedOut : task_.progress().cancelled() ? Outcome::Cancelled : Outcome::Failed;
        if (busyTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            uiLoop_->cancelDelayed(busyTimer_);
            busyTimer_ = newui::RunLoop::kInvalidTimerHandle;
        }
        batch_.reset();   // the document announces what the worker changed, here on the UI thread
        document_.setTaskProgress(nullptr);
        document_.setBusy(SketchDocument::Busy::None);
        std::function<void(Outcome)> finish = std::move(finish_);
        finish_ = nullptr;
        if (!released_) {
            onBusyFinished(*this);
        }
        if (finish) {
            finish(outcome);
        }
    }

    // A cancelled or timed-out Open / Import: SketchDocument::readFromFile has put the previous contents back (its undo history and selection
    // are gone), and the path, name and modified flag were never touched, so only the title and the like need to be told.
    void SketchDocumentController::keepPreviousDocument() {
        onStateChanged(*this);
    }

    // ---- Recent files ----

    void SketchDocumentController::setRecentFilesPath(const std::string& path) {
        recentPath_ = path;
        loadRecent();
        onRecentChanged(*this);
    }

    void SketchDocumentController::loadRecent() {
        recent_.clear();
        if (recentPath_.empty()) {
            return;
        }
        std::ifstream in(std::filesystem::u8path(recentPath_));
        std::string line;
        while (std::getline(in, line) && recent_.size() < maxRecent_) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty()) {
                recent_.push_back(line);
            }
        }
    }

    void SketchDocumentController::saveRecent() const {
        if (recentPath_.empty()) {
            return;
        }
        std::error_code ignored;
        std::filesystem::create_directories(std::filesystem::u8path(recentPath_).parent_path(), ignored);
        std::ofstream out(std::filesystem::u8path(recentPath_), std::ios::trunc);
        for (const std::string& path : recent_) {
            out << path << "\n";
        }
    }

    void SketchDocumentController::setMaxRecent(std::size_t count) {
        maxRecent_ = count;
        if (recent_.size() > maxRecent_) {
            recent_.resize(maxRecent_);
            saveRecent();
        }
    }

    void SketchDocumentController::remember(const std::string& path) {
        if (path.empty()) {
            return;
        }
        recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
        recent_.insert(recent_.begin(), path);
        if (recent_.size() > maxRecent_) {
            recent_.resize(maxRecent_);
        }
        saveRecent();
        onRecentChanged(*this);
    }

    void SketchDocumentController::forget(const std::string& path) {
        auto before = recent_.size();
        recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
        if (recent_.size() != before) {
            saveRecent();
            onRecentChanged(*this);
        }
    }

    void SketchDocumentController::clearRecentFiles() {
        recent_.clear();
        saveRecent();
        onRecentChanged(*this);
    }

    bool SketchDocumentController::newDocument() {
        if (refuseWhileBusy() || !confirmDiscardChanges(document_)) {
            return false;
        }
        NewDocumentModel settings;
        if (!NewDocumentDialog::run(frame_, settings)) {
            return false;
        }
        startDocument(settings);
        return true;
    }

    bool SketchDocumentController::newDocument(const std::string& name, double widthPoints, double heightPoints) {
        if (refuseWhileBusy() || !confirmDiscardChanges(document_)) {
            return false;
        }
        NewDocumentModel settings;
        settings.loadSetup(widthPoints, heightPoints, 72.0, ColorSpace::RGB, PageBackground::White);
        settings.setName(name);
        startDocument(settings);
        return true;
    }

    void SketchDocumentController::startDocument(const NewDocumentModel& settings) {
        document_.clearObjects();
        document_.setPageSize(float(settings.widthPoints()), float(settings.heightPoints()));
        document_.setPpi(float(settings.ppi()));
        document_.setColorMode(settings.colorMode());
        switch (settings.background()) {
            case PageBackground::Transparent:
                document_.setPageBackground(std::nullopt);
                break;
            case PageBackground::CurrentFill:
                document_.setPageBackground(document_.defaultFill().isNone() ? SketchColor::rgb(1.0f, 1.0f, 1.0f) : document_.defaultFill().color());
                break;
            default:
                document_.setPageBackground(SketchColor::rgb(1.0f, 1.0f, 1.0f));
                break;
        }
        document_.reset();  // untitled and unmodified
        untitledName_ = settings.name();
        onStatus(*this, "New document " + untitledName_);
        onDocumentReplaced(*this);
        onStateChanged(*this);
    }

    namespace {
        std::string fileNameOf(const std::string& path) { return std::filesystem::path(std::filesystem::u8path(path)).filename().string(); }
    }

    void SketchDocumentController::completeImport(const std::string& file) {
        document_.reset();   // untitled, unmodified ...
        document_.markModified();   // ... but not saved
        untitledName_ = std::filesystem::path(std::filesystem::u8path(file)).stem().string();
        const std::size_t boards = document_.artboards().size();
        onStatus(*this, "Imported " + file + " (" + std::to_string(boards) + (boards == 1 ? " artboard)" : " artboards)"));
        onDocumentReplaced(*this);
        onStateChanged(*this);
    }

    bool SketchDocumentController::startImport(const std::string& file) {
        return startBusy("Importing " + fileNameOf(file), SketchDocument::Busy::Loading, [this, file]() { return document_.load(file); },
            [this, file](Outcome outcome) {
                if (outcome == Outcome::Ok) {
                    completeImport(file);
                } else if (outcome == Outcome::Cancelled) {
                    keepPreviousDocument();
                    onStatus(*this, "Import of " + fileNameOf(file) + " cancelled");
                } else if (outcome == Outcome::TimedOut) {
                    keepPreviousDocument();
                    reportError("Importing " + file + " took longer than " + std::to_string(timeout_.count() / 60) + " minutes and was stopped.");
                } else {
                    reportError("The file can't be read as a PDF or Illustrator file: " + file);
                }
            });
    }

    bool SketchDocumentController::choosePdfPages(const std::string& file) {
        std::string data;
        try {
            std::ifstream in(std::filesystem::u8path(file), std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            data = buffer.str();
        } catch (...) {
            return true;   // the import itself reports what is wrong with the file
        }
        const std::size_t count = pdfPageCount(data);
        if (count < 2) {
            document_.setPdfPages({});
            return true;
        }
        std::vector<int> pages;
        if (!PdfPagesDialog::run(frame_, fileNameOf(file), count, pages)) {
            onStatus(*this, "Import of " + fileNameOf(file) + " cancelled");
            return false;
        }
        document_.setPdfPages(std::move(pages));
        return true;
    }

    bool SketchDocumentController::importPdf(const std::string& path) {
        if (refuseWhileBusy() || !confirmDiscardChanges(document_)) {
            return false;
        }
        std::string file = path;
        if (file.empty()) {
            newui::FileDialogOptions options;
            options.title = "Import PDF / Illustrator";
            options.filters.push_back({"PDF and Illustrator Files", "*.pdf;*.ai"});
            options.filters.push_back({"All Files", "*.*"});
            newui::Dialog::showOpenFile(frame_.frameHandle(), options, file);
            if (file.empty()) {
                return false;
            }
        }
        bool pdf = false;
        try {
            std::ifstream in(std::filesystem::u8path(file), std::ios::binary);
            char head[1024];
            in.read(head, sizeof(head));
            pdf = in.gcount() > 0 && isPdfDocument(std::string(head, std::size_t(in.gcount())));
        } catch (...) {
            pdf = false;
        }
        if (pdf && askPdfPages_ && !choosePdfPages(file)) {
            return false;
        }
        if (pdf && background_) {
            return startImport(file);
        }
        if (!pdf || !openInto(document_, file)) {
            reportError("The file can't be read as a PDF or Illustrator file: " + file);
            return false;
        }
        completeImport(file);
        return true;
    }

    void SketchDocumentController::completeOpen(const std::string&) {
        remember(document_.filePath());
        onStatus(*this, "Opened " + document_.filePath());
        onDocumentReplaced(*this);
        onStateChanged(*this);
    }

    bool SketchDocumentController::startOpen(const std::string& path) {
        return startBusy("Opening " + fileNameOf(path), SketchDocument::Busy::Loading, [this, path]() { return document_.load(path); },
            [this, path](Outcome outcome) {
                switch (outcome) {
                    case Outcome::Ok:
                        completeOpen(path);
                        break;
                    case Outcome::Cancelled:
                        keepPreviousDocument();
                        onStatus(*this, "Opening " + fileNameOf(path) + " cancelled");
                        break;
                    case Outcome::TimedOut:
                        keepPreviousDocument();
                        reportError("Opening " + path + " took longer than " + std::to_string(timeout_.count() / 60) + " minutes and was stopped.");
                        break;
                    case Outcome::Failed:
                        reportError("Could not open " + path);
                        if (!std::filesystem::exists(std::filesystem::u8path(path))) {
                            forget(path);   // a recent file that is gone
                        }
                        break;
                }
            });
    }

    bool SketchDocumentController::open(const std::string& path) {
        if (refuseWhileBusy()) {
            return false;
        }
        if (!path.empty()) {
            std::string extension = std::filesystem::path(std::filesystem::u8path(path)).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (extension == ".pdf" || extension == ".ai") {
                return importPdf(path);
            }
        }
        if (background_) {
            if (!confirmDiscardChanges(document_)) {
                return false;
            }
            const std::string target = path.empty() ? chooseOpenPath() : path;
            return !target.empty() && startOpen(target);
        }
        if (!openInto(document_, path)) {
            if (!path.empty() && !std::filesystem::exists(std::filesystem::u8path(path))) {
                forget(path);  // a recent file that is gone
            }
            return false;
        }
        completeOpen(path);
        return true;
    }

    bool SketchDocumentController::recoverFrom(const std::string& path, const std::string& name) {
        if (refuseWhileBusy() || !openInto(document_, path)) {
            return false;
        }
        document_.reset();   // keeps the content, drops the recovery file as its path
        document_.markModified();
        untitledName_ = name;
        onStatus(*this, "Recovered " + name);
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::startSave(const std::string& path) {
        return startBusy("Saving " + fileNameOf(path), SketchDocument::Busy::Saving, [this, path]() { return document_.save(path); },
            [this, path](Outcome outcome) {
                if (outcome == Outcome::Ok) {
                    remember(document_.filePath());
                    onStatus(*this, "Saved " + document_.filePath());
                    onStateChanged(*this);
                } else if (outcome == Outcome::Cancelled) {
                    onStatus(*this, "Save of " + fileNameOf(path) + " cancelled; the file on disk is unchanged");
                } else if (outcome == Outcome::TimedOut) {
                    reportError("Saving " + path + " took longer than " + std::to_string(timeout_.count() / 60) + " minutes and was stopped; the file on disk is unchanged.");
                } else {
                    reportError("Could not save " + path);
                }
            });
    }

    bool SketchDocumentController::save() {
        if (refuseWhileBusy()) {
            return false;
        }
        if (background_) {
            return document_.hasFilePath() ? startSave(document_.filePath()) : saveAs();
        }
        if (!saveDocument(document_)) {
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::saveAs() {
        if (refuseWhileBusy()) {
            return false;
        }
        if (background_) {
            const std::string target = chooseSavePath(document_);
            return !target.empty() && startSave(target);
        }
        if (!saveDocumentAs(document_)) {
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::startExport(const std::string& label, std::function<bool()> work, const std::string& doneStatus,
                                               const std::string& failMessage, std::shared_ptr<std::string> failedFile) {
        if (!background_) {
            if (!work()) {
                reportError(failedFile && !failedFile->empty() ? "Could not export the image to " + *failedFile : failMessage);
                return false;
            }
            onStatus(*this, doneStatus);
            return true;
        }
        return startBusy(label, SketchDocument::Busy::Saving, std::move(work),
            [this, doneStatus, failMessage, failedFile](Outcome outcome) {
                if (outcome == Outcome::Ok) {
                    onStatus(*this, doneStatus);
                } else if (outcome == Outcome::Cancelled) {
                    onStatus(*this, "Export cancelled");
                } else if (outcome == Outcome::TimedOut) {
                    reportError("The export took longer than " + std::to_string(timeout_.count() / 60) + " minutes and was stopped.");
                } else {
                    reportError(failedFile && !failedFile->empty() ? "Could not export the image to " + *failedFile : failMessage);
                }
            });
    }

    bool SketchDocumentController::exportImage() {
        if (refuseWhileBusy()) {
            return false;
        }
        newui::FileDialogOptions options;
        options.title = "Export As Image";
        options.filters.push_back({"PNG Image", "*.png"});
        options.filters.push_back({"Bitmap", "*.bmp"});
        options.defaultExtension = "png";
        std::string stem = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).stem().string() : untitledName_;
        options.defaultFileName = stem + ".png";
        options.initialDirectory = exportFolder_;
        std::string path;
        newui::Dialog::showSaveFile(frame_.frameHandle(), options, path);
        if (path.empty()) {
            return false;
        }
        const double scale = exportScaleFor(document_);
        return startExport("Exporting " + fileNameOf(path), [this, path, scale]() { return exportPageImage(document_, path, scale); },
            "Exported " + path, "Could not export the image to " + path);
    }

    bool SketchDocumentController::exportPdf() {
        if (refuseWhileBusy()) {
            return false;
        }
        newui::FileDialogOptions options;
        options.title = "Export As PDF";
        options.filters.push_back({"PDF Document", "*.pdf"});
        options.defaultExtension = "pdf";
        std::string stem = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).stem().string() : untitledName_;
        options.defaultFileName = stem + ".pdf";
        options.initialDirectory = exportFolder_;
        std::string path;
        newui::Dialog::showSaveFile(frame_.frameHandle(), options, path);
        if (path.empty()) {
            return false;
        }
        return startExport("Exporting " + fileNameOf(path), [this, path]() { return writePdf(document_, path); },
            "Exported " + path, "Could not export the PDF to " + path);
    }

    bool SketchDocumentController::exportEps() {
        if (refuseWhileBusy()) {
            return false;
        }
        newui::FileDialogOptions options;
        options.title = "Export As EPS";
        options.filters.push_back({"Encapsulated PostScript", "*.eps"});
        options.defaultExtension = "eps";
        std::string stem = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).stem().string() : untitledName_;
        options.defaultFileName = stem + ".eps";
        options.initialDirectory = exportFolder_;
        std::string path;
        newui::Dialog::showSaveFile(frame_.frameHandle(), options, path);
        if (path.empty()) {
            return false;
        }
        return startExport("Exporting " + fileNameOf(path), [this, path]() { return writeEps(document_, path); },
            "Exported " + path, "Could not export the EPS to " + path);
    }

    bool SketchDocumentController::exportArtboards() {
        if (refuseWhileBusy()) {
            return false;
        }
        newui::FileDialogOptions options;
        options.title = "Export All Artboards";
        options.filters.push_back({"PNG Image", "*.png"});
        options.filters.push_back({"Bitmap", "*.bmp"});
        options.defaultExtension = "png";
        std::string stem = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).stem().string() : untitledName_;
        options.defaultFileName = stem + ".png";
        options.initialDirectory = exportFolder_;
        std::string path;
        newui::Dialog::showSaveFile(frame_.frameHandle(), options, path);
        if (path.empty()) {
            return false;
        }
        const double scale = exportScaleFor(document_);
        const std::size_t count = document_.artboards().size();
        auto failedFile = std::make_shared<std::string>();   // written by the worker, read by finish after the worker has ended
        return startExport("Exporting " + std::to_string(count) + " artboards",
            [this, path, scale, count, failedFile]() {
                for (std::size_t i = 0; i < count; ++i) {
                    if (task_.progress().cancelled()) {
                        return false;
                    }
                    std::string file = artboardFilePath(path, document_.artboards()[i].name, i + 1);
                    if (!exportArtboardImage(document_, i, file, scale)) {
                        *failedFile = file;
                        return false;
                    }
                }
                return true;
            },
            "Exported " + std::to_string(count) + " artboards", "", failedFile);
    }

    bool SketchDocumentController::traceImage() {
        if (refuseWhileBusy()) {
            return false;
        }
        newui::FileDialogOptions options;
        options.title = "Trace Image";
        options.filters.push_back({"Pictures", "*.png;*.jpg;*.jpeg;*.bmp"});
        options.filters.push_back({"All Files", "*.*"});
        std::string path;
        newui::Dialog::showOpenFile(frame_.frameHandle(), options, path);
        if (path.empty()) {
            return false;
        }
        // The options of the last trace are the starting point of the next.
        const std::size_t slash = path.find_last_of("/" + std::string(1, char(92)));
        if (!TraceDialog::run(frame_, traceOptions_, slash == std::string::npos ? path : path.substr(slash + 1))) {
            return false;
        }
        return traceImageFile(path, traceOptions_);
    }

    bool SketchDocumentController::placeImage() {
        if (refuseWhileBusy()) {
            return false;
        }
        newui::FileDialogOptions options;
        options.title = "Place Image";
        options.filters.push_back({"Pictures", "*.png;*.jpg;*.jpeg;*.bmp;*.qoi"});
        options.filters.push_back({"All Files", "*.*"});
        std::string path;
        newui::Dialog::showOpenFile(frame_.frameHandle(), options, path);
        if (path.empty()) {
            return false;
        }
        std::size_t placed = 0;
        try {
            placed = document_.placeImageFile(path);
        } catch (...) {
            placed = 0;
        }
        if (placed == 0) {
            reportError("The image can't be loaded: " + path);
            return false;
        }
        onStatus(*this, "Placed " + path);
        return true;
    }

    bool SketchDocumentController::traceImageFile(const std::string& path, const TraceOptions& traceOptions) {
        if (refuseWhileBusy()) {
            return false;
        }
        std::vector<std::uint32_t> pixels;
        int width = 0, height = 0;
        bool loaded = false;
        try {
            loaded = loadImagePixels(path, pixels, width, height);
        } catch (...) {
            loaded = false;
        }
        if (!loaded) {
            reportError("The image can't be loaded: " + path);
            return false;
        }
        if (document_.addTrace(traceRgba(pixels, width, height, traceOptions)).empty()) {
            reportError("Nothing could be traced from " + path);
            return false;
        }
        onStatus(*this, "Traced " + path);
        return true;
    }

    bool SketchDocumentController::documentSetup() {
        if (refuseWhileBusy()) {
            return false;
        }
        NewDocumentModel settings;
        PageBackground background = PageBackground::White;
        if (!document_.pageBackground()) {
            background = PageBackground::Transparent;
        } else if (document_.pageBackground()->toRgba32().value != 0xFFFFFFFFu) {
            background = PageBackground::CurrentFill;
        }
        settings.loadSetup(document_.pageWidth(), document_.pageHeight(), document_.ppi(), document_.colorMode(), background);
        settings.setName(document_.hasFilePath() ? std::filesystem::path(document_.filePath()).filename().string() : untitledName_);
        if (!NewDocumentDialog::runSetup(frame_, settings, !document_.hasFilePath())) {
            return false;
        }
        if (!document_.hasFilePath() && !settings.name().empty()) {
            untitledName_ = settings.name();
        }
        document_.setPageSize(float(settings.widthPoints()), float(settings.heightPoints()));
        document_.setPpi(float(settings.ppi()));
        switch (settings.background()) {
            case PageBackground::Transparent:
                document_.setPageBackground(std::nullopt);
                break;
            case PageBackground::CurrentFill:
                if (background != PageBackground::CurrentFill) {  // keep an existing custom color when it is unchanged
                    document_.setPageBackground(document_.defaultFill().isNone() ? SketchColor::rgb(1.0f, 1.0f, 1.0f) : document_.defaultFill().color());
                }
                break;
            default:
                document_.setPageBackground(SketchColor::rgb(1.0f, 1.0f, 1.0f));
                break;
        }
        document_.convertToColorMode(settings.colorMode());  // a no-op (and no undo step) when it is already that mode
        onStatus(*this, "Document setup changed");
        onDocumentReplaced(*this);  // refit the view to the new page
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::canClose() {
        return !refuseWhileBusy() && confirmDiscardChanges(document_);
    }

    std::string SketchDocumentController::displayName() const {
        std::string name = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).filename().string() : untitledName_;
        return name + (document_.isModified() ? " *" : "");
    }

    namespace {
    newui::FileDialogOptions fileOptions() {
        newui::FileDialogOptions options;
        options.filters.push_back({"SVG Files", "*.svg;*.svgz"});
        options.filters.push_back({"Sketch Binary Files", "*.skb"});
        options.filters.push_back({"All Files", "*.*"});
        options.defaultExtension = "svg";
        return options;
    }
    }

    newui::UnsavedChangesChoice SketchDocumentController::askUnsavedChanges(newui::Document& document) {
        std::string name = document.hasFilePath() ? std::filesystem::path(document.filePath()).filename().string() : untitledName_;
        newui::DialogResult answer = newui::Dialog::showMessageBox(frame_.frameHandle(), "Save changes to " + name + "?", "sketch",
            newui::MessageBoxButtons::YesNoCancel, newui::MessageBoxIcon::Question);
        return answer == newui::DialogResult::Yes ? newui::UnsavedChangesChoice::Save
            : answer == newui::DialogResult::No ? newui::UnsavedChangesChoice::Discard
            : newui::UnsavedChangesChoice::Cancel;
    }

    std::string SketchDocumentController::chooseSavePath(newui::Document& document) {
        newui::FileDialogOptions options = fileOptions();
        options.title = "Save As";
        options.defaultFileName = document.hasFilePath() ? std::filesystem::path(document.filePath()).filename().string() : untitledName_ + ".svg";
        std::string path;
        newui::Dialog::showSaveFile(frame_.frameHandle(), options, path);
        return path;
    }

    std::string SketchDocumentController::chooseOpenPath() {
        newui::FileDialogOptions options = fileOptions();
        options.title = "Open";
        std::string path;
        newui::Dialog::showOpenFile(frame_.frameHandle(), options, path);
        return path;
    }

    void SketchDocumentController::reportError(const std::string& message) {
        std::string text = message;
        const bool openFailure = message.rfind("Could not open ", 0) == 0 || message.rfind("The file can't be read", 0) == 0;
        if (openFailure && !document_.lastError().empty()) {
            text += "\n\nThe file " + document_.lastError() + ".";
            document_.clearLastError();
        }
        if (errorReporter_) {
            errorReporter_(text);
            return;
        }
        newui::Dialog::showMessageBox(frame_.frameHandle(), text, "sketch", newui::MessageBoxButtons::Ok, newui::MessageBoxIcon::Error);
    }

}
