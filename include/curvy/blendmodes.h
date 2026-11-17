#pragma once

#include <blend2d/blend2d.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace curvy {

    // The blend modes an object offers. An object's `blend` parameter is the index into this list (0 is Normal); the names have no spaces
    // or commas because the Node panel reads them from the parameter's "choices:" tag.
    // @reflect ignore=true
    struct BlendModes {
        static const std::vector<std::string>& names() {
            static const std::vector<std::string> list = {"Normal",    "Multiply",  "Screen",    "Overlay",   "Darken",    "Lighten",
                                                           "ColorDodge", "ColorBurn", "HardLight", "SoftLight", "Difference", "Exclusion"};
            return list;
        }
        // "Normal,Multiply,..." for the tag.
        static std::string choicesText() {
            std::string text;
            for (const std::string& name : names()) {
                text += (text.empty() ? "" : ",") + name;
            }
            return text;
        }
        // An index outside the list is Normal.
        static BLCompOp compOp(int index) {
            static const BLCompOp ops[] = {BL_COMP_OP_SRC_OVER,   BL_COMP_OP_MULTIPLY,   BL_COMP_OP_SCREEN,     BL_COMP_OP_OVERLAY,
                                           BL_COMP_OP_DARKEN,     BL_COMP_OP_LIGHTEN,    BL_COMP_OP_COLOR_DODGE, BL_COMP_OP_COLOR_BURN,
                                           BL_COMP_OP_HARD_LIGHT, BL_COMP_OP_SOFT_LIGHT, BL_COMP_OP_DIFFERENCE,  BL_COMP_OP_EXCLUSION};
            if (index < 0 || std::size_t(index) >= std::size(ops)) {
                return BL_COMP_OP_SRC_OVER;
            }
            return ops[index];
        }
    };

}
