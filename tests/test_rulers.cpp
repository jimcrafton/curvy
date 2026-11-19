#include "curvy/guidedrag.h"
#include "curvy/rulers.h"

#include "testsupport.h"

#include <newui/mouse_constants.h>
#include <newui/pen.h>

using curvy::LengthUnit;
using curvy::Rulers;

TEST(Rulers, theMajorStepIsOneTwoOrFiveTimesAPowerOfTenAtLeastTheMinimumApart) {
    EXPECT_DOUBLE_EQ(100.0, Rulers::majorStep(1.0));   // 64 px wanted at 1 px per unit
    EXPECT_DOUBLE_EQ(50.0, Rulers::majorStep(1.5));    // 42.7 wanted
    EXPECT_DOUBLE_EQ(10.0, Rulers::majorStep(8.0));    // 8 wanted
    EXPECT_DOUBLE_EQ(5.0, Rulers::majorStep(16.0));    // 4 wanted
    EXPECT_DOUBLE_EQ(1.0, Rulers::majorStep(64.0));    // exactly the minimum
    EXPECT_DOUBLE_EQ(0.1, Rulers::majorStep(640.0));
    EXPECT_DOUBLE_EQ(1.0, Rulers::majorStep(0.0)) << "a nonsense scale still gives a usable step";
}

TEST(Rulers, subdivisionsFollowTheMantissa) {
    EXPECT_EQ(10, Rulers::subdivisions(1.0));
    EXPECT_EQ(10, Rulers::subdivisions(100.0));
    EXPECT_EQ(4, Rulers::subdivisions(2.0));
    EXPECT_EQ(4, Rulers::subdivisions(0.2));
    EXPECT_EQ(5, Rulers::subdivisions(5.0));
    EXPECT_EQ(5, Rulers::subdivisions(50.0));
}

TEST(Rulers, pixelTicksAtFullZoomAreEveryTenWithALabelEveryHundred) {
    // 72 ppi: one pixel is one point.
    const std::vector<Rulers::Tick> ticks = Rulers::ticks(0.0, 200.0, 1.0, LengthUnit::Pixels, 72.0);
    ASSERT_EQ(21u, ticks.size());
    EXPECT_DOUBLE_EQ(0.0, ticks.front().points);
    EXPECT_TRUE(ticks[0].major);
    EXPECT_FALSE(ticks[1].major);
    EXPECT_DOUBLE_EQ(10.0, ticks[1].points);
    EXPECT_TRUE(ticks[10].major);
    EXPECT_DOUBLE_EQ(100.0, ticks[10].value);
    EXPECT_TRUE(ticks[20].major);
}

TEST(Rulers, millimetreTicksAreInPointsButReadInMillimetres) {
    const std::vector<Rulers::Tick> ticks = Rulers::ticks(0.0, 72.0, 4.0, LengthUnit::Millimeters, 72.0);
    ASSERT_FALSE(ticks.empty());
    bool foundTen = false;
    for (const Rulers::Tick& tick : ticks) {
        EXPECT_NEAR(tick.value * 72.0 / 25.4, tick.points, 1e-9);
        if (tick.major && tick.value == 10.0) {
            foundTen = true;
        }
    }
    EXPECT_TRUE(foundTen);
}

TEST(Rulers, tooDenseMinorTicksAreLeftOut) {
    const std::vector<Rulers::Tick> ticks = Rulers::ticks(0.0, 1000.0, 1.0, LengthUnit::Pixels, 72.0, 20.0);   // minor ticks would be 10 px apart
    EXPECT_EQ(11u, ticks.size());
    for (const Rulers::Tick& tick : ticks) {
        EXPECT_TRUE(tick.major);
    }
}

TEST(Rulers, aRangeStartingBeforeZeroCoversTheNegativeSide) {
    const std::vector<Rulers::Tick> ticks = Rulers::ticks(-35.0, 15.0, 1.0, LengthUnit::Pixels, 72.0);
    ASSERT_FALSE(ticks.empty());
    EXPECT_DOUBLE_EQ(-30.0, ticks.front().points);
    EXPECT_DOUBLE_EQ(10.0, ticks.back().points);
}

