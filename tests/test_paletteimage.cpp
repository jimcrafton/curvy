#include "curvy/paletteimagedialogsession.h"
#include "curvy/paletteimagemodel.h"

#include "testsupport.h"

#include <filesystem>

using curvy::PaletteExtractOptions;
using curvy::PaletteImageModel;
using curvy::paletteimagedialog::Session;

namespace {

    struct Rgb {
        int r, g, b;
    };

    // A 4 by 4 PNG: the first `leftColumns` columns one colour, the rest another. The file is removed when the object goes.
    class TempImage {
    public:
        TempImage(const char* name, Rgb left, Rgb right, int leftColumns = 2) : path_((std::filesystem::temp_directory_path() / name).u8string()) {
            BLImage image(4, 4, BL_FORMAT_PRGB32);
            BLImageData data;
            image.get_data(&data);
            for (int y = 0; y < 4; ++y) {
                std::uint32_t* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
                for (int x = 0; x < 4; ++x) {
                    const Rgb& c = x < leftColumns ? left : right;
                    row[x] = 0xFF000000u | (std::uint32_t(c.r) << 16) | (std::uint32_t(c.g) << 8) | std::uint32_t(c.b);
                }
            }
            image.write_to_file(path_.c_str());
        }
        ~TempImage() { std::filesystem::remove(path_); }
        const std::string& path() const { return path_; }

    private:
        std::string path_;
    };

    const Rgb kRed = {200, 40, 40};
    const Rgb kBlue = {40, 40, 200};

}

TEST(PaletteImageModelTest, aMissingFileIsRefusedAndNothingIsFound) {
    PaletteImageModel model;
    EXPECT_FALSE(model.load((std::filesystem::temp_directory_path() / "curvyNoSuchImage.png").u8string()));
    EXPECT_NE(model.error(), "");
    EXPECT_TRUE(model.found().empty());
    EXPECT_FALSE(model.canCreate());
}

TEST(PaletteImageModelTest, anImageGivesItsColoursAndTheFilesNameForThePalette) {
    TempImage image("curvyModelTwo.png", kRed, kBlue);
    PaletteImageModel model;
    ASSERT_TRUE(model.load(image.path()));
    EXPECT_EQ(model.found().size(), 2u);
    EXPECT_EQ(model.name(), "curvyModelTwo");
    EXPECT_EQ(model.fileName(), "curvyModelTwo.png");
    EXPECT_TRUE(model.canCreate());
    model.setName("");
    EXPECT_FALSE(model.canCreate()) << "a palette needs a name";
}

TEST(PaletteImageModelTest, theCountIsKeptBetweenFourAndSixteen) {
    PaletteImageModel model;
    model.setCount(1);
    EXPECT_EQ(model.count(), 4);
    model.setCount(99);
    EXPECT_EQ(model.count(), 16);
    model.setCount(9);
    EXPECT_EQ(model.count(), 9);
}

TEST(PaletteImageModelTest, aTypedNameSurvivesChoosingAnotherImage) {
    TempImage first("curvyModelFirst.png", kRed, kBlue);
    TempImage second("curvyModelSecond.png", kRed, kBlue);
    PaletteImageModel model;
    ASSERT_TRUE(model.load(first.path()));
    ASSERT_TRUE(model.load(second.path()));
    EXPECT_EQ(model.name(), "curvyModelSecond") << "an untouched name follows the file";
    model.setName("Mine");
    ASSERT_TRUE(model.load(first.path()));
    EXPECT_EQ(model.name(), "Mine");
}

TEST(PaletteImageModelTest, aFailedLoadKeepsThePreviousImage) {
    TempImage image("curvyModelKeep.png", kRed, kBlue);
    PaletteImageModel model;
    ASSERT_TRUE(model.load(image.path()));
    EXPECT_FALSE(model.load((std::filesystem::temp_directory_path() / "curvyNoSuchImage.png").u8string()));
    EXPECT_EQ(model.found().size(), 2u);
    EXPECT_EQ(model.path(), image.path());
}

