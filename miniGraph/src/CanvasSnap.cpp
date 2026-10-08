#include "MiniGraph/CanvasSnap.h"

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasGeometry.h"

#include <QtGlobal>

#include <cmath>
#include <limits>

namespace mini_graph {

namespace {

struct SnapCandidate
{
    CanvasSnapResult result;
    qreal distanceSquared = std::numeric_limits<qreal>::max();
    int priority = std::numeric_limits<int>::max();
    bool valid = false;
};

qreal DistanceSquared(const CanvasVector &left, const CanvasVector &right)
{
    return left.SquaredDistanceTo(right);
}

int GetSnapPriority(CanvasSnapType type)
{
    switch (type) {
    case CanvasSnapType::Point:
        return 0;
    case CanvasSnapType::LineEndpoint:
    case CanvasSnapType::ArcEndpoint:
    case CanvasSnapType::PolylineVertex:
        return 1;
    case CanvasSnapType::ArcCenter:
    case CanvasSnapType::CircleCenter:
        return 2;
    case CanvasSnapType::None:
    case CanvasSnapType::Grid:
        break;
    }
    return 3;
}

} // namespace

CanvasSnapResult CanvasSnapper::FindSnap(const CanvasDocument *document,
                                         const CanvasVector &worldPosition,
                                         qreal toleranceWorld,
                                         qreal gridSpacing,
                                         bool geometrySnapEnabled,
                                         bool gridSnapEnabled)
{
    CanvasSnapResult result;
    if (!worldPosition.IsValid()) {
        return result;
    }

    const qreal tolerance = std::isfinite(static_cast<double>(toleranceWorld))
            ? qMax<qreal>(0.0, toleranceWorld)
            : 0.0;
    const qreal toleranceSquared = tolerance * tolerance;
    SnapCandidate bestCandidate;

    const auto consider = [&bestCandidate,
                           &worldPosition,
                           toleranceSquared](ItemId itemId,
                                             CanvasSnapType type,
                                             const CanvasVector &position) {
        const qreal distanceSquared = DistanceSquared(worldPosition, position);
        if (distanceSquared > toleranceSquared) {
            return;
        }

        const int priority = GetSnapPriority(type);
        const bool closer = distanceSquared < bestCandidate.distanceSquared;
        const bool sameDistance = qFuzzyCompare(distanceSquared + 1.0,
                                                bestCandidate.distanceSquared + 1.0);
        const bool higherPriority = priority < bestCandidate.priority;
        const bool samePriorityEarlierItem = priority == bestCandidate.priority
                && itemId != 0
                && (bestCandidate.result.itemId == 0
                    || itemId < bestCandidate.result.itemId);
        if (!bestCandidate.valid
                || closer
                || (sameDistance && (higherPriority || samePriorityEarlierItem))) {
            bestCandidate.result.type = type;
            bestCandidate.result.itemId = itemId;
            bestCandidate.result.position = position;
            bestCandidate.distanceSquared = distanceSquared;
            bestCandidate.priority = priority;
            bestCandidate.valid = true;
        }
    };

    if (geometrySnapEnabled && document) {
        for (const StruPointPrimitive &point : document->GetPointPrimitives()) {
            if (point.style.visible) {
                consider(point.id,
                         CanvasSnapType::Point,
                         point.geometry.position);
            }
        }

        for (const StruLinePrimitive &line : document->GetLinePrimitives()) {
            if (!line.style.visible) {
                continue;
            }
            consider(line.id,
                     CanvasSnapType::LineEndpoint,
                     line.geometry.startPoint);
            consider(line.id,
                     CanvasSnapType::LineEndpoint,
                     line.geometry.endPoint);
        }

        for (const StruArcPrimitive &arc : document->GetArcPrimitives()) {
            if (!arc.style.visible) {
                continue;
            }
            consider(arc.id,
                     CanvasSnapType::ArcCenter,
                     arc.geometry.center);
            consider(arc.id,
                     CanvasSnapType::ArcEndpoint,
                     CalculateArcPoint(arc.geometry, arc.geometry.startAngle));
            consider(arc.id,
                     CanvasSnapType::ArcEndpoint,
                     CalculateArcPoint(arc.geometry, arc.geometry.endAngle));
        }

        for (const StruCirclePrimitive &circle : document->GetCirclePrimitives()) {
            if (circle.style.visible) {
                consider(circle.id,
                         CanvasSnapType::CircleCenter,
                         circle.geometry.center);
            }
        }

        for (const StruPolylinePrimitive &polyline : document->GetPolylinePrimitives()) {
            if (!polyline.style.visible) {
                continue;
            }
            for (const CanvasVector &vertex : polyline.geometry.vertices) {
                consider(polyline.id,
                         CanvasSnapType::PolylineVertex,
                         vertex);
            }
        }
    }

    if (bestCandidate.valid) {
        return bestCandidate.result;
    }

    if (gridSnapEnabled
            && std::isfinite(static_cast<double>(gridSpacing))
            && gridSpacing > 0.0) {
        const qreal gridX = std::round(worldPosition.GetX() / gridSpacing)
                * gridSpacing;
        const qreal gridY = std::round(worldPosition.GetY() / gridSpacing)
                * gridSpacing;
        if (std::isfinite(static_cast<double>(gridX))
                && std::isfinite(static_cast<double>(gridY))) {
            result.type = CanvasSnapType::Grid;
            result.position = CanvasVector(gridX, gridY);
        }
    }

    return result;
}

} // namespace mini_graph
