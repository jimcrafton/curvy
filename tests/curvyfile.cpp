#include "curvy/curvyfile.h"
#include "curvy/imagedata.h"
#include "curvy/sketchbin.h"
#include "curvy/sketchdocument.h"
#include "curvy/sketchobject.h"
#include "curvy/io/svg/sketchsvg.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace curvy;

struct FileDocument : SketchDocument {
    using SketchDocument::readFromFile;
    using SketchDocument::writeToFile;
};

bool loadSample(SketchDocument& doc) {
    const std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path() / "Resources" / "samples" / "skyDreamer.svg";
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return documentFromSvg(buffer.str(), doc);
}

// The .skb image of a document after one trip through .skb: the oracle for "nothing was lost". Every loader relabels group ids and re-derives
// a few numbers, so the original itself is not the thing to compare with.
std::string viaSkb(const SketchDocument& doc) {
    SketchDocument loaded;
    EXPECT_TRUE(documentFromBinary(binaryFromDocument(doc, false), loaded));
    return binaryFromDocument(loaded, false);
}

::testing::AssertionResult same(const std::string& got, const std::string& want) {
    if (got == want) {
        return ::testing::AssertionSuccess();
    }
    std::size_t at = 0;
    while (at < got.size() && at < want.size() && got[at] == want[at]) {
        ++at;
    }
    const std::size_t from = at > 100 ? at - 100 : 0;
    return ::testing::AssertionFailure() << "first difference at " << at << " (sizes " << got.size() << " and " << want.size() << ")\n  got:  "
                                         << got.substr(from, 240) << "\n  want: " << want.substr(from, 240);
}

// Looks are floats in the file, so the byte-for-byte .skb oracle no longer holds; objects are compared field by field and by their render.
void expectSameDocument(const SketchDocument& want, const SketchDocument& got) {
    ASSERT_EQ(got.objectCount(), want.objectCount());
    std::size_t liveShapes = 0;
    for (std::size_t i = 0; i < want.objectCount(); ++i) {
        const SketchObject& a = want.object(i);
        const SketchObject& b = got.object(i);
        // A live shape (a generator node such as circle) holds no object state, so it comes back as a plain polygon with no name or flags;
        // that is the known N2 limit, checked here for what it is rather than hidden.
        const bool live = b.type() == SketchObjectType::Polygon && a.type() != SketchObjectType::Polygon;
        if (live) {
            ++liveShapes;
        } else {
            EXPECT_EQ(a.type(), b.type()) << i;
            EXPECT_EQ(a.name(), b.name()) << i;
            EXPECT_EQ(a.ownVisible(), b.ownVisible()) << i;
            EXPECT_EQ(a.ownLocked(), b.ownLocked()) << i;
        }
        EXPECT_EQ(want.layerOf(a), got.layerOf(b)) << i;
        EXPECT_EQ(a.groups().size(), b.groups().size()) << i;
        EXPECT_EQ(a.appearance().size(), b.appearance().size()) << i;
        EXPECT_NEAR(a.opacity(), b.opacity(), 1e-6) << i;
        BLBox boxA, boxB;
        a.geometry().get_bounding_box(&boxA);
        b.geometry().get_bounding_box(&boxB);
        EXPECT_NEAR(boxA.x0, boxB.x0, 1e-2) << i;
        EXPECT_NEAR(boxA.y1, boxB.y1, 1e-2) << i;
    }
    if (liveShapes != 0) {
        std::printf("%zu live shapes came back as plain polygons without name or flags\n", liveShapes);
    }
    const auto render = [](const SketchDocument& d) {
        const double scale = 400.0 / d.pageWidth();
        BLImage image(400, int(std::ceil(d.pageHeight() * scale)), BL_FORMAT_PRGB32);
        {
            BLContext ctx(image);
            ctx.clear_all();
            ctx.scale(scale);
            d.paint(ctx);
            ctx.end();
        }
        BLImageData data;
        image.get_data(&data);
        return std::vector<unsigned char>(static_cast<const unsigned char*>(data.pixel_data),
                                          static_cast<const unsigned char*>(data.pixel_data) + std::size_t(data.stride) * data.size.h);
    };
    const std::vector<unsigned char> a = render(want), b = render(got);
    ASSERT_EQ(a.size(), b.size());
    std::size_t off = 0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        off += std::abs(int(a[k]) - int(b[k])) > 6 ? 1 : 0;
    }
    EXPECT_LT(off, a.size() / 500) << off << " of " << a.size() << " bytes of the render differ";
}

}