TEST(Rulers, regionsAreTheTwoStripsAndTheirCorner) {
    EXPECT_EQ(Rulers::Region::Corner, Rulers::regionAt(3.0f, 3.0f, true));
    EXPECT_EQ(Rulers::Region::Top, Rulers::regionAt(100.0f, 3.0f, true));
    EXPECT_EQ(Rulers::Region::Left, Rulers::regionAt(3.0f, 100.0f, true));
    EXPECT_EQ(Rulers::Region::None, Rulers::regionAt(100.0f, 100.0f, true));
    EXPECT_EQ(Rulers::Region::None, Rulers::regionAt(3.0f, 3.0f, false));
    EXPECT_EQ(Rulers::Region::None, Rulers::regionAt(-1.0f, 3.0f, true));
}

namespace {

    class GuideDragTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Guides", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        curvy::SketchDocument& doc() { return window().document(); }
        newui::Point at(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return newui::Point(float(p.x), float(p.y));
        }
        // A point on a ruler: `along` pixels in from the canvas's top left.
        newui::Point onTopRuler(float along) {
            const newui::Rect client = canvas().getClientBounds();
            return newui::Point(client.left() + along, client.top() + 6.0f);
        }
        newui::Point onLeftRuler(float along) {
            const newui::Rect client = canvas().getClientBounds();
            return newui::Point(client.left() + 6.0f, client.top() + along);
        }
        bool press(const newui::Point& pt) { return window().pressed(pen_, pt, newui::mbmLeftButton, 0); }
        void drag(const newui::Point& pt) { canvas().onMouseMove(canvas(), pt, newui::mbmLeftButton, 0); }
        void release(const newui::Point& pt) { canvas().onMouseUp(canvas(), pt, newui::mbmLeftButton, 0); }

        newui::PenState pen_;
    };

}

TEST_F(GuideDragTest, draggingOutOfTheTopRulerMakesAHorizontalGuideInOneUndoStep) {
    EXPECT_TRUE(press(onTopRuler(200.0f)));
    drag(at(300.0, 150.0));
    EXPECT_TRUE(doc().guides().empty()) << "the document is untouched until the button is released";
    release(at(300.0, 150.0));
    ASSERT_EQ(1u, doc().guides().size());
    EXPECT_FALSE(doc().guides()[0].vertical);
    EXPECT_NEAR(150.0, doc().guides()[0].position, 0.01);
    EXPECT_EQ(1u, doc().undoStack().depth());
    doc().undoStack().undo();
    EXPECT_TRUE(doc().guides().empty());
}

TEST_F(GuideDragTest, draggingOutOfTheLeftRulerMakesAVerticalGuide) {
    EXPECT_TRUE(press(onLeftRuler(200.0f)));
    release(at(220.0, 400.0));
    ASSERT_EQ(1u, doc().guides().size());
    EXPECT_TRUE(doc().guides()[0].vertical);
    EXPECT_NEAR(220.0, doc().guides()[0].position, 0.01);
}

TEST_F(GuideDragTest, releasingOnTheRulerAgainMakesNoGuide) {
    EXPECT_TRUE(press(onTopRuler(200.0f)));
    release(onTopRuler(260.0f));
    EXPECT_TRUE(doc().guides().empty());
}

TEST_F(GuideDragTest, aGuideIsPickedUpAndMovedAsOneStep) {
    doc().addGuide(true, 200.0);
    const std::size_t before = doc().undoStack().depth();
    EXPECT_TRUE(press(at(200.0, 300.0)));
    drag(at(230.0, 300.0));
    drag(at(250.0, 310.0));
    EXPECT_DOUBLE_EQ(200.0, doc().guides()[0].position);
    release(at(250.0, 310.0));
    EXPECT_NEAR(250.0, doc().guides()[0].position, 0.01);
    EXPECT_EQ(before + 1, doc().undoStack().depth());
}

TEST_F(GuideDragTest, droppingAGuideBackOnItsRulerDeletesIt) {
    doc().addGuide(false, 300.0);
    EXPECT_TRUE(press(at(100.0, 300.0)));
    release(onTopRuler(250.0f));
    EXPECT_TRUE(doc().guides().empty());
}

TEST_F(GuideDragTest, aLockedGuideIsNotPickedUpButAnotherCanStillBePulledOut) {
    doc().addGuide(true, 200.0);
    doc().setGuidesLocked(true);
    EXPECT_FALSE(press(at(200.0, 300.0))) << "the press goes on to the tool";
    EXPECT_TRUE(press(onTopRuler(200.0f)));
    release(at(300.0, 100.0));
    EXPECT_EQ(2u, doc().guides().size());
}

