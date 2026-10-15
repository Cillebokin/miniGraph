#include "CanvasPainterRenderer.h"

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasData.h"
#include "MiniGraph/CanvasGeometry.h"
#include "CanvasGeometryPrivate.h"

#include <QBrush>
#include <QColor>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>

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
    if (!detail::EvaluateSplineInSpan(spline, knotSpan,
                                      firstQuarterParameter,
                                      &firstQuarterPoint)
            || !detail::EvaluateSplineInSpan(spline, knotSpan,
                                             middleParameter, &middlePoint)
            || !detail::EvaluateSplineInSpan(spline, knotSpan,
                                             thirdQuarterParameter,
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
        if (!detail::EvaluateSplineInSpan(spline, knotSpan,
                                          startParameter, &startPoint)
                || !detail::EvaluateSplineInSpan(spline, knotSpan,
                                                 endParameter, &endPoint)) {
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

bool ParseCommandReal(const QStringList &arguments, int index, qreal *value)
{
    if (!value || index < 0 || index >= arguments.size()) {
        return false;
    }

    bool ok = false;
    const qreal parsed = arguments.at(index).toDouble(&ok);
    if (!ok || !IsFinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseCommandInteger(const QStringList &arguments, int index, int *value)
{
    if (!value || index < 0 || index >= arguments.size()) {
        return false;
    }

    bool ok = false;
    const int parsed = arguments.at(index).toInt(&ok);
    if (!ok) {
        return false;
    }
    *value = parsed;
    return true;
}

QVector<CanvasVector> ParseCommandPoints(const QStringList &arguments,
                                         int firstValueIndex)
{
    QVector<CanvasVector> points;
    if (firstValueIndex < 0 || firstValueIndex > arguments.size()) {
        return points;
    }

    const int valueCount = arguments.size() - firstValueIndex;
    if (valueCount < 4 || valueCount % 2 != 0) {
        return points;
    }

    points.reserve(valueCount / 2);
    for (int index = firstValueIndex; index < arguments.size(); index += 2) {
        qreal x = 0.0;
        qreal y = 0.0;
        if (!ParseCommandReal(arguments, index, &x)
                || !ParseCommandReal(arguments, index + 1, &y)) {
            return QVector<CanvasVector>();
        }
        points.append(CanvasVector(x, y));
    }
    return points;
}

bool IsSupportedBrushStyle(int style)
{
    return (style >= 0 && style <= 17) || style == 24;
}

void DrawCanvasDataCommand(QPainter &painter,
                           const CanvasRenderContext &context,
                           const QStringList &arguments,
                           qreal maximumWorldError,
                           bool isGlobal)
{
    if (arguments.isEmpty()) {
        return;
    }

    const QString command = arguments.first().toLower();
    if (command == QStringLiteral("pen")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        int style = 0;
        qreal width = 0.0;
        if (arguments.size() < 7
                || !ParseCommandInteger(arguments, 1, &red)
                || !ParseCommandInteger(arguments, 2, &green)
                || !ParseCommandInteger(arguments, 3, &blue)
                || !ParseCommandInteger(arguments, 4, &alpha)
                || !ParseCommandReal(arguments, 5, &width)
                || !ParseCommandInteger(arguments, 6, &style)
                || red < 0 || red > 255 || green < 0 || green > 255
                || blue < 0 || blue > 255 || alpha < 0 || alpha > 255
                || width < 0.0 || style < 0 || style > 6) {
            return;
        }
        QPen pen(QColor(red, green, blue, alpha));
        pen.setWidthF(width);
        pen.setStyle(static_cast<Qt::PenStyle>(style));
        painter.setPen(pen);
        return;
    }

    if (command == QStringLiteral("brush")) {
        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        int style = 0;
        if (arguments.size() < 6
                || !ParseCommandInteger(arguments, 1, &red)
                || !ParseCommandInteger(arguments, 2, &green)
                || !ParseCommandInteger(arguments, 3, &blue)
                || !ParseCommandInteger(arguments, 4, &alpha)
                || !ParseCommandInteger(arguments, 5, &style)
                || red < 0 || red > 255 || green < 0 || green > 255
                || blue < 0 || blue > 255 || alpha < 0 || alpha > 255
                || !IsSupportedBrushStyle(style)) {
            return;
        }
        painter.setBrush(QBrush(QColor(red, green, blue, alpha),
                                static_cast<Qt::BrushStyle>(style)));
        return;
    }

    if (command == QStringLiteral("font")) {
        int pointSize = 0;
        int bold = 0;
        int italic = 0;
        if (arguments.size() < 5
                || !ParseCommandInteger(arguments, 2, &pointSize)
                || !ParseCommandInteger(arguments, 3, &bold)
                || !ParseCommandInteger(arguments, 4, &italic)
                || pointSize <= 0) {
            return;
        }
        QFont font;
        font.setFamily(arguments.at(1).simplified());
        font.setPointSize(pointSize);
        font.setBold(bold != 0);
        font.setItalic(italic != 0);
        painter.setFont(font);
        return;
    }

    if (command == QStringLiteral("point")) {
        qreal x = 0.0;
        qreal y = 0.0;
        if (arguments.size() >= 3
                && ParseCommandReal(arguments, 1, &x)
                && ParseCommandReal(arguments, 2, &y)) {
            painter.drawPoint(context.MapFromWorld(CanvasVector(x, y)));
        }
        return;
    }

    if (command == QStringLiteral("line")) {
        qreal x1 = 0.0;
        qreal y1 = 0.0;
        qreal x2 = 0.0;
        qreal y2 = 0.0;
        if (arguments.size() >= 5
                && ParseCommandReal(arguments, 1, &x1)
                && ParseCommandReal(arguments, 2, &y1)
                && ParseCommandReal(arguments, 3, &x2)
                && ParseCommandReal(arguments, 4, &y2)) {
            painter.drawLine(context.MapFromWorld(CanvasVector(x1, y1)),
                             context.MapFromWorld(CanvasVector(x2, y2)));
        }
        return;
    }

    if (command == QStringLiteral("arc")) {
        qreal centerX = 0.0;
        qreal centerY = 0.0;
        qreal radius = 0.0;
        qreal startAngle = 0.0;
        qreal endAngle = 0.0;
        int counterClockwise = 0;
        if (arguments.size() < 7
                || !ParseCommandReal(arguments, 1, &centerX)
                || !ParseCommandReal(arguments, 2, &centerY)
                || !ParseCommandReal(arguments, 3, &radius)
                || !ParseCommandReal(arguments, 4, &startAngle)
                || !ParseCommandReal(arguments, 5, &endAngle)
                || !ParseCommandInteger(arguments, 6, &counterClockwise)
                || radius <= 0.0
                || (counterClockwise != 0 && counterClockwise != 1)) {
            return;
        }
        StruArcGeometry geometry;
        geometry.center = CanvasVector(centerX, centerY);
        geometry.radius = radius;
        geometry.startAngle = startAngle;
        geometry.endAngle = endAngle;
        geometry.reversed = counterClockwise == 0;
        const QVector<CanvasVector> worldPoints = TessellateArc(
                    geometry, maximumWorldError);
        if (worldPoints.size() >= 2) {
            painter.drawPolyline(MapPolyline(worldPoints, context));
        }
        return;
    }

    if (command == QStringLiteral("ellipse")) {
        qreal centerX = 0.0;
        qreal centerY = 0.0;
        qreal radiusX = 0.0;
        qreal radiusY = 0.0;
        if (arguments.size() < 5
                || !ParseCommandReal(arguments, 1, &centerX)
                || !ParseCommandReal(arguments, 2, &centerY)
                || !ParseCommandReal(arguments, 3, &radiusX)
                || !ParseCommandReal(arguments, 4, &radiusY)
                || radiusX <= 0.0 || radiusY <= 0.0) {
            return;
        }
        const CanvasVector center(centerX, centerY);
        const QPointF viewportCenter = context.MapFromWorld(center);
        const QPointF viewportX = context.MapFromWorld(
                    CanvasVector(centerX + radiusX, centerY));
        const QPointF viewportY = context.MapFromWorld(
                    CanvasVector(centerX, centerY + radiusY));
        painter.drawEllipse(viewportCenter,
                            qAbs(viewportX.x() - viewportCenter.x()),
                            qAbs(viewportY.y() - viewportCenter.y()));
        return;
    }

    if (command == QStringLiteral("polyline")
            || command == QStringLiteral("polygon")) {
        const QVector<CanvasVector> worldPoints = ParseCommandPoints(arguments, 1);
        if ((command == QStringLiteral("polyline") && worldPoints.size() >= 2)
                || (command == QStringLiteral("polygon") && worldPoints.size() >= 3)) {
            const QPolygonF viewportPoints = MapPolyline(worldPoints, context);
            if (command == QStringLiteral("polygon")) {
                painter.drawPolygon(viewportPoints);
            } else {
                painter.drawPolyline(viewportPoints);
            }
        }
        return;
    }

    if (command == QStringLiteral("rect")) {
        qreal left = 0.0;
        qreal top = 0.0;
        qreal width = 0.0;
        qreal height = 0.0;
        if (arguments.size() < 5
                || !ParseCommandReal(arguments, 1, &left)
                || !ParseCommandReal(arguments, 2, &top)
                || !ParseCommandReal(arguments, 3, &width)
                || !ParseCommandReal(arguments, 4, &height)) {
            return;
        }
        const QVector<CanvasVector> corners = QVector<CanvasVector>()
                << CanvasVector(left, top)
                << CanvasVector(left + width, top)
                << CanvasVector(left + width, top - height)
                << CanvasVector(left, top - height);
        painter.drawPolygon(MapPolyline(corners, context));
        return;
    }

    if (command == QStringLiteral("cubic")
            || command == QStringLiteral("quad")) {
        int closed = 0;
        if (arguments.size() < 4
                || !ParseCommandInteger(arguments, 1, &closed)
                || (closed != 0 && closed != 1)) {
            return;
        }

        const int valuesPerSegment = command == QStringLiteral("cubic") ? 6 : 4;
        const int coordinateCount = arguments.size() - 4;
        if (coordinateCount < valuesPerSegment
                || coordinateCount % valuesPerSegment != 0) {
            return;
        }

        qreal startX = 0.0;
        qreal startY = 0.0;
        if (!ParseCommandReal(arguments, 2, &startX)
                || !ParseCommandReal(arguments, 3, &startY)) {
            return;
        }
        QPainterPath path(context.MapFromWorld(CanvasVector(startX, startY)));
        for (int index = 4; index < arguments.size();
             index += valuesPerSegment) {
            qreal firstX = 0.0;
            qreal firstY = 0.0;
            qreal secondX = 0.0;
            qreal secondY = 0.0;
            qreal endX = 0.0;
            qreal endY = 0.0;
            if (command == QStringLiteral("cubic")) {
                if (!ParseCommandReal(arguments, index, &firstX)
                        || !ParseCommandReal(arguments, index + 1, &firstY)
                        || !ParseCommandReal(arguments, index + 2, &secondX)
                        || !ParseCommandReal(arguments, index + 3, &secondY)
                        || !ParseCommandReal(arguments, index + 4, &endX)
                        || !ParseCommandReal(arguments, index + 5, &endY)) {
                    return;
                }
                path.cubicTo(context.MapFromWorld(CanvasVector(firstX, firstY)),
                             context.MapFromWorld(CanvasVector(secondX, secondY)),
                             context.MapFromWorld(CanvasVector(endX, endY)));
            } else {
                if (!ParseCommandReal(arguments, index, &firstX)
                        || !ParseCommandReal(arguments, index + 1, &firstY)
                        || !ParseCommandReal(arguments, index + 2, &endX)
                        || !ParseCommandReal(arguments, index + 3, &endY)) {
                    return;
                }
                path.quadTo(context.MapFromWorld(CanvasVector(firstX, firstY)),
                            context.MapFromWorld(CanvasVector(endX, endY)));
            }
        }
        if (closed == 1) {
            path.closeSubpath();
        }
        painter.drawPath(path);
        return;
    }

    if (command == QStringLiteral("text")) {
        qreal x = 0.0;
        qreal y = 0.0;
        qreal radians = 0.0;
        int alignment = 0;
        if (arguments.size() < 6
                || !ParseCommandReal(arguments, 2, &x)
                || !ParseCommandReal(arguments, 3, &y)
                || !ParseCommandInteger(arguments, 4, &alignment)
                || !ParseCommandReal(arguments, 5, &radians)) {
            return;
        }

        const QString text = arguments.at(1);
        const QFontMetricsF metrics(painter.font());
        const qreal descent = metrics.descent();
        const qreal textWidth = metrics.horizontalAdvance(text);
        const qreal textHeight = metrics.height();
        qreal baselineX = 0.0;
        qreal baselineY = 0.0;
        switch (alignment) {
        case 1:
            baselineY = textHeight - descent;
            break;
        case 2:
            baselineY = textHeight - descent;
            baselineX = -textWidth / 2.0;
            break;
        case 3:
            baselineY = textHeight - descent;
            baselineX = -textWidth;
            break;
        case 4:
            baselineY = textHeight / 2.0 - descent;
            break;
        case 5:
            baselineY = textHeight / 2.0 - descent;
            baselineX = -textWidth / 2.0;
            break;
        case 6:
            baselineY = textHeight / 2.0 - descent;
            baselineX = -textWidth;
            break;
        case 7:
            baselineY = -descent;
            break;
        case 8:
            baselineY = -descent;
            baselineX = -textWidth / 2.0;
            break;
        case 9:
            baselineY = -descent;
            baselineX = -textWidth;
            break;
        default:
            break;
        }

        Q_UNUSED(isGlobal)
        painter.save();
        painter.translate(context.MapFromWorld(CanvasVector(x, y)));
        painter.rotate(qRadiansToDegrees(radians));
        painter.drawText(QPointF(baselineX, baselineY), text);
        painter.restore();
        return;
    }

    if (command == QStringLiteral("image")) {
        qreal x = 0.0;
        qreal y = 0.0;
        qreal radians = 0.0;
        qreal scaleX = 1.0;
        qreal scaleY = 1.0;
        int followScale = 0;
        if (arguments.size() < 8
                || !ParseCommandReal(arguments, 2, &x)
                || !ParseCommandReal(arguments, 3, &y)
                || !ParseCommandReal(arguments, 4, &radians)
                || !ParseCommandReal(arguments, 5, &scaleX)
                || !ParseCommandReal(arguments, 6, &scaleY)
                || !ParseCommandInteger(arguments, 7, &followScale)) {
            return;
        }
        if (scaleX <= 0.0) {
            scaleX = 1.0;
        }
        if (scaleY <= 0.0) {
            scaleY = 1.0;
        }
        if (followScale == 1) {
            const qreal zoom = context.transform.GetZoom();
            scaleX *= zoom;
            scaleY *= zoom;
        } else {
            scaleX = 1.0;
            scaleY = 1.0;
        }

        QImageReader reader(arguments.at(1));
        reader.setDecideFormatFromContent(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            return;
        }

        painter.save();
        const QPointF viewportPosition =
                context.MapFromWorld(CanvasVector(x, y));
        const qreal axisX = context.transform.GetFlipX() ? -1.0 : 1.0;
        const qreal axisY = context.transform.GetFlipY() ? 1.0 : -1.0;
        const qreal cosine = std::cos(radians);
        const qreal sine = std::sin(radians);
        const QTransform transform(axisX * cosine * scaleX,
                                   axisY * sine * scaleX,
                                   axisX * sine * scaleY,
                                   -axisY * cosine * scaleY,
                                   viewportPosition.x(),
                                   viewportPosition.y());
        painter.setWorldTransform(transform);
        painter.drawImage(QPointF(0.0, 0.0), image);
        painter.restore();
    }
}

void RenderCanvasData(QPainter &painter,
                      const CanvasRenderContext &context,
                      const CanvasEntity &entity,
                      const StruCanvasStyle &style,
                      bool selected)
{
    const std::shared_ptr<CanvasData> &data = entity.GetCanvasData();
    if (!data) {
        return;
    }

    painter.save();
    painter.setPen(QPen(GetDisplayColor(style, selected),
                        qMax<qreal>(1.0, style.strokeWidth)));
    painter.setBrush(Qt::NoBrush);
    detail::RenderCanvasDataCommands(painter, context,
                                     data->CalculateToQStringList());
    painter.restore();
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
            if (!segment || !segment->GetStyle().visible) {
                continue;
            }
            RenderCanvasData(painter, context, *segment, style, selected);
            DrawEntityGeometry(painter, context, *segment, style,
                               selected, maximumError);
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
        RenderCanvasData(painter, context, *entity, entity->GetStyle(), selected);
        for (int index = 0; index < group->GetChildCount(); ++index) {
            RenderEntity(painter, context, document, group->GetChild(index),
                         selected, maximumError);
        }
        return;
    }

    RenderCanvasData(painter, context, *entity, entity->GetStyle(), selected);
    DrawEntityGeometry(painter, context, *entity, entity->GetStyle(),
                       selected, maximumError);
}

} // namespace

namespace detail {

void RenderCanvasDataCommands(QPainter &painter,
                              const CanvasRenderContext &context,
                              const QStringList &commands,
                              bool isGlobal)
{
    if (commands.isEmpty()) {
        return;
    }

    const qreal zoom = qMax(context.transform.GetZoom(),
                            CanvasTransform::GetMinimumZoom());
    const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const Qt::SplitBehavior splitBehavior = Qt::SkipEmptyParts;
#else
    const QString::SplitBehavior splitBehavior = QString::SkipEmptyParts;
#endif

    painter.save();
    for (const QString &line : commands) {
        const QStringList arguments = line.split(QLatin1Char(','), splitBehavior);
        if (!arguments.isEmpty()) {
            DrawCanvasDataCommand(painter, context, arguments, maximumWorldError,
                                  isGlobal);
        }
    }
    painter.restore();
}

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