TEST(CurvyFile, TheSampleComesBackAsTheSkbRoundTripDoes) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    ASSERT_TRUE(curvyFileSupports(doc));
    doc.touch();
    const std::string text = curvyFromDocument(doc);
    EXPECT_TRUE(isCurvyDocument(text));

    SketchDocument loaded;
    std::string error;
    ASSERT_TRUE(documentFromCurvy(text, loaded, &error)) << error;
    EXPECT_EQ(loaded.objectCount(), doc.objectCount());

    expectSameDocument(doc, loaded);
    EXPECT_EQ(text.find("objects:"), std::string::npos) << "version 2 has no object table";

    // The first load re-derives a few placement numbers from the objects at full precision; from then on a file reloads and resaves to itself.
    const std::string second = curvyFromDocument(loaded);
    SketchDocument again;
    ASSERT_TRUE(documentFromCurvy(second, again, &error)) << error;
    EXPECT_TRUE(same(curvyFromDocument(again), second)) << "a saved file reloads and saves to itself";
}

TEST(CurvyFile, TheTimeDrivenSparklesStillPulseAfterTheTrip) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    SketchDocument loaded;
    ASSERT_TRUE(documentFromCurvy(curvyFromDocument(doc), loaded));
    std::size_t moved = 0;
    for (std::size_t i = 0; i < loaded.objectCount(); ++i) {
        const BLBox before = loaded.object(i).geometryBounds();
        loaded.setTime(0.25);
        const BLBox after = loaded.object(i).geometryBounds();
        loaded.setTime(0.0);
        moved += (std::abs(before.x1 - after.x1) > 1e-6 || std::abs(before.y1 - after.y1) > 1e-6) ? 1 : 0;
    }
    EXPECT_GT(moved, 0u) << "expressions on the sparkles survive the file";
}

TEST(CurvyFile, TheFileIsReadableText) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    const std::string text = curvyFromDocument(doc);
    std::size_t lines = 0, longest = 0, run = 0;
    for (char c : text) {
        if (c == '\n') {
            ++lines;
            longest = std::max(longest, run);
            run = 0;
        } else {
            ++run;
        }
    }
    std::printf("skyDreamer.curvy: %zu bytes (svg %zu), %zu lines, longest line %zu\n", text.size(), svgFromDocument(doc).size(), lines, longest);
    EXPECT_GT(lines, 1000u);
    EXPECT_EQ(text.find("&quot;"), std::string::npos);
}

TEST(CurvyFile, BadFilesAreRefusedAndLeaveTheDocumentAlone) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    const std::string good = curvyFromDocument(doc);
    SketchDocument target;
    ASSERT_TRUE(documentFromCurvy(good, target));
    const std::string before = target.graphToText();
    const std::size_t count = target.objectCount();

    const auto refuses = [&](const std::string& text, const char* why) {
        std::string error;
        EXPECT_FALSE(documentFromCurvy(text, target, &error)) << why;
        EXPECT_FALSE(error.empty()) << why;
        EXPECT_EQ(target.objectCount(), count) << why;
        EXPECT_EQ(target.graphToText(), before) << why;
    };
    refuses("", "empty");
    refuses("not a file", "not JSON5");
    refuses(good.substr(0, good.size() / 2), "cut short");
    refuses("{ format: 'other', version: 1 }", "wrong format");
    refuses("{ format: 'curvy', version: 99, document: {}, graph: {}, objects: [] }", "newer version");
    refuses("{ format: 'curvy', version: 1, document: {}, objects: [] }", "no graph");
    refuses("{ format: 'curvy', version: 1, document: {}, graph: { format: 'jitnodes', version: 1, nodes: [] }, objects: [ { type: 6, node: 5, look: { items: [] } } ] }",
            "an object whose node is not in the graph");
    refuses("{ format: 'curvy', version: 1, document: { page: { width: -1 } }, graph: { format: 'jitnodes', version: 1, nodes: [] }, objects: [] }", "a negative page");
    refuses("{ format: 'curvy', version: 1, document: {}, graph: { format: 'jitnodes', version: 1, nodes: [] }, objects: [ { type: 99, node: 1, look: { items: [] } } ] }",
            "an object type out of range");
    refuses("{ format: 'curvy', version: 1, document: {}, graph: { format: 'jitnodes', version: 1, nodes: [] }, objects: [ { type: 6, node: 1, look: { items: [] }, outline: { cmds: 'MQ', xy: [0, 0, 1, 1] } } ] }",
            "a path that ends inside a curve");

    std::string broken = good;
    const std::size_t at = broken.find("kind: \"fill\"");
    ASSERT_NE(at, std::string::npos);
    broken.replace(at, 12, "kind: \"nope\"");
    refuses(broken, "an unknown appearance row");
}

