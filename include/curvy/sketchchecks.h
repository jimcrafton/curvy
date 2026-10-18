#pragma once

namespace curvy {

    class SketchWindowController;

    // The headless self-checks behind `sketch.exe --check-tools`: they drive the real window (the palette, the panels, the canvas
    // tools, the file list) the way a user would and print one "ok" or "FAIL" line per expectation. Returns the number of failures,
    // which is what the process exits with.
    int runToolChecks(SketchWindowController& window);

}
