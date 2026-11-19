#include "curvy/scriptmodule.h"

#include "curvy/blendmodes.h"
#include "curvy/objectnode.h"
#include "curvy/selectionmodel.h"
#include "curvy/shapepaths.h"
#include "curvy/shapeproperties.h"
#include "curvy/sketchdocument.h"
#include "curvy/wranglecheck.h"

#include <pyGuard.h>
#include <pyHandles.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace py = pybind11;

namespace curvy {

    namespace {

        using Rgba = std::tuple<float, float, float, float>;

        void notifyFileHook(bool contentReplaced);

        std::string fileFailure(const std::string& what, const std::string& path, const SketchDocument& document) {
            return "could not " + what + " " + path + (document.lastError().empty() ? std::string() : ": " + document.lastError());
        }

        // Held by every script object that reaches into the document: a document that closed turns into an error, not a crash.
        class DocumentGuard {
        public:
            DocumentGuard(SketchDocument& document, ScriptTransaction& transaction)
                : document_(&document), alive_(document.lifetime()), transaction_(&transaction) {}

            SketchDocument& get() const {
                if (alive_.expired()) {
                    throw std::runtime_error("the document is closed");
                }
                return *document_;
            }

            ScriptTransaction& transaction() const { return *transaction_; }

        private:
            SketchDocument* document_;
            std::weak_ptr<void> alive_;
            ScriptTransaction* transaction_;
        };

        // Around a call that moves or clears the undo history: what the script did so far is closed as a step first and a new one starts
        // after, so the depth the transaction noted stays meaningful.
        class HistoryChange {
        public:
            explicit HistoryChange(const DocumentGuard& guard) : document_(guard.get()), transaction_(guard.transaction()) {
                covered_ = transaction_.covers(document_);
                if (covered_) {
                    transaction_.end();
                }
            }

            ~HistoryChange() {
                if (covered_) {
                    transaction_.begin(document_);
                }
            }

            HistoryChange(const HistoryChange&) = delete;
            HistoryChange& operator=(const HistoryChange&) = delete;

        private:
            SketchDocument& document_;
            ScriptTransaction& transaction_;
            bool covered_ = false;
        };

        class ScriptDocument {
        public:
            ScriptDocument(SketchDocument& document, ScriptTransaction& transaction, SelectionModel* selection)
                : guard_(std::make_shared<DocumentGuard>(document, transaction)), selection_(selection) {}

            SketchDocument& get() const { return guard_->get(); }
            const std::shared_ptr<DocumentGuard>& guard() const { return guard_; }
            SelectionModel* selection() const { return selection_; }

        private:
            std::shared_ptr<DocumentGuard> guard_;
            SelectionModel* selection_;
        };

        NodeRef refOf(SketchDocument& document, const py::object& value);
        py::object wrapNode(SketchDocument& document, const NodeRef& ref);

        // `doc.assets`: the asset operations that rewrite nodes, as undo steps of the document (the functions of jitnodes.assets act on a bare
        // network and would bypass the document's undo stack and shape sync).
        class ScriptAssets {
        public:
            explicit ScriptAssets(std::shared_ptr<DocumentGuard> guard) : guard_(std::move(guard)) {}

            std::size_t updateInstances(const std::optional<std::string>& name) const {
                jitnodes::python::EventRestriction::checkLibrary("update_instances");
                SketchDocument& doc = guard_->get();
                const jitnodes::GraphSnapshot before = doc.snapshotGraph();
                const std::size_t count = jitnodes::updateAssetInstances(doc.network(), name ? *name : std::string());
                if (count > 0) {
                    doc.commitGraphEdit(before, "Update assets");
                }
                return count;
            }

            py::object unlock(const py::object& node) const {
                SketchDocument& doc = guard_->get();
                const NodeRef ref = refOf(doc, node);
                jitnodes::python::EventRestriction::checkNode(node.cast<const jitnodes::python::PyNode&>().network(), ref.id, "unlock");
                jitnodes::Network& network = doc.networkOf(ref.scope);
                if (!doc.hasNode(ref) || !network.node(ref.id).isAssetInstance()) {
                    throw std::runtime_error("the node is not an asset instance");
                }
                const jitnodes::GraphSnapshot before = doc.snapshotGraph();
                jitnodes::unlockAssetInstance(network, ref.id);
                doc.commitGraphEdit(before, "Unlock asset");
                return wrapNode(doc, ref);
            }

        private:
            std::shared_ptr<DocumentGuard> guard_;
        };

        // A layer or a group.
        class ScriptObject {
        public:
            ScriptObject(std::shared_ptr<DocumentGuard> guard, NodeRef ref)
                : guard_(std::move(guard)), ref_(ref), replaceCount_(guard_->get().replaceCount()) {}

            // Ids start again when the whole document is replaced (New, open), so an object from before may share its id with an
            // unrelated one now: it counts as gone.
            bool stale() const { return guard_->get().replaceCount() != replaceCount_; }

            SketchDocument& document() const {
                if (stale()) {
                    throw std::runtime_error("the object no longer exists");
                }
                return guard_->get();
            }
            const std::shared_ptr<DocumentGuard>& guard() const { return guard_; }
            const NodeRef& ref() const { return ref_; }
            const NodeRef& liveRef() const {
                (void)document();
                return ref_;
            }

            // The object may have been deleted or replaced (undo, move) since the script got it.
            jitnodes::Node& node() const {
                SketchDocument& doc = document();
                if (!doc.hasNode(ref_)) {
                    throw std::runtime_error("the object no longer exists");
                }
                return doc.node(ref_);
            }

        private:
            std::shared_ptr<DocumentGuard> guard_;
            NodeRef ref_;
            std::uint64_t replaceCount_;
        };

        py::object wrapNode(SketchDocument& document, const NodeRef& ref) {
            return py::cast(jitnodes::python::PyNode(jitnodes::python::NetworkRef::borrowed(document.networkOf(ref.scope)), ref.id));
        }

        py::object wrapObject(const std::shared_ptr<DocumentGuard>& guard, const NodeRef& ref) {
            return py::cast(ScriptObject(guard, ref));
        }

        py::list wrapObjects(const std::shared_ptr<DocumentGuard>& guard, const std::vector<NodeRef>& refs) {
            py::list list;
            for (const NodeRef& ref : refs) {
                list.append(wrapObject(guard, ref));
            }
            return list;
        }

        // A script names nodes either as a graph node handle or as a layer or group.
        NodeRef refOf(SketchDocument& document, const py::object& value) {
            if (py::isinstance<ScriptObject>(value)) {
                return value.cast<const ScriptObject&>().liveRef();
            }
            const auto& node = value.cast<const jitnodes::python::PyNode&>();
            const jitnodes::Network* network = node.network().raw();
            for (std::uint32_t scope = 0; scope < document.scopeCount(); ++scope) {
                if (document.findNetwork(scope) == network) {
                    return NodeRef(scope, node.id());
                }
            }
            throw std::invalid_argument("the node is not in this document's graph");
        }

        std::vector<NodeRef> refsOf(SketchDocument& document, const py::sequence& values) {
            std::vector<NodeRef> refs;
            for (const py::handle item : values) {
                refs.push_back(refOf(document, py::reinterpret_borrow<py::object>(item)));
            }
            return refs;
        }

        std::uint32_t scopeFor(SketchDocument& document, const py::object& parent) {
            if (parent.is_none()) {
                return 0;
            }
            return document.scopeOfSubnet(refOf(document, parent));
        }

        newui::Color colorOf(const Rgba& value) {
            return newui::Color(std::get<0>(value), std::get<1>(value), std::get<2>(value), std::get<3>(value));
        }

        Rgba rgbaOf(const newui::Color& color) {
            return Rgba(color.r, color.g, color.b, color.a);
        }

        std::string colorModeName(ColorSpace mode) {
            switch (mode) {
                case ColorSpace::CMYK:
                    return "cmyk";
                case ColorSpace::Gray:
                    return "gray";
                case ColorSpace::HSB:
                    return "hsb";
                default:
                    return "rgb";
            }
        }

        ColorSpace colorModeFrom(const std::string& name) {
            if (name == "rgb") {
                return ColorSpace::RGB;
            }
            if (name == "cmyk") {
                return ColorSpace::CMYK;
            }
            throw std::invalid_argument("color_mode is 'rgb' or 'cmyk'");
        }