TEST(CurvyFile, SaveAndOpenGoThroughTheFileByContentNotName) {
    FileDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "curvyFileF4";
    std::filesystem::create_directories(folder);
    const std::string plain = (folder / "a.curvy").string();
    const std::string packed = (folder / "b.curvyz").string();
    const std::string disguised = (folder / "c.svg").string();
    const std::string svg = (folder / "d.svg").string();

    ASSERT_TRUE(doc.writeToFile(plain));
    {
        FileDocument named;
        ASSERT_TRUE(named.readFromFile(plain));
        std::size_t unnamed = 0;
        for (std::size_t i = 0; i < named.objectCount(); ++i) {
            unnamed += named.object(i).name().empty() != doc.object(i).name().empty() ? 1 : 0;
        }
        EXPECT_EQ(unnamed, 0u) << "saving straight after opening an SVG keeps the names";
    }
    ASSERT_TRUE(doc.writeToFile(packed));
    ASSERT_TRUE(doc.writeToFile(svg));
    EXPECT_FALSE(std::filesystem::exists(plain + ".tmp")) << "the temporary file is renamed away";
    EXPECT_LT(std::filesystem::file_size(packed), std::filesystem::file_size(plain));
    std::filesystem::copy_file(plain, disguised, std::filesystem::copy_options::overwrite_existing);

    for (const std::string& path : {plain, packed, disguised, svg}) {
        FileDocument loaded;
        ASSERT_TRUE(loaded.readFromFile(path)) << path;
        EXPECT_EQ(loaded.objectCount(), doc.objectCount()) << path;
    }

    EXPECT_TRUE(doc.writeToFile(plain)) << "saving over an existing file works";
    EXPECT_FALSE(doc.writeToFile((folder / "missing" / "e.curvy").string())) << "an unwritable target is reported";
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

namespace {

std::string tinyPng() {
    BLImage image(8, 4, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(0xFFFF0000u));
    ctx.fill_rect(BLRect(0, 0, 4, 4));
    ctx.set_fill_style(BLRgba32(0xFF0000FFu));
    ctx.fill_rect(BLRect(4, 0, 4, 4));
    ctx.end();
    BLImageCodec codec;
    codec.find_by_name("PNG");
    BLArray<uint8_t> buffer;
    image.write_to_data(buffer, codec);
    return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

// One of everything slice 2 adds: text (styled, and on a path), a symbol and two instances, a pattern, brushes of all kinds, a picture.
void fillLibraries(SketchDocument& doc) {
    doc.setPageSize(300, 300);
    SketchObject& box = doc.add(SketchObject::makeRect(0, 0, 20, 20));
    box.setFill(SketchPaint::solid(SketchColor::fromHex("#ff0000")));
    doc.setSelection({box.id()});
    const std::size_t symbol = doc.makeSymbol("Box");
    doc.placeSymbol(symbol, BLPoint(70, 70));
    doc.placeSymbol(symbol, BLPoint(120, 70));

    SketchObject& dot = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    dot.setFill(SketchPaint::solid(SketchColor::fromHex("#00aa00")));
    dot.setStroke(SketchPaint::none());
    doc.setSelection({dot.id()});
    const std::size_t pattern = doc.makePattern("Green");
    SketchObject& area = doc.add(SketchObject::makeRect(150, 150, 60, 60));
    doc.setSelection({area.id()});
    doc.applyPattern(pattern, false);

    SketchObject& bar = doc.add(SketchObject::makeRect(0, 100, 60, 10));
    SketchObject& line = doc.add(SketchObject::makeLine(0, 150, 100, 150));
    doc.setSelection({bar.id()});
    doc.makeBrush(BrushKind::Scatter, "Bars");
    doc.setSelection({line.id()});
    const std::size_t nib = doc.makeBrush(BrushKind::Calligraphic, "Nib");
    doc.applyBrush(nib);

    SketchObject& words = doc.add(SketchObject::makeText("Hello\nthere \"you\"", "", 18.0, BLPoint(20, 250)));
    TextStyle style;
    style.bold = true;
    style.align = TextStyle::Center;
    style.tracking = 1.5;
    style.leading = 1.2;
    SketchObject::Prototype::setTextStyle(words, style);
    doc.add(SketchObject::makeText("plain", "", 12.0, BLPoint(200, 20)));

    doc.placeImage(decodeImage(tinyPng(), "two.png"), BLPoint(250, 250));
}

}

TEST(CurvyFile, TextSymbolsPatternsBrushesAndPicturesComeBackAsTheSkbRoundTripDoes) {
    SketchDocument doc;
    fillLibraries(doc);
    const std::size_t childId = doc.add(SketchObject::makeRect(300, 150, 20, 20)).id();
    const std::size_t parentId = doc.add(SketchObject::makeRect(260, 150, 20, 20)).id();
    ASSERT_TRUE(doc.setParent(childId, parentId));
    ASSERT_EQ(doc.symbols().size(), 1u);
    ASSERT_EQ(doc.patterns().size(), 1u);
    ASSERT_EQ(doc.brushes().size(), 2u);
    ASSERT_TRUE(curvyFileSupports(doc));

    const std::string text = curvyFromDocument(doc);
    SketchDocument loaded;
    std::string error;
    ASSERT_TRUE(documentFromCurvy(text, loaded, &error)) << error;
    EXPECT_EQ(loaded.objectCount(), doc.objectCount());
    EXPECT_EQ(loaded.symbols().size(), 1u);
    EXPECT_EQ(loaded.patterns().size(), 1u);
    EXPECT_EQ(loaded.brushes().size(), 2u);
    expectSameDocument(doc, loaded);

    bool text1 = false, picture = false, instance = false;
    for (std::size_t i = 0; i < loaded.objectCount(); ++i) {
        text1 = text1 || (loaded.object(i).isText() && loaded.object(i).textStyle().bold);
        picture = picture || (loaded.object(i).isImage() && loaded.object(i).image());
        instance = instance || (loaded.object(i).isSymbol() && loaded.object(i).symbol());
    }
    EXPECT_TRUE(text1);
    EXPECT_TRUE(picture);
    EXPECT_TRUE(instance);

    std::size_t linked = 0, linkedAgain = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        linked += doc.object(i).parent() != 0 ? 1 : 0;
    }
    for (std::size_t i = 0; i < loaded.objectCount(); ++i) {
        const SketchObject& child = loaded.object(i);
        linkedAgain += child.parent() != 0 ? 1 : 0;
        if (child.parent() != 0) {
            const SketchObject* parent = loaded.findById(child.parent());
            ASSERT_NE(parent, nullptr);
            EXPECT_EQ(parent->graphScope(), child.graphScope());
        }
    }
    EXPECT_GT(linked, 0u) << "the document has a parent link to lose";
    EXPECT_EQ(linkedAgain, linked) << "the parent link is read back from the transform node's input";

    const std::string second = curvyFromDocument(loaded);
    SketchDocument again;
    ASSERT_TRUE(documentFromCurvy(second, again, &error)) << error;
    EXPECT_TRUE(same(curvyFromDocument(again), second)) << "a saved file reloads and saves to itself";
}

TEST(CurvyFile, BadLibrariesAreRefused) {
    SketchDocument doc;
    fillLibraries(doc);
    const std::string good = curvyFromDocument(doc);
    SketchDocument target;
    target.add(SketchObject::makeRect(0, 0, 1, 1));
    const auto refuses = [&](std::string text, const char* from, const char* to, const char* why) {
        const std::size_t at = text.find(from);
        ASSERT_NE(at, std::string::npos) << why;
        text.replace(at, std::string(from).size(), to);
        EXPECT_FALSE(documentFromCurvy(text, target)) << why;
        EXPECT_EQ(target.objectCount(), 1u) << why;
    };
    refuses(good, "box: [", "boks: [", "a symbol without its box");
    refuses(good, "data: \"", "data: \"!!", "a picture that is not base64");
    refuses(good, "matrix:", "matrik:", "a text object without its matrix");
}

TEST(CurvyFile, AVersionOneFileWithAnObjectTableStillOpens) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    const std::filesystem::path file = std::filesystem::path(__FILE__).parent_path() / "data" / "skyDreamerV1.curvy";
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        GTEST_SKIP() << "no version 1 sample";
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    SketchDocument loaded;
    std::string error;
    ASSERT_TRUE(documentFromCurvy(buffer.str(), loaded, &error)) << error;
    expectSameDocument(doc, loaded);
}

