#include "testsupport.h"

#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/pen.h>

namespace {

    class CanvasSelectTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Select", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            layer_ = window().document().addLayer();
            look_ = window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
            second_ = window().document().addCircle(layer_, 300.0f, 100.0f, 30.0f, newui::Color(0.0f, 0.0f, 1.0f, 1.0f));
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        curvy::SelectionModel& selection() { return window().graph().selectionModel(); }
        newui::Point at(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return newui::Point(float(p.x), float(p.y));
        }
        bool press(double x, double y, std::uint32_t keys = 0) { return window().pressed(pen_, at(x, y), newui::mbmLeftButton, keys); }
        // Windows sends the double-click in place of the second press.
        bool doubleClick(double x, double y, std::uint32_t keys = 0) { return window().doubleClicked(pen_, at(x, y), newui::mbmLeftButton, keys); }
        std::string primaryType() { return window().document().node(selection().primary()).typeName(); }

        newui::PenState pen_;
        curvy::NodeRef layer_, look_, second_;
    };

}

TEST_F(CanvasSelectTest, clickingAShapeSelectsTheNodeThatMadeIt) {
    EXPECT_TRUE(selection().empty());
    EXPECT_TRUE(press(100.0, 100.0));
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(primaryType(), "circle");
    EXPECT_EQ(selection().fill(), look_);   // the look nodes of that shape are found from it
}

TEST_F(CanvasSelectTest, clickingAnotherShapeReplacesTheSelection) {
    press(100.0, 100.0);
    const curvy::NodeRef first = selection().primary();
    EXPECT_TRUE(press(300.0, 100.0));
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_NE(selection().primary(), first);
    EXPECT_EQ(selection().fill(), second_);
}

TEST_F(CanvasSelectTest, shiftAddsAndEmptyCanvasClears) {
    press(100.0, 100.0);
    EXPECT_TRUE(press(300.0, 100.0, newui::kmShift));
    EXPECT_EQ(selection().items().size(), 2u);

    EXPECT_FALSE(press(600.0, 700.0, newui::kmShift)) << "shift on empty canvas keeps the selection";
    EXPECT_EQ(selection().items().size(), 2u);

    EXPECT_TRUE(press(600.0, 700.0));
    EXPECT_TRUE(selection().empty());
    EXPECT_FALSE(press(600.0, 700.0)) << "nothing to clear: the press is not used";
}

TEST_F(CanvasSelectTest, clickingTheSelectedShapeAgainKeepsItSelected) {
    press(100.0, 100.0);
    const curvy::NodeRef first = selection().primary();
    EXPECT_TRUE(press(100.0, 100.0));
    EXPECT_EQ(selection().primary(), first) << "a layer is never the pick, and a shape in no group has nothing above it to step to";
    EXPECT_FALSE(selection().isolation().valid());
}

TEST_F(CanvasSelectTest, stepUpDoesNothingWhenNothingEncloses) {
    EXPECT_FALSE(selection().stepUp());
    press(100.0, 100.0);
    EXPECT_TRUE(selection().stepUp()) << "the script call still reaches the layer";
    EXPECT_FALSE(selection().stepUp()) << "a layer is the top";
}

namespace {

    // A group of two circles next to the fixture's two loose ones: (450,400) and (520,400), radius 30.
    class IsolationTest : public CanvasSelectTest {
    protected:
        void SetUp() override {
            CanvasSelectTest::SetUp();
            group_ = window().document().addGroup(layer_, "G");
            window().document().addCircle(group_, 450.0f, 400.0f, 30.0f, newui::Color(1.0f, 1.0f, 0.0f, 1.0f));
            window().document().addCircle(group_, 520.0f, 400.0f, 30.0f, newui::Color(0.0f, 1.0f, 1.0f, 1.0f));
        }

        bool escape() { return canvas().onKeyFilter(0, newui::vkEscape); }
        bool dimmed(double x, double y) {
            const auto hit = canvas().shapeAt(at(x, y));
            return hit.has_value() && selection().isShapeKeyDimmed(hit->key);
        }

        curvy::NodeRef group_;
    };

}

TEST_F(IsolationTest, aClickOnAGroupedShapeSelectsTheWholeGroup) {
    EXPECT_TRUE(press(450.0, 400.0));
    EXPECT_EQ(selection().primary(), group_);
    EXPECT_TRUE(selection().isShapeKeySelected(std::string(window().document().artwork()->shapes().back().key))) << "the group's other circle shows selected too";
    EXPECT_FALSE(selection().isolation().valid());
}

