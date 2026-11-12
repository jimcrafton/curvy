#include "curvy/newdocumentdialog.h"

#include "curvy/newdocumentdialogsession.h"

#ifdef CURVY_GUI_AUTOMATION
#include <newui/automation.h>
#endif
#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/dialogs.h>

namespace curvy {

    using namespace newui;

    namespace {
        bool showDialog(Frame& owner, NewDocumentModel& model, bool setup, bool nameEditable = false) {
            Dialog dialog;
            dialog.setName("newdocument");
            if (!Bundle::instance().loadDialog(dialog)) {
                return false;
            }
            NewDocumentModel working = model;
            newdocdialog::Session session(dialog.rootView(), working);
            session.wire([&dialog](DialogResult result) { dialog.close(result); });
            session.start();
            if (setup) {
                dialog.setTitle("Document Setup");
                if (auto* ok = dynamic_cast<Button*>(dialog.rootView().findView("createButton"))) {
                    ok->setText("OK");
                }
                for (const char* name : {"categoryControl", "presetList"}) {
                    if (auto* control = dynamic_cast<Control*>(dialog.rootView().findView(name))) {
                        control->setEnabled(false);
                    }
                }
                if (auto* nameBox = dynamic_cast<Control*>(dialog.rootView().findView("newDocumentName"))) {
                    nameBox->setEnabled(nameEditable);
                }
            }
#ifdef CURVY_GUI_AUTOMATION
            dialog.rootView().setName("newDocumentRoot");   // the workspace's own root is "rootview" too, and the driver finds the first
            Automation::expose(dialog.rootView());   // so a GUI test can reach the dialog's views while it is open
#endif
            if (dialog.showModal(&owner) != DialogResult::Ok) {
                return false;
            }
            model = working;
            return true;
        }
    }

    bool NewDocumentDialog::runSetup(Frame& owner, NewDocumentModel& model, bool nameEditable) {
        return showDialog(owner, model, true, nameEditable);
    }

    bool NewDocumentDialog::run(Frame& owner, NewDocumentModel& model) {
        return showDialog(owner, model, false);
    }

}
