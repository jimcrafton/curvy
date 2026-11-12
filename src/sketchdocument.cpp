#include "curvy/sketchdocument.h"

#include "curvy/objectnode.h"

#include <jitnodes/graph/serializeValue.h>
#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>
#include <json5/json5_input.hpp>
#include <json5/json5_output.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

#ifndef CURVY_VERSION
#define CURVY_VERSION "0"
#endif

namespace curvy {

    namespace {

        constexpr int kFileVersion = 3;

        void step(TaskProgress* progress, float fraction) {
            if (progress != nullptr) {
                progress->check();
                progress->set(fraction);
            }
        }

        // A float as the shortest decimal that reads back to the same float, so 0.1f is written 0.1 and not 0.10000000149011612.
        json5::value real(float value) {
            char buffer[32];
            const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
            double widened = double(value);
            std::from_chars(buffer, written.ptr, widened);
            return json5::value(widened);
        }

        std::string utcNow() {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm parts{};
            gmtime_s(&parts, &now);
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
            return buffer;
        }

        struct Bad : std::runtime_error {
            using std::runtime_error::runtime_error;
        };

        [[noreturn]] void fail(const std::string& what) { throw Bad(what); }

        bool find(const json5::value& object, const char* key, json5::value& out) {
            if (!object.is_object()) {
                return false;
            }
            for (auto [name, child] : json5::object_view(object)) {
                if (std::string_view(name) == key) {
                    out = child;
                    return true;
                }
            }
            return false;
        }

        json5::value need(const json5::value& object, const char* key, const std::string& where) {
            json5::value out;
            if (!find(object, key, out)) {
                fail(where + ": missing \"" + key + "\"");
            }
            return out;
        }

        double number(const json5::value& value, const std::string& where) {
            if (!value.is_number() || !std::isfinite(value.get<double>())) {
                fail(where + " must be a number");
            }
            return value.get<double>();
        }

        double numberOr(const json5::value& object, const char* key, double fallback, const std::string& where) {
            json5::value v;
            return find(object, key, v) ? number(v, where + "." + key) : fallback;
        }

        std::string stringOr(const json5::value& object, const char* key, const std::string& fallback, const std::string& where) {
            json5::value v;
            if (!find(object, key, v)) {
                return fallback;
            }
            if (!v.is_string()) {
                fail(where + "." + key + " must be a string");
            }
            return v.get_c_str();
        }

        newui::Color colorOf(const json5::value& value, const std::string& where) {
            const json5::array_view list(value);
            if (!list.is_valid() || list.size() != 4) {
                fail(where + " must be a colour: [r, g, b, alpha]");
            }
            float c[4];
            for (std::size_t i = 0; i < 4; ++i) {
                c[i] = float(number(list[i], where));
                if (!(c[i] >= 0.0f && c[i] <= 1.0f)) {
                    fail(where + " components must be between 0 and 1");
                }
            }
            return newui::Color(c[0], c[1], c[2], c[3]);
        }

        // The optional "view" section. Reading is lenient: a view is a convenience, so a part that is missing or malformed is dropped and the
        // file still opens (the page is then fitted as for any file without a view).
        class ViewFile {
        public:
            static void write(json5::builder& b, const ViewState& view) {
                b.push_object();
                if (view.canvas) {
                    b.push_object();
                    b["zoom"] = real(view.canvas->zoom);
                    b.push_array();
                    b += real(view.canvas->panX);
                    b += real(view.canvas->panY);
                    b["pan"] = b.pop();
                    b["canvas"] = b.pop();
                }
                if (view.graph) {
                    const GraphViewState& graph = *view.graph;
                    b.push_object();
                    b["open"] = json5::value(graph.open ? 1.0 : 0.0);
                    writeIds(b, "level", graph.level);
                    if (graph.placed) {
                        b["zoom"] = json5::value(graph.zoom);
                        b.push_array();
                        b += json5::value(graph.panX);
                        b += json5::value(graph.panY);
                        b["pan"] = b.pop();
                    }
                    writeIds(b, "selected", graph.selected);
                    b["graph"] = b.pop();
                }
                b["view"] = b.pop();
            }