TEST_F(IsolationTest, aDoubleClickEntersTheGroupAndSelectsTheShapeUnderThePointer) {
    press(450.0, 400.0);
    EXPECT_TRUE(doubleClick(450.0, 400.0));
    EXPECT_EQ(selection().isolation(), group_);
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(primaryType(), "circle");
    EXPECT_FALSE(dimmed(450.0, 400.0));
    EXPECT_FALSE(dimmed(520.0, 400.0));
    EXPECT_TRUE(dimmed(100.0, 100.0)) << "the artwork outside the group is dimmed";

    selection().clear();
    EXPECT_TRUE(press(520.0, 400.0));
    EXPECT_EQ(primaryType(), "circle") << "inside the group a click picks one member";
    EXPECT_EQ(selection().isolation(), group_);
}

TEST_F(IsolationTest, insideAGroupTheOutsideCannotBePicked) {
    press(450.0, 400.0);
    doubleClick(450.0, 400.0);
    ASSERT_EQ(selection().isolation(), group_);
    EXPECT_TRUE(press(100.0, 100.0)) << "the press only clears: the dimmed circle is not a hit";
    EXPECT_TRUE(selection().empty());
    EXPECT_EQ(selection().isolation(), group_);
}

TEST_F(IsolationTest, escapeLeavesTheGroupAndSelectsIt) {
    press(450.0, 400.0);
    doubleClick(450.0, 400.0);
    ASSERT_EQ(selection().isolation(), group_);
    EXPECT_TRUE(escape());
    EXPECT_FALSE(selection().isolation().valid());
    EXPECT_EQ(selection().primary(), group_);
    EXPECT_FALSE(dimmed(100.0, 100.0));
    EXPECT_TRUE(escape());
    EXPECT_TRUE(selection().empty()) << "the next Escape clears the selection as before";
}

TEST_F(IsolationTest, aDoubleClickOnEmptyCanvasLeavesTheGroup) {
    press(450.0, 400.0);
    doubleClick(450.0, 400.0);
    ASSERT_EQ(selection().isolation(), group_);
    press(600.0, 700.0);
    EXPECT_TRUE(doubleClick(600.0, 700.0));
    EXPECT_FALSE(selection().isolation().valid());
}

TEST_F(IsolationTest, aDoubleClickOnALooseShapeJustSelectsIt) {
    press(100.0, 100.0);
    EXPECT_TRUE(doubleClick(100.0, 100.0)) << "nothing to enter: it is an ordinary press";
    EXPECT_FALSE(selection().isolation().valid());
    EXPECT_EQ(primaryType(), "circle");
}

TEST_F(IsolationTest, aDoubleClickWithAModifierDoesNotEnter) {
    press(450.0, 400.0, newui::kmShift);
    doubleClick(450.0, 400.0, newui::kmShift);
    EXPECT_FALSE(selection().isolation().valid());
}

TEST_F(IsolationTest, theIsolationEndsWhenItsGroupIsDeleted) {
    press(450.0, 400.0);
    doubleClick(450.0, 400.0);
    ASSERT_EQ(selection().isolation(), group_);
    window().document().removeGraphNode(group_);
    EXPECT_FALSE(selection().isolation().valid());
}

TEST_F(CanvasSelectTest, theSelectionIsSharedWithTheGraphView) {
    press(100.0, 100.0);
    EXPECT_EQ(window().graph().selectionModel().primary(), selection().primary());
    EXPECT_TRUE(selection().isShapeKeySelected(std::string(window().document().artwork()->shapes().front().key)));
}

TEST_F(CanvasSelectTest, spaceHeldStillPansInsteadOfSelecting) {
    canvas().onKeyFilter(0, newui::vkSpaceBar);
    EXPECT_TRUE(press(100.0, 100.0));
    EXPECT_TRUE(selection().empty());
    canvas().onMouseUp(canvas(), at(100.0, 100.0), newui::mbmLeftButton, 0);
    canvas().onKeyUp(canvas(), 0, 0, 1, newui::vkSpaceBar);
}

TEST_F(CanvasSelectTest, aClosedPathIsSelectedByClickingInsideItAndItsGeometryNodeIsTheSelection) {
    curvy::SketchDocument::PathLook look;
    look.fill = newui::Color(0.0f, 1.0f, 0.0f, 1.0f);
    const curvy::NodeRef last = window().document().addPath(layer_, {{400.0f, 400.0f}, {500.0f, 400.0f}, {500.0f, 500.0f}, {400.0f, 500.0f}}, true, false, look);
    ASSERT_TRUE(last.valid());
    EXPECT_TRUE(press(450.0, 450.0));
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(primaryType(), "geometry");
    EXPECT_EQ(selection().primary(), window().document().pathNodeOf(last));
}

