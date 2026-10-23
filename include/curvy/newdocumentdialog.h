#pragma once

#include "curvy/newdocumentmodel.h"

#include <newui/frame.h>

namespace curvy {

    // The New Document dialog (Resources/newdocument.newui, generated from mockup/new-document-dialog.html):
    // loads it into a newui::Dialog, mirrors `model` into its controls and the controls' edits back into
    // the model, and runs it modally over `owner`.
    // @reflect ignore=true
    class NewDocumentDialog {
    public:
        // True when the user pressed Create (model then holds the choices); false on Cancel/close or if the
        // dialog file cannot be loaded. `model` is the starting state either way.
        static bool run(newui::Frame& owner, NewDocumentModel& model);

        // Document Setup: the same dialog changing an existing document's page - titled "Document Setup", the
        // button says OK, and the name and the starting shapes are disabled (they only make sense for a new one).
        // The name box is editable only when `nameEditable` (an unsaved document; a saved one is named by its file).
        static bool runSetup(newui::Frame& owner, NewDocumentModel& model, bool nameEditable);

        // Headless self-test (sketch.exe --check-newdoc): loads the dialog without showing it and drives the
        // controls like a user would. Returns the number of failed checks.
        static int check();
    };

}
