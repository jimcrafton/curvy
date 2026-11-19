#pragma once

#include <blend2d/blend2d.h>
#include <newui/geometry.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace curvy {

    class CanvasView;
    class SelectionModel;
    class SketchDocument;
    struct Settings;

    // One press, drag step or release as the pen or mouse made it, in the canvas's own pixels and in document units.
    // @reflect ignore=true
    struct ToolEvent {
        newui::Point view;
        BLPoint document;
        std::uint32_t buttons = 0;
        std::uint32_t keys = 0;     // newui key modifier mask (kmShift, kmCtrl, kmAlt)
        bool secondary = false;     // the right mouse button or the pen's barrel button
        bool pen = false;
    };

    // @reflect ignore=true
    struct ToolKeyEvent {
        std::uint32_t vk = 0;
        std::uint32_t mask = 0;
    };

    // One setting a tool offers for its next use. The options card is built from this list, so a tool never draws UI; the same list can be
    // read by the Settings dialog or a script. Every value is a double: a choice is the index into `choices`, a toggle is 0 or 1.
    // @reflect ignore=true
    struct ToolOption {
        enum class Kind { Choice, Toggle, Number };

        std::string id;
        std::string label;
        Kind kind = Kind::Number;
        std::vector<std::string> choices;
        double min = 0.0;
        double max = 1.0;
        double reset = 0.0;              // what the card's Reset puts back
        std::function<double()> get;
        std::function<void(double)> set;
        std::function<bool()> enabled;   // empty: always; false: the row is shown greyed or hidden by the card
    };

    // What a tool reaches the editor through. The controller fills it in; tools hold it by reference and never see the controller.
    // @reflect ignore=true
    struct ToolContext {
        SketchDocument& document;
        SelectionModel& selection;
        CanvasView& canvas;
        Settings& settings;
        std::function<void(const std::string&)> say;   // the status bar line
        std::function<void()> saveSettings;
        std::function<void()> openRing;                // the ring at the pointer
        std::function<void()> showSelection;           // brings the dock panels that name the selection into view
        std::function<bool()> documentOpen;
        std::function<bool()> graphShown;
        std::function<bool()> menuOpen;
    };

    // A canvas tool: it owns its gesture state, turns input into document or selection changes and paints its own overlay. The controller
    // keeps what sits above any tool (Space panning, the rings, the eraser end, shortcuts that are not a tool's) and hands everything else
    // to the current tool. A tool changes the document through the same operations scripts use, so each finished gesture is one undo step.
    // @reflect ignore=true
    class Tool {
    public:
        virtual ~Tool() = default;

        virtual std::string id() const = 0;      // "select": what ring slots, commands and scripts call it
        virtual std::string title() const = 0;   // "Select": what the options card shows

        // The tool became or stopped being the current one; deactivate() ends any half-made gesture.
        virtual void activate() {}
        virtual void deactivate() { cancel(); }
        // Drops the gesture in progress (Escape, a change of tool).
        virtual void cancel() {}
        // True while a gesture is in progress, so the controller knows not to treat the next key or press as its own.
        virtual bool busy() const { return false; }

        // True when the press was used; false lets the controller carry on (the secondary button then opens the ring).
        virtual bool inputDown(const ToolEvent&) { return false; }
        // The system's double-click (Windows sends it instead of the second press, for mouse and pen alike). False: the controller treats it
        // as an ordinary press.
        virtual bool inputDoubleClick(const ToolEvent&) { return false; }
        virtual void inputMove(const ToolEvent&) {}
        virtual void inputUp(const ToolEvent&) {}
        // True when the key was used.
        virtual bool keyDown(const ToolKeyEvent&) { return false; }
        virtual bool keyUp(const ToolKeyEvent&) { return false; }

        // Drawn over the canvas, in its pixels, after the selection outline.
        virtual void paintOverlay(BLContext&) const {}

        virtual std::vector<ToolOption> options() { return {}; }
        // The line at the foot of the options card.
        virtual std::string optionsNote() const { return "Applies to the next use"; }
    };

    // The tools by id, in the order they were added, and which one is current.
    // @reflect ignore=true
    class ToolRegistry {
    public:
        // Takes the tool; the first one added becomes current. A second tool with the same id replaces the first.
        Tool& add(std::unique_ptr<Tool> tool);
        Tool* find(const std::string& id) const;
        Tool* current() const { return current_; }
        // Deactivates the old tool and activates the new one; false (nothing changes) for an unknown id.
        bool setCurrent(const std::string& id);
        const std::vector<std::string>& ids() const { return order_; }

    private:
        std::map<std::string, std::unique_ptr<Tool>> tools_;
        std::vector<std::string> order_;
        Tool* current_ = nullptr;
    };

}
