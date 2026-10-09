#include "MiniGraph/CanvasSnap.h"

#include "MiniGraph/CanvasEntities.h"
#include "MiniGraph/CanvasDocument.h"
#include "CanvasGeometryPrivate.h"

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

template <typename ConsiderFunction>
void AddSnapCandidateIfValid(const ConsiderFunction &consider,
                             ItemId itemId,
                             CanvasSnapType type,
                             const CanvasVector &position)
{
    if (position.IsValid()) {
        consider(itemId, type, position);
    }
}

template <typename ConsiderFunction>
void CollectPolylineSegmentSnapCandidates(
        const CanvasEntity &segment,
        ItemId itemId,
        const ConsiderFunction &consider)
{
    if (!segment.IsValid() || !segment.GetStyle().visible) {
        return;
    }

    if (const CanvasLine *line = dynamic_cast<const CanvasLine *>(&segment)) {
        AddSnapCandidateIfValid(consider, itemId,
                                CanvasSnapType::PolylineVertex,
                                line->GetStartPoint());
        AddSnapCandidateIfValid(consider, itemId,
                                CanvasSnapType::PolylineVertex,
                                line->GetEndPoint());
        return;
    }

    if (const CanvasArc *arc = dynamic_cast<const CanvasArc *>(&segment)) {
        AddSnapCandidateIfValid(consider, itemId,
                                CanvasSnapType::ArcCenter,
                                arc->GetCenter());
        AddSnapCandidateIfValid(consider, itemId,
                                CanvasSnapType::PolylineVertex,
                                arc->GetStartPoint());
        AddSnapCandidateIfValid(consider, itemId,
                                CanvasSnapType::PolylineVertex,
                                arc->GetEndPoint());
    }
}

template <typename ConsiderFunction>
void CollectEntitySnapCandidates(const CanvasEntity &entity,
                                 ItemId itemId,
                                 bool parentVisible,
                                 const ConsiderFunction &consider)
{
    if (!parentVisible || !entity.GetStyle().visible || !entity.IsValid()) {
        return;
    }

    switch (entity.GetType()) {
    case CanvasEntityType::Point: {
        const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(&entity);
        if (point) {
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::Point,
                                    point->GetPosition());
        }
        break;
    }
    case CanvasEntityType::Line: {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(&entity);
        if (line) {
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::LineEndpoint,
                                    line->GetStartPoint());
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::LineEndpoint,
                                    line->GetEndPoint());
        }
        break;
    }
    case CanvasEntityType::Arc: {
        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(&entity);
        if (arc) {
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::ArcCenter,
                                    arc->GetCenter());
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::ArcEndpoint,
                                    arc->GetStartPoint());
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::ArcEndpoint,
                                    arc->GetEndPoint());
        }
        break;
    }
    case CanvasEntityType::Circle: {
        const CanvasCircle *circle =
                dynamic_cast<const CanvasCircle *>(&entity);
        if (circle) {
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::CircleCenter,
                                    circle->GetCenter());
        }
        break;
    }
    case CanvasEntityType::Ellipse: {
        const CanvasEllipse *ellipse =
                dynamic_cast<const CanvasEllipse *>(&entity);
        if (!ellipse) {
            break;
        }

        AddSnapCandidateIfValid(consider, itemId,
                                CanvasSnapType::EllipseCenter,
                                ellipse->GetCenter());
        const CanvasVector majorAxis = ellipse->GetMajorAxis();
        const CanvasVector minorAxis(-majorAxis.GetY()
                                     * ellipse->GetMinorMajorRatio(),
                                     majorAxis.GetX()
                                     * ellipse->GetMinorMajorRatio());
        if (ellipse->IsArc()) {
            const auto calculatePoint = [ellipse, &consider, itemId](qreal parameter) {
                const CanvasVector major = ellipse->GetMajorAxis();
                const qreal ratio = ellipse->GetMinorMajorRatio();
                const CanvasVector minor(-major.GetY() * ratio,
                                         major.GetX() * ratio);
                const CanvasVector point = ellipse->GetCenter()
                        + major * std::cos(static_cast<double>(parameter))
                        + minor * std::sin(static_cast<double>(parameter));
                AddSnapCandidateIfValid(consider, itemId,
                                        CanvasSnapType::EllipseEndpoint,
                                        point);
            };
            calculatePoint(ellipse->GetStartParameter());
            calculatePoint(ellipse->GetEndParameter());
        } else {
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::EllipseAxisPoint,
                                    ellipse->GetCenter() + majorAxis);
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::EllipseAxisPoint,
                                    ellipse->GetCenter() - majorAxis);
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::EllipseAxisPoint,
                                    ellipse->GetCenter() + minorAxis);
            AddSnapCandidateIfValid(consider, itemId,
                                    CanvasSnapType::EllipseAxisPoint,
                                    ellipse->GetCenter() - minorAxis);
        }
        break;
    }
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        if (!polyline) {
            break;
        }
        for (int index = 0; index < polyline->GetSegmentCount(); ++index) {
            const CanvasEntity *segment = polyline->GetSegment(index);
            if (segment) {
                CollectPolylineSegmentSnapCandidates(*segment, itemId,
                                                     consider);
            }
        }
        break;
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *spline =
                dynamic_cast<const CanvasSpline *>(&entity);
        if (spline) {
            CanvasVector startPoint;
            CanvasVector endPoint;
            if (detail::TryGetSplineEndpoints(*spline,
                                              &startPoint, &endPoint)) {
                AddSnapCandidateIfValid(consider, itemId,
                                        CanvasSnapType::SplineEndpoint,
                                        startPoint);
                AddSnapCandidateIfValid(consider, itemId,
                                        CanvasSnapType::SplineEndpoint,
                                        endPoint);
            }
        }
        break;
    }
    case CanvasEntityType::GroupContainer: {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(&entity);
        if (!group) {
            break;
        }
        for (int index = 0; index < group->GetChildCount(); ++index) {
            const CanvasEntity *child = group->GetChild(index);
            if (child) {
                CollectEntitySnapCandidates(*child, itemId, true, consider);
            }
        }
        break;
    }
    case CanvasEntityType::Invalid:
        break;
    }
}

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
    case CanvasSnapType::EllipseEndpoint:
    case CanvasSnapType::EllipseAxisPoint:
    case CanvasSnapType::SplineEndpoint:
        return 1;
    case CanvasSnapType::ArcCenter:
    case CanvasSnapType::CircleCenter:
    case CanvasSnapType::EllipseCenter:
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
        const QVector<ItemId> itemIds = document->GetItemIds();
        for (const ItemId itemId : itemIds) {
            const CanvasEntity *entity = document->GetEntity(itemId);
            if (entity) {
                CollectEntitySnapCandidates(*entity, itemId, true, consider);
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
