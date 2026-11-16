#pragma once

#include "curvy/panel.h"
#include "curvy/scriptconsole.h"
#include "curvy/sketchdocument.h"

#include <newui/controls.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The Python console of the right dock: a read-only transcript above one input line. Enter runs the line in the console's scope
    // (`doc` is the document, `network` its graph); what it printed, or the traceback, is appended to the transcript. Up and Down in the
    // input walk the lines run before. The console and the document must outlive the panel's use; the owner calls setDocument() when the
    // active document changes. Scripts run on the calling (main) thread, so a long script blocks the window until it returns.
    // @reflect ignore=true
    class PyConsolePanel : public Panel {
    public:
        PyConsolePanel(ScriptConsole& console, SketchDocument* document);
        ~PyConsolePanel() override;

        void setDocument(SketchDocument* document) { document_ = document; }

        // Runs `code` as if typed and Entered: echoes it, runs it, shows the result. Blank code does nothing.
        void execute(const std::string& code);
        // Runs a whole script (from the script editor): the transcript shows `>>> run <name>` instead of the code, and the input history
        // is left alone.
        void runScript(const std::string& code, const std::string& name);
        void clear();

        // Called right before a script starts, after the transcript shows that it is running: the owner paints the window now, because
        // nothing is painted until the script returns.
        std::function<void()> onBeforeRun;
        // Called after a script ran, so the owner can repaint what it may have changed.
        std::function<void()> onExecuted;
        // Called when the panel is destroyed (closing the window destroys the view tree before its owner).
        std::function<void()> onDestroyed;

        // What the panel shows, for tests.
        const std::wstring& transcript() const { return output_->text(); }
        void setInput(const std::string& text);
        std::string input() const;
        // As the Up (-1) and Down (+1) keys do.
        void recall(int direction);

    private:
        static std::wstring widen(const std::string& text);
        void runAndShow(const std::string& code, const std::string& echo);
        void setTranscript(const std::wstring& text);
        void layoutParts();

        ScriptConsole& console_;
        SketchDocument* document_;
        newui::ScrollView* scroll_ = nullptr;   // holds output_: scroll bar and wheel
        newui::TextControl* output_ = nullptr;
        newui::TextField* input_ = nullptr;
        std::vector<std::string> history_;
        std::size_t recallAt_ = 0;
    };

}
