#include "curvy/sketchcontroller.h"

#include "curvy/pdfexport.h"
#include "curvy/tracedialog.h"

#include "curvy/imagetrace.h"
#include "curvy/sketchexport.h"

#include "curvy/newdocumentdialog.h"

#include <newui/dialogs.h>

#include <newui/utils.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

namespace curvy {

    SketchDocumentController::SketchDocumentController(SketchDocument& document, newui::Frame& frame)
        : document_(document), frame_(frame) {
        document_.onModifiedChanged += [this](newui::Document&) {
            onStateChanged(*this);
            return newui::SyncReturn::Handled;
        };
        std::string folder = newui::SpecialFolders::roamingAppData();
        if (!folder.empty()) {
            setRecentFilesPath(folder + "/sketch/recent.txt");
        }
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
        if (!confirmDiscardChanges(document_)) {
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
        if (!confirmDiscardChanges(document_)) {
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

    bool SketchDocumentController::open(const std::string& path) {
        if (!openInto(document_, path)) {
            if (!path.empty() && !std::filesystem::exists(std::filesystem::u8path(path))) {
                forget(path);  // a recent file that is gone
            }
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Opened " + document_.filePath());
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::recoverFrom(const std::string& path, const std::string& name) {
        if (!openInto(document_, path)) {
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

    bool SketchDocumentController::save() {
        if (!saveDocument(document_)) {
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::saveAs() {
        if (!saveDocumentAs(document_)) {
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::exportImage() {
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
        if (!exportPageImage(document_, path, exportScaleFor(document_))) {
            reportError("Could not export the image to " + path);
            return false;
        }
        onStatus(*this, "Exported " + path);
        return true;
    }

    bool SketchDocumentController::exportPdf() {
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
        if (!writePdf(document_, path)) {
            reportError("Could not export the PDF to " + path);
            return false;
        }
        onStatus(*this, "Exported " + path);
        return true;
    }

    bool SketchDocumentController::exportArtboards() {
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
        for (std::size_t i = 0; i < document_.artboards().size(); ++i) {
            std::string file = artboardFilePath(path, document_.artboards()[i].name, i + 1);
            if (!exportArtboardImage(document_, i, file, exportScaleFor(document_))) {
                reportError("Could not export the image to " + file);
                return false;
            }
        }
        onStatus(*this, "Exported " + std::to_string(document_.artboards().size()) + " artboards");
        return true;
    }

    bool SketchDocumentController::traceImage() {
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
        return confirmDiscardChanges(document_);
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
        if (errorReporter_) {
            errorReporter_(message);
            return;
        }
        newui::Dialog::showMessageBox(frame_.frameHandle(), message, "sketch", newui::MessageBoxButtons::Ok, newui::MessageBoxIcon::Error);
    }

}