TEST_F(GuideDragTest, hiddenRulersGiveNoDrag) {
    canvas().setRulersVisible(false);
    EXPECT_EQ(Rulers::Region::None, canvas().rulerAt(onTopRuler(200.0f)));
    EXPECT_FALSE(press(onTopRuler(200.0f)));
}

TEST_F(GuideDragTest, guideAtFindsTheNearestWithinTheToleranceOnly) {
    doc().addGuide(true, 200.0);
    doc().addGuide(false, 300.0);
    EXPECT_EQ(std::optional<std::size_t>(0), canvas().guideAt(at(202.0, 500.0)));
    EXPECT_EQ(std::optional<std::size_t>(1), canvas().guideAt(at(500.0, 298.0)));
    EXPECT_FALSE(canvas().guideAt(at(210.0, 500.0)).has_value());
    doc().setGuidesVisible(false);
    EXPECT_FALSE(canvas().guideAt(at(200.0, 500.0)).has_value());
}

TEST_F(GuideDragTest, snapPullsOntoAGuideOfTheSameAxisOnly) {
    doc().addGuide(true, 200.0);
    EXPECT_DOUBLE_EQ(200.0, canvas().snap(203.0, true));
    EXPECT_DOUBLE_EQ(203.0, canvas().snap(203.0, false)) << "a vertical guide does not snap y";
    EXPECT_DOUBLE_EQ(210.0, canvas().snap(210.0, true)) << "outside the tolerance";
    doc().setGuidesVisible(false);
    EXPECT_DOUBLE_EQ(203.0, canvas().snap(203.0, true));
}

TEST_F(GuideDragTest, theRulersAreChildrenOfTheCanvasAlongItsTopAndLeftEdges) {
    const newui::Rect client = canvas().getClientBounds();
    newui::View* top = canvas().findView("TopRuler");
    newui::View* left = canvas().findView("LeftRuler");
    ASSERT_NE(nullptr, top);
    ASSERT_NE(nullptr, left);
    EXPECT_TRUE(top->isVisible());
    EXPECT_FLOAT_EQ(Rulers::kThickness, top->bounds().height());
    EXPECT_FLOAT_EQ(client.width() - Rulers::kThickness, top->bounds().width());
    EXPECT_FLOAT_EQ(Rulers::kThickness, left->bounds().width());
    canvas().setRulersVisible(false);
    EXPECT_FALSE(top->isVisible());
    EXPECT_FALSE(left->isVisible());
}

TEST_F(GuideDragTest, aPressOnARulerViewPullsOutAGuideAndTheDragFollowsThroughTheView) {
    newui::View* top = canvas().findView("TopRuler");
    ASSERT_NE(nullptr, top);
    const newui::Point local(40.0f, 6.0f);
    top->onMouseDown(*top, local, newui::mbmLeftButton, 0);
    const newui::Point landing = at(300.0, 150.0);
    top->onMouseMove(*top, newui::Point(landing.x - top->bounds().left(), landing.y - top->bounds().top()), newui::mbmLeftButton, 0);
    top->onMouseUp(*top, newui::Point(landing.x - top->bounds().left(), landing.y - top->bounds().top()), newui::mbmLeftButton, 0);
    ASSERT_EQ(1u, doc().guides().size());
    EXPECT_NEAR(150.0, doc().guides()[0].position, 0.01);
}

TEST(Rulers, ticksCountFromTheOriginButStayOnThePage) {
    const std::vector<Rulers::Tick> ticks = Rulers::ticks(0.0, 200.0, 1.0, LengthUnit::Pixels, 72.0, 5.0, 25.0);
    ASSERT_FALSE(ticks.empty());
    EXPECT_DOUBLE_EQ(5.0, ticks.front().points) << "the first tick on the page reads -20, 20 before the origin";
    EXPECT_DOUBLE_EQ(-20.0, ticks.front().value);
    bool zeroAtOrigin = false;
    for (const Rulers::Tick& tick : ticks) {
        EXPECT_DOUBLE_EQ(tick.points - 25.0, tick.value);
        if (tick.value == 0.0) {
            zeroAtOrigin = tick.points == 25.0 && tick.major;
        }
    }
    EXPECT_TRUE(zeroAtOrigin);
}

