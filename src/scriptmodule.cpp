#include "curvy/scriptmodule.h"

#include "curvy/objectnode.h"
#include "curvy/sketchdocument.h"

#include <pyHandles.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace py = pybind11;

namespace curvy {

    namespace {

        using Rgba = std::tuple<float, float, float, float>;

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
            ScriptDocument(SketchDocument& document, ScriptTransaction& transaction)
                : guard_(std::make_shared<DocumentGuard>(document, transaction)) {}

            SketchDocument& get() const { return guard_->get(); }
            const std::shared_ptr<DocumentGuard>& guard() const { return guard_; }

        private:
            std::shared_ptr<DocumentGuard> guard_;
        };

        // A layer or a group.
        class ScriptObject {
        public:
            ScriptObject(std::shared_ptr<DocumentGuard> guard, NodeRef ref) : guard_(std::move(guard)), ref_(ref) {}

            SketchDocument& document() const { return guard_->get(); }
            const std::shared_ptr<DocumentGuard>& guard() const { return guard_; }
            const NodeRef& ref() const { return ref_; }

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
                return value.cast<const ScriptObject&>().ref();
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

        void bindObject(py::module_& module) {
            py::class_<ScriptObject>(module, "Object", "A layer or a group. Edits are undo steps of the document.")
                .def_property("name", [](const ScriptObject& o) { return o.node().name(); },
                              [](const ScriptObject& o, const std::string& name) { o.document().renameGraphNode(o.ref(), name); })
                .def_property_readonly("id", [](const ScriptObject& o) { return o.ref().id; })
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
                    SketchDocument& doc = o.document();
                    return doc.hasNode(o.ref()) ? "Object('" + doc.node(o.ref()).name() + "')" : std::string("Object(<gone>)");
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

            py::class_<ScriptDocument>(module, "Document", "The open document. A whole script run is one undo step.")
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

    void registerCurvyModule() {
        py::module_ module = py::module_::import("types").attr("ModuleType")("curvy");
        py::module_::import("sys").attr("modules")["curvy"] = module;
        // A BaseException, like KeyboardInterrupt, so a script's `except Exception` does not swallow a cancel.
        module.attr("Cancelled") = py::module_::import("builtins").attr("type")(
            "Cancelled", py::make_tuple(py::handle(PyExc_BaseException)), py::dict());
        bindSwatches(module);
        bindObject(module);
        bindDocument(module);
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

    py::object makeScriptDocument(SketchDocument& document, ScriptTransaction& transaction) {
        return py::cast(ScriptDocument(document, transaction));
    }

}
