#include "curvy/newdocumentdialogsession.h"

#include "testsupport.h"

using curvy::NewDocumentModel;
using curvy::newdocdialog::narrow;
using curvy::newdocdialog::Session;

namespace {

    // The dialog loaded from Resources/newdocument.newui and driven like a user would, without showing it.
    class NewDocumentDialogTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            dialog_.setName("newdocument");
            ASSERT_TRUE(newui::Bundle::instance().loadDialog(dialog_));
            session_ = std::make_unique<Session>(dialog_.rootView(), model_);
            session_->wire([](newui::DialogResult) {});
            session_->start();
            dialog_.rootView().updateLayout();
        }

        void TearDown() override {
            session_.reset();
            WorkspaceTest::TearDown();
        }

        template <typename T>
        T* find(const char* name) {
            return dynamic_cast<T*>(dialog_.rootView().findView(name));
        }

        newui::Dialog dialog_;
        NewDocumentModel model_;
        std::unique_ptr<Session> session_;
    };
}

TEST_F(NewDocumentDialogTest, LoadsWithItsPositionAndAllItsControls) {
    EXPECT_EQ(newui::DialogPosition::CenterOnRootView, dialog_.position());
    EXPECT_NE(nullptr, find<newui::SegmentedControl>("categoryControl"));
    EXPECT_NE(nullptr, find<newui::ListView>("presetList"));
    EXPECT_NE(nullptr, find<newui::TextField>("widthField"));
    EXPECT_NE(nullptr, find<newui::TextField>("heightField"));
    EXPECT_NE(nullptr, find<newui::DropDownList>("unitCombo"));
    EXPECT_NE(nullptr, find<newui::SegmentedControl>("colorModeControl"));
    EXPECT_NE(nullptr, find<newui::ToolbarButton>("landscapeButton"));
    EXPECT_NE(nullptr, find<newui::ToolbarButton>("portraitButton"));
    EXPECT_NE(nullptr, find<newui::ToolbarButton>("linkProportionsButton"));
    EXPECT_NE(nullptr, find<newui::DropDownList>("ppiPresetCombo"));
    EXPECT_NE(nullptr, find<newui::Label>("summaryLabel"));
    EXPECT_NE(nullptr, find<curvy::PagePreview>("pagePreview"));
}

TEST_F(NewDocumentDialogTest, OpensOnLetter) {
    EXPECT_EQ(4u, find<newui::SegmentedControl>("categoryControl")->segments().size());
    ASSERT_NE(nullptr, find<newui::ListView>("presetList")->model());
    EXPECT_EQ(10u, find<newui::ListView>("presetList")->model()->size()) << "the Print list has ten presets";
    EXPECT_EQ("8.5", narrow(find<newui::TextField>("widthField")->text()));
    EXPECT_EQ("11", narrow(find<newui::TextField>("heightField")->text()));
    EXPECT_EQ(1u, find<newui::SegmentedControl>("colorModeControl")->selectedIndex()) << "Letter is a CMYK preset";
    EXPECT_TRUE(find<newui::ToolbarButton>("portraitButton")->isChecked());
    EXPECT_FALSE(find<newui::ToolbarButton>("landscapeButton")->isChecked());
}

TEST_F(NewDocumentDialogTest, ChoosingAPresetLoadsIt) {
    find<newui::SegmentedControl>("categoryControl")->setSelectedIndex(1);   // Web
    auto* list = find<newui::ListView>("presetList");
    EXPECT_EQ(6u, list->model()->size());
    EXPECT_EQ("1920", narrow(find<newui::TextField>("widthField")->text()));
    EXPECT_EQ("1080", narrow(find<newui::TextField>("heightField")->text()));
    EXPECT_EQ(0u, find<newui::SegmentedControl>("colorModeControl")->selectedIndex()) << "Full HD is an RGB preset";
    EXPECT_TRUE(find<newui::ToolbarButton>("landscapeButton")->isChecked());
    EXPECT_FALSE(find<newui::ToolbarButton>("portraitButton")->isChecked());
    auto* preview = find<curvy::PagePreview>("pagePreview");
    EXPECT_EQ(1920.0f, preview->pageWidth());
    EXPECT_EQ("1920 px", preview->widthLabel());

    list->setSelectedIndex(std::size_t(5));   // Banner 728 x 90
    EXPECT_EQ("728", narrow(find<newui::TextField>("widthField")->text()));
    EXPECT_EQ("90", narrow(find<newui::TextField>("heightField")->text()));
}

TEST_F(NewDocumentDialogTest, UnitsOrientationAndLinkedProportionsFollowTheNumbers) {
    find<newui::SegmentedControl>("categoryControl")->setSelectedIndex(1);
    find<newui::ListView>("presetList")->setSelectedIndex(std::size_t(5));   // 728 x 90 px
    auto* widthField = find<newui::TextField>("widthField");
    auto* heightField = find<newui::TextField>("heightField");

    find<newui::DropDownList>("unitCombo")->setSelectedIndex(std::size_t(4));   // inches
    EXPECT_EQ("10.11", narrow(widthField->text())) << "px to in at 72 ppi keeps the physical size";
    EXPECT_NE(std::string::npos, find<newui::Label>("summaryLabel")->text().find("728 x 90 pt"));

    find<newui::ToolbarButton>("portraitButton")->setChecked(true);
    EXPECT_EQ("1.25", narrow(widthField->text())) << "portrait swaps width and height";
    EXPECT_EQ("10.11", narrow(heightField->text()));
    EXPECT_FALSE(find<newui::ToolbarButton>("landscapeButton")->isChecked());

    auto* link = find<newui::ToolbarButton>("linkProportionsButton");
    link->setChecked(true);
    widthField->setText(L"2.5");
    widthField->onReturnPressed(*widthField);
    EXPECT_EQ("20.22", narrow(heightField->text())) << "linked proportions scale the height";
    link->setChecked(false);
}

TEST_F(NewDocumentDialogTest, ResolutionColorModeAndBackgroundChangeTheModel) {
    find<newui::DropDownList>("ppiPresetCombo")->setSelectedIndex(std::size_t(1));   // 96 ppi
    EXPECT_EQ("96", narrow(find<newui::TextField>("ppiField")->text()));
    find<newui::SegmentedControl>("colorModeControl")->setSelectedIndex(1);
    EXPECT_EQ(curvy::ColorSpace::CMYK, model_.colorMode());
    find<newui::DropDownList>("backgroundCombo")->setSelectedIndex(std::size_t(1));
    EXPECT_EQ(curvy::PageBackground::Transparent, model_.background());
}
