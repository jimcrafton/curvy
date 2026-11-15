#include "curvy/panelframe.h"

namespace curvy {

    using namespace newui;

    bool PanelFrame::handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT) {
        const bool handled = ToolFrame::handleMessage(message, wParam, lParam, outLRESULT);
        if (message == WM_MOVING || message == WM_EXITSIZEMOVE) {
            POINT cursor = {};
            ::GetCursorPos(&cursor);
            const Point at(float(cursor.x), float(cursor.y));
            if (message == WM_MOVING) {
                moved_ = true;
                onMoving(*this, at);
            } else if (moved_) {
                moved_ = false;
                onMoveEnded(*this, at);
            }
        }
        return handled;
    }

}
