#include "curvy/settingsdialog.h"

#include "curvy/settingsdialogsession.h"

#ifdef CURVY_GUI_AUTOMATION
#include <newui/automation.h>
#endif
#include <newui/bundle.h>
#include <newui/dialogs.h>

namespace curvy {

    using namespace newui;

    bool SettingsDialog::run(Frame& owner, Settings& settings, const std::function<void(const Settings&)>& apply) {
        Dialog dialog;
        dialog.setName("settings");
        if (!Bundle::instance().loadDialog(dialog)) {
            return false;
        }
        const Settings original = settings;
        Settings working = settings;
        settingsdialog::Session session(dialog.rootView(), working, apply);
        session.wire([&dialog](DialogResult result) { dialog.close(result); });
        session.start();
#ifdef CURVY_GUI_AUTOMATION
        dialog.rootView().setName("settingsRoot");   // the workspace's own root is "rootview" too, and the driver finds the first
        Automation::expose(dialog.rootView());   // so a GUI test can reach the dialog's views while it is open
#endif
        if (dialog.showModal(&owner) != DialogResult::Ok) {
            apply(original);
            return false;
        }
        settings = working;
        return true;
    }

}
