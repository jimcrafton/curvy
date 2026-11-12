#include "curvy/swatchpalettes.h"

#include <iterator>
#include <string>

namespace curvy {

    std::string SwatchLinks::tag(std::uint32_t link, const std::string& group) {
        return "swatch:" + std::to_string(link) + ":" + group;
    }

    bool SwatchLinks::parse(const std::string& tag, std::uint32_t& link, std::string& group) {
        constexpr char prefix[] = "swatch:";
        if (tag.rfind(prefix, 0) != 0) {
            return false;
        }
        const std::size_t colon = tag.find(':', sizeof(prefix) - 1);
        if (colon == std::string::npos || colon == sizeof(prefix) - 1) {
            return false;
        }
        const std::string number = tag.substr(sizeof(prefix) - 1, colon - (sizeof(prefix) - 1));
        if (number.find_first_not_of("0123456789") != std::string::npos || number.size() > 9) {
            return false;
        }
        link = std::uint32_t(std::stoul(number));
        group = tag.substr(colon + 1);
        return link != 0 && !group.empty();
    }

    bool SwatchLinks::channels(const jitnodes::Node& node, const std::string& group, Channels& out) {
        out = Channels();
        for (const std::string& name : node.floatNames()) {
            if (node.paramTagValue(name, "group").value_or("") != group || node.paramTagValue(name, "kind").value_or("") != "color") {
                continue;
            }
            const std::string channel = node.paramTagValue(name, "channel").value_or("");
            (channel == "r" ? out.r : channel == "g" ? out.g : channel == "b" ? out.b : out.a) = name;
        }
        return !out.r.empty() && !out.g.empty() && !out.b.empty();
    }

    Palette DefaultPalette::make() {
        Palette palette;
        palette.name = "Swatches";
        const Swatch colors[] = {
            {0.0f, 0.0f, 0.0f, 1.0f, "Black"},    {0.25f, 0.25f, 0.25f, 1.0f, "Dark grey"}, {0.5f, 0.5f, 0.5f, 1.0f, "Grey"},   {0.75f, 0.75f, 0.75f, 1.0f, "Light grey"},
            {1.0f, 1.0f, 1.0f, 1.0f, "White"},    {0.90f, 0.20f, 0.20f, 1.0f, "Red"},       {0.96f, 0.55f, 0.13f, 1.0f, "Orange"}, {0.98f, 0.80f, 0.15f, 1.0f, "Yellow"},
            {0.30f, 0.72f, 0.31f, 1.0f, "Green"}, {0.20f, 0.72f, 0.80f, 1.0f, "Cyan"},      {0.20f, 0.45f, 0.90f, 1.0f, "Blue"},   {0.55f, 0.30f, 0.85f, 1.0f, "Purple"},
            {0.92f, 0.35f, 0.65f, 1.0f, "Pink"},
        };
        palette.swatches.assign(std::begin(colors), std::end(colors));
        return palette;
    }

}