TEST(CurvyFile, AVersionTwoFileWithDamagedNodesIsRefusedOrKeepsWhatItCan) {
    SketchDocument doc;
    fillLibraries(doc);
    const std::string good = curvyFromDocument(doc);
    SketchDocument target;
    target.add(SketchObject::makeRect(0, 0, 1, 1));

    std::string noType = good;
    const std::size_t at = noType.find("type: \"image\"");
    ASSERT_NE(at, std::string::npos);
    noType.replace(at, 13, "type: \"nope\"");
    SketchDocument loaded;
    std::string error;
    EXPECT_TRUE(documentFromCurvy(noType, loaded, &error)) << error << " (an unknown type tag falls back to a plain path)";
    EXPECT_EQ(loaded.objectCount(), doc.objectCount());

    std::string noPicture = good;
    const std::size_t data = noPicture.find("data: \"");
    ASSERT_NE(data, std::string::npos);
    noPicture.replace(data, 7, "dat: \"");
    SketchDocument without;
    EXPECT_FALSE(documentFromCurvy(noPicture, without, &error)) << "a picture without its bytes is refused by the library reader";

    std::string noSymbol = good;
    const std::size_t sym = noSymbol.find("symbols:");
    ASSERT_NE(sym, std::string::npos);
    noSymbol.replace(sym, 8, "symbolz:");
    SketchDocument missing;
    ASSERT_TRUE(documentFromCurvy(noSymbol, missing, &error)) << error;
    EXPECT_EQ(missing.objectCount(), doc.objectCount());
    for (std::size_t i = 0; i < missing.objectCount(); ++i) {
        EXPECT_FALSE(missing.object(i).isSymbol()) << "an instance of a symbol nobody defines becomes a plain path";
    }
}

