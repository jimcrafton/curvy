#include "curvy/appearancenode.h"
#include "curvy/brush.h"
#include "curvy/curvyfile.h"
#include "curvy/imagedata.h"
#include "curvy/pathnode.h"
#include "curvy/sketchbin.h"
#include "curvy/sketchdocument.h"
#include "curvy/sketchobject.h"
#include "curvy/io/svg/sketchsvg.h"

#include <gtest/gtest.h>
#include <jitnodes/graph/serialize.h>
#include <jitnodes/graph/serializeValue.h>
#include <json5/json5_output.hpp>
#include <json5/json5_input.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Audit for the native file format (docs/native-format.md, stage F1): what of a document can be read back from its graph alone?
namespace {

using namespace curvy;

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

}

TEST(GraphProbe, TheGraphTextAloneBuildsNoObjectsYet) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    ASSERT_GT(doc.objectCount(), 0u);

    SketchDocument fromGraph;
    ASSERT_TRUE(fromGraph.graphFromText(doc.graphToText()));
    EXPECT_EQ(fromGraph.graphToText(), doc.graphToText()) << "the graph itself round-trips";
    std::printf("graph-only load: %zu objects of %zu, %zu layers of %zu\n", fromGraph.objectCount(), doc.objectCount(),
                fromGraph.layers().size(), doc.layers().size());
    EXPECT_EQ(fromGraph.objectCount(), 0u) << "when this fails, objects can be rebuilt from a graph: update docs/native-format.md and drop the object table";
}

TEST(GraphProbe, WhatEachObjectKeepsOutsideItsNodes) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    std::size_t withNode = 0, named = 0, nameInNode = 0, parented = 0, turned = 0, layerMatchesScope = 0, text = 0, wornActive = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& object = doc.object(i);
        if (!doc.hasNode(object.graphRef())) {
            continue;
        }
        ++withNode;
        const jitnodes::Node& shape = doc.node(object.graphRef());
        named += object.name().empty() ? 0 : 1;
        nameInNode += (!object.name().empty() && shape.name() == object.name()) ? 1 : 0;
        parented += object.parent() != 0 ? 1 : 0;
        turned += object.frameAngle() != 0.0 ? 1 : 0;
        text += object.isText() ? 1 : 0;
        wornActive += object.appearance().size() > 1 ? 1 : 0;
        layerMatchesScope += (object.graphScope() == 0 || doc.layerIdOfScope(object.graphScope()) == doc.layerOf(object)) ? 1 : 0;
    }
    std::printf("objects %zu; with a shape node %zu; named %zu, of which the shape node carries the name %zu; with a parent link %zu;\n"
                "with a non-zero frame angle %zu; text %zu; with more than one appearance row %zu; layer readable from the scope %zu\n",
                doc.objectCount(), withNode, named, nameInNode, parented, turned, text, wornActive, layerMatchesScope);
    EXPECT_EQ(withNode, doc.objectCount()) << "every object has its shape node, which is what the file's object table points at";
    EXPECT_EQ(layerMatchesScope, withNode) << "an object's layer follows from the scope its node is in";
}

TEST(GraphValue, AGraphStoredInsideAnotherJson5DocumentComesBackExactly) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    json5::document out;
    json5::builder b(out);
    b.push_object();
    b["format"] = b.new_string("test");
    b["graph"] = jitnodes::saveNetworkValue(b, doc.network(), true, true);
    b["after"] = json5::value(7.0);
    b.pop();
    std::ostringstream text;
    text << std::setprecision(9);   // the value holds the numbers whole; the stream decides how many digits reach the file
    json5::writer_params params;
    params.json_compatible = false;
    json5::to_stream(text, out, params);

    json5::document in;
    ASSERT_FALSE(json5::from_string(text.str(), in));
    ASSERT_TRUE(in["graph"].is_object());
    jitnodes::Network other(1);
    jitnodes::loadNetworkValue(other, in["graph"]);
    const std::string got = jitnodes::saveNetwork(other, true, true), want = doc.graphToText();
    std::size_t at = 0;
    while (at < got.size() && at < want.size() && got[at] == want[at]) { ++at; }
    EXPECT_TRUE(got == want) << "first difference at " << at << " (sizes " << got.size() << " and " << want.size() << ")\n  got:  "
                             << got.substr(at > 80 ? at - 80 : 0, 200) << "\n  want: " << want.substr(at > 80 ? at - 80 : 0, 200);
}

TEST(GraphValue, ABadGraphValueThrowsAndLeavesTheNetworkAlone) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    json5::document in;
    ASSERT_FALSE(json5::from_string("{ format: 'nope', version: 1, nodes: [] }", in));
    const std::size_t before = doc.network().nodeCount();
    EXPECT_THROW(jitnodes::loadNetworkValue(doc.network(), in), std::runtime_error);
    EXPECT_EQ(doc.network().nodeCount(), before);
}