TEST_F(CanvasSelectTest, aStrokedOpenPathIsSelectedOnItsLineNotInTheGapBesideIt) {
    curvy::SketchDocument::PathLook look;
    look.hasFill = false;
    look.hasStroke = true;
    look.strokeWidth = 6.0f;
    window().document().addPath(layer_, {{400.0f, 600.0f}, {550.0f, 600.0f}}, false, false, look);
    EXPECT_FALSE(press(475.0, 640.0));
    EXPECT_TRUE(selection().empty());
    EXPECT_TRUE(press(475.0, 601.0));
    EXPECT_EQ(primaryType(), "geometry");
}

TEST_F(CanvasSelectTest, aPathStaysSelectableAfterItsPointsAreEdited) {
    curvy::SketchDocument::PathLook look;
    const curvy::NodeRef last = window().document().addPath(layer_, {{400.0f, 400.0f}, {500.0f, 400.0f}, {500.0f, 500.0f}}, true, false, look);
    const curvy::NodeRef geometry = window().document().pathNodeOf(last);
    window().document().setPathPoints(geometry, {{300.0f, 600.0f}, {400.0f, 600.0f}, {400.0f, 700.0f}});
    EXPECT_FALSE(press(450.0, 430.0)) << "the old place is empty now";
    EXPECT_TRUE(press(380.0, 620.0));
    EXPECT_EQ(selection().primary(), geometry);
}

namespace {

    // The marquee tests: a drag through the controller as the mouse makes it (press, moves, release), with the two circles of the fixture
    // (centres (100,100) and (300,100), radius 30) as the targets.
    class MarqueeTest : public CanvasSelectTest {
    protected:
        void drag(std::vector<std::pair<double, double>> path, std::uint32_t button = newui::mbmLeftButton, std::uint32_t keys = 0) {
            window().pressed(pen_, at(path.front().first, path.front().second), button, keys);
            for (std::size_t i = 1; i < path.size(); ++i) {
                canvas().onMouseMove(canvas(), at(path[i].first, path[i].second), button, keys);
            }
            canvas().onMouseUp(canvas(), at(path.back().first, path.back().second), button, keys);
        }
    };

}

TEST_F(MarqueeTest, aRectangleFromThePrimaryButtonTakesWhatItFullyContains) {
    drag({{60.0, 60.0}, {200.0, 100.0}, {340.0, 140.0}});
    EXPECT_EQ(selection().items().size(), 2u);
    EXPECT_FALSE(window().marquee().active());
}

TEST_F(MarqueeTest, aRectangleThatOnlyCutsACircleTakesItOnlyWithTheSecondaryButton) {
    drag({{60.0, 60.0}, {80.0, 100.0}, {100.0, 140.0}});
    EXPECT_TRUE(selection().empty()) << "inside: the circle is only half in";
    drag({{60.0, 60.0}, {80.0, 100.0}, {100.0, 140.0}}, newui::mbmRightButton);
    EXPECT_EQ(selection().items().size(), 1u) << "touches";
}

TEST_F(MarqueeTest, aRegionInsideABigShapeTouchesItButDoesNotContainIt) {
    const jitnodes::GeometryHandle artwork = window().document().artwork();
    ASSERT_NE(artwork, nullptr);
    EXPECT_TRUE(curvy::shapesInRegion(*artwork, curvy::rectRegion(95.0, 95.0, 105.0, 105.0), curvy::RegionMode::Inside).empty());
    EXPECT_EQ(curvy::shapesInRegion(*artwork, curvy::rectRegion(95.0, 95.0, 105.0, 105.0), curvy::RegionMode::Touches).size(), 1u);
}

TEST_F(MarqueeTest, withoutTheTwoButtonRuleBothButtonsFollowTheContainmentSetting) {
    settings_.selectTwoButton = false;
    settings_.selectContainment = curvy::Settings::SelectContainment::Touches;
    drag({{60.0, 60.0}, {80.0, 100.0}, {100.0, 140.0}});
    EXPECT_EQ(selection().items().size(), 1u);
    settings_.selectContainment = curvy::Settings::SelectContainment::Inside;
    drag({{60.0, 60.0}, {80.0, 100.0}, {100.0, 140.0}});
    EXPECT_TRUE(selection().empty());
}

TEST_F(MarqueeTest, shiftAddsAndAltSubtracts) {
    press(100.0, 100.0);
    drag({{260.0, 60.0}, {300.0, 100.0}, {340.0, 140.0}}, newui::mbmLeftButton, newui::kmShift);
    EXPECT_EQ(selection().items().size(), 2u);
    drag({{260.0, 60.0}, {300.0, 100.0}, {340.0, 140.0}}, newui::mbmLeftButton, newui::kmAlt);
    EXPECT_EQ(selection().items().size(), 1u);
}