TEST(CurvyFile, ANativeFileWithComments) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    const std::string text = "// saved by hand\n" + curvyFromDocument(doc);
    SketchDocument loaded;
    EXPECT_TRUE(isCurvyDocument(text));
    EXPECT_TRUE(documentFromCurvy(text, loaded));
    EXPECT_EQ(loaded.objectCount(), doc.objectCount());
}

// N4a of docs/node-only-plan.md: autosave writes .curvy.
TEST(CurvyFile, ARecoveryCopyIsACurvyFileThatReopensAsTheSameDocument) {
    FileDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    doc.markModified();
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "curvyRecoveryN4a";
    std::filesystem::remove_all(folder);

    const std::string written = writeRecoveryCopy(doc, folder.string(), "Untitled-1");
    ASSERT_FALSE(written.empty());
    EXPECT_EQ(std::filesystem::path(written).extension().string(), ".curvy");
    EXPECT_TRUE(doc.isModified()) << "a recovery copy is not a save";
    EXPECT_FALSE(doc.hasFilePath());

    FileDocument loaded;
    ASSERT_TRUE(loaded.readFromFile(written));
    expectSameDocument(doc, loaded);
    std::filesystem::remove_all(folder);
}

TEST(CurvyFile, RecoveryCopiesListNewestFirstAndKeepOldSvgCopies) {
    SketchDocument doc;
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "curvyRecoveryN4a2";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    {
        std::ofstream old(folder / "Old.svg", std::ios::binary);
        old << svgFromDocument(doc);
    }
    std::filesystem::last_write_time(folder / "Old.svg", std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));
    {
        std::ofstream ignored(folder / "notes.txt");
        ignored << "not a recovery copy";
    }
    const std::string fresh = writeRecoveryCopy(doc, folder.string(), "New");
    ASSERT_FALSE(fresh.empty());

    const std::vector<std::string> found = recoveryCopies(folder.string());
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(std::filesystem::path(found[0]).filename().string(), "New.curvy");
    EXPECT_EQ(std::filesystem::path(found[1]).filename().string(), "Old.svg");

    // The same stem written again replaces an older build's SVG copy, so the recovery is offered once.
    {
        std::ofstream stale(folder / "New.svg", std::ios::binary);
        stale << svgFromDocument(doc);
    }
    ASSERT_FALSE(writeRecoveryCopy(doc, folder.string(), "New").empty());
    EXPECT_FALSE(std::filesystem::exists(folder / "New.svg"));
    std::filesystem::remove_all(folder);
}