TEST(GraphProbe, BareObjectsBoundToTheLoadedGraphRecoverWhatTheNodesHold) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    SketchDocument g;
    ASSERT_TRUE(g.graphFromText(doc.graphToText()));
    g.setLayers(doc.layers());
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& source = doc.object(i);
        std::unique_ptr<SketchObject> bare(SketchObject::make(source.type(), source.geometry()));
        bare->setGraphNode(source.graphNode());
        bare->setGraphRef(NodeRef(g.scopeOfText(doc.scopeText(source.graphScope())), source.graphNode()));
        bare->setLayer(source.layer());
        g.add(bare.release());
    }
    ASSERT_EQ(g.objectCount(), doc.objectCount());
    std::size_t sameLook = 0, sameOpacity = 0, sameBlend = 0, sameVisible = 0, sameLocked = 0, sameBounds = 0, sameLayer = 0, sameActive = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& a = doc.object(i);
        const SketchObject& c = g.object(i);
        sameLook += a.appearance() == c.appearance() ? 1 : 0;
        sameOpacity += a.opacity() == c.opacity() ? 1 : 0;
        sameBlend += a.blendMode() == c.blendMode() ? 1 : 0;
        sameVisible += a.ownVisible() == c.ownVisible() ? 1 : 0;
        sameLocked += a.ownLocked() == c.ownLocked() ? 1 : 0;
        sameLayer += g.layerOf(c) == doc.layerOf(a) ? 1 : 0;
        const BLBox x = a.geometryBounds(), y = c.geometryBounds();
        sameBounds += (std::abs(x.x0 - y.x0) < 1e-3 && std::abs(x.x1 - y.x1) < 1e-3 && std::abs(x.y0 - y.y0) < 1e-3 && std::abs(x.y1 - y.y1) < 1e-3) ? 1 : 0;
        sameActive += (a.activeIndex(AppearanceKind::Fill) == c.activeIndex(AppearanceKind::Fill) && a.activeIndex(AppearanceKind::Stroke) == c.activeIndex(AppearanceKind::Stroke)) ? 1 : 0;
    }
    for (std::size_t i : {std::size_t(0), std::size_t(5), std::size_t(100)}) {
        if (i < doc.objectCount()) {
            std::printf("object %zu: original %zu rows (fill kind %d, opacity %.2f) / from graph %zu rows\n", i, doc.object(i).appearance().size(),
                        doc.object(i).appearance().empty() ? -1 : int(doc.object(i).appearance()[0].kind), doc.object(i).opacity(), g.object(i).appearance().size());
        }
    }
    std::printf("of %zu bare objects bound to the graph: appearance equal %zu, opacity %zu, blend %zu, visible %zu, locked %zu, layer %zu, bounds %zu, active rows %zu\n",
                doc.objectCount(), sameLook, sameOpacity, sameBlend, sameVisible, sameLocked, sameLayer, sameBounds, sameActive);
}

// Do the appearance nodes hold the whole look (docs/native-format.md, approach B)? Every look node of a document is turned back into an item
// with the call the live sync uses to adopt a node's edits, and the item is compared with the one the object stores.
namespace {

bool nearFloat(double a, double b) { return float(a) == float(b); }

void paintDiff(const SketchPaint& a, const SketchPaint& b, std::vector<std::string>& out) {
    if (a.kind() != b.kind()) {
        out.push_back("paint.kind");
        return;
    }
    switch (a.kind()) {
        case SketchPaint::Kind::None:
            return;
        case SketchPaint::Kind::Solid:
            if (!(a.color() == b.color())) out.push_back("paint.color");
            return;
        case SketchPaint::Kind::Pattern:
            if (a.patternId() != b.patternId()) out.push_back("paint.patternId");
            if (!nearFloat(a.patternScale(), b.patternScale())) out.push_back("paint.patternScale");
            if (!nearFloat(a.patternAngle(), b.patternAngle())) out.push_back("paint.patternAngle");
            return;
        default:
            break;
    }
    const SketchGradient& g = a.gradient();
    const SketchGradient& h = b.gradient();
    if (g.stops.size() != h.stops.size()) {
        out.push_back("gradient.stopCount");
    } else {
        for (std::size_t i = 0; i < g.stops.size(); ++i) {
            if (!nearFloat(g.stops[i].offset, h.stops[i].offset)) out.push_back("gradient.stopOffset");
            if (!(g.stops[i].color == h.stops[i].color)) out.push_back("gradient.stopColor");
        }
    }
    if (g.points.size() != h.points.size()) {
        out.push_back("gradient.pointCount");
    } else {
        for (std::size_t i = 0; i < g.points.size(); ++i) {
            if (!nearFloat(g.points[i].x, h.points[i].x) || !nearFloat(g.points[i].y, h.points[i].y) || !nearFloat(g.points[i].radius, h.points[i].radius)) out.push_back("gradient.pointPlace");
            if (!(g.points[i].color == h.points[i].color)) out.push_back("gradient.pointColor");
        }
    }
    if (!nearFloat(g.angleDegrees, h.angleDegrees)) out.push_back("gradient.angle");
    if (g.spread != h.spread) out.push_back("gradient.spread");
    if (g.alongStroke != h.alongStroke) out.push_back("gradient.alongStroke");
    if (g.hasAxis != h.hasAxis) {
        out.push_back("gradient.hasAxis");
    } else if (g.hasAxis) {
        if (!nearFloat(g.axisX1, h.axisX1) || !nearFloat(g.axisY1, h.axisY1) || !nearFloat(g.axisX2, h.axisX2) || !nearFloat(g.axisY2, h.axisY2)) out.push_back("gradient.axis");
        if (!nearFloat(g.axisAspect, h.axisAspect)) out.push_back("gradient.axisAspect");
        if (!nearFloat(g.axisSkew, h.axisSkew)) out.push_back("gradient.axisSkew");
    }
}

std::size_t brushIdOf(const StrokeOptions& o) { return o.brush ? o.brush->id : o.brushRef; }

std::vector<std::string> lostFields(const AppearanceItem& a, const AppearanceItem& b) {
    std::vector<std::string> out;
    if (a.kind != b.kind) {
        out.push_back("kind");
        return out;
    }
    if (a.visible != b.visible) out.push_back("visible");
    if (!nearFloat(a.opacity, b.opacity)) out.push_back("opacity");
    if (a.blendMode != b.blendMode) out.push_back("blendMode");
    if (a.kind != AppearanceKind::Effect) {
        paintDiff(a.paint, b.paint, out);
    }
    if (a.kind == AppearanceKind::Stroke) {
        const StrokeOptions& o = a.strokeOptions;
        const StrokeOptions& p = b.strokeOptions;
        if (!nearFloat(a.strokeWidth, b.strokeWidth)) out.push_back("stroke.width");
        if (o.cap != p.cap) out.push_back("stroke.cap");
        if (o.join != p.join) out.push_back("stroke.join");
        if (!nearFloat(o.miterLimit, p.miterLimit)) out.push_back("stroke.miterLimit");
        if (o.dashes.size() != p.dashes.size()) {
            out.push_back("stroke.dashCount");
        } else {
            for (std::size_t i = 0; i < o.dashes.size(); ++i) {
                if (!nearFloat(o.dashes[i], p.dashes[i])) out.push_back("stroke.dash");
            }
        }
        if (o.widths.size() != p.widths.size()) {
            out.push_back("stroke.widthPointCount");
        } else {
            for (std::size_t i = 0; i < o.widths.size(); ++i) {
                if (!nearFloat(o.widths[i].t, p.widths[i].t) || !nearFloat(o.widths[i].left, p.widths[i].left) || !nearFloat(o.widths[i].right, p.widths[i].right)) out.push_back("stroke.widthPoint");
            }
        }
        if (brushIdOf(o) != brushIdOf(p)) out.push_back("stroke.brush");
        if (o.align != p.align) out.push_back("stroke.align");
        if (o.arrowStart != p.arrowStart || o.arrowEnd != p.arrowEnd) out.push_back("stroke.arrowStyle");
        if (!nearFloat(o.arrowScale, p.arrowScale)) out.push_back("stroke.arrowScale");
    }
    if (a.kind == AppearanceKind::Effect) {
        if (a.effect != b.effect) out.push_back("effect.kind");
        if (!(a.color == b.color)) out.push_back("effect.color");
        if (!nearFloat(a.softness, b.softness)) out.push_back("effect.softness");
        if (!nearFloat(a.offsetX, b.offsetX) || !nearFloat(a.offsetY, b.offsetY)) out.push_back("effect.offset");
        if (!nearFloat(a.radius, b.radius)) out.push_back("effect.radius");
    }
    return out;
}

struct LookTally {
    std::size_t objects = 0, items = 0, chainLengthWrong = 0, nodeTypeWrong = 0, exact = 0, withinFloat = 0, lost = 0;
    std::map<std::string, std::size_t> lostBy;
    std::map<std::string, std::size_t> kinds;
};

LookTally tallyLooks(SketchDocument& doc) {
    LookTally t;
    const AppearanceLibrary library{[&doc](std::size_t id) { return doc.findPattern(id); }, [&doc](std::size_t id) { return doc.findBrush(id); }};
    std::map<std::uint32_t, std::unordered_map<jitnodes::NodeId, jitnodes::NodeId>> successors;
    for (std::uint32_t scope = 0; scope < doc.scopeCount(); ++scope) {
        if (const jitnodes::Network* net = doc.findNetwork(scope)) {
            successors[scope] = appearanceSuccessors(*net, net->nodeIds());
        }
    }
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& object = doc.object(i);
        const auto found = successors.find(object.graphScope());
        if (found == successors.end() || object.graphNode() == jitnodes::invalidNodeId) {
            continue;
        }
        ++t.objects;
        const jitnodes::Network& net = *doc.findNetwork(object.graphScope());
        const std::vector<jitnodes::NodeId> chain = appearanceChain(found->second, object.graphNode());
        const std::vector<AppearanceItem>& items = object.appearance();
        t.items += items.size();
        if (chain.size() != items.size()) {
            ++t.chainLengthWrong;
            continue;
        }
        for (std::size_t k = 0; k < items.size(); ++k) {
            const jitnodes::Node& node = net.node(chain[k]);
            if (node.typeName() != appearanceNodeType(items[k].kind)) {
                ++t.nodeTypeWrong;
                continue;
            }
            AppearanceItem base;
            base.kind = items[k].kind;
            const AppearanceItem got = itemFrom(paramsOf(node), paramsOf(base), base, library);
            const std::vector<std::string> lost = lostFields(items[k], got);
            const char* kindName = items[k].kind == AppearanceKind::Fill ? "fill" : items[k].kind == AppearanceKind::Stroke ? "stroke" : "effect";
            ++t.kinds[kindName];
            if (!lost.empty()) {
                ++t.lost;
                for (const std::string& field : lost) {
                    ++t.lostBy[field];
                }
            } else if (got == items[k]) {
                ++t.exact;
            } else {
                ++t.withinFloat;
            }
        }
    }
    return t;
}

