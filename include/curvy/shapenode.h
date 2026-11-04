#pragma once

#include "curvy/liveshape.h"

#include <jitnodes/graph/node.h>

#include <map>
#include <string>

namespace curvy {

    // A star, polygon, rounded rectangle or arc held as numbers: the node cooks the outline the same way the canvas handles and the Shape box do
    // (liveShapePath), so the layout of the points exists in one place. Its output is a `curve` geometry like a path node's, so the tag, move-points,
    // transform and appearance nodes work after it. Parameters: `kind` (string: polygon, star, roundRect, arc); centerX, centerY; points (sides or
    // tips); outer (circumradius, or the arc's radius); innerRatio (star, inner / outer); width, height, radius (rounded rectangle corner); angle
    // (degrees: first corner of a polygon or star, turn of a rounded rectangle); start, sweep (degrees, arc). Unused parameters keep their values.
    inline constexpr const char* kShapeNodeType = "curvyShape";

    void registerShapeNode();

    // The shape the node's numbers describe (kind None for an unknown kind), and the numbers of `shape` written to the node.
    LiveShape shapeNodeLive(const jitnodes::Node& node);
    void storeShapeNode(jitnodes::Node& node, const LiveShape& shape);
    // The float parameters storeShapeNode writes for `shape` (degrees for angles), so an edit can write only those that changed.
    std::map<std::string, double> shapeNodeValues(const LiveShape& shape);

}
