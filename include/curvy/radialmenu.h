#pragma once

#include <functional>

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
        std::string optionsCommand;   // when set the slot carries an option box at its outer end; tapping the box (or Shift + the key) runs this instead
    };

    // @reflect ignore=true
    struct RadialRing {
        std::string title;     // the hub's name
        std::string caption;   // under the hub's "...": what tapping the hub opens
        std::string hubCommand;
        std::vector<RadialSlot> slots;
        int bottomSlot = -1;   // turns the ring so this slot sits at the bottom; -1 leaves slot 0 at the top
        // The ring's turn as a fraction of a full circle, for RadialLayout.
        double phase() const { return bottomSlot >= 0 && !slots.empty() ? 0.5 - double(bottomSlot) / double(slots.size()) : 0.0; }
    };

    // Where the pointer is relative to a ring, with no drawing involved.
    struct RadialLayout {
        static constexpr double hubRadius = 46.0;
        static constexpr double ringRadius = 118.0;
        static constexpr double reach = 200.0;     // beyond this the pointer is outside the ring
        static constexpr double slotWidth = 132.0;
        static constexpr double slotHeight = 34.0;
        static constexpr int hub = -1;
        static constexpr int outside = -2;

        // Slot i of n sits at the top for i = 0 and goes clockwise.
        // `phase` turns the whole ring clockwise by that fraction of a circle.
        static newui::Point slotCenter(int index, int count, double phase = 0.0);
        // The slot nearest to the direction of (dx, dy) from the ring's centre, the hub, or outside.
        static int slotAt(double dx, double dy, int count, double phase = 0.0);
        // Where a slot's option box sits relative to the ring's centre: inside the slot's pill, at its outer end (the left end for a slot on
        // the left side of the ring, the right end otherwise), as in docs/mockups/tooloptions.html.
        static newui::Point optionBoxCenter(int index, int count, double phase = 0.0);
        // True when the slot's outer end is its left end.
        static bool slotOnLeft(int index, int count, double phase = 0.0);
        static constexpr double optionBoxReach = 13.0;   // the box's tap target, a pen-sized circle
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

        // Keys while open; returns true when the key was used. With `options` (Shift held) a slot that has options runs its optionsCommand.
        bool handleKey(const std::string& key, bool options = false);
        bool handleEscape();

        newui::Delegate<RadialMenu, const std::string&> onCommand;
        // Asked for every command of the ring when it is drawn and chosen. A command it refuses is shown dimmed and does nothing (the ring
        // stays open). Empty: every command is enabled.
        std::function<bool(const std::string& command)> isEnabled;
        bool commandEnabled(const std::string& command) const;
        // Where the option box of the slot last chosen through its options command sat (the menu's own pixels, the ring's centre included):
        // the point a card opened by that command should sit beside, so the pointer has no distance to travel.
        const newui::Point& optionAnchor() const { return optionAnchor_; }

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        void choose(int hit, bool options = false);
        int optionBoxAt(const newui::Point& pt) const;
        int hitAt(const newui::Point& pt) const;

        RadialRing ring_;
        newui::Point center_;
        newui::Point optionAnchor_;
        bool open_ = false;
        bool openedByPress_ = false;
        int hot_ = RadialLayout::outside;
    };

}