void printTally(const char* name, const LookTally& t) {
    std::printf("%s: %zu objects with a chain, %zu look rows (", name, t.objects, t.items);
    for (const auto& [kind, count] : t.kinds) {
        std::printf("%s %zu ", kind.c_str(), count);
    }
    std::printf("); chain length wrong %zu, node type wrong %zu; recovered exactly %zu, to a float step %zu, LOST SOMETHING %zu\n", t.chainLengthWrong, t.nodeTypeWrong, t.exact,
                t.withinFloat, t.lost);
    for (const auto& [field, count] : t.lostBy) {
        std::printf("    lost: %-24s %zu\n", field.c_str(), count);
    }
}

}

TEST(GraphProbe, TheSamplesLookNodesHoldTheLooks) {
    SketchDocument doc;
    if (!loadSample(doc)) {
        GTEST_SKIP() << "Resources/samples/skyDreamer.svg is not there";
    }
    const LookTally t = tallyLooks(doc);
    printTally("skyDreamer", t);
    EXPECT_EQ(t.chainLengthWrong, 0u);
    EXPECT_EQ(t.nodeTypeWrong, 0u);
}

// N0 of docs/node-only-plan.md: what of an object is not recoverable from its shape node alone. A bare path object with an EMPTY outline is bound
// to each shape node of a copy of the graph and every getter is compared with the original's.
namespace {

std::string pngBytes() {
    BLImage image(8, 4, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(0xFFFF0000u));
    ctx.fill_rect(BLRect(0, 0, 8, 4));
    ctx.end();
    BLImageCodec codec;
    codec.find_by_name("PNG");
    BLArray<uint8_t> buffer;
    image.write_to_data(buffer, codec);
    return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

void fillEverything(SketchDocument& doc) {
    doc.setPageSize(400, 400);
    const std::size_t extra = doc.addLayer(0, "Extra");

    SketchObject& a = doc.add(SketchObject::makeRect(10, 10, 40, 30));
    doc.renameObject(a.id(), "Alpha");
    a.setOpacity(0.4);
    a.setBlendMode(BlendMode::Multiply);
    a.setFill(SketchPaint::solid(SketchColor::fromHex("#336699")));
    SketchObject& b = doc.add(SketchObject::makeEllipse(100, 40, 30, 20));
    doc.renameObject(b.id(), "Beta");
    b.setVisible(false);
    SketchObject& c = doc.add(SketchObject::makeLine(0, 80, 90, 120));
    c.setLocked(true);
    SketchObject& d = doc.add(SketchObject::makeRoundRect(200, 10, 50, 40, 6));
    doc.rotateSelectionBy(0.0);
    doc.setSelection({d.id()});
    doc.rotateSelectionBy(0.5);
    SketchObject& e = doc.add(SketchObject::makePolygon({BLPoint(300, 10), BLPoint(340, 20), BLPoint(320, 60)}));
    e.setActiveIndices(0, 0, 0);
    SketchObject& f = doc.add(SketchObject::makePath([] { BLPath p; p.move_to(0, 200); p.cubic_to(30, 150, 60, 250, 90, 200); return p; }()));
    doc.renameObject(f.id(), "Curve");

    SketchObject& g1 = doc.add(SketchObject::makeRect(10, 150, 20, 20));
    SketchObject& g2 = doc.add(SketchObject::makeRect(40, 150, 20, 20));
    doc.setSelection({g1.id(), g2.id()});
    doc.groupSelection(false);
    SketchObject& m1 = doc.add(SketchObject::makeRect(100, 150, 30, 30));
    SketchObject& m2 = doc.add(SketchObject::makeEllipse(115, 165, 20, 20));
    doc.setSelection({m1.id(), m2.id()});
    doc.makeClippingMask();

    SketchObject& child = doc.add(SketchObject::makeRect(300, 150, 20, 20));
    SketchObject& parentObject = doc.add(SketchObject::makeRect(260, 150, 20, 20));
    doc.setParent(child.id(), parentObject.id());

    SketchObject& onLayer = doc.add(SketchObject::makeRect(10, 300, 20, 20));
    onLayer.setLayer(extra);

    SketchObject& box = doc.add(SketchObject::makeRect(0, 0, 20, 20));
    doc.setSelection({box.id()});
    const std::size_t symbol = doc.makeSymbol("Box");
    doc.placeSymbol(symbol, BLPoint(150, 250));

    SketchObject& words = doc.add(SketchObject::makeText("Hello\nthere", "", 18.0, BLPoint(20, 360)));
    TextStyle style;
    style.bold = true;
    style.align = TextStyle::Center;
    SketchObject::Prototype::setTextStyle(words, style);
    SketchObject& onPath = doc.add(SketchObject::makeText("along", "", 14.0, BLPoint(200, 300)));
    SketchObject& guide = doc.add(SketchObject::makePath([] { BLPath p; p.move_to(200, 380); p.cubic_to(240, 330, 300, 430, 340, 380); return p; }()));
    doc.setSelection({onPath.id(), guide.id()});
    doc.makeTextOnPath();

    doc.placeImage(decodeImage(pngBytes(), "red.png"), BLPoint(350, 350));
}

struct Audit {
    std::size_t objects = 0;
    std::map<std::string, std::size_t> differs;
};

Audit auditBareObjects(SketchDocument& doc) {
    Audit audit;
    SketchDocument g;
    if (!g.graphFromText(doc.graphToText())) {
        return audit;
    }
    g.graphReplaced();
    g.setLayers(doc.layers());
    std::vector<std::size_t> originals;
    for (std::uint32_t s = 1; s < doc.scopeCount() && s < 8; ++s) {
        const std::uint32_t cs = g.scopeOfText(doc.scopeText(s));
        std::printf("  scope %u '%s': layerId original %zu, copy (scope %u) %zu, group original %zu\n", s, doc.scopeText(s).c_str(), doc.layerIdOfScope(s), cs,
                    g.layerIdOfScope(cs), doc.groupIdOfScope(s));
    }
    for (const Layer& l : doc.layers()) {
        std::printf("  layer id %zu parent %zu '%s'\n", l.id, l.parent, l.name.c_str());
    }
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& source = doc.object(i);
        if (!doc.hasNode(source.graphRef())) {
            continue;
        }
        std::unique_ptr<SketchObject> bare(SketchObject::make(SketchObjectType::Path, BLPath()));
        bare->setGraphNode(source.graphNode());
        bare->setGraphRef(NodeRef(g.scopeOfText(doc.scopeText(source.graphScope())), source.graphNode()));
        g.add(bare.release());
        originals.push_back(i);
    }
    audit.objects = originals.size();
    const auto note = [&](bool same, const char* field) {
        if (!same) {
            ++audit.differs[field];
        }
    };
    const auto sameBox = [](const BLBox& x, const BLBox& y) {
        return std::abs(x.x0 - y.x0) < 1e-3 && std::abs(x.x1 - y.x1) < 1e-3 && std::abs(x.y0 - y.y0) < 1e-3 && std::abs(x.y1 - y.y1) < 1e-3;
    };
    std::map<std::string, std::size_t> copyOf;   // add() may reorder, so objects are paired by their node and not by position
    for (std::size_t j = 0; j < g.objectCount(); ++j) {
        copyOf[g.scopeText(g.object(j).graphScope()) + "#" + std::to_string(g.object(j).graphNode())] = j;
    }
    for (std::size_t j = 0; j < g.objectCount(); ++j) {
        const std::string key = g.scopeText(g.object(j).graphScope()) + "#" + std::to_string(g.object(j).graphNode());
        bool known = false;
        for (std::size_t o : originals) {
            known = known || key == doc.scopeText(doc.object(o).graphScope()) + "#" + std::to_string(doc.object(o).graphNode());
        }
        if (!known) {
            std::printf("  the copy has an object at %s that the original does not (copy object %zu, type %d)\n", key.c_str(), j, int(g.object(j).type()));
        }
    }
    for (std::size_t k = 0; k < originals.size(); ++k) {
        const SketchObject& a = doc.object(originals[k]);
        const auto twin = copyOf.find(doc.scopeText(a.graphScope()) + "#" + std::to_string(a.graphNode()));
        if (twin == copyOf.end()) {
            ++audit.differs["node lost on add"];
            std::printf("  no copy has node %s of object #%zu '%s' (object type %d, node type '%s', copy has %zu objects, original %zu)\n",
                        (doc.scopeText(a.graphScope()) + "#" + std::to_string(a.graphNode())).c_str(), originals[k], a.name().c_str(), int(a.type()),
                        doc.node(a.graphRef()).typeName().c_str(), g.objectCount(), doc.objectCount());
            continue;
        }
        const SketchObject& c = g.object(twin->second);
        note(a.type() == c.type(), "type");
        note(a.name() == c.name(), "name");
        note(a.ownVisible() == c.ownVisible(), "visible");
        note(a.ownLocked() == c.ownLocked(), "locked");
        note(a.opacity() == c.opacity(), "opacity");
        note(a.blendMode() == c.blendMode(), "blendMode");
        note(g.layerOf(c) == doc.layerOf(a), "layer");
        if (g.layerOf(c) != doc.layerOf(a)) {
            std::printf("  copy: scope %u, layer_ %zu, placed %zu, scope layer %zu, objects in the original's scope %zu\n", c.graphScope(), c.layer(), c.placedLayer(),
                        g.layerIdOfScope(c.graphScope()), std::size_t(std::count_if(originals.begin(), originals.end(), [&](std::size_t o) { return doc.object(o).graphScope() == a.graphScope(); })));
            std::printf("  layer differs: object #%zu '%s' type %d scope %u node %u; original layer %zu (own layer_ %zu, scope layer %zu), got %zu; groups %zu, parent %zu\n",
                        originals[k], a.name().c_str(), int(a.type()), a.graphScope(), a.graphNode(), doc.layerOf(a), a.layer(), doc.layerIdOfScope(a.graphScope()),
                        g.layerOf(c), a.groups().size(), a.parent());
        }
        note(a.groups().size() == c.groups().size(), "groups");
        note((a.parent() != 0) == (c.parent() != 0), "parent");
        note(a.appearance().size() == c.appearance().size(), "appearance.rowCount");
        note(a.activeFillIndex() == c.activeFillIndex() && a.activeStrokeIndex() == c.activeStrokeIndex() && a.activeEffectIndex() == c.activeEffectIndex(), "activeRows");
        note(a.frameAngle() == c.frameAngle(), "frameAngle");
        note(sameBox(a.geometryBounds(), c.geometryBounds()), "outline");
        note(a.geometry().size() == c.geometry().size(), "outline.segments");
        if (a.isText() || c.isText()) {
            note(a.text() == c.text() && a.fontName() == c.fontName() && a.fontSize() == c.fontSize(), "text.words");
            note(a.textMatrix() == c.textMatrix(), "text.matrix");
            note(a.textStyle() == c.textStyle(), "text.style");
            note(a.textWarp() == c.textWarp(), "text.warp");
            note(a.warpFrameLeft() == c.warpFrameLeft(), "text.warpFrame");
        }
        if (a.isSymbol() || c.isSymbol()) {
            note(a.symbol() == c.symbol() || (a.symbol() && c.symbol() && a.symbol()->id == c.symbol()->id), "symbol");
        }
        if (a.isImage() || c.isImage()) {
            note(static_cast<bool>(c.image()) == static_cast<bool>(a.image()), "image");
        }
    }
    return audit;
}

void printAudit(const char* name, const Audit& audit) {
    std::printf("%s: %zu objects bare-built from their shape nodes; fields that differ from the original:\n", name, audit.objects);
    if (audit.differs.empty()) {
        std::printf("    none\n");
    }
    for (const auto& [field, count] : audit.differs) {
        std::printf("    %-22s %zu\n", field.c_str(), count);
    }
}

}

