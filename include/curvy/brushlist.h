#pragma once

#include "curvy/brush.h"

#include <newui/subview.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace curvy {

    // The brush picker: sections (Favorites, Most Recent, then one per category), each header followed by one row per brush showing a sample
    // stroke made with it, its name and a star (a click on the star toggles Favorites). A click on a row picks the brush; a click on a header
    // folds or unfolds its section; the wheel scrolls. The Brushes panel fills it (setSections) and applies what is picked.
    // @reflect category=basic
    class BrushList : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        BrushList();
        virtual ~BrushList() {}

        struct Section {
            std::string title;
            std::vector<std::shared_ptr<const BrushDef>> brushes;
        };
        // Replaces what is listed; `active` is the picked brush (0 = none). Folded sections stay folded and the scroll position stays.
        void setSections(std::vector<Section> sections, std::size_t active);

        // The brush of the last click on a row, and whether that click was on its star.
        std::size_t pickedBrush() const { return picked_; }
        bool pickedStar() const { return pickedStar_; }
        newui::Delegate<BrushList> onBrushPicked;

        // Rows as laid out for the current content; y is the top, in content coordinates.
        struct Row {
            bool header = false;
            std::size_t section = 0;
            std::shared_ptr<const BrushDef> brush;
            float y = 0.0f;
            float height = 0.0f;
        };
        const std::vector<Row>& rows() const { return rows_; }
        float contentHeight() const { return contentHeight_; }
        std::size_t activeBrush() const { return active_; }
        // Index of the row at `y` (from the top of the view), -1 for none.
        int rowAt(float y) const;
        void setScroll(float offset);
        float scroll() const { return scroll_; }
        bool isFolded(const std::string& title) const { return folded_.count(title) != 0; }

        static constexpr float kRowHeight = 30.0f;
        static constexpr float kHeaderHeight = 20.0f;
        static constexpr float kStarWidth = 22.0f;
        static constexpr float kBarWidth = 10.0f;
        // The scroll bar (drawn on the right when the content is taller than the view): its thumb as {top, height} from the top of the view.
        bool scrollable() const { return contentHeight_ > getClientBounds().height() + 0.5f; }
        std::pair<float, float> thumb() const;

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseWheel(newui::View& sender, const newui::Point& pt, float delta);
        void layout();
        const BLImage& thumbnail(const std::shared_ptr<const BrushDef>& brush, int width, int height);

        std::vector<Section> sections_;
        std::vector<Row> rows_;
        std::set<std::string> folded_;
        std::map<std::pair<const BrushDef*, int>, BLImage> thumbnails_;
        std::size_t active_ = 0;
        std::size_t picked_ = 0;
        bool pickedStar_ = false;
        float contentHeight_ = 0.0f;
        float scroll_ = 0.0f;
        bool draggingThumb_ = false;
        float thumbGrab_ = 0.0f;   // where in the thumb it was grabbed
    };

}
