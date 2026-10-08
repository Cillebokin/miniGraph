#include "CanvasPainterRenderer.h"

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasGeometry.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <cmath>
#include <limits>

namespace mini_graph {

namespace {

const qreal kTwoPi = 6.28318530717958647692;
const qreal kAngleTolerance = 1.0e-12;
const int kMaximumTessellationSegments = 8192;
const int kMinimumSplineSegmentBudget = 8192;
const int kMaximumSplineSubdivisionDepth = 12;

bool IsFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

QColor GetDisplayColor(const StruCanvasStyle &style, bool selected)
{
    return selected ? style.selectedStrokeColor : style.strokeColor;
}

QPolygonF MapPolyline(const QVector<CanvasVector> &worldPoints,
                      const CanvasRenderContext &context)
{
    QPolygonF viewportPoints;
    viewportPoints.reserve(worldPoints.size());
    for (const CanvasVector &worldPoint : worldPoints) {
        viewportPoints.append(context.MapFromWorld(worldPoint));
    }
    return viewportPoints;
}

void DrawWorldPath(QPainter &painter,
                   const QVector<CanvasVector> &worldPoints,
                   const CanvasRenderContext &context,
                   const StruCanvasStyle &style,
                   bool selected,
                   bool closed)
{
    if (worldPoints.size() < 2) {
        return;
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(GetDisplayColor(style, selected),
                        qMax<qreal>(1.0, style.strokeWidth)));
    const QPolygonF viewportPoints = MapPolyline(worldPoints, context);
    if (closed && viewportPoints.size() >= 3) {
        painter.drawPolygon(viewportPoints);
    } else {
        painter.drawPolyline(viewportPoints);
    }
}

int CalculateSegmentCount(qreal sweep,
                          qreal radius,
                          qreal maximumError,
                          int minimumSegments)
{
    if (!IsFinite(sweep) || !IsFinite(radius) || !IsFinite(maximumError)
            || sweep <= 0.0 || radius <= 0.0 || maximumError <= 0.0) {
        return 0;
    }

    qreal maximumStep = kTwoPi / 4.0;
    if (maximumError < radius) {
        const qreal cosineArgument = qBound<qreal>(
                    -1.0, 1.0 - maximumError / radius, 1.0);
        maximumStep = 2.0 * std::acos(cosineArgument);
    }
    if (!IsFinite(maximumStep) || maximumStep <= 0.0) {
        return kMaximumTessellationSegments;
    }

    const qreal requiredSegments = std::ceil(sweep / maximumStep);
    if (!IsFinite(requiredSegments)
            || requiredSegments >= kMaximumTessellationSegments) {
        return kMaximumTessellationSegments;
    }
    return qBound(minimumSegments,
                  qMax(1, static_cast<int>(requiredSegments)),
                  kMaximumTessellationSegments);
}

qreal NormalizeAngle(qreal angle)
{
    qreal normalized = std::fmod(angle, kTwoPi);
    if (normalized < 0.0) {
        normalized += kTwoPi;
    }
    return normalized;
}

qreal CalculateDirectedSweep(qreal startAngle,
                              qreal endAngle,
                              bool reversed)
{
    qreal directedDelta = reversed ? startAngle - endAngle
                                   : endAngle - startAngle;
    if (!IsFinite(directedDelta)) {
        const qreal normalizedStart = NormalizeAngle(startAngle);
        const qreal normalizedEnd = NormalizeAngle(endAngle);
        directedDelta = reversed ? normalizedStart - normalizedEnd
                                 : normalizedEnd - normalizedStart;
    }
    if (!IsFinite(directedDelta)) {
        return 0.0;
    }

    qreal sweep = std::fmod(directedDelta, kTwoPi);
    if (sweep < 0.0) {
        sweep += kTwoPi;
    }
    if (qAbs(sweep) <= kAngleTolerance
            && qAbs(directedDelta) > kAngleTolerance) {
        return kTwoPi;
    }
    return sweep;
}

QVector<CanvasVector> TessellateEllipse(const CanvasEllipse &ellipse,
                                        qreal maximumError,
                                        bool *closed)
{
    QVector<CanvasVector> points;
    if (closed) {
        *closed = false;
    }
    if (!ellipse.IsValid() || !IsFinite(maximumError) || maximumError <= 0.0) {
        return points;
    }

    const CanvasVector center = ellipse.GetCenter();
    const CanvasVector majorAxis = ellipse.GetMajorAxis();
    const qreal majorLength = majorAxis.Length();
    const bool isArc = ellipse.IsArc();
    const qreal startParameter = isArc ? ellipse.GetStartParameter() : 0.0;
    const qreal sweep = isArc
            ? CalculateDirectedSweep(startParameter,
                                     ellipse.GetEndParameter(),
                                     ellipse.IsReversed())
            : kTwoPi;
    if (majorLength <= 0.0 || sweep <= 0.0) {
        return points;
    }

    const bool isClosed = !isArc || sweep >= kTwoPi - kAngleTolerance;
    const int segmentCount = CalculateSegmentCount(
                sweep, majorLength, maximumError, isClosed ? 16 : 2);
    if (segmentCount <= 0) {
        return points;
    }

    const qreal ratio = ellipse.GetMinorMajorRatio();
    const qreal minorAxisX = -majorAxis.GetY() * ratio;
    const qreal minorAxisY = majorAxis.GetX() * ratio;
    if (!IsFinite(minorAxisX) || !IsFinite(minorAxisY)) {
        return points;
    }

    points.reserve(segmentCount + 1);
    const bool reversed = isArc && ellipse.IsReversed();
    for (int index = 0; index <= segmentCount; ++index) {
        if (isClosed && index == segmentCount) {
            points.append(points.first());
            continue;
        }

        const qreal progress = static_cast<qreal>(index)
                / static_cast<qreal>(segmentCount);
        const qreal parameter = reversed
                ? startParameter - sweep * progress
                : startParameter + sweep * progress;
        const qreal x = center.GetX()
                + majorAxis.GetX() * std::cos(parameter)
                + minorAxisX * std::sin(parameter);
        const qreal y = center.GetY()
                + majorAxis.GetY() * std::cos(parameter)
                + minorAxisY * std::sin(parameter);
        const CanvasVector point(x, y);
        if (!point.IsValid()) {
            points.clear();
            return points;
        }
        points.append(point);
    }

    if (closed) {
        *closed = isClosed;
    }
    return points;
}

bool EvaluateSpline(const CanvasSpline &spline,
                    int knotSpan,
                    qreal parameter,
                    CanvasVector *point)
{
    if (!point || !IsFinite(parameter)) {
        return false;
    }

    const int degree = spline.GetDegree();
    const QVector<CanvasVector> &controlPoints = spline.GetControlPoints();
    const QVector<qreal> &knots = spline.GetKnots();
    const QVector<qreal> &weights = spline.GetWeights();
    if (knotSpan < degree || knotSpan >= controlPoints.size()) {
        return false;
    }

    qreal basis[4] = {0.0, 0.0, 0.0, 0.0};
    qreal left[4] = {0.0, 0.0, 0.0, 0.0};
    qreal right[4] = {0.0, 0.0, 0.0, 0.0};
    basis[0] = 1.0;
    for (int order = 1; order <= degree; ++order) {
        left[order] = parameter - knots.at(knotSpan + 1 - order);
        right[order] = knots.at(knotSpan + order) - parameter;
        if (!IsFinite(left[order]) || !IsFinite(right[order])) {
            return false;
        }

        qreal saved = 0.0;
        for (int index = 0; index < order; ++index) {
            const qreal denominator = right[index + 1]
                    + left[order - index];
            if (!IsFinite(denominator)) {
                return false;
            }
            const qreal temporary = denominator > 0.0
                    ? basis[index] / denominator : 0.0;
            basis[index] = saved + right[index + 1] * temporary;
            saved = left[order - index] * temporary;
        }
        basis[order] = saved;
    }

    qreal termMantissas[4] = {0.0, 0.0, 0.0, 0.0};
    int termExponents[4] = {0, 0, 0, 0};
    int maximumExponent = std::numeric_limits<int>::min();
    for (int index = 0; index <= degree; ++index) {
        if (!IsFinite(basis[index]) || basis[index] < 0.0) {
            return false;
        }
        if (basis[index] == 0.0) {
            continue;
        }

        const int controlIndex = knotSpan - degree + index;
        const qreal weight = weights.isEmpty() ? 1.0
                                               : weights.at(controlIndex);
        if (!IsFinite(weight) || weight <= 0.0) {
            return false;
        }

        int basisExponent = 0;
        int weightExponent = 0;
        const qreal basisMantissa = std::frexp(basis[index], &basisExponent);
        const qreal weightMantissa = std::frexp(weight, &weightExponent);
        termMantissas[index] = basisMantissa * weightMantissa;
        termExponents[index] = basisExponent + weightExponent;
        maximumExponent = qMax(maximumExponent, termExponents[index]);
    }

    if (maximumExponent == std::numeric_limits<int>::min()) {
        return false;
    }

    qreal normalizedTerms[4] = {0.0, 0.0, 0.0, 0.0};
    qreal normalizedWeightSum = 0.0;
    for (int index = 0; index <= degree; ++index) {
        if (termMantissas[index] == 0.0) {
            continue;
        }
        normalizedTerms[index] = std::ldexp(
                    termMantissas[index],
                    termExponents[index] - maximumExponent);
        normalizedWeightSum += normalizedTerms[index];
    }
    if (!IsFinite(normalizedWeightSum) || normalizedWeightSum <= 0.0) {
        return false;
    }

    qreal resultX = 0.0;
    qreal resultY = 0.0;
    for (int index = 0; index <= degree; ++index) {
        if (normalizedTerms[index] == 0.0) {
            continue;
        }
        const int controlIndex = knotSpan - degree + index;
        const qreal contribution = normalizedTerms[index]
                / normalizedWeightSum;
        resultX = std::fma(controlPoints.at(controlIndex).GetX(),
                           contribution, resultX);
        resultY = std::fma(controlPoints.at(controlIndex).GetY(),
                           contribution, resultY);
    }

    const CanvasVector candidate(resultX, resultY);
    if (!candidate.IsValid()) {
        return false;
    }
    *point = candidate;
    return true;
}

bool DistanceToSegment(const CanvasVector &point,
                       const CanvasVector &start,
                       const CanvasVector &end,
                       qreal *distance)
{
    if (!distance) {
        return false;
    }

    const qreal deltaX = end.GetX() - start.GetX();
    const qreal deltaY = end.GetY() - start.GetY();
    const qreal pointX = point.GetX() - start.GetX();
    const qreal pointY = point.GetY() - start.GetY();
    const qreal lengthSquared = deltaX * deltaX + deltaY * deltaY;
    if (!IsFinite(deltaX) || !IsFinite(deltaY)
            || !IsFinite(pointX) || !IsFinite(pointY)
            || !IsFinite(lengthSquared)) {
        return false;
    }

    qreal progress = 0.0;
    if (lengthSquared > 0.0) {
        progress = (pointX * deltaX + pointY * deltaY) / lengthSquared;
        if (!IsFinite(progress)) {
            return false;
        }
        progress = qBound<qreal>(0.0, progress, 1.0);
    }

    const qreal nearestX = start.GetX() + progress * deltaX;
    const qreal nearestY = start.GetY() + progress * deltaY;
    *distance = std::hypot(point.GetX() - nearestX,
                           point.GetY() - nearestY);
    return IsFinite(*distance);
}

bool AppendSplineInterval(const CanvasSpline &spline,
                          int knotSpan,
                          qreal startParameter,
                          qreal endParameter,
                          const CanvasVector &startPoint,
                          const CanvasVector &endPoint,
                          qreal maximumError,
                          int depth,
                          int segmentBudget,
                          QVector<CanvasVector> *points)
{
    if (!points || segmentBudget <= 0) {
        return false;
    }

    const qreal parameterDelta = endParameter - startParameter;
    if (!IsFinite(parameterDelta) || parameterDelta <= 0.0) {
        return false;
    }
    if (segmentBudget == 1) {
        points->append(endPoint);
        return true;
    }

    const qreal firstQuarterParameter = startParameter + parameterDelta * 0.25;
    const qreal middleParameter = startParameter + parameterDelta * 0.5;
    const qreal thirdQuarterParameter = startParameter + parameterDelta * 0.75;
    CanvasVector firstQuarterPoint;
    CanvasVector middlePoint;
    CanvasVector thirdQuarterPoint;
    if (!EvaluateSpline(spline, knotSpan, firstQuarterParameter,
                        &firstQuarterPoint)
            || !EvaluateSpline(spline, knotSpan, middleParameter, &middlePoint)
            || !EvaluateSpline(spline, knotSpan, thirdQuarterParameter,
                               &thirdQuarterPoint)) {
        return false;
    }

    qreal firstDeviation = 0.0;
    qreal middleDeviation = 0.0;
    qreal thirdDeviation = 0.0;
    if (!DistanceToSegment(firstQuarterPoint, startPoint, endPoint,
                           &firstDeviation)
            || !DistanceToSegment(middlePoint, startPoint, endPoint,
                                  &middleDeviation)
            || !DistanceToSegment(thirdQuarterPoint, startPoint, endPoint,
                                  &thirdDeviation)) {
        return false;
    }

    const qreal maximumDeviation = qMax(firstDeviation,
                                        qMax(middleDeviation, thirdDeviation));
    if (maximumDeviation <= maximumError
            || depth >= kMaximumSplineSubdivisionDepth) {
        points->append(endPoint);
        return true;
    }

    const int leftBudget = segmentBudget / 2;
    const int rightBudget = segmentBudget - leftBudget;
    return AppendSplineInterval(spline, knotSpan,
                                startParameter, middleParameter,
                                startPoint, middlePoint, maximumError,
                                depth + 1, leftBudget, points)
            && AppendSplineInterval(spline, knotSpan,
                                    middleParameter, endParameter,
                                    middlePoint, endPoint, maximumError,
                                    depth + 1, rightBudget, points);
}

QVector<QVector<CanvasVector>> TessellateSpline(const CanvasSpline &spline,
                                                 qreal maximumError)
{
    QVector<QVector<CanvasVector>> paths;
    if (!spline.IsValid() || !IsFinite(maximumError) || maximumError <= 0.0) {
        return paths;
    }

    const QVector<CanvasVector> &controlPoints = spline.GetControlPoints();
    const QVector<qreal> &knots = spline.GetKnots();
    const int degree = spline.GetDegree();
    const qreal domainStart = knots.at(degree);
    const qreal domainEnd = knots.at(controlPoints.size());
    if (!IsFinite(domainStart) || !IsFinite(domainEnd)
            || domainStart >= domainEnd) {
        return paths;
    }

    int activeSpanCount = 0;
    for (int knotSpan = degree;
         knotSpan < controlPoints.size();
         ++knotSpan) {
        if (qMax(domainStart, knots.at(knotSpan))
                < qMin(domainEnd, knots.at(knotSpan + 1))) {
            ++activeSpanCount;
        }
    }
    if (activeSpanCount == 0) {
        return paths;
    }

    int remainingSegmentBudget = qMax(kMinimumSplineSegmentBudget,
                                      activeSpanCount);
    int remainingSpanCount = activeSpanCount;
    const qreal joinTolerance = qMax<qreal>(1.0e-9, maximumError * 0.01);
    for (int knotSpan = degree;
         knotSpan < controlPoints.size();
         ++knotSpan) {
        const qreal startParameter = qMax(domainStart, knots.at(knotSpan));
        const qreal endParameter = qMin(domainEnd, knots.at(knotSpan + 1));
        if (startParameter >= endParameter) {
            continue;
        }

        const int spanBudget = qMax(1,
                                    remainingSegmentBudget
                                    / remainingSpanCount);
        remainingSegmentBudget -= spanBudget;
        --remainingSpanCount;

        CanvasVector startPoint;
        CanvasVector endPoint;
        if (!EvaluateSpline(spline, knotSpan, startParameter, &startPoint)
                || !EvaluateSpline(spline, knotSpan, endParameter, &endPoint)) {
            return QVector<QVector<CanvasVector>>();
        }

        QVector<CanvasVector> spanPoints;
        spanPoints.append(startPoint);
        if (!AppendSplineInterval(spline, knotSpan,
                                  startParameter, endParameter,
                                  startPoint, endPoint, maximumError,
                                  0, spanBudget, &spanPoints)) {
            return QVector<QVector<CanvasVector>>();
        }
        if (spanPoints.size() < 2) {
            continue;
        }

        if (!paths.isEmpty()
                && paths.last().last().IsNearTo(spanPoints.first(),
                                                 joinTolerance)) {
            for (int index = 1; index < spanPoints.size(); ++index) {
                paths.last().append(spanPoints.at(index));
            }
        } else {
            paths.append(spanPoints);
        }
    }

    if (spline.IsClosed() && !paths.isEmpty()) {
        QVector<CanvasVector> &lastPath = paths.last();
        const CanvasVector firstPoint = paths.first().first();
        if (!lastPath.last().IsNearTo(firstPoint, joinTolerance)) {
            lastPath.append(firstPoint);
        }
    }

    return paths;
}

void DrawEntityGeometry(QPainter &painter,
                        const CanvasRenderContext &context,
                        const CanvasEntity &entity,
                        const StruCanvasStyle &style,
                        bool selected,
                        qreal maximumError)
{
    switch (entity.GetType()) {
    case CanvasEntityType::Point: {
        const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(&entity);
        if (!point || !point->IsValid()) {
            return;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(GetDisplayColor(style, selected));
        const QPointF viewportPosition = context.MapFromWorld(point->GetPosition());
        const qreal radius = qMax<qreal>(1.0, style.pointSize) * 0.5;
        painter.drawEllipse(viewportPosition, radius, radius);
        return;
    }
    case CanvasEntityType::Line: {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(&entity);
        if (!line || !line->IsValid()) {
            return;
        }

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(GetDisplayColor(style, selected),
                            qMax<qreal>(1.0, style.strokeWidth)));
        painter.drawLine(context.MapFromWorld(line->GetStartPoint()),
                         context.MapFromWorld(line->GetEndPoint()));
        return;
    }
    case CanvasEntityType::Arc: {
        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(&entity);
        if (!arc || !arc->IsValid()) {
            return;
        }

        StruArcGeometry geometry;
        geometry.center = arc->GetCenter();
        geometry.radius = arc->GetRadius();
        geometry.startAngle = arc->GetStartAngle();
        geometry.endAngle = arc->GetEndAngle();
        geometry.reversed = arc->IsReversed();
        DrawWorldPath(painter, TessellateArc(geometry, maximumError),
                      context, style, selected, false);
        return;
    }
    case CanvasEntityType::Circle: {
        const CanvasCircle *circle = dynamic_cast<const CanvasCircle *>(&entity);
        if (!circle || !circle->IsValid()) {
            return;
        }

        StruCircleGeometry geometry;
        geometry.center = circle->GetCenter();
        geometry.radius = circle->GetRadius();
        DrawWorldPath(painter, TessellateCircle(geometry, maximumError),
                      context, style, selected, true);
        return;
    }
    case CanvasEntityType::Ellipse: {
        const CanvasEllipse *ellipse = dynamic_cast<const CanvasEllipse *>(&entity);
        if (!ellipse) {
            return;
        }

        bool closed = false;
        const QVector<CanvasVector> points = TessellateEllipse(
                    *ellipse, maximumError, &closed);
        DrawWorldPath(painter, points, context, style, selected, closed);
        return;
    }
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        if (!polyline || !polyline->IsValid()) {
            return;
        }

        for (int index = 0; index < polyline->GetSegmentCount(); ++index) {
            const CanvasEntity *segment = polyline->GetSegment(index);
            if (segment) {
                DrawEntityGeometry(painter, context, *segment, style,
                                   selected, maximumError);
            }
        }
        return;
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *spline = dynamic_cast<const CanvasSpline *>(&entity);
        if (!spline) {
            return;
        }

        const QVector<QVector<CanvasVector>> paths = TessellateSpline(
                    *spline, maximumError);
        for (int index = 0; index < paths.size(); ++index) {
            DrawWorldPath(painter, paths.at(index), context, style, selected,
                          false);
        }
        return;
    }
    case CanvasEntityType::GroupContainer:
    case CanvasEntityType::Invalid:
        return;
    }
}

void RenderEntity(QPainter &painter,
                  const CanvasRenderContext &context,
                  const CanvasDocument &document,
                  const CanvasEntity *entity,
                  bool inheritedSelection,
                  qreal maximumError)
{
    if (!entity || !entity->GetStyle().visible) {
        return;
    }

    bool selected = inheritedSelection;
    if (!selected && entity->GetId() != 0) {
        selected = document.IsItemSelected(entity->GetId());
    }

    if (entity->GetType() == CanvasEntityType::GroupContainer) {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(entity);
        if (!group) {
            return;
        }
        for (int index = 0; index < group->GetChildCount(); ++index) {
            RenderEntity(painter, context, document, group->GetChild(index),
                         selected, maximumError);
        }
        return;
    }

    DrawEntityGeometry(painter, context, *entity, entity->GetStyle(),
                       selected, maximumError);
}

} // namespace

namespace detail {

QVector<CanvasVector> TessellateEllipseForSelection(
        const CanvasEllipse &ellipse, qreal maximumError, bool *closed)
{
    return TessellateEllipse(ellipse, maximumError, closed);
}

QVector<QVector<CanvasVector>> TessellateSplineForSelection(
        const CanvasSpline &spline, qreal maximumError)
{
    return TessellateSpline(spline, maximumError);
}

} // namespace detail

void CanvasPainterRenderer::Render(
        QPainter &painter,
        const CanvasRenderContext &context) const
{
    const CanvasDocument *document = context.document;
    if (!document) {
        return;
    }

    const qreal zoom = qMax(context.transform.GetZoom(),
                            CanvasTransform::GetMinimumZoom());
    const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const ItemId id : document->GetItemIds()) {
        RenderEntity(painter, context, *document, document->GetEntity(id),
                     false, maximumWorldError);
    }
    painter.restore();
}

} // namespace mini_graph