TEST_F(MarqueeTest, aPressWithoutADragIsTheClickThatClears) {
    press(100.0, 100.0);
    ASSERT_FALSE(selection().empty());
    drag({{600.0, 700.0}, {600.0, 700.0}});
    EXPECT_TRUE(selection().empty());
}

TEST_F(MarqueeTest, aDragThatStartsOnAShapeIsNotAMarquee) {
    drag({{100.0, 100.0}, {200.0, 100.0}, {340.0, 140.0}});
    EXPECT_EQ(selection().items().size(), 1u) << "the press selected the circle; nothing was dragged out";
}

TEST_F(MarqueeTest, anEllipseTakesWhatItsOutlineEncloses) {
    settings_.selectRegion = curvy::Settings::SelectRegion::Ellipse;
    drag({{40.0, 40.0}, {200.0, 100.0}, {360.0, 160.0}});
    EXPECT_EQ(selection().items().size(), 2u);
    drag({{40.0, 40.0}, {100.0, 100.0}, {160.0, 160.0}});
    EXPECT_EQ(selection().items().size(), 1u);
}

TEST_F(MarqueeTest, aFreehandLoopTakesWhatItEncloses) {
    settings_.selectRegion = curvy::Settings::SelectRegion::Freehand;
    const std::vector<std::pair<double, double>> diamond = {{60.0, 100.0}, {100.0, 60.0}, {140.0, 100.0}, {100.0, 140.0}, {60.0, 100.0}};
    drag(diamond);
    EXPECT_TRUE(selection().empty()) << "a diamond inside the circle's rim does not contain the circle";
    drag(diamond, newui::mbmRightButton);
    EXPECT_EQ(selection().items().size(), 1u);
}

TEST_F(MarqueeTest, aPolygonTakesOnePressPerCornerAndClosesWithEnter) {
    settings_.selectRegion = curvy::Settings::SelectRegion::Polygon;
    drag({{50.0, 50.0}, {50.0, 50.0}});
    EXPECT_TRUE(window().marquee().polygonOpen());
    drag({{160.0, 50.0}, {160.0, 50.0}});
    drag({{160.0, 150.0}, {160.0, 150.0}});
    drag({{50.0, 150.0}, {50.0, 150.0}});
    EXPECT_TRUE(window().marquee().polygonOpen());
    EXPECT_TRUE(window().marquee().canClose());
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkReturn));
    EXPECT_FALSE(window().marquee().active());
    EXPECT_EQ(selection().items().size(), 1u);
}

TEST_F(MarqueeTest, aPolygonClosesOnAPressNearItsFirstCorner) {
    settings_.selectRegion = curvy::Settings::SelectRegion::Polygon;
    drag({{50.0, 50.0}, {50.0, 50.0}});
    drag({{160.0, 50.0}, {160.0, 50.0}});
    drag({{160.0, 150.0}, {160.0, 150.0}});
    drag({{50.0, 150.0}, {50.0, 150.0}});
    EXPECT_TRUE(window().marquee().polygonOpen());
    drag({{52.0, 52.0}, {52.0, 52.0}});
    EXPECT_FALSE(window().marquee().active());
    EXPECT_EQ(selection().items().size(), 1u);
}

TEST_F(MarqueeTest, escapeCancelsAPolygon) {
    settings_.selectRegion = curvy::Settings::SelectRegion::Polygon;
    drag({{50.0, 50.0}, {50.0, 50.0}});
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkEscape));
    EXPECT_FALSE(window().marquee().active());
    EXPECT_TRUE(selection().empty());
}

TEST_F(MarqueeTest, theMKeyCyclesTheRegionShape) {
    EXPECT_EQ(settings_.selectRegion, curvy::Settings::SelectRegion::Rectangle);
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkLetterM));
    EXPECT_EQ(settings_.selectRegion, curvy::Settings::SelectRegion::Ellipse);
    canvas().onKeyFilter(0, newui::vkLetterM);
    canvas().onKeyFilter(0, newui::vkLetterM);
    canvas().onKeyFilter(0, newui::vkLetterM);
    EXPECT_EQ(settings_.selectRegion, curvy::Settings::SelectRegion::Rectangle);
}

