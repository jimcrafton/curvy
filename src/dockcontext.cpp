#include "curvy/dockcontext.h"

namespace curvy {

    bool DockContext::isDrawingTool(SketchTool tool) {
        switch (tool) {
            case SketchTool::Rectangle:
            case SketchTool::Ellipse:
            case SketchTool::Circle:
            case SketchTool::RoundRect:
            case SketchTool::Polygon:
            case SketchTool::Star:
            case SketchTool::Arc:
            case SketchTool::Line:
            case SketchTool::Pen:
            case SketchTool::Pencil:
            case SketchTool::Paintbrush:
            case SketchTool::Text:
                return true;
            default:
                return false;
        }
    }

    DockBoxes DockContext::boxesFor(const SketchDocument& document, SketchTool tool) {
        DockBoxes boxes;
        const std::vector<std::size_t>& selection = document.selection();
        if (selection.empty()) {
            if (isDrawingTool(tool)) {
                boxes.colour = boxes.stroke = boxes.opacity = true;
                boxes.caption = "New shapes";
            } else {
                boxes.caption = "Nothing selected";
            }
            return boxes;
        }
        if (document.selectedGroup() != 0) {
            boxes.transform = boxes.opacity = true;
            return boxes;
        }
        bool allText = true;
        bool anyText = false;
        for (std::size_t id : selection) {
            const SketchObject* object = document.findById(id);
            if (object != nullptr && object->type() != SketchObjectType::Text) {
                allText = false;
            } else if (object != nullptr) {
                anyText = true;
            }
        }
        boxes.transform = boxes.colour = boxes.opacity = true;
        boxes.stroke = !allText;
        boxes.text = anyText;
        return boxes;
    }

}
