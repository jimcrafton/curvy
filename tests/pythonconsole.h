#pragma once

#include "curvy/scriptconsole.h"

namespace curvytest {

    // CPython starts once per process, so every test that needs Python shares this console.
    inline curvy::ScriptConsole& sharedConsole() {
        static curvy::ScriptConsole instance(PYTHON_HOME_DIR);
        return instance;
    }

}