TEST_F(MarqueeTest, theRegionQueryReadsTheArtworkDirectly) {
    const jitnodes::GeometryHandle artwork = window().document().artwork();
    ASSERT_NE(artwork, nullptr);
    EXPECT_EQ(curvy::shapesInRegion(*artwork, curvy::rectRegion(0.0, 0.0, 400.0, 200.0), curvy::RegionMode::Inside).size(), 2u);
    EXPECT_EQ(curvy::shapesInRegion(*artwork, curvy::rectRegion(0.0, 0.0, 100.0, 200.0), curvy::RegionMode::Inside).size(), 0u);
    EXPECT_EQ(curvy::shapesInRegion(*artwork, curvy::rectRegion(0.0, 0.0, 100.0, 200.0), curvy::RegionMode::Touches).size(), 1u);
    EXPECT_EQ(curvy::shapesInRegion(*artwork, curvy::ellipseRegion(0.0, 0.0, 200.0, 200.0), curvy::RegionMode::Inside).size(), 1u);
    EXPECT_EQ(curvy::shapesInRegion(*artwork, curvy::rectRegion(500.0, 500.0, 600.0, 600.0), curvy::RegionMode::Touches).size(), 0u);
    EXPECT_TRUE(curvy::shapesInRegion(*artwork, {}, curvy::RegionMode::Touches).empty());
}

TEST_F(MarqueeTest, controlAselectsEveryShape) {
    EXPECT_TRUE(canvas().onKeyFilter(newui::kmCtrl, newui::vkLetterA));
    EXPECT_EQ(selection().items().size(), 2u);
}

TEST_F(MarqueeTest, escapeClearsTheSelectionAndLeavesAnEmptyOneAlone) {
    press(100.0, 100.0);
    ASSERT_FALSE(selection().empty());
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkEscape));
    EXPECT_TRUE(selection().empty());
    EXPECT_FALSE(canvas().onKeyFilter(0, newui::vkEscape)) << "nothing to clear: Escape is not used";
}

TEST_F(MarqueeTest, tabStepsToTheNextShapeAndShiftTabBackWrappingRound) {
    press(100.0, 100.0);
    ASSERT_EQ(selection().fill(), look_);
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkTab));
    EXPECT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(selection().fill(), second_);
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkTab));
    EXPECT_EQ(selection().fill(), look_) << "after the last shape it comes round to the first";
    EXPECT_TRUE(canvas().onKeyFilter(newui::kmShift, newui::vkTab));
    EXPECT_EQ(selection().fill(), second_) << "Shift+Tab goes back, wrapping the other way";
}

TEST_F(MarqueeTest, tabWithNothingSelectedStillTogglesTheGraph) {
    ASSERT_TRUE(selection().empty());
    const bool before = window().graphShown();
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkTab));
    EXPECT_NE(window().graphShown(), before);
    EXPECT_TRUE(selection().empty());
}

TEST(MarqueeGesture, aDragPastTheThresholdMovesAndAFreehandCollectsItsPath) {
    curvy::Marquee marquee;
    marquee.begin(curvy::Settings::SelectRegion::Freehand, newui::Point(10.0f, 10.0f), curvy::RegionMode::Inside, curvy::SelectionModel::Combine::Replace, false);
    marquee.moveTo(newui::Point(11.0f, 10.0f));
    EXPECT_FALSE(marquee.moved());
    marquee.moveTo(newui::Point(20.0f, 10.0f));
    EXPECT_TRUE(marquee.moved());
    marquee.moveTo(newui::Point(20.0f, 30.0f));
    EXPECT_EQ(marquee.outline().size(), 3u);
    marquee.cancel();
    EXPECT_FALSE(marquee.active());
    EXPECT_TRUE(marquee.outline().empty());
}

TEST(MarqueeGesture, aRectangleOutlineSpansThePressAndThePointer) {
    curvy::Marquee marquee;
    marquee.begin(curvy::Settings::SelectRegion::Rectangle, newui::Point(10.0f, 20.0f), curvy::RegionMode::Touches, curvy::SelectionModel::Combine::Add, true);
    marquee.moveTo(newui::Point(50.0f, 60.0f));
    const std::vector<newui::Point> outline = marquee.outline();
    ASSERT_EQ(outline.size(), 4u);
    EXPECT_FLOAT_EQ(outline[2].x, 50.0f);
    EXPECT_FLOAT_EQ(outline[2].y, 60.0f);
    EXPECT_TRUE(marquee.ringOnClick());
    EXPECT_EQ(marquee.mode(), curvy::RegionMode::Touches);
    EXPECT_EQ(marquee.how(), curvy::SelectionModel::Combine::Add);
}

namespace {

    class CountingTool : public curvy::Tool {
    public:
        explicit CountingTool(std::string id) : id_(std::move(id)) {}
        std::string id() const override { return id_; }
        std::string title() const override { return id_; }
        void activate() override { ++activated; }
        void deactivate() override { ++deactivated; }
        int activated = 0;
        int deactivated = 0;

