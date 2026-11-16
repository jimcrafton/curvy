#include "curvy/paletteimagedialog.h"

#include "curvy/paletteimagedialogsession.h"

#ifdef CURVY_GUI_AUTOMATION
#include <newui/automation.h>
#endif
#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/dialogs.h>

namespace curvy {

    using namespace newui;

    bool PaletteImageDialog::run(Frame& owner, PaletteImageModel& model) {
        Dialog dialog;
        dialog.setName("paletteFromImage");
        if (!Bundle::instance().loadDialog(dialog)) {
            return false;
        }
        dialog.setTitle("New palette from image");
        PaletteImageModel working = model;
        const auto choose = [&dialog]() {
            FileDialogOptions options;
            options.title = "Choose image";
            options.filters.push_back({"Images", "*.png;*.jpg;*.jpeg;*.bmp"});
            options.filters.push_back({"All Files", "*.*"});
            std::string file;
            return Dialog::showOpenFile(dialog.frameHandle(), options, file) ? file : std::string();
        };
        paletteimagedialog::Session session(dialog.rootView(), working, choose);
        session.wire([&dialog](DialogResult result) { dialog.close(result); });
        session.start();
#ifdef CURVY_GUI_AUTOMATION
        dialog.rootView().setName("paletteImageRoot");   // the workspace's own root is "rootview" too, and the driver finds the first
        Automation::expose(dialog.rootView());
#endif
        if (dialog.showModal(&owner) != DialogResult::Ok) {
            return false;
        }
        model = working;
        return true;
    }

}