TEST(GraphProbe, WhatABareObjectBuiltFromItsShapeNodeStillLacks) {
    SketchDocument sample;
    if (loadSample(sample)) {
        printAudit("skyDreamer", auditBareObjects(sample));
    }
    SketchDocument everything;
    fillEverything(everything);
    ASSERT_GT(everything.objectCount(), 15u);
    printAudit("one of everything", auditBareObjects(everything));
}

namespace {

// Loads `doc`'s graph, layers and libraries into a new document and builds its objects from the nodes alone; returns how the two compare.
void expectObjectsFromGraph(SketchDocument& doc, const char* name) {
    doc.touch();
    SketchDocument g;
    {
        SketchDocument::Batch batch(g);
        ASSERT_TRUE(g.graphFromText(doc.graphToText()));
        g.graphReplaced();
        g.setPageSize(doc.pageWidth(), doc.pageHeight());
        g.setSymbols(doc.symbols());
        g.setPatterns(doc.patterns());
        g.setBrushes(doc.brushes());
        g.setLayers(doc.layers());
        std::map<std::string, std::shared_ptr<const ImageData>> pictures;
        for (std::size_t i = 0; i < doc.objectCount(); ++i) {
            if (doc.object(i).isImage() && doc.object(i).image()) {
                pictures[doc.object(i).image()->key] = doc.object(i).image();
            }
        }
        const std::size_t made = g.objectsFromGraph([&](const std::string& key) {
            const auto found = pictures.find(key);
            return found != pictures.end() ? found->second : nullptr;
        });
        EXPECT_EQ(made, doc.objectCount()) << name;
    }
    ASSERT_EQ(g.objectCount(), doc.objectCount()) << name;

    std::map<std::string, std::size_t> twin;
    for (std::size_t j = 0; j < g.objectCount(); ++j) {
        twin[g.scopeText(g.object(j).graphScope()) + "#" + std::to_string(g.object(j).graphNode())] = j;
    }
    std::map<std::string, std::size_t> differs;
    const auto note = [&](bool same, const char* field) {
        if (!same) {
            ++differs[field];
        }
    };
    const auto sameBox = [](const BLBox& x, const BLBox& y) {
        return std::abs(x.x0 - y.x0) < 1e-2 && std::abs(x.x1 - y.x1) < 1e-2 && std::abs(x.y0 - y.y0) < 1e-2 && std::abs(x.y1 - y.y1) < 1e-2;
    };
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& a = doc.object(i);
        const auto found = twin.find(doc.scopeText(a.graphScope()) + "#" + std::to_string(a.graphNode()));
        if (found == twin.end()) {
            ++differs["no twin"];
            continue;
        }
        const SketchObject& c = g.object(found->second);
        note(a.type() == c.type(), "type");
        note(a.name() == c.name(), "name");
        note(a.ownVisible() == c.ownVisible() && a.ownLocked() == c.ownLocked(), "flags");
        note(std::abs(a.opacity() - c.opacity()) < 1e-6 && a.blendMode() == c.blendMode(), "opacity/blend");
        note(g.layerOf(c) == doc.layerOf(a), "layer");
        note(a.groups().size() == c.groups().size(), "groups");
        note((a.parent() != 0) == (c.parent() != 0), "parent");
        note(a.activeFillIndex() == c.activeFillIndex() && a.activeStrokeIndex() == c.activeStrokeIndex() && a.activeEffectIndex() == c.activeEffectIndex(),
             "activeRows");
        note(std::abs(a.frameAngle() - c.frameAngle()) < 1e-5, "frameAngle");
        bool looks = a.appearance().size() == c.appearance().size();
        for (std::size_t k = 0; looks && k < a.appearance().size(); ++k) {
            looks = paramsOf(a.appearance()[k]) == paramsOf(c.appearance()[k]);
        }
        note(looks, "looks");
        BLBox boxA, boxB;
        a.geometry().get_bounding_box(&boxA);
        c.geometry().get_bounding_box(&boxB);
        note(sameBox(boxA, boxB), "outline");
        if (a.isText() || c.isText()) {
            note(a.text() == c.text() && a.fontName() == c.fontName() && a.textStyle() == c.textStyle(), "text");
        }
        if (a.isSymbol() || c.isSymbol()) {
            note(a.symbol() && c.symbol() && a.symbol()->id == c.symbol()->id, "symbol");
        }
        if (a.isImage() || c.isImage()) {
            note(a.image() && c.image() && a.image()->key == c.image()->key, "image");
        }
    }
    for (const auto& [field, count] : differs) {
        ADD_FAILURE() << name << ": " << count << " objects differ in " << field;
    }
    EXPECT_GT(doc.objectCount(), 15u) << name;
    g.touch();   // the first sync must leave what was built alone
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
    const std::vector<unsigned char> was = render(doc), now = render(g);
    ASSERT_EQ(was.size(), now.size()) << name;
    std::size_t off = 0;
    for (std::size_t k = 0; k < was.size(); ++k) {
        off += std::abs(int(was[k]) - int(now[k])) > 6 ? 1 : 0;
    }
    EXPECT_LT(off, was.size() / 500) << name << ": " << off << " of " << was.size() << " bytes of the render differ";
    for (std::size_t j = 0; j < g.objectCount(); ++j) {
        const SketchObject& c = g.object(j);
        EXPECT_TRUE(c.boundToNodes()) << name << ": object " << j << " '" << c.name() << "' was not kept bound to its node";
    }
}

}