    private:
        std::string id_;
    };

}

TEST(ToolRegistryTest, theFirstToolIsCurrentAndSwitchingActivatesAndDeactivates) {
    curvy::ToolRegistry registry;
    EXPECT_EQ(registry.current(), nullptr);
    auto& first = static_cast<CountingTool&>(registry.add(std::make_unique<CountingTool>("a")));
    auto& second = static_cast<CountingTool&>(registry.add(std::make_unique<CountingTool>("b")));
    EXPECT_EQ(registry.current(), &first);
    EXPECT_EQ(first.activated, 1);
    EXPECT_EQ(registry.ids(), (std::vector<std::string>{"a", "b"}));

    EXPECT_TRUE(registry.setCurrent("b"));
    EXPECT_EQ(registry.current(), &second);
    EXPECT_EQ(first.deactivated, 1);
    EXPECT_EQ(second.activated, 1);
    EXPECT_FALSE(registry.setCurrent("nope"));
    EXPECT_EQ(registry.current(), &second);
    EXPECT_TRUE(registry.setCurrent("b")) << "already current: nothing to do";
    EXPECT_EQ(second.activated, 1);
    EXPECT_EQ(registry.find("a"), &first);
    EXPECT_EQ(registry.find("zzz"), nullptr);
}

TEST_F(CanvasSelectTest, theSelectToolIsRegisteredAndCurrent) {
    ASSERT_NE(window().tools().current(), nullptr);
    EXPECT_EQ(window().tools().current()->id(), "select");
    EXPECT_EQ(window().tools().find("select"), window().tools().current());
}

TEST_F(CanvasSelectTest, theSelectToolOffersItsSettingsAsOptions) {
    std::vector<curvy::ToolOption> options = window().tools().find("select")->options();
    ASSERT_EQ(options.size(), 3u);
    EXPECT_EQ(options[0].id, "region");
    EXPECT_EQ(options[0].choices.size(), 4u);
    EXPECT_EQ(options[0].get(), 0.0);
    options[0].set(3.0);
    EXPECT_EQ(settings_.selectRegion, curvy::Settings::SelectRegion::Polygon);

    EXPECT_FALSE(options[2].enabled()) << "containment only matters when the two buttons are off";
    options[1].set(0.0);
    EXPECT_FALSE(settings_.selectTwoButton);
    EXPECT_TRUE(options[2].enabled());
    options[2].set(1.0);
    EXPECT_EQ(settings_.selectContainment, curvy::Settings::SelectContainment::Touches);
}

TEST_F(CanvasSelectTest, theOptionsCardIsBuiltFromTheOptionsAndChangesTheSettings) {
    EXPECT_FALSE(window().showToolOptions("nope", newui::Point(10.0f, 10.0f)));
    EXPECT_FALSE(window().optionsCard().isOpen());

    ASSERT_TRUE(window().showToolOptions("select", newui::Point(10.0f, 10.0f)));
    curvy::ToolOptionsCard& card = window().optionsCard();
    EXPECT_TRUE(card.isOpen());
    EXPECT_EQ(card.rowCount(), 3u);
    EXPECT_EQ(card.value(1), 1.0);
    for (std::size_t row = 0; row < card.rowCount(); ++row) {
        ASSERT_NE(card.controlOf(row), nullptr);
        EXPECT_TRUE(card.controlOf(row)->isVisible()) << "row " << row << ": a view made in code starts hidden";
    }

    auto* region = dynamic_cast<newui::SegmentedControl*>(card.controlOf(0));
    ASSERT_NE(region, nullptr);
    region->setSelectedIndex(2);
    EXPECT_EQ(settings_.selectRegion, curvy::Settings::SelectRegion::Freehand) << "a click on the segment sets the option";

    auto* two = dynamic_cast<newui::Toggle*>(card.controlOf(1));
    ASSERT_NE(two, nullptr);
    two->setChecked(false);
    EXPECT_FALSE(settings_.selectTwoButton);

    card.apply(2, 1.0);
    EXPECT_EQ(settings_.selectContainment, curvy::Settings::SelectContainment::Touches);
    card.apply(1, 1.0);
    card.apply(2, 0.0);
    EXPECT_EQ(settings_.selectContainment, curvy::Settings::SelectContainment::Touches) << "greyed while the two buttons decide";
}

