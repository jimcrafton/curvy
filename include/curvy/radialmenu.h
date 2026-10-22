#pragma once

#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/subview.h>

#include <string>
#include <vector>

namespace curvy {

    // @reflect ignore=true
    struct RadialSlot {
        std::string label;
        std::string key;       // shown on the slot and picked with the same key while the ring is open
        std::string command;
    };

    // @reflect ignore=true
    struct RadialRing {
        std::string title;     // the hub's name
        std::string caption;   // under the hub's "...": what tapping the hub opens
        std::string hubCommand;
        std::vector<RadialSlot> slots;
    };

    // Where the pointer is relative to a ring, with no drawing involved.
    struct RadialLayout {
        static constexpr double hubRadius = 46.0;
        static constexpr double ringRadius = 118.0;
        static constexpr double reach = 200.0;     // beyond this the pointer is outside the ring
        static constexpr double slotWidth = 108.0;
        static constexpr double slotHeight = 38.0;
        static constexpr int hub = -1;
        static constexpr int outside = -2;

        // Slot i of n sits at the top for i = 0 and goes clockwise.
        static newui::Point slotCenter(int index, int count);
        // The slot nearest to the direction of (dx, dy) from the ring's centre, the hub, or outside.
        static int slotAt(double dx, double dy, int count);
    };

    // The ring menu: opened at a point (the pen or the cursor), it names itself in the hub, runs a slot's command on a tap or on its
    // key, and runs the hub's command (the "..." dialog) on a tap of the centre. A press that opened it also works as a flick:
    // release over a slot to choose it.
    // @reflect ignore=true
    class RadialMenu : public newui::SubView {
    public:
        RadialMenu();

        void open(const RadialRing& ring, const newui::Point& center, bool openedByPress);
        void close();
        bool isOpen() const { return open_; }
        const RadialRing& ring() const { return ring_; }
        int hot() const { return hot_; }

        // Keys while open; returns true when the key was used.
        bool handleKey(const std::string& key);
        bool handleEscape();

        newui::Delegate<RadialMenu, const std::string&> onCommand;

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        void choose(int hit);
        int hitAt(const newui::Point& pt) const;

        RadialRing ring_;
        newui::Point center_;
        bool open_ = false;
        bool openedByPress_ = false;
        int hot_ = RadialLayout::outside;
    };

}
