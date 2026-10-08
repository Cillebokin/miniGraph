#include "MiniGraph/CanvasGeometry.h"

#include <QtGlobal>

#include <cmath>

namespace mini_graph {

namespace {

const qreal GEOMETRY_TOLERANCE = 1.0e-12;
const qreal TWO_PI = 6.28318530717958647692;
const int MAX_TESSELLATION_SEGMENTS = 8192;
const int MIN_CIRCLE_SEGMENTS = 16;
const int MIN_ARC_SEGMENTS = 2;

bool IsFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

qreal NormalizeAngle(qreal angle)
{
    qreal normalized = std::fmod(angle, TWO_PI);
    if (normalized < 0.0) {
        normalized += TWO_PI;
    }
    return normalized;
}

qreal CalculateDirectedSweep(const StruArcGeometry &geometry)
{
    const qreal directedDelta = geometry.reversed
            ? geometry.startAngle - geometry.endAngle
            : geometry.endAngle - geometry.startAngle;
    qreal sweep = std::fmod(directedDelta, TWO_PI);
    if (sweep < 0.0) {
        sweep += TWO_PI;
    }

    if (qAbs(sweep) <= GEOMETRY_TOLERANCE
            && qAbs(directedDelta) > GEOMETRY_TOLERANCE) {
        sweep = TWO_PI;
    }

    return sweep;
}

bool IsAngleOnArcInternal(const StruArcGeometry &geometry, qreal angle)
{
    const qreal sweep = CalculateDirectedSweep(geometry);
    if (sweep >= TWO_PI - GEOMETRY_TOLERANCE) {
        return true;
    }

    const qreal startAngle = NormalizeAngle(geometry.startAngle);
    const qreal normalizedAngle = NormalizeAngle(angle);
    const qreal delta = geometry.reversed
            ? NormalizeAngle(startAngle - normalizedAngle)
            : NormalizeAngle(normalizedAngle - startAngle);
    return delta <= sweep + GEOMETRY_TOLERANCE;
}

void IncludePoint(qreal *left,
                  qreal *right,
                  qreal *top,
                  qreal *bottom,
                  const CanvasVector &point)
{
    *left = qMin(*left, point.GetX());
    *right = qMax(*right, point.GetX());
    *top = qMin(*top, point.GetY());
    *bottom = qMax(*bottom, point.GetY());
}

int CalculateSegmentCount(qreal sweep,
                          qreal radius,
                          qreal maximumError,
                          int minimumSegments)
{
    if (!IsFinite(sweep) || !IsFinite(radius) || !IsFinite(maximumError)
            || radius <= 0.0 || maximumError <= 0.0) {
        return 0;
    }

    qreal maximumStep = TWO_PI / 4.0;
    if (maximumError < radius) {
        const qreal cosineArgument = qBound<qreal>(-1.0,
                                                   1.0 - maximumError / radius,
                                                   1.0);
        maximumStep = 2.0 * std::acos(cosineArgument);
    }

    if (!IsFinite(maximumStep) || maximumStep <= GEOMETRY_TOLERANCE) {
        maximumStep = sweep;
    }

    const int calculatedSegments = static_cast<int>(std::ceil(sweep / maximumStep));
    return qBound(minimumSegments,
                  qMax(1, calculatedSegments),
                  MAX_TESSELLATION_SEGMENTS);
}

} // namespace

bool IsValidGeometry(const StruArcGeometry &geometry)
{
    return geometry.center.IsValid()
            && IsFinite(geometry.radius)
            && geometry.radius > 0.0
            && IsFinite(geometry.startAngle)
            && IsFinite(geometry.endAngle)
            && qAbs(geometry.endAngle - geometry.startAngle)
            > GEOMETRY_TOLERANCE;
}

bool IsValidGeometry(const StruCircleGeometry &geometry)
{
    return geometry.center.IsValid()
            && IsFinite(geometry.radius)
            && geometry.radius > 0.0;
}

bool IsValidGeometry(const StruPolylineGeometry &geometry)
{
    const int minimumVertexCount = geometry.closed ? 3 : 2;
    if (geometry.vertices.size() < minimumVertexCount) {
        return false;
    }

    for (const CanvasVector &vertex : geometry.vertices) {
        if (!vertex.IsValid()) {
            return false;
        }
    }

    return true;
}

qreal GetArcSweep(const StruArcGeometry &geometry)
{
    if (!IsValidGeometry(geometry)) {
        return 0.0;
    }

    return CalculateDirectedSweep(geometry);
}

bool IsAngleOnArc(const StruArcGeometry &geometry, qreal angle)
{
    if (!IsValidGeometry(geometry) || !IsFinite(angle)) {
        return false;
    }

    return IsAngleOnArcInternal(geometry, angle);
}

CanvasVector CalculateArcPoint(const StruArcGeometry &geometry, qreal angle)
{
    return geometry.center
            + CanvasVector(geometry.radius * std::cos(angle),
                           geometry.radius * std::sin(angle));
}

QRectF GetGeometryBounds(const StruPointGeometry &geometry)
{
    return QRectF(geometry.position.GetX(), geometry.position.GetY(), 0.0, 0.0);
}

QRectF GetGeometryBounds(const StruLineGeometry &geometry)
{
    const qreal left = qMin(geometry.startPoint.GetX(),
                            geometry.endPoint.GetX());
    const qreal top = qMin(geometry.startPoint.GetY(),
                           geometry.endPoint.GetY());
    const qreal right = qMax(geometry.startPoint.GetX(),
                             geometry.endPoint.GetX());
    const qreal bottom = qMax(geometry.startPoint.GetY(),
                              geometry.endPoint.GetY());
    return QRectF(left, top, right - left, bottom - top);
}

QRectF GetGeometryBounds(const StruArcGeometry &geometry)
{
    if (!IsValidGeometry(geometry)) {
        return QRectF();
    }

    const CanvasVector startPoint = CalculateArcPoint(geometry, geometry.startAngle);
    const CanvasVector endPoint = CalculateArcPoint(geometry, geometry.endAngle);
    qreal left = qMin(startPoint.GetX(), endPoint.GetX());
    qreal right = qMax(startPoint.GetX(), endPoint.GetX());
    qreal top = qMin(startPoint.GetY(), endPoint.GetY());
    qreal bottom = qMax(startPoint.GetY(), endPoint.GetY());

    const qreal cardinalAngles[] = {
        0.0,
        TWO_PI / 4.0,
        TWO_PI / 2.0,
        TWO_PI * 3.0 / 4.0
    };
    for (const qreal cardinalAngle : cardinalAngles) {
        if (IsAngleOnArcInternal(geometry, cardinalAngle)) {
            IncludePoint(&left,
                         &right,
                         &top,
                         &bottom,
                         CalculateArcPoint(geometry, cardinalAngle));
        }
    }

    return QRectF(left, top, right - left, bottom - top);
}

QRectF GetGeometryBounds(const StruCircleGeometry &geometry)
{
    if (!IsValidGeometry(geometry)) {
        return QRectF();
    }

    return QRectF(geometry.center.GetX() - geometry.radius,
                  geometry.center.GetY() - geometry.radius,
                  geometry.radius * 2.0,
                  geometry.radius * 2.0);
}

QRectF GetGeometryBounds(const StruPolylineGeometry &geometry)
{
    if (!IsValidGeometry(geometry)) {
        return QRectF();
    }

    qreal left = geometry.vertices.first().GetX();
    qreal right = left;
    qreal top = geometry.vertices.first().GetY();
    qreal bottom = top;
    for (int index = 1; index < geometry.vertices.size(); ++index) {
        IncludePoint(&left, &right, &top, &bottom, geometry.vertices.at(index));
    }

    return QRectF(left, top, right - left, bottom - top);
}

QVector<CanvasVector> TessellateArc(const StruArcGeometry &geometry,
                                    qreal maximumError)
{
    QVector<CanvasVector> points;
    if (!IsValidGeometry(geometry) || !IsFinite(maximumError)
            || maximumError <= 0.0) {
        return points;
    }

    const qreal sweep = CalculateDirectedSweep(geometry);
    const int segmentCount = CalculateSegmentCount(sweep,
                                                   geometry.radius,
                                                   maximumError,
                                                   MIN_ARC_SEGMENTS);
    if (segmentCount <= 0) {
        return points;
    }

    points.reserve(segmentCount + 1);
    const bool fullSweep = sweep >= TWO_PI - GEOMETRY_TOLERANCE;
    for (int index = 0; index <= segmentCount; ++index) {
        if (fullSweep && index == segmentCount) {
            points.append(CalculateArcPoint(geometry, geometry.startAngle));
            continue;
        }

        const qreal progress = static_cast<qreal>(index)
                / static_cast<qreal>(segmentCount);
        const qreal angle = geometry.reversed
                ? geometry.startAngle - sweep * progress
                : geometry.startAngle + sweep * progress;
        points.append(CalculateArcPoint(geometry, angle));
    }

    return points;
}

QVector<CanvasVector> TessellateCircle(const StruCircleGeometry &geometry,
                                       qreal maximumError)
{
    QVector<CanvasVector> points;
    if (!IsValidGeometry(geometry) || !IsFinite(maximumError)
            || maximumError <= 0.0) {
        return points;
    }

    const int segmentCount = CalculateSegmentCount(TWO_PI,
                                                   geometry.radius,
                                                   maximumError,
                                                   MIN_CIRCLE_SEGMENTS);
    if (segmentCount <= 0) {
        return points;
    }

    points.reserve(segmentCount + 1);
    for (int index = 0; index < segmentCount; ++index) {
        const qreal angle = TWO_PI * static_cast<qreal>(index)
                / static_cast<qreal>(segmentCount);
        points.append(geometry.center
                      + CanvasVector(geometry.radius * std::cos(angle),
                                     geometry.radius * std::sin(angle)));
    }
    points.append(points.first());
    return points;
}

} // namespace mini_graph