TEST_F(CanvasSelectTest, everyRowAndTheFootLieInsideTheCardWithoutOverlap) {
    ASSERT_TRUE(window().showToolOptions("select", newui::Point(10.0f, 10.0f)));
    curvy::ToolOptionsCard& card = window().optionsCard();
    const newui::Rect cardBounds = card.bounds();
    float previousBottom = 0.0f;   // row bounds are in the body, which starts under the 24 px head
    for (std::size_t row = 0; row < card.rowCount(); ++row) {
        const newui::View* holder = card.controlOf(row)->parent();
        ASSERT_NE(holder, nullptr);
        const newui::Rect b = holder->bounds();
        EXPECT_GE(b.top(), previousBottom - 0.5f) << "row " << row << " overlaps the one above";
        EXPECT_GT(b.height(), 10.0f) << "row " << row;
        EXPECT_LE(b.bottom(), cardBounds.height() - 24.0f - 24.0f - 7.0f + 0.5f) << "row " << row << " runs into the foot";
        previousBottom = b.bottom();
        EXPECT_TRUE(holder->isVisible()) << "row " << row;
    }
    newui::View* foot = card.footView();
    ASSERT_NE(foot, nullptr);
    EXPECT_TRUE(foot->isVisible());
    EXPECT_GE(foot->bounds().top(), previousBottom - 0.5f) << "the foot sits under the last row";
    EXPECT_GT(foot->bounds().height(), 10.0f);
    EXPECT_LE(foot->bounds().bottom(), cardBounds.height() - 24.0f + 0.5f);
}

TEST_F(CanvasSelectTest, theCardOpensBesideTheAnchorAndFlipsOrSlidesToStayInsideTheCanvas) {
    const newui::Rect client = canvas().getClientBounds();
    ASSERT_GT(client.width(), 400.0f);
    const newui::Point anchor(40.0f, 60.0f);
    ASSERT_TRUE(window().showToolOptions("select", anchor));
    newui::Rect card = window().optionsCard().bounds();
    EXPECT_GT(card.left(), anchor.x) << "to the right of the anchor";
    EXPECT_LT(card.left() - anchor.x, 30.0f) << "close to it";
    EXPECT_FALSE(card.contains(anchor));

    const newui::Point right(client.width() - 20.0f, 60.0f);
    ASSERT_TRUE(window().showToolOptions("select", right));
    card = window().optionsCard().bounds();
    EXPECT_LT(card.right(), right.x) << "no room on the right: it opens to the left";
    EXPECT_FALSE(card.contains(right));
    EXPECT_LE(card.right(), client.width());

    const newui::Point bottom(client.width() * 0.5f, client.height() - 5.0f);
    ASSERT_TRUE(window().showToolOptions("select", bottom));
    card = window().optionsCard().bounds();
    EXPECT_LE(card.bottom(), client.height()) << "a tall card slides up";
    EXPECT_GE(card.top(), 0.0f);
}

TEST_F(CanvasSelectTest, escapeAndAPressOutsideCloseTheOptionsCard) {
    ASSERT_TRUE(window().showToolOptions("select", newui::Point(10.0f, 10.0f)));
    EXPECT_TRUE(canvas().onKeyFilter(0, newui::vkEscape));
    EXPECT_FALSE(window().optionsCard().isOpen());

    ASSERT_TRUE(window().showToolOptions("select", newui::Point(10.0f, 10.0f)));
    press(600.0, 700.0);
    EXPECT_FALSE(window().optionsCard().isOpen());
}

TEST_F(CanvasSelectTest, theOptionsCommandOfTheSelectSlotOpensTheCard) {
    window().runCommand("options.select");
    EXPECT_TRUE(window().optionsCard().isOpen());
    EXPECT_EQ(window().optionsCard().toolId(), "select");
}

namespace {

    class RingChoiceTest : public CanvasSelectTest {
    protected:
        // Leaves the pointer at a document point (a click that selects nothing there), then opens the ring as Space would.
        std::string ringAt(double x, double y) {
            press(x, y);
            canvas().onMouseUp(canvas(), at(x, y), newui::mbmLeftButton, 0);
            window().openRing(false);
            return window().menu().isOpen() ? window().menu().ring().title : std::string();
        }
    };

}

TEST_F(RingChoiceTest, overAShapeItIsTheObjectRingAndTheShapeIsSelected) {
    selection().clear();
    EXPECT_EQ(ringAt(100.0, 100.0), "Object");
    EXPECT_EQ(selection().items().size(), 1u);
}

TEST_F(RingChoiceTest, overTheEmptyPageItIsTheCreationRing) {
    EXPECT_EQ(ringAt(400.0, 500.0), "Create");
}

TEST_F(RingChoiceTest, overTheWorkspaceAroundThePageItIsTheDocumentRing) {
    EXPECT_EQ(ringAt(-60.0, -60.0), "Document");
}

