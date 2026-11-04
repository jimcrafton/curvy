#include "curvy/liveshape.h"

namespace curvy {

    bool recogniseLiveShape(const SketchObject& object, LiveShape& out) {
        out = LiveShape();
        out.object = object.id();
        if (object.type() == SketchObjectType::Polygon) {
            std::vector<SubPath> subs = pathToSubPaths(object.geometry());
            if (subs.size() != 1 || !subs[0].closed) {
                return false;
            }
            std::vector<BLPoint> corners;
            for (const PenAnchor& a : subs[0].anchors) {
                if (a.hasIn || a.hasOut) {
                    return false;
                }
                corners.push_back(a.point);
            }
            if (!analyzeRegularShape(corners, out.regular)) {
                return false;
            }
            out.kind = out.regular.star ? LiveKind::Star : LiveKind::Polygon;
            return true;
        }
        if (object.type() == SketchObjectType::RoundRect) {
            if (!analyzeRoundRect(object.geometry(), object.frameAngle(), out.rect)) {
                return false;
            }
            out.kind = LiveKind::RoundRect;
            return true;
        }
        if (object.type() == SketchObjectType::Path && analyzeArc(object.geometry(), out.arc)) {
            out.kind = LiveKind::Arc;
            return true;
        }
        return false;
    }

    BLPath liveShapePath(const LiveShape& shape) {
        switch (shape.kind) {
            case LiveKind::Star:
            case LiveKind::Polygon: return regularShapePath(shape.regular);
            case LiveKind::RoundRect: return roundRectPath(shape.rect);
            case LiveKind::Arc: return arcPath(shape.arc);
            default: return BLPath();
        }
    }

}