TEST_F(GuideDragTest, draggingFromTheCornerSetsTheRulerOriginOnReleaseAndPreviewsMeanwhile) {
    const newui::Point corner = onTopRuler(4.0f);
    ASSERT_EQ(Rulers::Region::Corner, canvas().rulerAt(corner));
    EXPECT_TRUE(press(corner));
    drag(at(120.0, 80.0));
    EXPECT_EQ((curvy::RulerOrigin{0.0, 0.0}), doc().rulerOrigin());
    release(at(120.0, 80.0));
    EXPECT_NEAR(120.0, doc().rulerOrigin().x, 0.01);
    EXPECT_NEAR(80.0, doc().rulerOrigin().y, 0.01);
    EXPECT_TRUE(doc().guides().empty());
}

TEST_F(GuideDragTest, aCornerClickOpensTheRulerMenuInsteadOfMovingTheOrigin) {
    std::size_t shown = 0;
    window().presentMenu = [&shown](const std::vector<curvy::PanelMenuEntry>& entries, const newui::Point&) { shown = entries.size(); };
    const newui::Point corner = onTopRuler(4.0f);
    EXPECT_TRUE(press(corner));
    release(corner);
    EXPECT_GT(shown, 0u);
    EXPECT_EQ((curvy::RulerOrigin{0.0, 0.0}), doc().rulerOrigin());
}

TEST_F(GuideDragTest, aRightClickOnARulerOpensTheRulerMenuNotTheRing) {
    std::size_t shown = 0;
    window().presentMenu = [&shown](const std::vector<curvy::PanelMenuEntry>& entries, const newui::Point&) { shown = entries.size(); };
    EXPECT_TRUE(window().pressed(pen_, onTopRuler(200.0f), newui::mbmRightButton, 0));
    EXPECT_GT(shown, 0u);
    EXPECT_FALSE(window().menu().isOpen());
}

TEST_F(GuideDragTest, theRulerMenuTicksTheCurrentUnitsAndItsLinesDoWhatTheyName) {
    std::vector<curvy::PanelMenuEntry> entries = window().rulerMenuEntries();
    const auto line = [&entries](const std::string& text) -> curvy::PanelMenuEntry* {
        for (curvy::PanelMenuEntry& entry : entries) {
            if (entry.text == text) {
                return &entry;
            }
        }
        return nullptr;
    };
    ASSERT_NE(nullptr, line("px"));
    EXPECT_TRUE(line("px")->checked);
    EXPECT_FALSE(line("mm")->checked);
    EXPECT_TRUE(line("deg")->checked);

    line("mm")->run();
    EXPECT_EQ(curvy::LengthUnit::Millimeters, doc().units().length);
    line("rad")->run();
    EXPECT_EQ(curvy::AngleUnit::Radians, doc().units().angle);

    doc().addGuide(true, 50.0);
    window().runCommand("view.lockGuides");
    EXPECT_TRUE(doc().guidesLocked());
    window().runCommand("view.clearGuides");
    EXPECT_EQ(1u, doc().guides().size()) << "locked guides are not cleared";
    window().runCommand("view.lockGuides");
    window().runCommand("view.toggleGuides");
    EXPECT_FALSE(doc().guidesVisible());
    window().runCommand("view.clearGuides");
    EXPECT_TRUE(doc().guides().empty());
    doc().setRulerOrigin(curvy::RulerOrigin{5.0, 6.0});
    window().runCommand("view.resetRulerOrigin");
    EXPECT_EQ((curvy::RulerOrigin{0.0, 0.0}), doc().rulerOrigin());
}

TEST_F(GuideDragTest, theCanvasShowsAResizeCursorOverAnUnlockedGuideOnly) {
    doc().addGuide(true, 200.0);
    doc().addGuide(false, 300.0);
    canvas().setPointer(at(201.0, 500.0));
    EXPECT_EQ(newui::CursorKind::SizeWE, canvas().cursor().kind());
    canvas().setPointer(at(500.0, 299.0));
    EXPECT_EQ(newui::CursorKind::SizeNS, canvas().cursor().kind());
    canvas().setPointer(at(100.0, 100.0));
    EXPECT_EQ(newui::CursorKind::Arrow, canvas().cursor().kind());
    doc().setGuidesLocked(true);
    canvas().setPointer(at(201.0, 500.0));
    EXPECT_EQ(newui::CursorKind::Arrow, canvas().cursor().kind());
}