TEST(PaletteImageModelTest, theOrderChangesWhatComesFirst) {
    TempImage image("curvyModelOrder.png", kBlue, kRed, 3);   // three columns blue, one red
    PaletteImageModel model;
    ASSERT_TRUE(model.load(image.path()));
    ASSERT_EQ(model.found().size(), 2u);
    EXPECT_GT(model.found()[0].b, model.found()[0].r) << "most used first: the blue";
    model.setOrder(PaletteExtractOptions::Order::Hue);
    EXPECT_GT(model.found()[0].r, model.found()[0].b) << "by hue, red (0 degrees) comes before blue";
}

namespace {

    // The dialog loaded from Resources/paletteFromImage.newui and driven like a user would, without showing it.
    class PaletteImageDialogTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            dialog_.setName("paletteFromImage");
            ASSERT_TRUE(newui::Bundle::instance().loadDialog(dialog_));
        }

        void TearDown() override {
            session_.reset();
            WorkspaceTest::TearDown();
        }

        void open(const std::string& path = std::string(), std::function<std::string()> choose = nullptr) {
            if (!path.empty()) {
                ASSERT_TRUE(model_.load(path));
            }
            session_ = std::make_unique<Session>(dialog_.rootView(), model_, choose);
            session_->wire([this](newui::DialogResult result) { result_ = result; });
            session_->start();
            dialog_.rootView().updateLayout();
        }

        template <typename T>
        T* find(const char* name) {
            return dynamic_cast<T*>(dialog_.rootView().findView(name));
        }
        void click(const char* name) {
            auto* button = find<newui::Button>(name);
            ASSERT_NE(button, nullptr) << name;
            button->onClick(*button);
        }

        newui::Dialog dialog_;
        PaletteImageModel model_;
        std::unique_ptr<Session> session_;
        newui::DialogResult result_ = newui::DialogResult::None;
    };

}

TEST_F(PaletteImageDialogTest, loadsWithAllItsControlsAndTheDefaults) {
    TempImage image("curvyDialogDefaults.png", kRed, kBlue);
    open(image.path());
    EXPECT_NE(find<newui::SubView>("picture"), nullptr);
    EXPECT_NE(find<newui::Slider>("count"), nullptr);
    EXPECT_NE(find<newui::DropDownList>("order"), nullptr);
    EXPECT_NE(find<newui::Toggle>("merge"), nullptr);
    EXPECT_NE(find<newui::Toggle>("skip"), nullptr);
    EXPECT_NE(find<newui::TextField>("newName"), nullptr);
    EXPECT_EQ(find<newui::Slider>("count")->minValue(), 4.0f);
    EXPECT_EQ(find<newui::Slider>("count")->maxValue(), 16.0f);
    EXPECT_EQ(find<newui::Slider>("count")->value(), 8.0f);
    EXPECT_EQ(session_->countText(), "8");
    EXPECT_EQ(find<newui::DropDownList>("order")->selectedIndex(), std::optional<std::size_t>(0));
    EXPECT_TRUE(find<newui::Toggle>("merge")->isChecked());
    EXPECT_TRUE(find<newui::Toggle>("skip")->isChecked());
    EXPECT_EQ(session_->imageNameText(), "curvyDialogDefaults.png");
    EXPECT_TRUE(session_->pictureShown());
}

TEST_F(PaletteImageDialogTest, theFoundColoursAreShownAsChipsAndCounted) {
    TempImage image("curvyDialogChips.png", kRed, kBlue);
    open(image.path());
    EXPECT_EQ(session_->chipCount(), 2u);
    EXPECT_EQ(session_->foundText(), "2 colours found");
    EXPECT_TRUE(session_->createEnabled());
    EXPECT_NEAR(session_->chipColor(0).r + session_->chipColor(1).r, (200.0f + 40.0f) / 255.0f, 0.01f) << "the chips hold the found colours";
}