TEST(GraphProbe, ObjectsBuiltFromTheGraphAloneMatchTheOriginals) {
    SketchDocument sample;
    if (loadSample(sample)) {
        expectObjectsFromGraph(sample, "skyDreamer");
    }
    SketchDocument everything;
    fillEverything(everything);
    expectObjectsFromGraph(everything, "one of everything");
}

namespace {

// Builds `doc`'s objects in `g` from a copy of its graph after `edit` has changed that copy.
void buildFromEditedGraph(SketchDocument& doc, SketchDocument& g, const std::function<void(jitnodes::Network&)>& edit, std::vector<std::string>* notes = nullptr) {
    doc.touch();
    SketchDocument::Batch batch(g);
    ASSERT_TRUE(g.graphFromText(doc.graphToText()));
    edit(g.network());
    g.graphReplaced();
    g.setPageSize(doc.pageWidth(), doc.pageHeight());
    g.setLayers(doc.layers());
    g.objectsFromGraph(nullptr, notes);
}

std::size_t countOfType(const jitnodes::Network& net, const char* type) {
    std::size_t n = 0;
    for (jitnodes::NodeId id : net.nodeIds()) {
        n += net.node(id).typeName() == type ? 1 : 0;
    }
    return n;
}

double widthOf(const SketchObject& object) {
    BLBox box;
    object.geometry().get_bounding_box(&box);
    return box.x1 - box.x0;
}

}

