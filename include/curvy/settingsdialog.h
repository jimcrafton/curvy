#pragma once

#include "curvy/settings.h"

#include <newui/frame.h>

#include <functional>

namespace curvy {

    // The settings dialog (Resources/settings.newui, from docs/mockups/settings.html). It edits a copy of the settings and calls `apply` with
    // it after every change, so the workspace follows live; Cancel applies the original again. The left toolbar and right-hand boxes toggles
    // are the left and right dock areas.
    // @reflect ignore=true
    class SettingsDialog {
    public:
        // True when the user pressed OK (settings then holds the choices); false on Cancel/close or if the file cannot be loaded.
        static bool run(newui::Frame& owner, Settings& settings, const std::function<void(const Settings&)>& apply);
    };

}
