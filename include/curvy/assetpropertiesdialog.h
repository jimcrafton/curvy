#pragma once

#include "curvy/assetpropertiessession.h"

#include <newui/frame.h>

namespace curvy {

    namespace assetpropsdialog {
        struct Callbacks;
    }

    // The Asset Properties dialog (Resources/assetProperties.newui, from docs/mockups/assetProperties.html): edits the AssetPropertiesSession it is
    // given, which holds the working copy. Apply registers the copy in memory and keeps the dialog open; Save (Create for a new asset) writes the file,
    // registers it and closes; Cancel leaves the library as it was. The callbacks are how the editor helps (the file dialog, the selected subnet).
    // @reflect ignore=true
    class AssetPropertiesDialog {
    public:
        // True when the library changed because of Save, or of Edit (which applies the copy first); false on Cancel, or when the file does
        // not load. Changes that Apply made stay either way.
        static bool run(newui::Frame& owner, AssetPropertiesSession& session, const assetpropsdialog::Callbacks& callbacks);
    };

}