TEST_F(RingChoiceTest, theCreationRingsSelectSlotHasAnOptionBoxAndPicksTheSelectTool) {
    ASSERT_EQ(ringAt(400.0, 500.0), "Create");
    const auto& slots = window().menu().ring().slots;
    const auto select = std::find_if(slots.begin(), slots.end(), [](const curvy::RadialSlot& slot) { return slot.label == "Select"; });
    ASSERT_NE(select, slots.end());
    EXPECT_EQ(select->command, "tool.select");
    EXPECT_EQ(select->optionsCommand, "options.select");

    window().runCommand("tool.select");
    EXPECT_EQ(window().tools().current()->id(), "select");
}

TEST(CommandWiredTest, onlyWhatRunCommandDoesIsWired) {
    for (const char* command : {"file.open", "file.save", "new.a4", "ring.create", "ring.select", "tool.select", "options.select", "select.by",
                                "dialog.settings", "graph.bypass", "view.theme"}) {
        EXPECT_TRUE(curvy::WorkspaceController::commandWired(command)) << command;
    }
    for (const char* command : {"tool.rect", "tool.star", "options.polygon", "options.star", "object.group", "arrange.bringToFront",
                                "file.exportPdf", "doc.newArtboard", "view.toggleRulers", "dialog.shape", "dialog.object", ""}) {
        EXPECT_FALSE(curvy::WorkspaceController::commandWired(command)) << command;
    }
}

TEST_F(RingChoiceTest, theCardOpensBesideTheOptionBoxThatWasChosen) {
    ASSERT_EQ(ringAt(400.0, 500.0), "Create");
    EXPECT_TRUE(window().menu().handleKey("V", true));
    ASSERT_TRUE(window().optionsCard().isOpen());
    const newui::Point anchor = window().menu().optionAnchor();
    const newui::Rect card = window().optionsCard().bounds();
    EXPECT_FALSE(card.contains(anchor)) << "the card does not cover the box that opened it";
    const float nearestX = anchor.x < card.left() ? card.left() - anchor.x : anchor.x - card.right();
    EXPECT_LT(nearestX, 30.0f) << "and it is right next to it";
}

TEST_F(RingChoiceTest, aSlotThatIsNotBuiltDoesNothingAndTheRingStaysOpen) {
    ASSERT_EQ(ringAt(400.0, 500.0), "Create");
    EXPECT_FALSE(window().menu().commandEnabled("tool.rect"));
    EXPECT_TRUE(window().menu().commandEnabled("tool.select"));
    EXPECT_TRUE(window().menu().handleKey("R")) << "the key is taken, but nothing runs";
    EXPECT_TRUE(window().menu().isOpen());
    EXPECT_TRUE(window().menu().handleKey("P", true)) << "an option box that is not built is just as dead";
    EXPECT_TRUE(window().menu().isOpen());
    EXPECT_TRUE(window().menu().handleKey("V"));
    EXPECT_FALSE(window().menu().isOpen());
    EXPECT_EQ(window().tools().current()->id(), "select");
}

TEST(RadialLayoutTest, anOptionBoxSitsInsideTheSlotAtItsOuterEnd) {
    const int count = 8;
    for (int i = 0; i < count; ++i) {
        const newui::Point slot = curvy::RadialLayout::slotCenter(i, count);
        const newui::Point box = curvy::RadialLayout::optionBoxCenter(i, count);
        EXPECT_FLOAT_EQ(box.y, slot.y);
        EXPECT_LT(std::fabs(box.x - slot.x), curvy::RadialLayout::slotWidth * 0.5f);
        if (curvy::RadialLayout::slotOnLeft(i, count)) {
            EXPECT_LT(box.x, slot.x);
        } else {
            EXPECT_GT(box.x, slot.x);
        }
    }
    EXPECT_TRUE(curvy::RadialLayout::slotOnLeft(6, count)) << "the slot at nine o'clock is on the left";
    EXPECT_FALSE(curvy::RadialLayout::slotOnLeft(2, count));
}

TEST_F(CanvasSelectTest, theBodyOfTheCardIsAsTallAsItsRowsAndFootSoNothingIsClipped) {
    ASSERT_TRUE(window().showToolOptions("select", newui::Point(40.0f, 60.0f)));
    curvy::ToolOptionsCard& card = window().optionsCard();
    const newui::View* body = card.footView()->parent();
    ASSERT_NE(body, nullptr);
    EXPECT_GE(body->bounds().height(), card.footView()->bounds().bottom()) << "a child below the body's bottom edge is not painted";
    EXPECT_NEAR(body->bounds().bottom(), card.bounds().height(), 1.0f);
}
