#include "curvy/assetpropertiesdialog.h"

#include "curvy/assetpropertiesdialogsession.h"

#ifdef CURVY_GUI_AUTOMATION
#include <newui/automation.h>
#endif
#include <newui/bundle.h>
#include <newui/dialogs.h>

namespace curvy {

    using namespace newui;

    bool AssetPropertiesDialog::run(Frame& owner, AssetPropertiesSession& session, const assetpropsdialog::Callbacks& callbacks) {
        Dialog dialog;
        dialog.setName("assetProperties");
        if (!Bundle::instance().loadDialog(dialog)) {
            return false;
        }
        bool saved = false;
        assetpropsdialog::Session ui(dialog.rootView(), session, callbacks);
        ui.wire([&dialog, &saved](DialogResult result) {
            saved = result == DialogResult::Ok;
            dialog.close(result);
        });
        ui.start();
#ifdef CURVY_GUI_AUTOMATION
        dialog.rootView().setName("assetPropertiesRoot");   // the workspace's own root is "rootview" too, and the driver finds the first
        Automation::expose(dialog.rootView());
#endif
        dialog.showModal(&owner);
        return saved;
    }

}