TEST(GraphProbe, AGeneratorWiredInByHandIsNotBuiltAsAnObject) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    doc.add(SketchObject::makeRect(50, 50, 30, 30));

    SketchDocument g;
    buildFromEditedGraph(doc, g, [](jitnodes::Network& net) { net.addNode("circle"); });
    EXPECT_EQ(g.objectCount(), 1u) << "only shape nodes become objects";
}

TEST(GraphProbe, AnObjectWhoseShapeHasNoPlacementNodeKeepsItsLook) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    doc.add(SketchObject::makeRect(10, 10, 40, 30));
    SketchObject& target = doc.add(SketchObject::makeRect(70, 10, 40, 30));
    target.setFill(SketchPaint::solid(SketchColor::fromHex("#336699")));
    doc.add(SketchObject::makeRect(130, 10, 40, 30));
    doc.touch();
    const jitnodes::NodeId shape = target.graphNode();
    const std::size_t looks = target.appearance().size();
    ASSERT_GT(looks, 0u);

    SketchDocument g;
    buildFromEditedGraph(doc, g, [&](jitnodes::Network& net) {
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const jitnodes::NodeId place = placementNodes(net, ids).at(shape);
        const jitnodes::NodeId first = appearanceSuccessors(net, ids).at(shape);
        net.removeNode(place);
        net.connect(shape, first, 0);
    });
    ASSERT_EQ(g.objectCount(), 3u);
    EXPECT_EQ(countOfType(g.network(), kTransformNodeType), 3u) << "the sync that ends the batch gives it a placement node";
    const SketchObject* built = nullptr;
    for (std::size_t i = 0; i < g.objectCount(); ++i) {
        built = g.object(i).graphNode() == shape ? &g.object(i) : built;
    }
    ASSERT_NE(built, nullptr);
    EXPECT_EQ(built->appearance().size(), looks);
    EXPECT_NEAR(widthOf(*built), 40.0, 0.01);

    g.touch();
    EXPECT_EQ(g.objectCount(), 3u);
    EXPECT_EQ(countOfType(g.network(), kTransformNodeType), 3u);
    EXPECT_NEAR(widthOf(*g.findById(built->id())), 40.0, 0.01);
}

