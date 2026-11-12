#pragma once

#include <jitnodes/graph/node.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace curvy {

    // A swatch: a colour (red, green, blue and alpha in 0..1) with an optional name. A plain one (link 0) is a copy of the colour: nothing refers
    // to it. A linked one has a number that is unique in the document; a colour row that was given it carries the node tag SwatchLinks::tag(link, group),
    // and changing the swatch's colour recolours every such row (see SketchDocument::replaceSwatch).
    // @reflect ignore=true
    struct Swatch {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        std::string name;
        std::uint32_t link = 0;

        bool sameColor(const Swatch& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }
        bool operator==(const Swatch& other) const { return sameColor(other) && name == other.name && link == other.link; }
    };

    // The node tags and parameters that tie a colour row to a linked swatch.
    // @reflect ignore=true
    struct SwatchLinks {
        // "swatch:<link>:<group>", group being the colour row's group tag (the colour parameters tagged group:<group> and kind:color).
        static std::string tag(std::uint32_t link, const std::string& group);
        static bool parse(const std::string& tag, std::uint32_t& link, std::string& group);
        // The parameter names of the colour row `group` of a node (channel:r|g|b|a tags); false without r, g and b. `a` stays empty without alpha.
        struct Channels {
            std::string r, g, b, a;
        };
        static bool channels(const jitnodes::Node& node, const std::string& group, Channels& out);
    };

    // A named list of swatches; a document keeps one or more of them, one active.
    // @reflect ignore=true
    struct Palette {
        std::string name;
        std::vector<Swatch> swatches;

        bool operator==(const Palette& other) const { return name == other.name && swatches == other.swatches; }
    };

    // The swatches a new document starts with: black, white, three greys and the primary and secondary colours.
    // @reflect ignore=true
    struct DefaultPalette {
        static Palette make();
    };

}
