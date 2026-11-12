#pragma once

#include <newui/controls.h>
#include <newui/subview.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The breadcrumb of the graph view: "root > Layer1 > Group1". Every level above the current one is a hot link (hand cursor, amber on hover);
    // the current level is the plain label that already sits in the container. Labels are pooled, never destroyed, so show() is cheap.
    // @reflect ignore=true
    class GraphCrumbs {
    public:
        // The container lays its children out in a row; `current` is one of them and stays the last.
        GraphCrumbs(newui::SubView& container, newui::Label& current);

        // names: the levels from the root to the current one (GraphView::levelNames()).
        void show(const std::vector<std::string>& names);

        // A link was clicked: the level index (0 is the root).
        std::function<void(std::size_t level)> onNavigate;

        // For tests: the link labels in use, root first.
        std::size_t linkCount() const { return used_; }
        newui::Label* link(std::size_t level) const { return level < used_ ? segments_[level].name : nullptr; }

    private:
        struct Segment {
            newui::Label* name = nullptr;
            newui::Label* separator = nullptr;
        };

        void addSegment();
        void fit(newui::Label& label, const std::string& text) const;

        newui::SubView& container_;
        newui::Label& current_;
        std::vector<Segment> segments_;
        std::size_t used_ = 0;
    };

}