            static ViewState read(const json5::value& root) {
                ViewState view;
                json5::value part;
                if (!find(root, "view", part) || !part.is_object()) {
                    return view;
                }
                json5::value canvasPart;
                if (find(part, "canvas", canvasPart)) {
                    double zoom = 0.0;
                    double pan[2] = {0.0, 0.0};
                    if (readNumber(canvasPart, "zoom", zoom) && zoom > 0.0 && readPair(canvasPart, "pan", pan)) {
                        view.canvas = CanvasViewState{float(zoom), float(pan[0]), float(pan[1])};
                    }
                }
                json5::value graphPart;
                if (find(part, "graph", graphPart) && graphPart.is_object()) {
                    GraphViewState graph;
                    double open = 0.0;
                    graph.open = readNumber(graphPart, "open", open) && open != 0.0;
                    graph.level = readIds(graphPart, "level");
                    graph.selected = readIds(graphPart, "selected");
                    double pan[2] = {0.0, 0.0};
                    if (readNumber(graphPart, "zoom", graph.zoom) && graph.zoom > 0.0 && readPair(graphPart, "pan", pan)) {
                        graph.placed = true;
                        graph.panX = pan[0];
                        graph.panY = pan[1];
                    }
                    view.graph = std::move(graph);
                }
                return view;
            }

        private:
            static void writeIds(json5::builder& b, const char* key, const std::vector<jitnodes::NodeId>& ids) {
                if (ids.empty()) {
                    return;
                }
                b.push_array();
                for (jitnodes::NodeId id : ids) {
                    b += json5::value(double(id));
                }
                b[key] = b.pop();
            }

            static bool readNumber(const json5::value& object, const char* key, double& out) {
                json5::value v;
                if (!find(object, key, v) || !v.is_number() || !std::isfinite(v.get<double>())) {
                    return false;
                }
                out = v.get<double>();
                return true;
            }

            static bool readPair(const json5::value& object, const char* key, double (&out)[2]) {
                json5::value v;
                if (!find(object, key, v)) {
                    return false;
                }
                const json5::array_view list(v);
                if (!list.is_valid() || list.size() != 2 || !list[0].is_number() || !list[1].is_number()) {
                    return false;
                }
                out[0] = list[0].get<double>();
                out[1] = list[1].get<double>();
                return std::isfinite(out[0]) && std::isfinite(out[1]);
            }

            static std::vector<jitnodes::NodeId> readIds(const json5::value& object, const char* key) {
                std::vector<jitnodes::NodeId> ids;
                json5::value v;
                if (!find(object, key, v)) {
                    return ids;
                }
                const json5::array_view list(v);
                if (!list.is_valid()) {
                    return ids;
                }
                for (std::size_t i = 0; i < list.size(); ++i) {
                    if (!list[i].is_number() || !(list[i].get<double>() >= 0.0 && list[i].get<double>() < 4e9)) {
                        return {};
                    }
                    ids.push_back(jitnodes::NodeId(list[i].get<double>()));
                }
                return ids;
            }
        };