TEST(GraphProbe, AKeyedLookNumberKeepsItsBaseValueThroughABuildAndASync) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    SketchObject& rect = doc.add(SketchObject::makeRect(10, 10, 40, 30));
    doc.touch();
    std::size_t row = rect.appearance().size();
    for (std::size_t i = 0; i < rect.appearance().size(); ++i) {
        row = rect.appearance()[i].kind == AppearanceKind::Stroke ? i : row;
    }
    ASSERT_LT(row, rect.appearance().size());
    const double base = rect.appearance()[row].strokeWidth;

    jitnodes::NodeId stroke = jitnodes::invalidNodeId;
    SketchDocument g;
    buildFromEditedGraph(doc, g, [&](jitnodes::Network& net) {
        for (jitnodes::NodeId id : net.nodeIds()) {
            stroke = net.node(id).typeName() == kStrokeNodeType ? id : stroke;
        }
        ASSERT_NE(stroke, jitnodes::invalidNodeId);
        net.node(stroke).setKeyframes("width", {{0.0, 3.0f, jitnodes::KeyInterp::linear}, {1.0, 30.0f, jitnodes::KeyInterp::linear}});
    });
    ASSERT_EQ(g.objectCount(), 1u);
    const float held = g.network().node(stroke).baseFloat("width");
    EXPECT_NEAR(g.object(0).appearance()[row].strokeWidth, double(held), 1e-4) << "the object takes the stored value, not the cooked one";
    g.touch();
    EXPECT_EQ(g.network().node(stroke).keyframes("width").size(), 2u);
    EXPECT_FLOAT_EQ(g.network().node(stroke).baseFloat("width"), held);
    (void)base;
}

TEST(GraphProbe, AParentLinkThatCannotBeResolvedIsReported) {
    SketchDocument doc;
    doc.setPageSize(200, 200);
    SketchObject& child = doc.add(SketchObject::makeRect(10, 10, 20, 20));
    SketchObject& parent = doc.add(SketchObject::makeRect(50, 10, 20, 20));
    doc.setParent(child.id(), parent.id());
    doc.touch();

    std::vector<std::string> notes;
    SketchDocument whole;
    buildFromEditedGraph(doc, whole, [](jitnodes::Network&) {}, &notes);
    ASSERT_EQ(whole.objectCount(), 2u);
    EXPECT_TRUE(notes.empty());
    std::size_t parented = 0;
    for (std::size_t i = 0; i < whole.objectCount(); ++i) {
        parented += whole.object(i).parent() != 0 ? 1 : 0;
    }
    EXPECT_EQ(parented, 1u);

    SketchDocument broken;
    buildFromEditedGraph(doc, broken, [](jitnodes::Network& net) {
        jitnodes::NodeId shape = jitnodes::invalidNodeId, linked = jitnodes::invalidNodeId;
        for (jitnodes::NodeId id : net.nodeIds()) {
            if (net.node(id).typeName() == kTransformNodeType && net.inputOf(id, 1) != jitnodes::invalidNodeId) {
                linked = id;
            } else if (isShapeNodeType(net.node(id).typeName())) {
                shape = id;
            }
        }
        ASSERT_NE(linked, jitnodes::invalidNodeId);
        net.connect(shape, linked, 1);   // a wire to something that is no object's placement
    }, &notes);
    ASSERT_EQ(broken.objectCount(), 2u);
    EXPECT_EQ(notes.size(), 1u);
    for (std::size_t i = 0; i < broken.objectCount(); ++i) {
        EXPECT_EQ(broken.object(i).parent(), 0u);
    }
}

TEST(GraphProbe, ATextObjectIsRebuiltFromNothingButItsNodesParameters) {
    SketchDocument doc;
    fillEverything(doc);
    doc.touch();
    std::size_t checked = 0, warped = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& source = doc.object(i);
        if (!source.isText()) {
            continue;
        }
        std::unique_ptr<SketchObject> bare(SketchObject::makeText("x", "", 9.0, BLPoint(0, 0)));
        ASSERT_TRUE(applyTextNodeParams(*bare, doc.node(source.graphRef())));
        EXPECT_EQ(bare->text(), source.text());
        EXPECT_EQ(bare->fontName(), source.fontName());
        EXPECT_NEAR(bare->fontSize(), source.fontSize(), 1e-4);
        EXPECT_TRUE(bare->textStyle() == source.textStyle());
        EXPECT_TRUE(bare->textWarp() == source.textWarp());
        EXPECT_NEAR(bare->warpFrameLeft(), source.warpFrameLeft(), 1e-3) << "the warp frame comes back from the layout";
        for (int k = 0; k < 6; ++k) {
            EXPECT_NEAR(bare->textMatrix().m[k], source.textMatrix().m[k], 1e-3);
        }
        BLBox a, b;
        bare->geometry().get_bounding_box(&a);
        source.geometry().get_bounding_box(&b);
        EXPECT_NEAR(a.x0, b.x0, 1e-2);
        EXPECT_NEAR(a.y0, b.y0, 1e-2);
        EXPECT_NEAR(a.x1, b.x1, 1e-2);
        EXPECT_NEAR(a.y1, b.y1, 1e-2);
        warped += source.textWarp().active() ? 1 : 0;
        ++checked;
    }
    EXPECT_GE(checked, 2u);
    EXPECT_GE(warped, 1u);
}

TEST(GraphProbe, ASymbolOrImageObjectNamesWhatItShowsInItsNode) {
    SketchDocument doc;
    fillEverything(doc);
    doc.touch();
    std::size_t symbols = 0, images = 0;
    for (std::size_t i = 0; i < doc.objectCount(); ++i) {
        const SketchObject& object = doc.object(i);
        const jitnodes::Node& node = doc.node(object.graphRef());
        if (object.isSymbol()) {
            ASSERT_TRUE(object.symbol());
            EXPECT_EQ(node.getString("symbol"), std::to_string(object.symbol()->id));
            EXPECT_EQ(doc.findSymbol(object.symbol()->id), object.symbol()) << "the document resolves the node's reference";
            ++symbols;
        } else if (object.isImage()) {
            ASSERT_TRUE(object.image());
            EXPECT_FALSE(object.image()->key.empty());
            EXPECT_EQ(node.getString("imageKey"), object.image()->key);
            EXPECT_EQ(node.getString("image"), object.image()->name);
            ++images;
        }
    }
    EXPECT_GE(symbols, 1u);
    EXPECT_GE(images, 1u);
    const auto a = decodeImage(pngBytes(), "same.png"), b = decodeImage(pngBytes(), "other.png");
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->key, b->key) << "the key follows the bytes, not the name";
    std::string changed = pngBytes();
    changed.back() ^= 1;
    EXPECT_NE(a->key, std::string(decodeImage(changed, "same.png") ? decodeImage(changed, "same.png")->key : std::string()));
}

