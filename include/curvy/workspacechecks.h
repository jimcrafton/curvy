#pragma once

namespace curvy {

    class WorkspaceController;

    // --check-workspace: drives the workspace headlessly (keys and presses sent to the canvas) and prints ok / FAIL per expectation;
    // returns the number of failures.
    int runWorkspaceChecks(WorkspaceController& workspace);

}
