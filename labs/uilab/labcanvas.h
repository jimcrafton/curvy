#pragma once

#include "curvy/radialmenu.h"

#include <newui/delegate.h>
#include <newui/pen.h>
#include <newui/subview.h>

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace lab {

    // The prototype drawing surface for trying the pen+keyboard interaction: a grey workspace, artboards, a few shapes, rulers and the
    // ring menu. It has no document model; shapes are plain values, so what is learned here moves to the real canvas.
    // @reflect category=basic
    class LabCanvas : public newui::SubView {
    public:
        LabCanvas();
        ~LabCanvas() override = default;

        // tool, key hints, pointer/zoom info: what the hint bar shows.
        newui::Delegate<LabCanvas, const std::string&, const std::string&, const std::string&> onHint;
        // The document name changed ("" when no document is open).
        newui::Delegate<LabCanvas, const std::string&> onDocumentName;

        // @reflect ignore=true
        bool hasDocument() const { return !boards_.empty(); }
        // @reflect ignore=true
        const std::string& tool() const { return tool_; }
        // @reflect ignore=true
        std::size_t shapeCount() const { return shapes_.size(); }
        // @reflect ignore=true
        std::size_t boardCount() const { return boards_.size(); }
        // @reflect ignore=true
        const std::string& lastMessage() const { return message_; }
        // @reflect ignore=true
        bool ringOpen() const { return menu_ != nullptr && menu_->isOpen(); }
        // @reflect ignore=true
        const std::string& ringTitle() const { return menu_->ring().title; }

        void setBounds(const newui::Rect& bounds) override;
        void paint(BLContext& ctx) override;

    private:
        enum class Kind { Rect, Ellipse, Polygon, Star, Line };
        struct Shape {
            Kind kind = Kind::Rect;
            BLPoint a, b;          // two corners of the box, in world units
            int sides = 5;
            double fillHue = 215, fillL = 88, fillOp = 100;
            double strokeW = 2, strokeHue = 215, strokeOp = 100;
        };
        enum class Part { None, Stroke, Fill };
        // One scrubbable property of a shape part: hold its key and move along the scrub axis.
        struct Prop {
            char key;
            const char* name;
            double Shape::* field;
            double min, max, rate;
            const char* unit;
            int digits;
            std::vector<double> snaps;
        };
        struct Hover {
            int shape = -1;
            Part part = Part::None;
        };
        struct Scrub {
            bool active = false;
            int shape = -1;
            Part part = Part::None;
            const Prop* prop = nullptr;
            std::string key;
            BLPoint anchor;
            double start = 0.0, startValue = 0.0;
        };
        enum class ScrubAxis { Horizontal, Vertical };
        ScrubAxis scrubAxis_ = ScrubAxis::Vertical;
        struct Board {
            BLRect rect;
            std::string name;
        };

        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        newui::SyncReturn handleKeyUp(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        newui::SyncReturn handleCommand(curvy::RadialMenu& sender, const std::string& command);

        double scrubTravel() const;
        BLPoint toWorld(const newui::Point& pt) const;
        BLPoint toView(const BLPoint& world) const;
        int shapeAt(const BLPoint& world) const;
        int boardAt(const BLPoint& world) const;
        BLPath outline(const Shape& shape) const;
        void paintRulers(BLContext& ctx, const newui::Rect& client) const;
        Shape draftShape(bool constrain, bool fromCenter) const;

        void openRing(bool byPress);
        newui::Point ringCenter() const;
        void addBoard(const std::string& name, double width, double height, const BLPoint* at);
        void fitBoards();
        void say(const std::string& text);
        void reportHint();
        void finishDraw(bool commit);
        static const std::vector<Prop>& propsFor(Part part);
        std::vector<BLPoint> outlinePoints(const Shape& shape) const;
        Hover contextAt(const BLPoint& world) const;
        bool startScrub(const std::string& key);
        void updateScrub();
        void endScrub(bool keep);
        void paintScrubCard(BLContext& ctx) const;
        void paintHoverChip(BLContext& ctx) const;
        void paintPenReadout(BLContext& ctx) const;
        void notePen();
        bool eraseUnderPen(const BLPoint& world);
        static void paintIcon(BLContext& ctx, char key, double cx, double cy, double hue);

        // A hint shows until the user has done its thing a few times.
        bool fresh(const char* feature) const;
        void used(const char* feature);
        std::string idleHint() const;
        static std::string keyName(std::uint32_t vkCode);
        static Kind kindForKey(const std::string& key, bool& found);

        curvy::RadialMenu* menu_ = nullptr;
        std::vector<Board> boards_;
        std::vector<Shape> shapes_;
        int selected_ = -1;
        std::string tool_ = "Select";
        bool toolSticky_ = false;
        std::string heldToolKey_;
        std::string message_;
        bool showRulers_ = true;
        std::map<std::string, int> uses_;
        newui::PenState pen_;
        bool penSeen_ = false;
        double drawPressure_ = 0.0;
        Hover hover_;
        Scrub scrub_;

        double zoom_ = 1.0;
        double panX_ = 0.0, panY_ = 0.0;
        newui::Point pointer_;
        bool pointerKnown_ = false;
        bool shiftDown_ = false, altDown_ = false;

        bool spaceDown_ = false, spaceUsed_ = false;
        std::chrono::steady_clock::time_point spaceStart_;
        bool panning_ = false;
        BLPoint panFrom_;

        bool drawing_ = false;
        Kind drawKind_ = Kind::Rect;
        BLPoint drawStart_, drawNow_;
        int drawSides_ = 5;
    };

}
