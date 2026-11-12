#pragma once

#include <newui/subview.h>

namespace curvy {

    class GraphView;

    // The look the two boxes share: a drop shadow under them (drawn by the GraphView, since a child cannot paint outside its own bounds) and a
    // crisp outline with a faint top highlight (drawn by the box on top of its fill).
    // @reflect ignore=true
    class GraphOverlayChrome {
    public:
        static void paintShadow(BLContext& ctx, const BLRect& box);
        static void paintOutline(BLContext& ctx, const newui::Rect& client);
    };

    // The two small boxes laid over the graph view: GraphPreview shows the document's page and artwork at the top left, GraphMiniMap shows
    // every node of the shown network with the part in view outlined, at the lower right. Both read from the GraphView they sit in.

    // @reflect category=basic
    class GraphPreview : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        GraphPreview();
        ~GraphPreview() override = default;

        void paint(BLContext& ctx) override;

    private:
        GraphView* graph() const;
    };

    // A press or drag recentres the graph view on that spot.
    // @reflect category=basic
    class GraphMiniMap : public newui::SubView {
    public:
        GraphMiniMap();
        ~GraphMiniMap() override = default;

        void paint(BLContext& ctx) override;

    private:
        struct Frame {
            double scale = 1.0, offsetX = 0.0, offsetY = 0.0;
            double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
        };

        GraphView* graph() const;
        Frame frame() const;
        void recentre(const newui::Point& pt);
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
    };

}