TEST(GraphProbe, GroupsAndClipsSurviveTheNativeFileAndTheSkbRoundTrip) {
    SketchDocument everything;
    fillEverything(everything);
    const std::size_t groups = [&] {
        std::size_t n = 0;
        for (std::size_t i = 0; i < everything.objectCount(); ++i) {
            n += everything.object(i).groups().empty() ? 0 : 1;
        }
        return n;
    }();
    ASSERT_EQ(groups, 4u);

    SketchDocument viaCurvy, viaSkb;
    std::string error;
    ASSERT_TRUE(documentFromCurvy(curvyFromDocument(everything), viaCurvy, &error)) << error;
    ASSERT_TRUE(documentFromBinary(binaryFromDocument(everything, false), viaSkb));
    for (SketchDocument* loaded : {&viaCurvy, &viaSkb}) {
        std::size_t n = 0;
        for (std::size_t i = 0; i < loaded->objectCount(); ++i) {
            n += loaded->object(i).groups().empty() ? 0 : 1;
        }
        EXPECT_EQ(n, groups);
        EXPECT_EQ(loaded->objectCount(), everything.objectCount());
    }
    EXPECT_EQ(viaCurvy.graphToText(), viaSkb.graphToText());
}

TEST(GraphProbe, EveryKindOfLookSurvivesItsNodes) {
    SketchDocument doc;
    doc.setPageSize(400, 400);
    SketchObject& swatch = doc.add(SketchObject::makeRect(0, 0, 10, 10));
    doc.setSelection({swatch.id()});
    const std::size_t pattern = doc.makePattern("P");
    SketchObject& bar = doc.add(SketchObject::makeRect(0, 100, 60, 10));
    doc.setSelection({bar.id()});
    const std::size_t brush = doc.makeBrush(BrushKind::Scatter, "B");
    ASSERT_NE(pattern, 0u);
    ASSERT_NE(brush, 0u);

    SketchGradient linear;
    linear.stops = {{0.0, SketchColor::fromHex("#102030")}, {0.37, SketchColor::rgb(0.9f, 0.2f, 0.1f, 0.5f)}, {1.0, SketchColor::fromHex("#ffee00")}};
    linear.angleDegrees = 33.3;
    linear.spread = GradientSpread::Reflect;
    SketchGradient radial = linear;
    radial.hasAxis = true;
    radial.axisX1 = 0.31;
    radial.axisY1 = 0.42;
    radial.axisX2 = 0.9;
    radial.axisY2 = 1.2;
    radial.axisAspect = 1.7;
    radial.axisSkew = 0.25;
    SketchGradient points;
    points.points = {{0.1, 0.2, SketchColor::fromHex("#ff0000"), 0.3}, {0.8, 0.7, SketchColor::fromHex("#0000ff"), 0.55}};
    SketchGradient along = linear;
    along.alongStroke = true;

    StrokeOptions busy;
    busy.cap = StrokeCap::Round;
    busy.join = StrokeJoin::Bevel;
    busy.miterLimit = 4.5;
    busy.dashes = {5.5, 2.25, 1.0};
    busy.align = StrokeAlign::Outside;
    busy.arrowStart = ArrowStyle::Filled;
    busy.arrowEnd = ArrowStyle::Dot;
    busy.arrowScale = 140.0;
    StrokeOptions variable;
    variable.widths = {{0.0, 1.0, 1.0}, {0.4, 2.5, 0.5}, {1.0, 0.2, 0.2}};
    StrokeOptions brushed;
    brushed.brush = doc.findBrush(brush);

    std::vector<AppearanceItem> items;
    items.push_back(AppearanceItem::makeFill(SketchPaint::solid(SketchColor::cmyk(0.1f, 0.2f, 0.3f, 0.4f, 0.8f))));
    items.back().opacity = 0.37;
    items.back().blendMode = BlendMode::Multiply;
    items.push_back(AppearanceItem::makeFill(SketchPaint::linear(linear)));
    items.push_back(AppearanceItem::makeFill(SketchPaint::radial(radial)));
    items.push_back(AppearanceItem::makeFill(SketchPaint::points(points)));
    items.push_back(AppearanceItem::makeFill(SketchPaint::pattern(doc.findPattern(pattern), 1.75, 33.0)));
    items.back().visible = false;
    items.push_back(AppearanceItem::makeFill(SketchPaint::none()));
    items.push_back(AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::fromHex("#336699")), 2.75, busy));
    items.push_back(AppearanceItem::makeStroke(SketchPaint::linear(along), 6.0, variable));
    items.push_back(AppearanceItem::makeStroke(SketchPaint::radial(radial), 3.0, brushed));
    items.push_back(AppearanceItem::makeStroke(SketchPaint::pattern(doc.findPattern(pattern), 0.5, 10.0), 1.0));
    items.push_back(AppearanceItem::makeStroke(SketchPaint::points(points), 1.0));
    for (EffectKind kind : {EffectKind::DropShadow, EffectKind::OuterGlow, EffectKind::GaussianBlur, EffectKind::RoundCorners, EffectKind::InnerShadow, EffectKind::OffsetPath, EffectKind::Roughen}) {
        items.push_back(AppearanceItem::makeEffect(kind));
    }
    items.push_back(AppearanceItem::makeEffect(EffectKind::DropShadow));
    items.back().color = SketchColor::fromHex("#223344");
    items.back().softness = 9.3;
    items.back().offsetX = -2.25;
    items.back().offsetY = 7.5;
    items.back().opacity = 0.61;
    items.back().blendMode = BlendMode::Screen;

    SketchObject& target = doc.add(SketchObject::makeRect(100, 100, 80, 60));
    target.setAppearance(items);
    doc.setSelection({target.id()});
    doc.add(SketchObject::makeRect(200, 100, 80, 60));

    const LookTally t = tallyLooks(doc);
    printTally("every-kind document", t);
    EXPECT_EQ(t.chainLengthWrong, 0u);
    EXPECT_EQ(t.nodeTypeWrong, 0u);
}