TEST_F(PaletteImageDialogTest, withNoImageThereIsNothingToCreate) {
    open();
    EXPECT_EQ(session_->chipCount(), 0u);
    EXPECT_EQ(session_->foundText(), "Choose an image");
    EXPECT_EQ(session_->imageNameText(), "No image chosen");
    EXPECT_FALSE(session_->createEnabled());
    click("ok");
    EXPECT_EQ(result_, newui::DialogResult::None) << "Create palette does nothing without colours";
}

TEST_F(PaletteImageDialogTest, theControlsChangeTheModel) {
    TempImage image("curvyDialogControls.png", kRed, kBlue);
    open(image.path());
    find<newui::Slider>("count")->setValue(5.0f);
    EXPECT_EQ(model_.count(), 5);
    EXPECT_EQ(session_->countText(), "5");
    find<newui::DropDownList>("order")->setSelectedIndex(std::size_t(1));
    EXPECT_EQ(model_.order(), PaletteExtractOptions::Order::Hue);
    find<newui::DropDownList>("order")->setSelectedIndex(std::size_t(2));
    EXPECT_EQ(model_.order(), PaletteExtractOptions::Order::Lightness);
    find<newui::Toggle>("merge")->setChecked(false);
    EXPECT_FALSE(model_.mergeNearDuplicates());
    find<newui::Toggle>("skip")->setChecked(false);
    EXPECT_FALSE(model_.ignoreNearWhiteAndBlack());
}

TEST_F(PaletteImageDialogTest, chooseImageReplacesThePictureAndTheColoursAndFollowsTheName) {
    TempImage first("curvyDialogFirst.png", kRed, kBlue);
    TempImage second("curvyDialogSecond.png", {20, 160, 60}, {230, 190, 40}, 1);
    open(first.path(), [&]() { return second.path(); });
    click("chooseImage");
    EXPECT_EQ(model_.path(), second.path());
    EXPECT_EQ(session_->imageNameText(), "curvyDialogSecond.png");
    EXPECT_EQ(model_.name(), "curvyDialogSecond");
    EXPECT_EQ(session_->chipCount(), 2u);
}

TEST_F(PaletteImageDialogTest, cancellingTheFileChoiceChangesNothing) {
    TempImage first("curvyDialogKeep.png", kRed, kBlue);
    open(first.path(), []() { return std::string(); });
    click("chooseImage");
    EXPECT_EQ(model_.path(), first.path());
    EXPECT_EQ(session_->chipCount(), 2u);
}

TEST_F(PaletteImageDialogTest, aFileThatIsNotAnImageKeepsTheOldOneAndSaysNothingNewWasLoaded) {
    TempImage first("curvyDialogGood.png", kRed, kBlue);
    const std::string bad = (std::filesystem::temp_directory_path() / "curvyDialogBad.png").u8string();
    open(first.path(), [&]() { return bad; });
    click("chooseImage");
    EXPECT_EQ(model_.path(), first.path());
    EXPECT_EQ(session_->chipCount(), 2u);
}

TEST_F(PaletteImageDialogTest, createTakesTheTypedNameAndCancelEndsWithoutIt) {
    TempImage image("curvyDialogName.png", kRed, kBlue);
    open(image.path());
    find<newui::TextField>("newName")->setText(L"Sunset");
    click("ok");
    EXPECT_EQ(result_, newui::DialogResult::Ok);
    EXPECT_EQ(model_.name(), "Sunset");

    result_ = newui::DialogResult::None;
    click("cancel");
    EXPECT_EQ(result_, newui::DialogResult::Cancel);
}

TEST_F(PaletteImageDialogTest, aBlankNameStopsCreate) {
    TempImage image("curvyDialogBlank.png", kRed, kBlue);
    open(image.path());
    find<newui::TextField>("newName")->setText(L"");
    click("ok");
    EXPECT_EQ(result_, newui::DialogResult::None);
    EXPECT_FALSE(session_->createEnabled());
}
