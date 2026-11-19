#include "curvy/tool.h"

#include <algorithm>

namespace curvy {

    Tool& ToolRegistry::add(std::unique_ptr<Tool> tool) {
        const std::string id = tool->id();
        Tool* replaced = find(id);
        const bool wasCurrent = replaced != nullptr && replaced == current_;
        if (replaced == nullptr) {
            order_.push_back(id);
        } else if (wasCurrent) {
            current_ = nullptr;
        }
        Tool& added = *tool;
        tools_[id] = std::move(tool);
        if (current_ == nullptr) {
            current_ = &added;
            added.activate();
        }
        return added;
    }

    Tool* ToolRegistry::find(const std::string& id) const {
        const auto found = tools_.find(id);
        return found == tools_.end() ? nullptr : found->second.get();
    }

    bool ToolRegistry::setCurrent(const std::string& id) {
        Tool* next = find(id);
        if (next == nullptr) {
            return false;
        }
        if (next == current_) {
            return true;
        }
        if (current_ != nullptr) {
            current_->deactivate();
        }
        current_ = next;
        current_->activate();
        return true;
    }

}