        void bindSwatches(py::module_& module) {
            py::class_<Swatch>(module, "Swatch", "A colour (red, green, blue, alpha in 0..1) with an optional name. Changing one changes a copy only; use the document's swatch calls to edit a palette.")
                .def(py::init([](float r, float g, float b, float a, const std::string& name) {
                         Swatch swatch;
                         swatch.r = r;
                         swatch.g = g;
                         swatch.b = b;
                         swatch.a = a;
                         swatch.name = name;
                         return swatch;
                     }),
                     py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a") = 1.0f, py::arg("name") = "")
                .def_readwrite("r", &Swatch::r)
                .def_readwrite("g", &Swatch::g)
                .def_readwrite("b", &Swatch::b)
                .def_readwrite("a", &Swatch::a)
                .def_readwrite("name", &Swatch::name)
                .def_readonly("link", &Swatch::link)
                .def("__repr__", [](const Swatch& s) {
                    return "Swatch(" + std::to_string(s.r) + ", " + std::to_string(s.g) + ", " + std::to_string(s.b) + ", " + std::to_string(s.a) + ", '" + s.name + "')";
                });

            py::class_<Palette>(module, "Palette", "A named list of swatches (a copy: edit through the document).")
                .def_readonly("name", &Palette::name)
                .def_readonly("swatches", &Palette::swatches)
                .def("__len__", [](const Palette& p) { return p.swatches.size(); })
                .def("__repr__", [](const Palette& p) { return "Palette('" + p.name + "', " + std::to_string(p.swatches.size()) + " swatches)"; });
        }

        py::object wrapAny(const std::shared_ptr<DocumentGuard>& guard, const NodeRef& ref) {
            SketchDocument& doc = guard->get();
            if (dynamic_cast<ObjectNode*>(&doc.node(ref)) != nullptr) {
                return wrapObject(guard, ref);
            }
            return wrapNode(doc, ref);
        }

        py::object wrapOptional(const std::shared_ptr<DocumentGuard>& guard, const NodeRef& ref) {
            if (!ref.valid() || !guard->get().hasNode(ref)) {
                return py::none();
            }
            return wrapAny(guard, ref);
        }

        py::list nodesOfShapeKeys(const ScriptDocument& d, const std::vector<std::string>& keys) {
            py::list list;
            std::vector<NodeRef> seen;
            for (const std::string& key : keys) {
                const NodeRef ref = d.get().nodeForShapeKey(key);
                if (ref.valid() && std::find(seen.begin(), seen.end(), ref) == seen.end()) {
                    seen.push_back(ref);
                    list.append(wrapAny(d.guard(), ref));
                }
            }
            return list;
        }

        std::string criterionText(const py::handle& value, bool colour) {
            if (value.is_none()) {
                return kNoValue;
            }
            std::string text = py::str(value);
            if (colour) {
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            }
            return text;
        }

        std::vector<std::string> keysWhere(const ScriptDocument& d, const py::kwargs& criteria) {
            const jitnodes::GeometryHandle geometry = d.get().artwork();
            if (geometry == nullptr) {
                return {};
            }
            std::vector<ShapeValues> shapes = describeShapes(d.get(), *geometry);
            for (const auto& item : criteria) {
                const std::string name = py::str(item.first);
                ShapeProperty property = ShapeProperty::Tag;
                std::string wanted;
                if (name == "tag") {
                    wanted = criterionText(item.second, false);
                } else if (name == "fill" || name == "stroke") {
                    property = name == "fill" ? ShapeProperty::Fill : ShapeProperty::Stroke;
                    wanted = criterionText(item.second, true);
                } else if (name == "width") {
                    property = ShapeProperty::Width;
                    if (item.second.is_none()) {
                        wanted = kNoValue;
                    } else {
                        char text[32];
                        std::snprintf(text, sizeof(text), "%g px", std::round(py::cast<double>(item.second) * 2.0) / 2.0);
                        wanted = text;
                    }
                } else if (name == "type") {
                    property = ShapeProperty::Type;
                    wanted = criterionText(item.second, false);
                } else if (name == "layer") {
                    property = ShapeProperty::Layer;
                    wanted = criterionText(item.second, false);
                } else {
                    throw std::runtime_error("unknown criterion '" + name + "' (tag, fill, stroke, width, type, layer)");
                }
                const std::vector<std::string> keep = shapesWithValue(shapes, property, wanted);
                shapes.erase(std::remove_if(shapes.begin(), shapes.end(), [&](const ShapeValues& s) {
                                 return std::find(keep.begin(), keep.end(), s.key) == keep.end();
                             }), shapes.end());
            }
            std::vector<std::string> keys;
            for (const ShapeValues& shape : shapes) {
                keys.push_back(shape.key);
            }
            return keys;
        }

        // What is selected in the editor: shared with the canvas, graph view and panels, so a script's change shows there and a user's
        // pick is what the script reads. Changing it is not an undo step.
        class ScriptSelection {
        public:
            ScriptSelection(std::shared_ptr<DocumentGuard> guard, SelectionModel& model) : guard_(std::move(guard)), model_(&model) {}

            const std::shared_ptr<DocumentGuard>& guard() const { return guard_; }
            SelectionModel& model() const {
                SketchDocument& doc = guard_->get();
                if (model_->document() != &doc) {
                    throw std::runtime_error("the selection no longer belongs to this document");
                }
                return *model_;
            }

        private:
            std::shared_ptr<DocumentGuard> guard_;
            SelectionModel* model_;
        };

        py::list selectionItems(const ScriptSelection& s) {
            py::list list;
            for (const NodeRef& ref : s.model().items()) {
                list.append(wrapAny(s.guard(), ref));
            }
            return list;
        }

        void bindSelection(py::module_& module) {
            using Role = SelectionModel::Role;
            const auto role = [](Role which) {
                return [which](const ScriptSelection& s) { return wrapOptional(s.guard(), s.model().find(which)); };
            };
            py::class_<ScriptSelection>(module, "Selection", "What is selected in the editor (layers, groups and graph nodes). Changing it is not an undo step.")
                .def_property_readonly("items", &selectionItems, "The selected objects and nodes, in selection order.")
                .def_property_readonly("primary", [](const ScriptSelection& s) { return wrapOptional(s.guard(), s.model().primary()); },
                                       "The one the panels show, or None.")
                .def("__len__", [](const ScriptSelection& s) { return s.model().items().size(); })
                .def("__iter__", [](const ScriptSelection& s) { return py::iter(selectionItems(s)); })
                .def("__contains__", [](const ScriptSelection& s, const py::object& node) { return s.model().contains(refOf(s.guard()->get(), node)); })
                .def("set", [](const ScriptSelection& s, const py::object& node) { return s.model().set(refOf(s.guard()->get(), node)); },
                     "Selects just this one.")
                .def("add", [](const ScriptSelection& s, const py::object& node) { return s.model().add(refOf(s.guard()->get(), node)); })
                .def("remove", [](const ScriptSelection& s, const py::object& node) { return s.model().remove(refOf(s.guard()->get(), node)); })
                .def("toggle", [](const ScriptSelection& s, const py::object& node) { return s.model().toggle(refOf(s.guard()->get(), node)); })
                .def("replace", [](const ScriptSelection& s, const py::sequence& nodes) { return s.model().replace(refsOf(s.guard()->get(), nodes)); },
                     "Selects exactly these; the last becomes the primary.")
                .def("clear", [](const ScriptSelection& s) { return s.model().clear(); })
                .def("step_up", [](const ScriptSelection& s) { return s.model().stepUp(); },
                     "Selects the group or layer around the primary; False at the top level.")
                .def_property_readonly("object", role(Role::Object), "The layer or group the primary belongs to, or None.")
                .def_property_readonly("layer", role(Role::Layer))
                .def_property_readonly("fill", role(Role::Fill))
                .def_property_readonly("stroke", role(Role::Stroke))
                .def_property_readonly("shape", role(Role::Shape))
                .def("__repr__", [](const ScriptSelection& s) { return "Selection(" + std::to_string(s.model().items().size()) + " selected)"; });
        }

        NodeRef pathNodeFor(SketchDocument& document, const py::object& node) {
            const NodeRef path = document.pathNodeOf(refOf(document, node));
            if (!path.valid()) {
                throw std::runtime_error("the node has no path (points node) upstream");
            }
            return path;
        }

        std::size_t pointIndex(std::size_t size, long index) {
            const long at = index < 0 ? index + long(size) : index;
            if (at < 0 || at >= long(size)) {
                throw std::out_of_range("point index out of range");
            }
            return std::size_t(at);
        }

        using OptionalRgba = std::optional<Rgba>;

        // Colour given: visible with those channels; None: hidden (the node stays, so a later colour needs no new node).
        std::map<std::string, float> lookValues(const OptionalRgba& color) {
            if (!color) {
                return {{"visible", 0.0f}};
            }
            return {{"visible", 1.0f}, {"r", std::get<0>(*color)}, {"g", std::get<1>(*color)}, {"b", std::get<2>(*color)}, {"alpha", std::get<3>(*color)}};
        }

        py::object lookColor(SketchDocument& document, const py::object& node, SketchDocument::LookKind kind) {
            const auto look = document.shapeLook(refOf(document, node), kind);
            if (!look || look->at("visible") == 0.0f) {
                return py::none();
            }
            return py::cast(Rgba(look->at("r"), look->at("g"), look->at("b"), look->at("alpha")));
        }
        using PointList = std::vector<std::pair<float, float>>;

        std::vector<SketchDocument::PathPoint> pathPointsOf(const PointList& points) {
            std::vector<SketchDocument::PathPoint> result;
            result.reserve(points.size());
            for (const auto& point : points) {
                SketchDocument::PathPoint p;
                p.x = point.first;
                p.y = point.second;
                result.push_back(p);
            }
            return result;
        }

        PointList pointListOf(const std::vector<SketchDocument::PathPoint>& points) {
            PointList result;
            result.reserve(points.size());
            for (const SketchDocument::PathPoint& p : points) {
                result.emplace_back(p.x, p.y);
            }
            return result;
        }

        SketchDocument::PathLook lookOf(const OptionalRgba& fill, const OptionalRgba& stroke, float strokeWidth) {
            SketchDocument::PathLook look;
            look.hasFill = fill.has_value();
            if (fill) {
                look.fill = colorOf(*fill);
            }
            look.hasStroke = stroke.has_value();
            if (stroke) {
                look.stroke = colorOf(*stroke);
            }
            look.strokeWidth = strokeWidth;
            return look;
        }

        // Closed or open run of cubic segments, built the way the points are stored: anchor, out handle, in handle, anchor ...
        class CurveBuilder {
        public:
            explicit CurveBuilder(float x, float y) { points_.push_back({x, y}); }

            void line(float x, float y) {
                const SketchDocument::PathPoint from = points_.back();
                points_.push_back({from.x + (x - from.x) / 3.0f, from.y + (y - from.y) / 3.0f});
                points_.push_back({from.x + (x - from.x) * 2.0f / 3.0f, from.y + (y - from.y) * 2.0f / 3.0f});
                points_.push_back({x, y});
            }

            void cubic(float c1x, float c1y, float c2x, float c2y, float x, float y) {
                points_.push_back({c1x, c1y});
                points_.push_back({c2x, c2y});
                points_.push_back({x, y});
            }

            // The path comes back to where it started: the last anchor is the first one's twin.
            std::vector<SketchDocument::PathPoint> closed() {
                points_.pop_back();
                return points_;
            }

        private:
            std::vector<SketchDocument::PathPoint> points_;
        };

        constexpr float kCircleHandle = 0.5522847498f;

        std::vector<SketchDocument::PathPoint> ellipsePoints(float cx, float cy, float rx, float ry) {
            const float kx = rx * kCircleHandle;
            const float ky = ry * kCircleHandle;
            CurveBuilder curve(cx + rx, cy);
            curve.cubic(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
            curve.cubic(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
            curve.cubic(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
            curve.cubic(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
            return curve.closed();
        }

        std::vector<SketchDocument::PathPoint> roundedRectPoints(float x, float y, float w, float h, float radius) {
            const float r = std::min(radius, std::min(std::abs(w), std::abs(h)) / 2.0f);
            const float k = r * (1.0f - kCircleHandle);
            const float right = x + w;
            const float bottom = y + h;
            CurveBuilder curve(x + r, y);
            curve.line(right - r, y);
            curve.cubic(right - k, y, right, y + k, right, y + r);
            curve.line(right, bottom - r);
            curve.cubic(right, bottom - k, right - k, bottom, right - r, bottom);
            curve.line(x + r, bottom);
            curve.cubic(x + k, bottom, x, bottom - k, x, bottom - r);
            curve.line(x, y + r);
            curve.cubic(x, y + k, x + k, y, x + r, y);
            return curve.closed();
        }

        // `count` vertices round a centre; `inner` > 0 alternates the radius (a star). The first vertex is at `rotation` degrees, which
        // is straight up by default (y grows downward on the page).
        std::vector<SketchDocument::PathPoint> polygonPoints(float cx, float cy, float outer, float inner, int count, float rotation) {
            const int vertices = inner > 0.0f ? count * 2 : count;
            std::vector<SketchDocument::PathPoint> points;
            for (int i = 0; i < vertices; ++i) {
                const float radius = (inner > 0.0f && i % 2 == 1) ? inner : outer;
                const float angle = (rotation + 360.0f * float(i) / float(vertices)) * 3.14159265358979f / 180.0f;
                points.push_back({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
            }
            return points;
        }

        void bindObject(py::module_& module) {
            py::class_<ScriptObject>(module, "Object", "A layer or a group. Edits are undo steps of the document.")
                .def_property("name", [](const ScriptObject& o) { return o.node().name(); },
                              [](const ScriptObject& o, const std::string& name) { o.document().renameGraphNode(o.ref(), name); })
                .def_property_readonly("id", [](const ScriptObject& o) { return o.ref().id; })
                .def_property("opacity", [](const ScriptObject& o) { return o.node().getFloat("opacity"); },
                              [](const ScriptObject& o, float opacity) { o.document().setGraphParam(o.ref(), "opacity", std::clamp(opacity, 0.0f, 1.0f)); },
                              "0 to 1; the object's shapes fade together.")
                .def_property("blend", [](const ScriptObject& o) {
                                  const auto& names = BlendModes::names();
                                  const long index = std::lround(o.node().getFloat("blend"));
                                  return names[std::size_t(std::clamp<long>(index, 0, long(names.size()) - 1))];
                              },
                              [](const ScriptObject& o, const std::string& name) {
                                  const auto& names = BlendModes::names();
                                  const auto found = std::find_if(names.begin(), names.end(), [&](const std::string& candidate) {
                                      return candidate.size() == name.size() &&
                                             std::equal(candidate.begin(), candidate.end(), name.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); });
                                  });
                                  if (found == names.end()) {
                                      throw py::value_error("unknown blend mode '" + name + "'; see curvy.blend_modes()");
                                  }
                                  o.document().setGraphParam(o.ref(), "blend", float(found - names.begin()));
                              },
                              "The blend mode's name, one of curvy.blend_modes() (case does not matter).")
                .def_property_readonly("is_layer", [](const ScriptObject& o) {
                    const auto& tags = o.node().tags();
                    return std::find(tags.begin(), tags.end(), std::string(layerTag)) != tags.end();
                })
                .def_property_readonly("network", [](const ScriptObject& o) {
                    SketchDocument& doc = o.document();
                    (void)o.node();
                    return jitnodes::python::wrapNetwork(doc.networkOf(doc.scopeOfSubnet(o.ref())));
                }, "The graph inside the object, as a jitnodes network.")
                .def_property_readonly("groups", [](const ScriptObject& o) {
                    return wrapObjects(o.guard(), o.document().childrenOf(o.ref()));
                }, "The groups directly inside, bottom first.")
                .def("add_group", [](const ScriptObject& o, const std::string& name) {
                         return wrapObject(o.guard(), o.document().addGroup(o.ref(), name));
                     }, py::arg("name") = "")
                .def("add_circle", [](const ScriptObject& o, float cx, float cy, float radius, const Rgba& fill) {
                         return wrapNode(o.document(), o.document().addCircle(o.ref(), cx, cy, radius, colorOf(fill)));
                     }, py::arg("cx"), py::arg("cy"), py::arg("radius"), py::arg("fill") = Rgba(0.0f, 0.0f, 0.0f, 1.0f),
                     "Adds a filled circle on top and returns its fill node.")
                .def("add_path", [](const ScriptObject& o, const PointList& points, bool closed, bool curve, const OptionalRgba& fill,
                                    const OptionalRgba& stroke, float strokeWidth, const std::string& name) {
                         return wrapNode(o.document(), o.document().addPath(o.ref(), pathPointsOf(points), closed, curve, lookOf(fill, stroke, strokeWidth), name));
                     }, py::arg("points"), py::arg("closed") = false, py::arg("curve") = false, py::arg("fill") = py::none(),
                     py::arg("stroke") = OptionalRgba(Rgba(0.0f, 0.0f, 0.0f, 1.0f)), py::arg("stroke_width") = 1.0f, py::arg("name") = "",
                     "Adds a path through (x, y) points on top and returns the last node of its chain (stroke, else fill, else the points node). "
                     "With curve=True the points are Bezier control points in SVG order: anchor, out handle, in handle, anchor ... "
                     "(3n+1 points open, 3n closed). fill and stroke are (r, g, b, a) or None.")
                .def("add_line", [](const ScriptObject& o, float x1, float y1, float x2, float y2, const OptionalRgba& stroke, float strokeWidth) {
                         return wrapNode(o.document(), o.document().addPath(o.ref(), pathPointsOf({{x1, y1}, {x2, y2}}), false, false, lookOf(std::nullopt, stroke, strokeWidth)));
                     }, py::arg("x1"), py::arg("y1"), py::arg("x2"), py::arg("y2"), py::arg("stroke") = OptionalRgba(Rgba(0.0f, 0.0f, 0.0f, 1.0f)),
                     py::arg("stroke_width") = 1.0f)
                .def("add_rect", [](const ScriptObject& o, float x, float y, float width, float height, float radius, const OptionalRgba& fill,
                                    const OptionalRgba& stroke, float strokeWidth) {
                         const SketchDocument::PathLook look = lookOf(fill, stroke, strokeWidth);
                         if (radius > 0.0f) {
                             return wrapNode(o.document(), o.document().addPath(o.ref(), roundedRectPoints(x, y, width, height, radius), true, true, look));
                         }
                         return wrapNode(o.document(), o.document().addPath(o.ref(), pathPointsOf({{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}), true, false, look));
                     }, py::arg("x"), py::arg("y"), py::arg("width"), py::arg("height"), py::arg("radius") = 0.0f,
                     py::arg("fill") = OptionalRgba(Rgba(0.0f, 0.0f, 0.0f, 1.0f)), py::arg("stroke") = py::none(), py::arg("stroke_width") = 1.0f,
                     "Adds a rectangle (rounded corners when radius > 0, as curves).")
                .def("add_ellipse", [](const ScriptObject& o, float cx, float cy, float rx, float ry, const OptionalRgba& fill, const OptionalRgba& stroke,
                                       float strokeWidth) {
                         return wrapNode(o.document(), o.document().addPath(o.ref(), ellipsePoints(cx, cy, rx, ry), true, true, lookOf(fill, stroke, strokeWidth)));
                     }, py::arg("cx"), py::arg("cy"), py::arg("rx"), py::arg("ry"), py::arg("fill") = OptionalRgba(Rgba(0.0f, 0.0f, 0.0f, 1.0f)),
                     py::arg("stroke") = py::none(), py::arg("stroke_width") = 1.0f, "Adds an ellipse made of four Bezier segments.")
                .def("add_polygon", [](const ScriptObject& o, float cx, float cy, float radius, int sides, float rotation, const OptionalRgba& fill,
                                       const OptionalRgba& stroke, float strokeWidth) {
                         if (sides < 3) {
                             throw std::invalid_argument("a polygon has at least 3 sides");
                         }
                         return wrapNode(o.document(), o.document().addPath(o.ref(), polygonPoints(cx, cy, radius, 0.0f, sides, rotation), true, false, lookOf(fill, stroke, strokeWidth)));
                     }, py::arg("cx"), py::arg("cy"), py::arg("radius"), py::arg("sides"), py::arg("rotation") = -90.0f,
                     py::arg("fill") = OptionalRgba(Rgba(0.0f, 0.0f, 0.0f, 1.0f)), py::arg("stroke") = py::none(), py::arg("stroke_width") = 1.0f,
                     "Adds a regular polygon; rotation is the first vertex's angle in degrees (-90 is straight up).")
                .def("add_star", [](const ScriptObject& o, float cx, float cy, float outer, float inner, int points, float rotation, const OptionalRgba& fill,
                                    const OptionalRgba& stroke, float strokeWidth) {
                         if (points < 2 || inner <= 0.0f) {
                             throw std::invalid_argument("a star needs at least 2 points and an inner radius above 0");
                         }
                         return wrapNode(o.document(), o.document().addPath(o.ref(), polygonPoints(cx, cy, outer, inner, points, rotation), true, false, lookOf(fill, stroke, strokeWidth)));
                     }, py::arg("cx"), py::arg("cy"), py::arg("outer"), py::arg("inner"), py::arg("points") = 5, py::arg("rotation") = -90.0f,
                     py::arg("fill") = OptionalRgba(Rgba(0.0f, 0.0f, 0.0f, 1.0f)), py::arg("stroke") = py::none(), py::arg("stroke_width") = 1.0f)
                .def_property_readonly("transform", [](const ScriptObject& o) { return o.document().objectTransform(o.ref()); },
                                       "The object's transform as a dict: tx ty rot sx sy skew px py (degrees; pivot in the object's space).")
                .def("set_transform", [](const ScriptObject& o, const py::kwargs& values) {
                         std::map<std::string, float> changes;
                         for (const auto& entry : values) {
                             changes[entry.first.cast<std::string>()] = entry.second.cast<float>();
                         }
                         o.document().setObjectTransform(o.ref(), changes);
                     }, "Sets the named transform parameters, e.g. set_transform(tx=10, rot=45); the others keep their values. One undo step.")
                .def("translate", [](const ScriptObject& o, float dx, float dy) {
                         auto t = o.document().objectTransform(o.ref());
                         o.document().setObjectTransform(o.ref(), {{"tx", t["tx"] + dx}, {"ty", t["ty"] + dy}});
                     }, py::arg("dx"), py::arg("dy"), "Moves the whole object by (dx, dy) on top of its current transform.")
                .def("rotate", [](const ScriptObject& o, float degrees, const std::optional<float>& pivotX, const std::optional<float>& pivotY) {
                         auto t = o.document().objectTransform(o.ref());
                         std::map<std::string, float> changes{{"rot", t["rot"] + degrees}};
                         if (pivotX) {
                             changes["px"] = *pivotX;
                         }
                         if (pivotY) {
                             changes["py"] = *pivotY;
                         }
                         o.document().setObjectTransform(o.ref(), changes);
                     }, py::arg("degrees"), py::arg("px") = py::none(), py::arg("py") = py::none(),
                     "Adds to the rotation. px, py move the pivot (object space), which also moves the result if the object already has a transform.")
                .def("scale", [](const ScriptObject& o, float sx, const std::optional<float>& sy) {
                         auto t = o.document().objectTransform(o.ref());
                         o.document().setObjectTransform(o.ref(), {{"sx", t["sx"] * sx}, {"sy", t["sy"] * sy.value_or(sx)}});
                     }, py::arg("sx"), py::arg("sy") = py::none(), "Multiplies the scale (sy defaults to sx).")
                .def("reset_transform", [](const ScriptObject& o) {
                    o.document().setObjectTransform(o.ref(), {{"tx", 0.0f}, {"ty", 0.0f}, {"rot", 0.0f}, {"sx", 1.0f}, {"sy", 1.0f}, {"skew", 0.0f}, {"px", 0.0f}, {"py", 0.0f}});
                })
                .def("reorder_groups", [](const ScriptObject& o, const py::sequence& order) {
                    o.document().reorderChildren(o.ref(), refsOf(o.document(), order));
                })
                .def("move_to", [](const ScriptObject& o, const py::object& parent, std::size_t index) {
                         return wrapObject(o.guard(), o.document().moveGroup(o.ref(), refOf(o.document(), parent), index));
                     }, py::arg("parent"), py::arg("index") = std::size_t(-1),
                     "Moves a group under another layer or group; the object gets a new id, so keep the returned one.")
                .def("group", [](const ScriptObject& o) { return wrapObject(o.guard(), o.document().groupNode(o.ref())); })
                .def("ungroup", [](const ScriptObject& o) { return wrapObjects(o.guard(), o.document().ungroup(o.ref())); })
                .def("remove", [](const ScriptObject& o) { o.document().removeGraphNode(o.ref()); })
                .def("__eq__", [](const ScriptObject& a, const ScriptObject& b) { return a.ref() == b.ref(); })
                .def("__repr__", [](const ScriptObject& o) {
                    SketchDocument& doc = o.guard()->get();
                    return !o.stale() && doc.hasNode(o.ref()) ? "Object('" + doc.node(o.ref()).name() + "')" : std::string("Object(<gone>)");
                });
        }

        // `with doc.transaction(name):` - an all-or-nothing block inside a script. Leaving it normally keeps its edits (as one step of the
        // script's own); leaving it with an exception takes them all back, and the exception goes on.
        class ScriptBlock {
        public:
            ScriptBlock(std::shared_ptr<DocumentGuard> guard, std::string name) : guard_(std::move(guard)), name_(std::move(name)) {}

            void enter() {
                if (entered_) {
                    throw std::runtime_error("this transaction block is already open");
                }
                mark_ = guard_->get().undoStack().depth();
                entered_ = true;
            }

            void leave(bool failed) {
                if (!entered_) {
                    return;
                }
                entered_ = false;
                // A closed document has nothing left to undo or merge.
                SketchDocument& document = guard_->get();
                if (failed) {
                    document.undoStack().rollbackTo(mark_);
                } else {
                    document.undoStack().mergeSince(mark_, name_);
                }
            }

        private:
            std::shared_ptr<DocumentGuard> guard_;
            std::string name_;
            std::size_t mark_ = 0;
            bool entered_ = false;
        };

        void bindDocument(py::module_& module) {
            py::class_<ScriptBlock>(module, "Transaction", "Returned by Document.transaction(); use it in a with statement.")
                .def("__enter__", [](py::object self) {
                    self.cast<ScriptBlock&>().enter();
                    return self;
                })
                .def("__exit__", [](ScriptBlock& block, const py::object& type, const py::object&, const py::object&) {
                    try {
                        block.leave(!type.is_none());
                    } catch (const std::runtime_error&) {
                        // The document closed inside the block: the block's own error (if any) matters more than this one.
                        if (type.is_none()) {
                            throw;
                        }
                    }
                    return false;
                });

            py::class_<ScriptAssets>(module, "Assets", "doc.assets: update_instances and unlock as undo steps of the document.")
                .def("update_instances", &ScriptAssets::updateInstances, py::arg("name") = py::none(),
                     "Rebuilds the instances built from an older version of their asset (all assets, or one); returns how many. One undo step.")
                .def("unlock", &ScriptAssets::unlock, py::arg("node"),
                     "Turns an asset instance back into an ordinary subnet with a copy of the contents; returns the node. One undo step.");

            py::class_<ScriptDocument>(module, "Document","The open document. A whole script run is one undo step.")
                .def("transaction", [](const ScriptDocument& d, const std::string& name) { return ScriptBlock(d.guard(), name); },
                     py::arg("name") = "Script step",
                     "Use as `with doc.transaction('name'):`. If the block raises, everything it changed is undone and the error continues.")
                .def_property("name", [](const ScriptDocument& d) { return d.get().name(); },
                              [](const ScriptDocument& d, const std::string& name) { d.get().setName(name); })
                .def_property_readonly("page_width", [](const ScriptDocument& d) { return d.get().pageWidth(); })
                .def_property_readonly("page_height", [](const ScriptDocument& d) { return d.get().pageHeight(); })
                .def_property("ppi", [](const ScriptDocument& d) { return d.get().ppi(); },
                              [](const ScriptDocument& d, float ppi) { d.get().setPpi(ppi); })
                .def_property("page_color", [](const ScriptDocument& d) { return rgbaOf(d.get().pageColor()); },
                              [](const ScriptDocument& d, const Rgba& color) { d.get().setPageColor(colorOf(color)); },
                              "(r, g, b, a) in 0..1; alpha 0 is a transparent page.")
                .def_property("color_mode", [](const ScriptDocument& d) { return colorModeName(d.get().colorMode()); },
                              [](const ScriptDocument& d, const std::string& mode) { d.get().setColorMode(colorModeFrom(mode)); })
                .def("set_page_size", [](const ScriptDocument& d, float width, float height) { d.get().setPageSize(width, height); })
                .def("new_blank", [](const ScriptDocument& d, const std::string& name, float width, float height, float ppi) {
                         HistoryChange change(*d.guard());
                         d.get().newBlank(name, width, height, ppi);
                     }, py::arg("name"), py::arg("width"), py::arg("height"), py::arg("ppi") = 72.0f,
                     "Replaces everything with an empty document. Not undoable; objects and nodes from before are gone.")
                .def_property_readonly("path", [](const ScriptDocument& d) -> py::object {
                    return d.get().hasFilePath() ? py::cast(d.get().filePath()) : py::none();
                }, "The .curvy file the document was opened from or last saved to, or None when it has none yet.")
                .def_property_readonly("is_modified", [](const ScriptDocument& d) { return d.get().isModified(); },
                                       "True when the document changed since it was opened or saved (this script's own edits count).")
                .def("save", [](const ScriptDocument& d, const std::optional<std::string>& path) {
                         SketchDocument& doc = d.get();
                         const std::string target = path ? *path : doc.filePath();
                         if (target.empty()) {
                             throw std::runtime_error("the document has no file yet: pass a path to save()");
                         }
                         if (!doc.save(target)) {
                             throw std::runtime_error(fileFailure("save", target, doc));
                         }
                         notifyFileHook(false);
                         return target;
                     }, py::arg("path") = py::none(),
                     "Saves the document as a .curvy file and returns the path. Without a path it saves to doc.path; with one it is Save As, and the "
                     "document belongs to that file from then on. Not an undo step. Raises RuntimeError when it cannot write.")
                .def("open", [](const ScriptDocument& d, const std::string& path, bool discardChanges) {
                         SketchDocument& doc = d.get();
                         if (doc.isModified() && !discardChanges) {
                             throw std::runtime_error("the document has unsaved changes: save it first, or pass discard_changes=True");
                         }
                         {
                             HistoryChange change(*d.guard());
                             if (!doc.load(path)) {
                                 throw std::runtime_error(fileFailure("open", path, doc));
                             }
                         }
                         notifyFileHook(true);
                     }, py::arg("path"), py::arg("discard_changes") = false,
                     "Replaces the document with a .curvy file. Refuses while the document has unsaved changes unless discard_changes=True. Not "
                     "undoable; objects and nodes from before are gone, and a file that cannot be read leaves the document as it was.")

                .def_property_readonly("layers", [](const ScriptDocument& d) { return wrapObjects(d.guard(), d.get().childrenOf()); },
                                       "The layers, bottom first.")
                .def("add_layer", [](const ScriptDocument& d, const std::string& name) { return wrapObject(d.guard(), d.get().addLayer(name)); },
                     py::arg("name") = "")
                .def("reorder_layers", [](const ScriptDocument& d, const py::sequence& order) { d.get().reorderLayers(refsOf(d.get(), order)); })

                .def("add_node", [](const ScriptDocument& d, const std::string& type, const std::string& name, const py::object& parent) {
                         SketchDocument& doc = d.get();
                         return wrapNode(doc, doc.addGraphNode(type, name, scopeFor(doc, parent)));
                     }, py::arg("type"), py::arg("name") = "", py::arg("parent") = py::none(),
                     "Adds a node to the document's graph, or inside `parent` (a layer or group).")
                .def_property_readonly("assets", [](const ScriptDocument& d) { return ScriptAssets(d.guard()); },
                                       "Asset operations that rewrite nodes (update_instances, unlock) as undo steps of the document.")
                .def("remove_node", [](const ScriptDocument& d, const py::object& node) { d.get().removeGraphNode(refOf(d.get(), node)); })
                .def("connect", [](const ScriptDocument& d, const py::object& from, const py::object& to, int input) {
                         d.get().connectGraphNodes(refOf(d.get(), from), refOf(d.get(), to), input);
                     }, py::arg("from_node"), py::arg("to_node"), py::arg("input") = 0)
                .def("disconnect", [](const ScriptDocument& d, const py::object& to, int input) { d.get().disconnectGraphNode(refOf(d.get(), to), input); })
                .def("set_param", [](const ScriptDocument& d, const py::object& node, const std::string& name, float value) {
                    d.get().setGraphParam(refOf(d.get(), node), name, value);
                })
                .def("set_string", [](const ScriptDocument& d, const py::object& node, const std::string& name, const std::string& value) {
                    d.get().setGraphString(refOf(d.get(), node), name, value);
                })
                .def("rename_node", [](const ScriptDocument& d, const py::object& node, const std::string& name) {
                    d.get().renameGraphNode(refOf(d.get(), node), name);
                })
                .def("set_bypassed", [](const ScriptDocument& d, const py::object& node, bool on) { d.get().setNodeBypassed(refOf(d.get(), node), on); })
                .def("set_frozen", [](const ScriptDocument& d, const py::object& node, bool on) { d.get().setNodeFrozen(refOf(d.get(), node), on); })
                .def("set_displayed", [](const ScriptDocument& d, const py::object& node) { d.get().setNodeDisplayed(refOf(d.get(), node)); })

                .def_property_readonly("selection", [](const ScriptDocument& d) -> py::object {
                    if (d.selection() == nullptr) {
                        return py::none();
                    }
                    return py::cast(ScriptSelection(d.guard(), *d.selection()));
                }, "What is selected in the editor, or None when this run has no editor (tests).")

                .def("path_node", [](const ScriptDocument& d, const py::object& node) -> py::object {
                         SketchDocument& doc = d.get();
                         const NodeRef found = doc.pathNodeOf(refOf(doc, node));
                         return found.valid() ? wrapNode(doc, found) : py::none();
                     }, py::arg("node"), "The points node ('geometry') that feeds this node, or None.")
                .def("points", [](const ScriptDocument& d, const py::object& node) { return pointListOf(d.get().pathPoints(pathNodeFor(d.get(), node))); },
                     py::arg("node"), "The (x, y) points of a path: every anchor and handle of a curve. `node` is any node of the path's chain.")
                .def("set_points", [](const ScriptDocument& d, const py::object& node, const PointList& points) {
                         d.get().setPathPoints(pathNodeFor(d.get(), node), pathPointsOf(points));
                     }, py::arg("node"), py::arg("points"),
                     "Replaces the points (one undo step). The same count moves them; another count needs a path of one piece, which keeps its kind and closed flag.")
                .def("set_point", [](const ScriptDocument& d, const py::object& node, long index, float x, float y) {
                         SketchDocument& doc = d.get();
                         const NodeRef path = pathNodeFor(doc, node);
                         std::vector<SketchDocument::PathPoint> points = doc.pathPoints(path);
                         points.at(pointIndex(points.size(), index)) = {x, y};
                         doc.setPathPoints(path, points);
                     }, py::arg("node"), py::arg("index"), py::arg("x"), py::arg("y"))
                .def("move_points", [](const ScriptDocument& d, const py::object& node, float dx, float dy, const py::object& indices) {
                         SketchDocument& doc = d.get();
                         const NodeRef path = pathNodeFor(doc, node);
                         std::vector<SketchDocument::PathPoint> points = doc.pathPoints(path);
                         const auto move = [&](SketchDocument::PathPoint& p) {
                             p.x += dx;
                             p.y += dy;
                         };
                         if (indices.is_none()) {
                             for (SketchDocument::PathPoint& p : points) {
                                 move(p);
                             }
                         } else {
                             for (const py::handle item : py::reinterpret_borrow<py::sequence>(indices)) {
                                 move(points.at(pointIndex(points.size(), item.cast<long>())));
                             }
                         }
                         doc.setPathPoints(path, points);
                     }, py::arg("node"), py::arg("dx"), py::arg("dy"), py::arg("indices") = py::none(),
                     "Moves the given point indices (all when None) by (dx, dy) as one undo step.")
                .def("insert_point", [](const ScriptDocument& d, const py::object& node, long index, float x, float y) {
                         SketchDocument& doc = d.get();
                         const NodeRef path = pathNodeFor(doc, node);
                         if (doc.pathCubic(path)) {
                             throw std::runtime_error("insert_point and remove_point work on straight paths; use set_points on a curve");
                         }
                         std::vector<SketchDocument::PathPoint> points = doc.pathPoints(path);
                         const long size = long(points.size());
                         const long at = index < 0 ? index + size : index;
                         if (at < 0 || at > size) {
                             throw std::out_of_range("point index out of range");
                         }
                         points.insert(points.begin() + at, SketchDocument::PathPoint{x, y});
                         doc.setPathPoints(path, points);
                     }, py::arg("node"), py::arg("index"), py::arg("x"), py::arg("y"), "Inserts a point before `index` (len(points) appends).")
                .def("remove_point", [](const ScriptDocument& d, const py::object& node, long index) {
                         SketchDocument& doc = d.get();
                         const NodeRef path = pathNodeFor(doc, node);
                         if (doc.pathCubic(path)) {
                             throw std::runtime_error("insert_point and remove_point work on straight paths; use set_points on a curve");
                         }
                         std::vector<SketchDocument::PathPoint> points = doc.pathPoints(path);
                         points.erase(points.begin() + long(pointIndex(points.size(), index)));
                         doc.setPathPoints(path, points);
                     }, py::arg("node"), py::arg("index"))
                .def("shape_transform", [](const ScriptDocument& d, const py::object& node) {
                         return d.get().shapeTransform(refOf(d.get(), node));
                     }, py::arg("node"),
                     "The transform of one shape as a dict (tx ty rot sx sy skew px py; identity values when it has none). `node` is any node of the shape's chain.")
                .def("set_shape_transform", [](const ScriptDocument& d, const py::object& node, const py::kwargs& values) {
                         std::map<std::string, float> changes;
                         for (const auto& entry : values) {
                             changes[entry.first.cast<std::string>()] = entry.second.cast<float>();
                         }
                         d.get().setShapeTransform(refOf(d.get(), node), changes);
                     }, py::arg("node"),
                     "Sets named transform parameters of one shape, e.g. set_shape_transform(rect, rot=15, tx=10), making its transform node first. One undo step.")
                .def("translate_shape", [](const ScriptDocument& d, const py::object& node, float dx, float dy) {
                         SketchDocument& doc = d.get();
                         const NodeRef ref = refOf(doc, node);
                         auto t = doc.shapeTransform(ref);
                         doc.setShapeTransform(ref, {{"tx", t["tx"] + dx}, {"ty", t["ty"] + dy}});
                     }, py::arg("node"), py::arg("dx"), py::arg("dy"))
                .def("rotate_shape", [](const ScriptDocument& d, const py::object& node, float degrees, const std::optional<float>& pivotX,
                                        const std::optional<float>& pivotY) {
                         SketchDocument& doc = d.get();
                         const NodeRef ref = refOf(doc, node);
                         auto t = doc.shapeTransform(ref);
                         std::map<std::string, float> changes{{"rot", t["rot"] + degrees}};
                         if (pivotX) {
                             changes["px"] = *pivotX;
                         }
                         if (pivotY) {
                             changes["py"] = *pivotY;
                         }
                         doc.setShapeTransform(ref, changes);
                     }, py::arg("node"), py::arg("degrees"), py::arg("px") = py::none(), py::arg("py") = py::none(),
                     "Adds to the rotation; px, py set the pivot in page units (it is the page, not the shape's centre, unless you pass one).")
                .def("scale_shape", [](const ScriptDocument& d, const py::object& node, float sx, const std::optional<float>& sy) {
                         SketchDocument& doc = d.get();
                         const NodeRef ref = refOf(doc, node);
                         auto t = doc.shapeTransform(ref);
                         doc.setShapeTransform(ref, {{"sx", t["sx"] * sx}, {"sy", t["sy"] * sy.value_or(sx)}});
                     }, py::arg("node"), py::arg("sx"), py::arg("sy") = py::none())
                .def("reset_shape_transform", [](const ScriptDocument& d, const py::object& node) {
                         d.get().setShapeTransform(refOf(d.get(), node), {{"tx", 0.0f}, {"ty", 0.0f}, {"rot", 0.0f}, {"sx", 1.0f}, {"sy", 1.0f}, {"skew", 0.0f}, {"px", 0.0f}, {"py", 0.0f}});
                     }, py::arg("node"))
                .def("fill", [](const ScriptDocument& d, const py::object& node) -> py::object {
                         return lookColor(d.get(), node, SketchDocument::LookKind::Fill);
                     }, py::arg("node"), "The shape's fill as (r, g, b, a), or None when it has no visible fill. `node` is any node of the shape's chain.")
                .def("stroke", [](const ScriptDocument& d, const py::object& node) -> py::object {
                         return lookColor(d.get(), node, SketchDocument::LookKind::Stroke);
                     }, py::arg("node"), "The shape's stroke as (r, g, b, a), or None when it has no visible stroke.")
                .def("stroke_width", [](const ScriptDocument& d, const py::object& node) -> py::object {
                         const auto look = d.get().shapeLook(refOf(d.get(), node), SketchDocument::LookKind::Stroke);
                         return look ? py::cast(look->at("width")) : py::none();
                     }, py::arg("node"), "The width of the shape's stroke node, or None when it has none.")
                .def("set_fill", [](const ScriptDocument& d, const py::object& node, const OptionalRgba& color, const std::optional<float>& opacity,
                                    const std::optional<bool>& evenOdd) {
                         std::map<std::string, float> values = lookValues(color);
                         if (opacity) {
                             values["opacity"] = *opacity;
                         }
                         if (evenOdd) {
                             values["evenOdd"] = *evenOdd ? 1.0f : 0.0f;
                         }
                         d.get().setShapeLook(refOf(d.get(), node), SketchDocument::LookKind::Fill, values);
                     }, py::arg("node"), py::arg("color"), py::arg("opacity") = py::none(), py::arg("even_odd") = py::none(),
                     "Sets the fill as (r, g, b, a); None hides it. Makes the fill node when the shape has none. opacity is 0..1; even_odd picks the "
                     "even-odd rule for compound paths. One undo step.")
                .def("set_stroke", [](const ScriptDocument& d, const py::object& node, const OptionalRgba& color, const std::optional<float>& width,
                                      const std::optional<float>& opacity) {
                         std::map<std::string, float> values = lookValues(color);
                         if (width) {
                             values["width"] = *width;
                         }
                         if (opacity) {
                             values["opacity"] = *opacity;
                         }
                         d.get().setShapeLook(refOf(d.get(), node), SketchDocument::LookKind::Stroke, values);
                     }, py::arg("node"), py::arg("color"), py::arg("width") = py::none(), py::arg("opacity") = py::none(),
                     "Sets the stroke as (r, g, b, a); None hides it. Makes the stroke node when the shape has none. One undo step.")
                .def("is_closed", [](const ScriptDocument& d, const py::object& node) { return d.get().pathClosed(pathNodeFor(d.get(), node)); }, py::arg("node"))
                .def("set_closed", [](const ScriptDocument& d, const py::object& node, bool closed) {
                         d.get().setPathClosed(pathNodeFor(d.get(), node), closed);
                     }, py::arg("node"), py::arg("closed") = true)
                .def("is_curve", [](const ScriptDocument& d, const py::object& node) { return d.get().pathCubic(pathNodeFor(d.get(), node)); }, py::arg("node"),
                     "True when the path's points are Bezier control points.")

                .def("shape_at", [](const ScriptDocument& d, float x, float y, float tolerance) -> py::object {
                         SketchDocument& doc = d.get();
                         const jitnodes::GeometryHandle geometry = doc.artwork();
                         if (geometry == nullptr) {
                             return py::none();
                         }
                         const std::optional<ShapeHit> hit = curvy::shapeAt(*geometry, x, y, tolerance);
                         return hit.has_value() ? wrapOptional(d.guard(), doc.nodeForShapeKey(hit->key)) : py::none();
                     }, py::arg("x"), py::arg("y"), py::arg("tolerance") = 2.0f,
                     "The node that made the topmost shape under (x, y) in document units (inside its fill, or within `tolerance` of its stroke), or None.")
                .def("shapes_in_rect", [](const ScriptDocument& d, float x, float y, float width, float height, bool inside) {
                         const jitnodes::GeometryHandle geometry = d.get().artwork();
                         if (geometry == nullptr) {
                             return py::list();
                         }
                         const std::vector<BLPoint> region = rectRegion(x, y, double(x) + width, double(y) + height);
                         return nodesOfShapeKeys(d, shapesInRegion(*geometry, region, inside ? RegionMode::Inside : RegionMode::Touches));
                     }, py::arg("x"), py::arg("y"), py::arg("width"), py::arg("height"), py::arg("inside") = true,
                     "The nodes that made the shapes in a rectangle: wholly inside it, or (inside=False) touching it. Drawing order, each node once.")
                .def("shapes_where", [](const ScriptDocument& d, const py::kwargs& criteria) { return nodesOfShapeKeys(d, keysWhere(d, criteria)); },
                     "The nodes that made the shapes that match every given criterion: tag='name' (any tag of the shape), fill='#RRGGBB' or None "
                     "(not filled), stroke=..., width=2.0 (stroke width, to half a unit) or None, type='circle' (node type), layer='name'.")
                .def("select_where", [](const ScriptDocument& d, const std::string& combine, const py::kwargs& criteria) {
                         if (d.selection() == nullptr) {
                             throw std::runtime_error("this run has no editor selection");
                         }
                         SelectionModel::Combine how = SelectionModel::Combine::Replace;
                         if (combine == "add") {
                             how = SelectionModel::Combine::Add;
                         } else if (combine == "subtract") {
                             how = SelectionModel::Combine::Subtract;
                         } else if (combine != "replace") {
                             throw std::runtime_error("combine must be 'replace', 'add' or 'subtract'");
                         }
                         const std::vector<std::string> keys = keysWhere(d, criteria);
                         d.selection()->selectShapeKeys(keys, how);
                         return nodesOfShapeKeys(d, keys);
                     }, py::arg("combine") = "replace",
                     "Selects the shapes shapes_where(**criteria) finds (combine: 'replace', 'add' or 'subtract'); returns their nodes. Not an undo step.")

                .def("artwork", [](const ScriptDocument& d) -> py::object {
                    jitnodes::GeometryHandle geometry = d.get().artwork();
                    return geometry ? py::cast(geometry) : py::none();
                }, "What the document draws: the cooked layers as a read-only jitnodes Geometry, or None.")

                .def_property_readonly("palettes", [](const ScriptDocument& d) { return d.get().palettes(); }, "Copies of the palettes.")
                .def_property("active_palette", [](const ScriptDocument& d) { return d.get().activePalette(); },
                              [](const ScriptDocument& d, std::size_t index) {
                                  if (index >= d.get().palettes().size()) {
                                      throw std::out_of_range("no such palette");
                                  }
                                  d.get().setActivePalette(index);
                              })
                .def("add_palette", [](const ScriptDocument& d, const std::string& name) { return d.get().addPalette(name); })
                .def("rename_palette", [](const ScriptDocument& d, std::size_t index, const std::string& name) { d.get().renamePalette(index, name); })
                .def("delete_palette", [](const ScriptDocument& d, std::size_t index) { d.get().deletePalette(index); })
                .def("add_swatch", [](const ScriptDocument& d, std::size_t palette, const Swatch& swatch) { return d.get().addSwatch(palette, swatch); })
                .def("replace_swatch", [](const ScriptDocument& d, std::size_t palette, std::size_t index, const Swatch& swatch) {
                    d.get().replaceSwatch(palette, index, swatch);
                })
                .def("delete_swatch", [](const ScriptDocument& d, std::size_t palette, std::size_t index) { d.get().deleteSwatch(palette, index); })

                .def("can_undo", [](const ScriptDocument& d) { return d.get().undoStack().canUndo(); })
                .def("can_redo", [](const ScriptDocument& d) { return d.get().undoStack().canRedo(); })
                .def("undo", [](const ScriptDocument& d) {
                    HistoryChange change(*d.guard());
                    d.get().undoStack().undo();
                })
                .def("redo", [](const ScriptDocument& d) {
                    HistoryChange change(*d.guard());
                    d.get().undoStack().redo();
                });
        }

    }

    namespace {

        struct Dispatch {
            std::mutex mutex;
            MainThreadPoster poster;
            std::thread::id mainThread;
            std::atomic<bool> aborting{false};
            ScriptProgress progress;
        };

        Dispatch& dispatch() {
            static Dispatch instance;
            return instance;
        }

        // One call a script thread hands to the main thread. The pointers belong to the waiting thread, which stays blocked until the
        // call finished or was abandoned before it started, so the main thread never sees them after that.
        struct MainCall {
            std::mutex mutex;
            std::condition_variable finishedCondition;
            bool started = false;
            bool finished = false;
            bool abandoned = false;
            bool failed = false;
            PyObject* function = nullptr;
            PyObject* args = nullptr;
            PyObject* kwargs = nullptr;
            // Only touched with the GIL held.
            py::object result;
            py::object error;
        };

        void runMainCall(MainCall& call) {
            {
                std::lock_guard<std::mutex> lock(call.mutex);
                if (call.abandoned) {
                    return;
                }
                call.started = true;
            }
            {
                py::gil_scoped_acquire gil;
                try {
                    const py::object function = py::reinterpret_borrow<py::object>(call.function);
                    const py::args args = py::reinterpret_borrow<py::args>(call.args);
                    const py::kwargs kwargs = py::reinterpret_borrow<py::kwargs>(call.kwargs);
                    call.result = function(*args, **kwargs);
                } catch (py::error_already_set& e) {
                    call.failed = true;
                    call.error = e.value();
                }
            }
            {
                std::lock_guard<std::mutex> lock(call.mutex);
                call.finished = true;
            }
            call.finishedCondition.notify_all();
        }

        py::object callOnMain(const py::object& function, const py::args& args, const py::kwargs& kwargs) {
            Dispatch& state = dispatch();
            MainThreadPoster poster;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                poster = state.poster;
            }
            if (!poster || std::this_thread::get_id() == state.mainThread) {
                return function(*args, **kwargs);
            }

            auto call = std::make_shared<MainCall>();
            call->function = function.ptr();
            call->args = args.ptr();
            call->kwargs = kwargs.ptr();
            {
                py::gil_scoped_release release;
                poster([call]() { runMainCall(*call); });
                std::unique_lock<std::mutex> lock(call->mutex);
                while (!call->finished) {
                    call->finishedCondition.wait_for(lock, std::chrono::milliseconds(20));
                    if (!call->finished && !call->started && state.aborting.load()) {
                        call->abandoned = true;
                        break;
                    }
                }
            }
            if (call->abandoned) {
                PyErr_SetString(py::module_::import("curvy").attr("Cancelled").ptr(), "the window is closing");
                throw py::error_already_set();
            }
            if (call->failed) {
                py::object error = std::move(call->error);
                PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(error.ptr())), error.ptr());
                throw py::error_already_set();
            }
            return std::move(call->result);
        }

        py::object guarded(const py::object& original) {
            if (original.is_none()) {
                return original;
            }
            return py::cpp_function([original](const py::args& args, const py::kwargs& kwargs) { return callOnMain(original, args, kwargs); });
        }

        // Replaces every method and property of a class with one that runs on the main thread (see setScriptMainThreadPoster). The
        // constructor and the other construction and pickling hooks stay: they touch nothing shared.
        void guardClass(const py::object& cls) {
            py::module_ builtins = py::module_::import("builtins");
            const py::object propertyType = builtins.attr("property");
            const py::list members = builtins.attr("list")(cls.attr("__dict__").attr("items")());
            for (const py::handle item : members) {
                const py::tuple pair = py::reinterpret_borrow<py::tuple>(item);
                const std::string name = pair[0].cast<std::string>();
                const py::object value = py::reinterpret_borrow<py::object>(pair[1]);
                if (name == "__init__" || name == "__new__" || name == "__del__" || name == "__getstate__" || name == "__setstate__") {
                    continue;
                }
                if (py::isinstance(value, propertyType)) {
                    cls.attr(name.c_str()) = propertyType(guarded(value.attr("fget")), guarded(value.attr("fset")), guarded(value.attr("fdel")),
                                                          value.attr("__doc__"));
                } else if (std::string(py::str(py::type::of(value).attr("__name__"))) == "instancemethod") {
                    const std::string doc = value.attr("__doc__").is_none() ? std::string() : value.attr("__doc__").cast<std::string>();
                    cls.attr(name.c_str()) = py::cpp_function(
                        [value](const py::args& args, const py::kwargs& kwargs) { return callOnMain(value, args, kwargs); },
                        py::name(name.c_str()), py::is_method(cls), py::doc(doc.c_str()));
                }
            }
        }

    }

    void setScriptMainThreadPoster(MainThreadPoster poster) {
        Dispatch& state = dispatch();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.poster = std::move(poster);
    }

    bool hasScriptMainThreadPoster() {
        Dispatch& state = dispatch();
        std::lock_guard<std::mutex> lock(state.mutex);
        return static_cast<bool>(state.poster);
    }

    namespace {

        std::mutex& fileHookMutex() {
            static std::mutex instance;
            return instance;
        }

        ScriptDocumentFileHook& fileHook() {
            static ScriptDocumentFileHook instance;
            return instance;
        }

        void notifyFileHook(bool contentReplaced) {
            ScriptDocumentFileHook hook;
            {
                std::lock_guard<std::mutex> lock(fileHookMutex());
                hook = fileHook();
            }
            if (hook) {
                hook(contentReplaced);
            }
        }

    }

    void setScriptDocumentFileHook(ScriptDocumentFileHook hook) {
        std::lock_guard<std::mutex> lock(fileHookMutex());
        fileHook() = std::move(hook);
    }

    void setScriptDispatchAborting(bool aborting) {
        dispatch().aborting = aborting;
    }

    ScriptProgress scriptProgress() {
        Dispatch& state = dispatch();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.progress;
    }

    void resetScriptProgress() {
        Dispatch& state = dispatch();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.progress = ScriptProgress();
    }

    void registerCurvyModule() {
        dispatch().mainThread = std::this_thread::get_id();
        py::module_ module = py::module_::import("types").attr("ModuleType")("curvy");
        py::module_::import("sys").attr("modules")["curvy"] = module;
        // A BaseException, like KeyboardInterrupt, so a script's `except Exception` does not swallow a cancel.
        module.attr("Cancelled") = py::module_::import("builtins").attr("type")(
            "Cancelled", py::make_tuple(py::handle(PyExc_BaseException)), py::dict());
        bindSwatches(module);
        bindSelection(module);
        bindObject(module);
        bindDocument(module);
        module.def("progress", [](float fraction, const std::string& message) {
            Dispatch& state = dispatch();
            std::lock_guard<std::mutex> lock(state.mutex);
            state.progress.fraction = fraction;
            state.progress.message = message;
        }, py::arg("fraction"), py::arg("message") = "",
           "Reports how far the script is (0..1, or negative for unknown) and what it is doing; the console panel shows it while the script runs.");
        module.def("check_wrangle", [](const std::string& code) -> py::object {
            const std::optional<SyntaxProblem> problem = checkWrangle(code);
            if (!problem) {
                return py::none();
            }
            return py::make_tuple(problem->line, problem->column, problem->message);
        }, py::arg("code"),
           "Compiles wrangle code without running it: None when it parses, else (line, column, message), 1-based. What the Node panel's Code editor shows.");
        module.def("blend_modes", []() { return BlendModes::names(); }, "The blend modes an object offers, as names (Normal first).");
        module.def("wrangle_functions", []() {
            std::vector<std::tuple<std::string, int>> functions;
            for (const WrangleFunction& function : wrangleFunctions()) {
                functions.emplace_back(function.name, function.arity);
            }
            return functions;
        }, "The functions the wrangle language has, as (name, number of arguments).");

        for (const char* name : {"Selection", "Object", "Transaction", "Document", "Assets"}) {
            guardClass(module.attr(name));
        }
        py::module_ jitnodes = py::module_::import("jitnodes");
        for (const char* name : {"Network", "Node", "History"}) {
            guardClass(jitnodes.attr(name));
        }
        // The asset library is process-global and not thread safe, so a script on a worker thread reaches it through the main thread too.
        py::module_ assets = jitnodes.attr("assets");
        guardClass(assets.attr("AssetDef"));
        guardClass(assets.attr("_Restricted"));
        for (const char* name : {"list", "has", "get", "install", "install_directory", "reload", "uninstall", "update_instances", "create", "unlock",
                                 "rename_parameter", "check_scripts", "restricted"}) {
            assets.attr(name) = guarded(assets.attr(name));
        }
    }

    void ScriptTransaction::begin(SketchDocument& document) {
        document_ = &document;
        alive_ = document.lifetime();
        mark_ = document.undoStack().depth();
    }

    void ScriptTransaction::end() {
        if (document_ != nullptr && !alive_.expired()) {
            document_->undoStack().mergeSince(mark_, "Script");
        }
        document_ = nullptr;
    }

    bool ScriptTransaction::covers(const SketchDocument& document) const {
        return document_ == &document && !alive_.expired();
    }

    py::object makeScriptDocument(SketchDocument& document, ScriptTransaction& transaction, SelectionModel* selection) {
        return py::cast(ScriptDocument(document, transaction, selection));
    }

}