        // Written beside the target and renamed over it, so a crash or a full disk leaves the previous version whole.
        bool writeAtomically(const std::string& path, const std::string& text) {
            const std::filesystem::path target = std::filesystem::u8path(path);
            std::filesystem::path temporary = target;
            temporary += ".tmp";
            std::error_code error;
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out) {
                    return false;
                }
                out << text;
                out.flush();
                if (!out) {
                    out.close();
                    std::filesystem::remove(temporary, error);
                    return false;
                }
            }
            std::filesystem::rename(temporary, target, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                return false;
            }
            return true;
        }
    }

    void SketchDocument::requireUnlocked() const {
        if (isLocked()) {
            throw std::runtime_error("the document is locked while it loads or saves");
        }
    }

    void SketchDocument::changed() {
        markModified();
        updateAllViews();
    }

    void SketchDocument::setPageSize(float width, float height) {
        requireUnlocked();
        width = (std::max)(width, 1.0f);
        height = (std::max)(height, 1.0f);
        if (width == pageWidth_ && height == pageHeight_) {
            return;
        }
        pageWidth_ = width;
        pageHeight_ = height;
        changed();
    }

    void SketchDocument::setPpi(float ppi) {
        requireUnlocked();
        ppi = (std::max)(ppi, 1.0f);
        if (ppi != ppi_) {
            ppi_ = ppi;
            changed();
        }
    }

    void SketchDocument::setPageColor(const newui::Color& color) {
        requireUnlocked();
        if (!(color == pageColor_)) {
            pageColor_ = color;
            changed();
        }
    }

    void SketchDocument::setName(const std::string& name) {
        requireUnlocked();
        if (name != name_) {
            name_ = name;
            changed();
        }
    }

    void SketchDocument::setColorMode(ColorSpace mode) {
        requireUnlocked();
        if ((mode == ColorSpace::RGB || mode == ColorSpace::CMYK) && mode != colorMode_) {
            colorMode_ = mode;
            changed();
        }
    }

    // ---- Swatches and palettes ----

    void SketchDocument::clampActivePalette() {
        if (activePalette_ >= palettes_.size()) {
            activePalette_ = palettes_.empty() ? 0 : palettes_.size() - 1;
        }
    }

    Palette& SketchDocument::paletteAt(std::size_t index) {
        if (index >= palettes_.size()) {
            throw std::runtime_error("there is no palette " + std::to_string(index));
        }
        return palettes_[index];
    }

    Swatch& SketchDocument::swatchAt(std::size_t palette, std::size_t index) {
        Palette& list = paletteAt(palette);
        if (index >= list.swatches.size()) {
            throw std::runtime_error("palette \"" + list.name + "\" has no swatch " + std::to_string(index));
        }
        return list.swatches[index];
    }

    void SketchDocument::editPalettes(const std::string& description, const std::function<void()>& edit, bool withGraph) {
        requireUnlocked();
        auto before = std::make_shared<const std::vector<Palette>>(palettes_);
        std::shared_ptr<const jitnodes::GraphSnapshot> graphBefore, graphAfter;
        if (withGraph) {
            graphBefore = std::make_shared<const jitnodes::GraphSnapshot>(snapshotGraph());
        }
        edit();
        auto after = std::make_shared<const std::vector<Palette>>(palettes_);
        bool graphSame = true;
        if (withGraph) {
            graphAfter = std::make_shared<const jitnodes::GraphSnapshot>(snapshotGraph());
            graphSame = jitnodes::sameGraph(*graphBefore, *graphAfter);
        }
        if (*before == *after && graphSame) {
            return;
        }
        auto applied = std::make_shared<bool>(true);   // push() runs doIt at once, and the edit is already made
        undoStack_.push({description,
            [this, after, graphAfter, applied]() {
                if (*applied) {
                    *applied = false;
                } else {
                    palettes_ = *after;
                    clampActivePalette();
                    if (graphAfter) {
                        restoreGraph(*graphAfter);
                    }
                    changed();
                    palettesChanged();
                }
            },
            [this, before, graphBefore]() {
                palettes_ = *before;
                clampActivePalette();
                if (graphBefore) {
                    restoreGraph(*graphBefore);
                }
                changed();
                palettesChanged();
            }});
        changed();
        palettesChanged();
    }

    void SketchDocument::forEachNode(const std::function<void(jitnodes::Network&, jitnodes::NodeId)>& visit) {
        const std::function<void(jitnodes::Network&)> walk = [&](jitnodes::Network& net) {
            for (const jitnodes::NodeId id : net.nodeIds()) {
                visit(net, id);
                if (jitnodes::Network* inner = net.node(id).childNetwork()) {
                    walk(*inner);
                }
            }
        };
        walk(network());
    }

    void SketchDocument::applyLink(std::uint32_t link, const Swatch* color) {
        forEachNode([&](jitnodes::Network& net, jitnodes::NodeId id) {
            jitnodes::Node& node = net.node(id);
            const std::vector<std::string> tags = node.tags();
            for (const std::string& tag : tags) {
                std::uint32_t tied = 0;
                std::string group;
                if (!SwatchLinks::parse(tag, tied, group) || tied != link) {
                    continue;
                }
                if (color == nullptr) {
                    node.removeTag(tag);
                    continue;
                }
                SwatchLinks::Channels channels;
                if (!SwatchLinks::channels(node, group, channels)) {
                    continue;
                }
                node.setFloat(channels.r, color->r);
                node.setFloat(channels.g, color->g);
                node.setFloat(channels.b, color->b);
                if (!channels.a.empty()) {
                    node.setFloat(channels.a, color->a);
                }
                net.markDirty(id);
            }
        });
    }

    void SketchDocument::setActivePalette(std::size_t index) {
        requireUnlocked();
        if (index >= palettes_.size()) {
            throw std::runtime_error("there is no palette " + std::to_string(index));
        }
        activePalette_ = index;
        palettesChanged();
    }

    namespace {
        std::string uniqueName(const std::vector<Palette>& palettes, const std::string& wanted) {
            const auto taken = [&](const std::string& name) {
                return std::any_of(palettes.begin(), palettes.end(), [&](const Palette& p) { return p.name == name; });
            };
            std::string name = wanted;
            for (int n = 2; taken(name); ++n) {
                name = wanted + " " + std::to_string(n);
            }
            return name;
        }
    }

    std::size_t SketchDocument::addPalette(const std::string& name) {
        if (name.empty()) {
            throw std::runtime_error("a palette needs a name");
        }
        std::size_t index = 0;
        editPalettes("Add Palette", [&]() {
            palettes_.push_back(Palette{uniqueName(palettes_, name), {}});
            index = palettes_.size() - 1;
        });
        return index;
    }

    std::size_t SketchDocument::duplicatePalette(std::size_t index) {
        Palette copy = paletteAt(index);
        for (Swatch& swatch : copy.swatches) {
            swatch.link = 0;   // a copy is plain: only the original is tied to colour rows
        }
        std::size_t result = 0;
        editPalettes("Duplicate Palette", [&]() {
            copy.name = uniqueName(palettes_, copy.name + " copy");
            palettes_.insert(palettes_.begin() + std::ptrdiff_t(index) + 1, copy);
            result = index + 1;
        });
        return result;
    }

    void SketchDocument::renamePalette(std::size_t index, const std::string& name) {
        Palette& palette = paletteAt(index);
        if (name.empty()) {
            throw std::runtime_error("a palette needs a name");
        }
        if (name == palette.name) {
            return;
        }
        for (std::size_t i = 0; i < palettes_.size(); ++i) {
            if (i != index && palettes_[i].name == name) {
                throw std::runtime_error("there is already a palette called \"" + name + "\"");
            }
        }
        editPalettes("Rename Palette", [&]() { palette.name = name; });
    }

    void SketchDocument::deletePalette(std::size_t index) {
        paletteAt(index);
        if (palettes_.size() < 2) {
            throw std::runtime_error("a document keeps at least one palette");
        }
        std::vector<std::uint32_t> links;
        for (const Swatch& swatch : palettes_[index].swatches) {
            if (swatch.link != 0) {
                links.push_back(swatch.link);
            }
        }
        editPalettes("Delete Palette", [&]() {
            for (const std::uint32_t link : links) {
                applyLink(link, nullptr);
            }
            palettes_.erase(palettes_.begin() + std::ptrdiff_t(index));
            if (activePalette_ > index || activePalette_ >= palettes_.size()) {
                activePalette_ = activePalette_ > 0 ? activePalette_ - 1 : 0;
            }
        }, !links.empty());
    }

    std::size_t SketchDocument::addSwatch(std::size_t palette, const Swatch& swatch) {
        Palette& list = paletteAt(palette);
        std::size_t index = 0;
        editPalettes("Add Swatch", [&]() {
            list.swatches.push_back(swatch);
            list.swatches.back().link = 0;
            index = list.swatches.size() - 1;
        });
        return index;
    }

    void SketchDocument::replaceSwatch(std::size_t palette, std::size_t index, const Swatch& swatch) {
        Swatch& target = swatchAt(palette, index);
        const std::uint32_t link = target.link;
        editPalettes("Replace Swatch", [&]() {
            const std::string name = swatch.name.empty() ? target.name : swatch.name;
            target = swatch;
            target.name = name;
            target.link = link;
            if (link != 0) {
                applyLink(link, &target);
            }
        }, link != 0);
    }

    void SketchDocument::setSwatchLinked(std::size_t palette, std::size_t index, bool linked) {
        Swatch& target = swatchAt(palette, index);
        const std::uint32_t old = target.link;
        if (linked == (old != 0)) {
            return;
        }
        std::uint32_t next = 1;
        for (const Palette& list : palettes_) {
            for (const Swatch& swatch : list.swatches) {
                next = (std::max)(next, swatch.link + 1);
            }
        }
        editPalettes(linked ? "Link Swatch" : "Unlink Swatch", [&]() {
            if (linked) {
                target.link = next;
            } else {
                applyLink(old, nullptr);
                target.link = 0;
            }
        }, !linked);
    }

    void SketchDocument::renameSwatch(std::size_t palette, std::size_t index, const std::string& name) {
        Swatch& target = swatchAt(palette, index);
        editPalettes("Rename Swatch", [&]() { target.name = name; });
    }

    void SketchDocument::deleteSwatch(std::size_t palette, std::size_t index) {
        const std::uint32_t link = swatchAt(palette, index).link;
        editPalettes("Delete Swatch", [&]() {
            if (link != 0) {
                applyLink(link, nullptr);
            }
            std::vector<Swatch>& list = palettes_[palette].swatches;
            list.erase(list.begin() + std::ptrdiff_t(index));
        }, link != 0);
    }

    void SketchDocument::moveSwatch(std::size_t palette, std::size_t from, std::size_t to) {
        swatchAt(palette, from);
        editPalettes("Move Swatch", [&]() {
            std::vector<Swatch>& list = palettes_[palette].swatches;
            const Swatch moved = list[from];
            list.erase(list.begin() + std::ptrdiff_t(from));
            list.insert(list.begin() + std::ptrdiff_t((std::min)(to, list.size())), moved);
        });
    }

    void SketchDocument::noteRecentColor(const Swatch& color) {
        Swatch plain = color;
        plain.name.clear();
        plain.link = 0;
        recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const Swatch& s) { return s.sameColor(plain); }), recent_.end());
        recent_.insert(recent_.begin(), plain);
        if (recent_.size() > kRecentLimit) {
            recent_.resize(kRecentLimit);
        }
        palettesChanged();
    }

    void SketchDocument::install(const std::string& name, float width, float height, float ppi, ColorSpace colorMode, const newui::Color& pageColor) {
        clearGraph();
        objectProblems_.clear();
        undoStack_.clear();
        reset();
        name_ = name;
        pageWidth_ = (std::max)(width, 1.0f);
        pageHeight_ = (std::max)(height, 1.0f);
        ppi_ = (std::max)(ppi, 1.0f);
        colorMode_ = colorMode == ColorSpace::CMYK ? ColorSpace::CMYK : ColorSpace::RGB;
        pageColor_ = pageColor;
        palettes_ = {DefaultPalette::make()};
        activePalette_ = 0;
        recent_.clear();
        viewState_ = ViewState();
        updateAllViews();
    }

    void SketchDocument::newBlank(const std::string& name, float width, float height, float ppi, ColorSpace colorMode, const newui::Color& pageColor) {
        requireUnlocked();
        install(name, width, height, ppi, colorMode, pageColor);
    }

    // ---- The .curvy file ----

    bool SketchDocument::writeToFile(const std::string& path) {
        LockGuard lock(*this);
        lastError_.clear();
        try {
            step(taskProgress_, 0.02f);
            json5::document out;
            json5::builder b(out);
            b.push_object();
            b["format"] = b.new_string("curvy");
            b["version"] = json5::value(double(kFileVersion));

            b.push_object();
            b["app"] = b.new_string("curvy");
            b["appVersion"] = b.new_string(CURVY_VERSION);
            b["saved"] = b.new_string(utcNow());
            b["meta"] = b.pop();

            b.push_object();
            b["name"] = b.new_string(name_);
            b.push_object();
            b["width"] = real(pageWidth_);
            b["height"] = real(pageHeight_);
            b["ppi"] = real(ppi_);
            b["colorMode"] = b.new_string(colorMode_ == ColorSpace::CMYK ? "cmyk" : "rgb");
            b.push_array();
            b += real(pageColor_.r);
            b += real(pageColor_.g);
            b += real(pageColor_.b);
            b += real(pageColor_.a);
            b["background"] = b.pop();
            b["page"] = b.pop();

            b.push_object();
            b["active"] = json5::value(double(activePalette_));
            b.push_array();
            for (const Palette& palette : palettes_) {
                b.push_object();
                b["name"] = b.new_string(palette.name);
                b.push_array();
                for (const Swatch& swatch : palette.swatches) {
                    b.push_object();
                    b.push_array();
                    b += real(swatch.r);
                    b += real(swatch.g);
                    b += real(swatch.b);
                    b += real(swatch.a);
                    b["color"] = b.pop();
                    if (!swatch.name.empty()) {
                        b["name"] = b.new_string(swatch.name);
                    }
                    if (swatch.link != 0) {
                        b["link"] = json5::value(double(swatch.link));
                    }
                    b += b.pop();
                }
                b["swatches"] = b.pop();
                b += b.pop();
            }
            b["list"] = b.pop();
            b["palettes"] = b.pop();
            b["document"] = b.pop();
            if (viewState_.canvas || viewState_.graph) {
                ViewFile::write(b, viewState_);
            }
            step(taskProgress_, 0.05f);

            b["graph"] = jitnodes::saveNetworkValue(b, network(), true, true);
            b.pop();
            step(taskProgress_, 0.5f);

            json5::writer_params params;
            std::ostringstream os;
            os << std::setprecision(9);
            json5::to_stream(os, out, params);
            step(taskProgress_, 0.85f);

            if (!writeAtomically(path, os.str())) {
                lastError_ = "the file could not be written";
                return false;
            }
            step(taskProgress_, 1.0f);
            return true;
        } catch (const TaskCancelled&) {
            return false;   // nothing was written: the old file stays as it was
        } catch (const std::exception& problem) {
            lastError_ = problem.what();
            return false;
        }
    }

    bool SketchDocument::readFromFile(const std::string& path) {
        LockGuard lock(*this);
        lastError_.clear();
        try {
            step(taskProgress_, 0.02f);
            std::string text;
            {
                std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
                if (!in) {
                    fail("the file could not be opened");
                }
                std::ostringstream buffer;
                buffer << in.rdbuf();
                text = buffer.str();
            }
            step(taskProgress_, 0.2f);

            json5::document parsed;
            if (const json5::error problem = json5::from_string(text, parsed)) {
                fail(std::string("parse error at line ") + std::to_string(problem.line) + ", column " + std::to_string(problem.column) + ": " +
                     json5::error::type_string[problem.type]);
            }
            const json5::value root = parsed;
            step(taskProgress_, 0.4f);
            if (!root.is_object() || stringOr(root, "format", "", "file") != "curvy") {
                fail("not a curvy file (format is not \"curvy\")");
            }
            const int version = int(number(need(root, "version", "file"), "version"));
            if (version != kFileVersion) {
                fail("unsupported version " + std::to_string(version) + " (this build reads version " + std::to_string(kFileVersion) + ")");
            }

            const json5::value info = need(root, "document", "file");
            const std::string name = stringOr(info, "name", "Untitled", "document");
            const json5::value page = need(info, "page", "document");
            const float width = float(numberOr(page, "width", 612.0, "page"));
            const float height = float(numberOr(page, "height", 792.0, "page"));
            const float ppi = float(numberOr(page, "ppi", 72.0, "page"));
            if (!(width > 0.0f) || !(height > 0.0f) || !(ppi > 0.0f)) {
                fail("page: the size and ppi must be positive");
            }
            const std::string mode = stringOr(page, "colorMode", "rgb", "page");
            if (mode != "rgb" && mode != "cmyk") {
                fail("page: colorMode must be \"rgb\" or \"cmyk\"");
            }
            newui::Color background(1.0f, 1.0f, 1.0f, 1.0f);
            json5::value part;
            if (find(page, "background", part)) {
                background = colorOf(part, "page.background");
            }
            // The palettes are optional (files from before swatches have none and get the default palette).
            std::vector<Palette> palettes = {DefaultPalette::make()};
            std::size_t active = 0;
            json5::value palettePart;
            if (find(info, "palettes", palettePart)) {
                const json5::value listPart = need(palettePart, "list", "palettes");
                const json5::array_view list(listPart);
                if (!list.is_valid() || list.size() == 0) {
                    fail("palettes.list must be a list with at least one palette");
                }
                palettes.clear();
                for (std::size_t p = 0; p < list.size(); ++p) {
                    const std::string where = "palettes.list[" + std::to_string(p) + "]";
                    Palette palette;
                    palette.name = stringOr(list[p], "name", "", where);
                    if (palette.name.empty()) {
                        fail(where + " needs a name");
                    }
                    if (std::any_of(palettes.begin(), palettes.end(), [&](const Palette& other) { return other.name == palette.name; })) {
                        fail(where + ": the palette name \"" + palette.name + "\" is used twice");
                    }
                    json5::value swatchesPart;
                    if (find(list[p], "swatches", swatchesPart)) {
                        const json5::array_view swatches(swatchesPart);
                        if (!swatches.is_valid()) {
                            fail(where + ".swatches must be a list");
                        }
                        for (std::size_t s = 0; s < swatches.size(); ++s) {
                            const std::string swatchWhere = where + ".swatches[" + std::to_string(s) + "]";
                            const newui::Color color = colorOf(need(swatches[s], "color", swatchWhere), swatchWhere + ".color");
                            const double link = numberOr(swatches[s], "link", 0.0, swatchWhere);
                            palette.swatches.push_back(Swatch{color.r, color.g, color.b, color.a, stringOr(swatches[s], "name", "", swatchWhere),
                                                              link >= 1.0 && link < 1e9 ? std::uint32_t(link) : 0u});
                        }
                    }
                    palettes.push_back(std::move(palette));
                }
                active = std::size_t((std::max)(0.0, numberOr(palettePart, "active", 0.0, "palettes")));
                if (active >= palettes.size()) {
                    active = 0;
                }
            }
            step(taskProgress_, 0.5f);

            // The graph is checked on a scratch network first, so a bad one refuses the file before this document is touched.
            const json5::value graph = need(root, "graph", "file");
            try {
                jitnodes::Network scratch(1);
                jitnodes::loadNetworkValue(scratch, graph);
            } catch (const std::exception& problem) {
                fail(std::string("graph: ") + problem.what());
            }
            step(taskProgress_, 0.9f);   // the last point a cancel is honoured: from here the document changes

            install(name, width, height, ppi, mode == "cmyk" ? ColorSpace::CMYK : ColorSpace::RGB, background);
            palettes_ = std::move(palettes);
            activePalette_ = active;
            viewState_ = ViewFile::read(root);
            jitnodes::loadNetworkValue(network(), graph);
            std::vector<jitnodes::NodeId> path;
            findObjectProblems(network(), path);
            graphReplaced();
            if (taskProgress_ != nullptr) {
                taskProgress_->set(1.0f);
            }
            return true;
        } catch (const TaskCancelled&) {
            return false;
        } catch (const std::exception& problem) {
            lastError_ = problem.what();
            return false;
        }
    }

}
