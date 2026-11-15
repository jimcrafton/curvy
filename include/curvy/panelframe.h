#pragma once

#include <newui/delegate.h>
#include <newui/toolframe.h>

namespace curvy {

    // The tool window a floating panel lives in. It tells its owner where the pointer is while the user drags the window by its caption, and where it let
    // go, so the owner can dock the panel back when the pointer is over its dock. A resize from the edge is not a move and fires nothing.
    // @reflect ignore=true
    class PanelFrame : public newui::ToolFrame {
    public:
        // Screen coordinates of the pointer.
        newui::Delegate<PanelFrame, const newui::Point&> onMoving;
        newui::Delegate<PanelFrame, const newui::Point&> onMoveEnded;

    protected:
        bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT) override;

    private:
        bool moved_ = false;
    };

}
