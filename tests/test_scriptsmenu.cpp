#include "testsupport.h"

#include "curvy/scriptsmenu.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

    // A folder of its own per test, emptied first.
    std::filesystem::path freshFolder(const char* name) {
        const std::filesystem::path folder = curvytest::tempFile(name);
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        return folder;
    }

    void write(const std::filesystem::path& file, const std::string& text) {
        std::ofstream out(file, std::ios::binary);
        out << text;
    }

    std::vector<std::string> texts(const std::vector<curvy::PanelMenuEntry>& entries) {
        std::vector<std::string> result;
        for (const curvy::PanelMenuEntry& entry : entries) {
            result.push_back(entry.separator ? std::string("-") : entry.text);
        }
        return result;
    }

}

TEST(ScriptsMenu, scriptsInListsOnlyPythonFilesByNameIgnoringCase) {
    const auto folder = freshFolder("curvy_scripts_list");
    write(folder / "beta.py", "");
    write(folder / "Alpha.PY", "");
    write(folder / "gamma.txt", "");
    write(folder / "delta.py.bak", "");
    std::filesystem::create_directories(folder / "sub.py");   // a folder called sub.py is not a script
    write(folder / "sub.py" / "inner.py", "");

    const auto files = curvy::ScriptsMenu::scriptsIn(folder);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].name, "Alpha");
    EXPECT_EQ(files[1].name, "beta");
    EXPECT_EQ(files[1].path, folder / "beta.py");
    EXPECT_FALSE(files[0].sample);
    EXPECT_TRUE(curvy::ScriptsMenu::scriptsIn(folder, true)[0].sample);
    std::filesystem::remove_all(folder);
}

TEST(ScriptsMenu, aMissingFolderHasNoScripts) {
    EXPECT_TRUE(curvy::ScriptsMenu::scriptsIn(curvytest::tempFile("curvy_scripts_nowhere_at_all")).empty());
}

TEST(ScriptsMenu, theMenuListsYourScriptsThenTheSamplesThenTheFolderLine) {
    const auto own = freshFolder("curvy_scripts_own");
    const auto samples = freshFolder("curvy_scripts_samples");
    write(own / "tidy.py", "");
    write(own / "batch.py", "");
    write(samples / "circles.py", "");

    const auto entries = curvy::ScriptsMenu::entries(own, samples, nullptr, nullptr);
    EXPECT_EQ(texts(entries), (std::vector<std::string>{"batch", "tidy", "-", "circles (sample)", "-", "Open scripts folder"}));
    std::filesystem::remove_all(own);
    std::filesystem::remove_all(samples);
}

TEST(ScriptsMenu, withoutScriptsOfYourOwnOneLineSaysSoAndDoesNothing) {
    const auto own = freshFolder("curvy_scripts_none");
    const auto entries = curvy::ScriptsMenu::entries(own, curvytest::tempFile("curvy_scripts_no_samples"), nullptr, nullptr);
    EXPECT_EQ(texts(entries), (std::vector<std::string>{"No scripts in your scripts folder yet", "-", "Open scripts folder"}));
    EXPECT_FALSE(static_cast<bool>(entries[0].run));
    std::filesystem::remove_all(own);
}

TEST(ScriptsMenu, aLineRunsItsScriptAndTheFolderLineOpensTheFolder) {
    const auto own = freshFolder("curvy_scripts_run");
    const auto samples = freshFolder("curvy_scripts_run_samples");
    write(own / "mine.py", "");
    write(samples / "demo.py", "");

    std::vector<std::string> ran;
    std::vector<bool> samplesRan;
    int opened = 0;
    const auto entries = curvy::ScriptsMenu::entries(
        own, samples,
        [&](const curvy::ScriptFile& file) {
            ran.push_back(file.name);
            samplesRan.push_back(file.sample);
        },
        [&opened]() { ++opened; });
    entries[0].run();
    entries[2].run();
    entries.back().run();
    EXPECT_EQ(ran, (std::vector<std::string>{"mine", "demo"}));
    EXPECT_EQ(samplesRan, (std::vector<bool>{false, true}));
    EXPECT_EQ(opened, 1);
    std::filesystem::remove_all(own);
    std::filesystem::remove_all(samples);
}

TEST(ScriptsMenu, readScriptDropsAByteOrderMarkAndReportsAMissingFile) {
    const auto folder = freshFolder("curvy_scripts_read");
    write(folder / "bom.py", "\xEF\xBB\xBF" "print(1)\n");
    write(folder / "plain.py", "print(2)\n");
    std::string code;
    ASSERT_TRUE(curvy::ScriptsMenu::readScript(folder / "bom.py", code));
    EXPECT_EQ(code, "print(1)\n");
    ASSERT_TRUE(curvy::ScriptsMenu::readScript(folder / "plain.py", code));
    EXPECT_EQ(code, "print(2)\n");
    EXPECT_FALSE(curvy::ScriptsMenu::readScript(folder / "missing.py", code));
    std::filesystem::remove_all(folder);
}
