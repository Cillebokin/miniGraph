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

namespace mini_graph
{

namespace
{

const qreal kTwoPi = 6.28318530717958647692;
const qreal kAngleTolerance = 1.0e-12;
const int kMaximumTessellationSegments = 8192;
const int kMinimumSplineSegmentBudget = 8192;
const int kMaximumSplineSubdivisionDepth = 12;

bool IsFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

int CalculateColorDistancePercent(const QColor &first, const QColor &second)
{
    const int red = first.red();
    const int otherRed = second.red();
    const int redMean = (red + otherRed) / 2;
    const qreal redDelta = second.red() - red;
    const qreal greenDelta = second.green() - first.green();
    const qreal blueDelta = second.blue() - first.blue();
    const qreal distance = std::sqrt(
                redDelta * redDelta * (512 + redMean) / 256.0
                + greenDelta * greenDelta * 4.0
                + blueDelta * blueDelta * (767 - redMean) / 256.0);
    return static_cast<int>(std::lround(distance / 7.64834));
}

QColor GetDisplayColor(const StruCanvasStyle &style,
                       bool selected,
                       const QColor &background)
{
    QColor color = style.strokeColor;
    const bool sameAsBackground = color.rgb() == background.rgb();
    const bool isBlack = color.red() == 0
            && color.green() == 0 && color.blue() == 0;

    if (color.alpha() > 0
            && (sameAsBackground
                || (isBlack
                    && CalculateColorDistancePercent(color, background) < 20)))
    {
        const QColor foreground = background.lightness() < 128
                ? QColor(Qt::white) : QColor(Qt::black);
        color.setRgb(foreground.red(), foreground.green(), foreground.blue(),
                     color.alpha());
    }

    // CADLab 在背景对比回退之后应用选中颜色。
    return selected ? style.selectedStrokeColor : color;
}

struct CadLabLineTypePattern
{
    qreal values[6];
    int count;
};

const CadLabLineTypePattern *GetCadLabLineTypePattern(CanvasLineType lineType)
{
    static const CadLabLineTypePattern patterns[] = {
        {{0.2, -6.2, 0.0, 0.0, 0.0, 0.0}, 2},
        {{0.15, -1.0, 0.0, 0.0, 0.0, 0.0}, 2},
        {{0.2, -3.1, 0.0, 0.0, 0.0, 0.0}, 2},
        {{0.2, -12.4, 0.0, 0.0, 0.0, 0.0}, 2},
        {{12.0, -6.0, 0.0, 0.0, 0.0, 0.0}, 2},
        {{2.0, -1.0, 0.0, 0.0, 0.0, 0.0}, 2},
        {{6.0, -3.0, 0.0, 0.0, 0.0, 0.0}, 2},
        {{24.0, -12.0, 0.0, 0.0, 0.0, 0.0}, 2},
        {{12.0, -5.0, 0.2, -5.95, 0.0, 0.0}, 4},
        {{2.0, -0.7, 0.15, -2.0, 0.0, 0.0}, 4},
        {{6.0, -2.0, 0.2, -2.0, 0.0, 0.0}, 4},
        {{24.0, -8.0, 0.2, -8.0, 0.0, 0.0}, 4},
        {{12.0, -4.9, 0.2, -4.9, 0.2, -4.9}, 6},
        {{2.0, -0.7, 0.15, -0.7, 0.15, -0.7}, 6},
        {{6.0, -1.9, 0.2, -1.9, 0.2, -1.9}, 6},
        {{24.0, -8.0, 0.2, -8.0, 0.2, -8.0}, 6},
        {{32.0, -6.0, 6.0, -6.0, 0.0, 0.0}, 4},
        {{5.0, -1.0, 1.0, -1.0, 0.0, 0.0}, 4},
        {{16.0, -3.0, 3.0, -3.0, 0.0, 0.0}, 4},
        {{64.0, -12.0, 12.0, -12.0, 0.0, 0.0}, 4},
        {{12.0, -4.0, 12.0, -4.0, 0.2, -4.0}, 6},
        {{2.0, -1.0, 2.0, -1.0, 0.15, -1.0}, 6},
        {{6.0, -3.0, 6.0, -3.0, 0.2, -3.0}, 6},
        {{24.0, -8.0, 24.0, -8.0, 0.2, -8.0}, 6}
    };

    const int value = static_cast<int>(lineType);
    if (value < static_cast<int>(CanvasLineType::DotLine)
            || value > static_cast<int>(CanvasLineType::BorderLineX2))
    {
        return nullptr;
    }
    return &patterns[value - static_cast<int>(CanvasLineType::DotLine)];
}

qreal GetEffectiveLogicalDpi(qreal logicalDpiX)
{
    return IsFinite(logicalDpiX) && logicalDpiX > 0.0
            ? logicalDpiX : 96.0;
}

qreal GetPenWidth(const StruCanvasStyle &style,
                  qreal logicalDpiX,
                  bool lineWidthVisible)
{
    if (style.strokeWidthUnit == CanvasLineWidthUnit::ViewportPixels)
    {
        return qMax<qreal>(1.0, style.strokeWidth);
    }
    if (!lineWidthVisible || style.strokeWidth <= 0.0)
    {
        return 0.0;
    }

    const qreal widthPixels = style.strokeWidth
            * GetEffectiveLogicalDpi(logicalDpiX) / 25.4;
    if (!IsFinite(widthPixels))
    {
        return 0.0;
    }

    const qreal roundedWidth = std::floor(widthPixels + 0.5);
    // CADLab uses a cosmetic one-pixel pen for rounded width 1.
    return roundedWidth == 1.0 ? 0.0 : roundedWidth;
}

// 按全部输入构建画笔（不做缓存），调用方应使用带缓存的 CreateEntityPen。
QPen BuildEntityPen(const StruCanvasStyle &style,
                    bool selected,
                    qreal logicalDpiX,
                    bool lineWidthVisible,
                    const QColor &backgroundColor,
                    qreal dashOffsetPixels)
{
    const CanvasLineType lineType = selected
            ? CanvasLineType::DashLine2 : style.lineType;
    const QColor color = GetDisplayColor(style, selected, backgroundColor);
    const qreal width = GetPenWidth(style, logicalDpiX, lineWidthVisible);
    QPen pen(color, width, Qt::SolidLine);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    if (lineType == CanvasLineType::NoPen)
    {
        pen.setStyle(Qt::NoPen);
        return pen;
    }
    if (lineType == CanvasLineType::SolidLine)
    {
        return pen;
    }

    const CadLabLineTypePattern *pattern = GetCadLabLineTypePattern(lineType);
    if (!pattern || pattern->count <= 0)
    {
        return pen;
    }

    const qreal pixelsPerMillimeter = GetEffectiveLogicalDpi(logicalDpiX)
            / 25.4;
    const qreal patternPenWidth = width > 0.0 ? width : 1.0;
    QVector<qreal> dashPattern;
    dashPattern.reserve(pattern->count);
    qreal patternLengthPixels = 0.0;
    for (int index = 0; index < pattern->count; ++index)
    {
        const qreal segmentPixels = qMax<qreal>(
                    1.0, std::fabs(pattern->values[index])
                    * pixelsPerMillimeter);
        patternLengthPixels += segmentPixels;
        dashPattern.append(segmentPixels / patternPenWidth);
    }
    pen.setStyle(Qt::CustomDashLine);
    pen.setDashPattern(dashPattern);
    if (IsFinite(dashOffsetPixels) && patternLengthPixels > 0.0)
    {
        qreal normalizedOffset = std::fmod(dashOffsetPixels,
                                           patternLengthPixels);
        if (normalizedOffset < 0.0)
        {
            normalizedOffset += patternLengthPixels;
        }
        pen.setDashOffset(normalizedOffset / patternPenWidth);
    }
    return pen;
}

// 画笔缓存条目：保存一组完整输入及由其构建的画笔。
struct EntityPenCacheEntry
{
    QRgb strokeColor;
    QRgb selectedStrokeColor;
    QRgb backgroundColor;
    int lineType;
    int strokeWidthUnit;
    qreal strokeWidth;
    qreal logicalDpiX;
    qreal dashOffsetPixels;
    bool selected;
    bool lineWidthVisible;
    QPen pen;
};

const int kMaximumEntityPenCacheSize = 64;

// 带缓存的画笔获取：逐图元重复构建画笔（颜色对比度计算、虚线数组分配）是绘制热点。
// 输入完全相同时直接返回缓存副本；条目超过上限时整体清空，保证缓存规模有界。
// 仅在 GUI 线程的绘制路径中使用，无需加锁。
QPen CreateEntityPen(const StruCanvasStyle &style,
                     bool selected,
                     qreal logicalDpiX,
                     bool lineWidthVisible,
                     const QColor &backgroundColor,
                     qreal dashOffsetPixels = 0.0)
{
    static QVector<EntityPenCacheEntry> cache;

    const QRgb strokeColor = style.strokeColor.rgba();
    const QRgb selectedStrokeColor = style.selectedStrokeColor.rgba();
    const QRgb background = backgroundColor.rgba();
    const int lineType = static_cast<int>(style.lineType);
    const int strokeWidthUnit = static_cast<int>(style.strokeWidthUnit);
    const qreal strokeWidth = style.strokeWidth;

    for (const EntityPenCacheEntry &entry : cache)
    {
        if (entry.strokeColor == strokeColor
                && entry.selectedStrokeColor == selectedStrokeColor
                && entry.backgroundColor == background
                && entry.lineType == lineType
                && entry.strokeWidthUnit == strokeWidthUnit
                && entry.strokeWidth == strokeWidth
                && entry.logicalDpiX == logicalDpiX
                && entry.dashOffsetPixels == dashOffsetPixels
                && entry.selected == selected
                && entry.lineWidthVisible == lineWidthVisible)
        {
            return entry.pen;
        }
    }

    const QPen pen = BuildEntityPen(style, selected, logicalDpiX,
                                    lineWidthVisible, backgroundColor,
                                    dashOffsetPixels);
    if (cache.size() >= kMaximumEntityPenCacheSize)
    {
        cache.clear();
    }
    cache.append(EntityPenCacheEntry{strokeColor, selectedStrokeColor, background,
                                     lineType, strokeWidthUnit, strokeWidth,
                                     logicalDpiX, dashOffsetPixels,
                                     selected, lineWidthVisible, pen});
    return pen;
}

QPolygonF MapPolyline(const QVector<CanvasVector> &worldPoints,
                      const CanvasRenderContext &context)
{
    QPolygonF viewportPoints;
    viewportPoints.reserve(worldPoints.size());
    for (const CanvasVector &worldPoint : worldPoints)
    {
        viewportPoints.append(context.MapFromWorld(worldPoint));
    }
    return viewportPoints;
}

void DrawWorldPath(QPainter &painter,
                   const QVector<CanvasVector> &worldPoints,
                   const CanvasRenderContext &context,
                   const StruCanvasStyle &style,
                   bool selected,
                   bool closed,
                   qreal logicalDpiX,
                   bool lineWidthVisible,
                   qreal dashOffsetPixels = 0.0)
{
    if (worldPoints.size() < 2)
    {
        return;
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(CreateEntityPen(style, selected, logicalDpiX,
                                   lineWidthVisible, context.backgroundColor,
                                   dashOffsetPixels));
    const QPolygonF viewportPoints = MapPolyline(worldPoints, context);
    if (closed && viewportPoints.size() >= 3)
    {
        painter.drawPolygon(viewportPoints);
    }
    else
    {
        painter.drawPolyline(viewportPoints);
    }
}

int CalculateSegmentCount(qreal sweep,
                          qreal radius,
                          qreal maximumError,
                          int minimumSegments)
{
    if (!IsFinite(sweep) || !IsFinite(radius) || !IsFinite(maximumError)
            || sweep <= 0.0 || radius <= 0.0 || maximumError <= 0.0)
    {
        return 0;
    }

    qreal maximumStep = kTwoPi / 4.0;
    if (maximumError < radius)
    {
        const qreal cosineArgument = qBound<qreal>(
                    -1.0, 1.0 - maximumError / radius, 1.0);
        maximumStep = 2.0 * std::acos(cosineArgument);
    }
    if (!IsFinite(maximumStep) || maximumStep <= 0.0)
    {
        return kMaximumTessellationSegments;
    }

    const qreal requiredSegments = std::ceil(sweep / maximumStep);
    if (!IsFinite(requiredSegments)
            || requiredSegments >= kMaximumTessellationSegments)
    {
        return kMaximumTessellationSegments;
    }
    return qBound(minimumSegments,
                  qMax(1, static_cast<int>(requiredSegments)),
                  kMaximumTessellationSegments);
}

qreal NormalizeAngle(qreal angle)
{
    qreal normalized = std::fmod(angle, kTwoPi);
    if (normalized < 0.0)
    {
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
    if (!IsFinite(directedDelta))
    {
        const qreal normalizedStart = NormalizeAngle(startAngle);
        const qreal normalizedEnd = NormalizeAngle(endAngle);
        directedDelta = reversed ? normalizedStart - normalizedEnd
                                 : normalizedEnd - normalizedStart;
    }
    if (!IsFinite(directedDelta))
    {
        return 0.0;
    }

    qreal sweep = std::fmod(directedDelta, kTwoPi);
    if (sweep < 0.0)
    {
        sweep += kTwoPi;
    }
    if (qAbs(sweep) <= kAngleTolerance
            && qAbs(directedDelta) > kAngleTolerance)
    {
        return kTwoPi;
    }
    return sweep;
}

QVector<CanvasVector> TessellateEllipse(const CanvasEllipse &ellipse,
                                        qreal maximumError,
                                        bool *closed)
{
    QVector<CanvasVector> points;
    if (closed)
    {
        *closed = false;
    }
    if (!ellipse.IsValid() || !IsFinite(maximumError) || maximumError <= 0.0)
    {
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
    if (majorLength <= 0.0 || sweep <= 0.0)
    {
        return points;
    }

    const bool isClosed = !isArc || sweep >= kTwoPi - kAngleTolerance;
    const int segmentCount = CalculateSegmentCount(
                sweep, majorLength, maximumError, isClosed ? 16 : 2);
    if (segmentCount <= 0)
    {
        return points;
    }

    const qreal ratio = ellipse.GetMinorMajorRatio();
    const qreal minorAxisX = -majorAxis.GetY() * ratio;
    const qreal minorAxisY = majorAxis.GetX() * ratio;
    if (!IsFinite(minorAxisX) || !IsFinite(minorAxisY))
    {
        return points;
    }

    points.reserve(segmentCount + 1);
    const bool reversed = isArc && ellipse.IsReversed();
    for (int index = 0; index <= segmentCount; ++index)
    {
        if (isClosed && index == segmentCount)
        {
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
        if (!point.IsValid())
        {
            points.clear();
            return points;
        }
        points.append(point);
    }

    if (closed)
    {
        *closed = isClosed;
    }
    return points;
}

bool DistanceToSegment(const CanvasVector &point,
                       const CanvasVector &start,
                       const CanvasVector &end,
                       qreal *distance)
{
    if (!distance)
    {
        return false;
    }

    const qreal deltaX = end.GetX() - start.GetX();
    const qreal deltaY = end.GetY() - start.GetY();
    const qreal pointX = point.GetX() - start.GetX();
    const qreal pointY = point.GetY() - start.GetY();
    const qreal lengthSquared = deltaX * deltaX + deltaY * deltaY;
    if (!IsFinite(deltaX) || !IsFinite(deltaY)
            || !IsFinite(pointX) || !IsFinite(pointY)
            || !IsFinite(lengthSquared))
    {
        return false;
    }

    qreal progress = 0.0;
    if (lengthSquared > 0.0)
    {
        progress = (pointX * deltaX + pointY * deltaY) / lengthSquared;
        if (!IsFinite(progress))
        {
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
    if (!points || segmentBudget <= 0)
    {
        return false;
    }

    const qreal parameterDelta = endParameter - startParameter;
    if (!IsFinite(parameterDelta) || parameterDelta <= 0.0)
    {
        return false;
    }
    if (segmentBudget == 1)
    {
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
                                             &thirdQuarterPoint))
    {
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
                                  &thirdDeviation))
    {
        return false;
    }

    const qreal maximumDeviation = qMax(firstDeviation,
                                        qMax(middleDeviation, thirdDeviation));
    if (maximumDeviation <= maximumError
            || depth >= kMaximumSplineSubdivisionDepth)
    {
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
    if (!spline.IsValid() || !IsFinite(maximumError) || maximumError <= 0.0)
    {
        return paths;
    }

    const QVector<CanvasVector> &controlPoints = spline.GetControlPoints();
    const QVector<qreal> &knots = spline.GetKnots();
    const int degree = spline.GetDegree();
    const qreal domainStart = knots.at(degree);
    const qreal domainEnd = knots.at(controlPoints.size());
    if (!IsFinite(domainStart) || !IsFinite(domainEnd)
            || domainStart >= domainEnd)
    {
        return paths;
    }

    int activeSpanCount = 0;
    for (int knotSpan = degree;
         knotSpan < controlPoints.size();
         ++knotSpan)
    {
        if (qMax(domainStart, knots.at(knotSpan))
                < qMin(domainEnd, knots.at(knotSpan + 1)))
        {
            ++activeSpanCount;
        }
    }
    if (activeSpanCount == 0)
    {
        return paths;
    }

    int remainingSegmentBudget = qMax(kMinimumSplineSegmentBudget,
                                      activeSpanCount);
    int remainingSpanCount = activeSpanCount;
    const qreal joinTolerance = qMax<qreal>(1.0e-9, maximumError * 0.01);
    for (int knotSpan = degree;
         knotSpan < controlPoints.size();
         ++knotSpan)
    {
        const qreal startParameter = qMax(domainStart, knots.at(knotSpan));
        const qreal endParameter = qMin(domainEnd, knots.at(knotSpan + 1));
        if (startParameter >= endParameter)
        {
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
                                                 endParameter, &endPoint))
        {
            return QVector<QVector<CanvasVector>>();
        }

        QVector<CanvasVector> spanPoints;
        spanPoints.append(startPoint);
        if (!AppendSplineInterval(spline, knotSpan,
                                  startParameter, endParameter,
                                  startPoint, endPoint, maximumError,
                                  0, spanBudget, &spanPoints))
        {
            return QVector<QVector<CanvasVector>>();
        }
        if (spanPoints.size() < 2)
        {
            continue;
        }

        if (!paths.isEmpty()
                && paths.last().last().IsNearTo(spanPoints.first(),
                                                 joinTolerance))
        {
            for (int index = 1; index < spanPoints.size(); ++index)
            {
                paths.last().append(spanPoints.at(index));
            }
        }
        else
        {
            paths.append(spanPoints);
        }
    }

    if (spline.IsClosed() && !paths.isEmpty())
    {
        QVector<CanvasVector> &lastPath = paths.last();
        const CanvasVector firstPoint = paths.first().first();
        if (!lastPath.last().IsNearTo(firstPoint, joinTolerance))
        {
            lastPath.append(firstPoint);
        }
    }

    return paths;
}

bool ParseCommandReal(const QStringList &arguments, int index, qreal *value)
{
    if (!value || index < 0 || index >= arguments.size())
    {
        return false;
    }

    bool ok = false;
    const qreal parsed = arguments.at(index).toDouble(&ok);
    if (!ok || !IsFinite(parsed))
    {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseCommandInteger(const QStringList &arguments, int index, int *value)
{
    if (!value || index < 0 || index >= arguments.size())
    {
        return false;
    }

    bool ok = false;
    const int parsed = arguments.at(index).toInt(&ok);
    if (!ok)
    {
        return false;
    }
    *value = parsed;
    return true;
}

QVector<CanvasVector> ParseCommandPoints(const QStringList &arguments,
                                         int firstValueIndex)
{
    QVector<CanvasVector> points;
    if (firstValueIndex < 0 || firstValueIndex > arguments.size())
    {
        return points;
    }

    const int valueCount = arguments.size() - firstValueIndex;
    if (valueCount < 4 || valueCount % 2 != 0)
    {
        return points;
    }

    points.reserve(valueCount / 2);
    for (int index = firstValueIndex; index < arguments.size(); index += 2)
    {
        qreal x = 0.0;
        qreal y = 0.0;
        if (!ParseCommandReal(arguments, index, &x)
                || !ParseCommandReal(arguments, index + 1, &y))
        {
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
    if (arguments.isEmpty())
    {
        return;
    }

    const QString command = arguments.first().toLower();
    if (command == QStringLiteral("pen"))
    {
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
                || width < 0.0 || style < 0 || style > 6)
        {
            return;
        }
        QPen pen(QColor(red, green, blue, alpha));
        pen.setWidthF(width);
        pen.setStyle(static_cast<Qt::PenStyle>(style));
        painter.setPen(pen);
        return;
    }

    if (command == QStringLiteral("brush"))
    {
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
                || !IsSupportedBrushStyle(style))
        {
            return;
        }
        painter.setBrush(QBrush(QColor(red, green, blue, alpha),
                                static_cast<Qt::BrushStyle>(style)));
        return;
    }

    if (command == QStringLiteral("font"))
    {
        int pointSize = 0;
        int bold = 0;
        int italic = 0;
        if (arguments.size() < 5
                || !ParseCommandInteger(arguments, 2, &pointSize)
                || !ParseCommandInteger(arguments, 3, &bold)
                || !ParseCommandInteger(arguments, 4, &italic)
                || pointSize <= 0)
        {
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

    if (command == QStringLiteral("point"))
    {
        qreal x = 0.0;
        qreal y = 0.0;
        if (arguments.size() >= 3
                && ParseCommandReal(arguments, 1, &x)
                && ParseCommandReal(arguments, 2, &y))
        {
            painter.drawPoint(context.MapFromWorld(CanvasVector(x, y)));
        }
        return;
    }

    if (command == QStringLiteral("line"))
    {
        qreal x1 = 0.0;
        qreal y1 = 0.0;
        qreal x2 = 0.0;
        qreal y2 = 0.0;
        if (arguments.size() >= 5
                && ParseCommandReal(arguments, 1, &x1)
                && ParseCommandReal(arguments, 2, &y1)
                && ParseCommandReal(arguments, 3, &x2)
                && ParseCommandReal(arguments, 4, &y2))
        {
            painter.drawLine(context.MapFromWorld(CanvasVector(x1, y1)),
                             context.MapFromWorld(CanvasVector(x2, y2)));
        }
        return;
    }

    if (command == QStringLiteral("arc"))
    {
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
                || (counterClockwise != 0 && counterClockwise != 1))
        {
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
        if (worldPoints.size() >= 2)
        {
            painter.drawPolyline(MapPolyline(worldPoints, context));
        }
        return;
    }

    if (command == QStringLiteral("ellipse"))
    {
        qreal centerX = 0.0;
        qreal centerY = 0.0;
        qreal radiusX = 0.0;
        qreal radiusY = 0.0;
        if (arguments.size() < 5
                || !ParseCommandReal(arguments, 1, &centerX)
                || !ParseCommandReal(arguments, 2, &centerY)
                || !ParseCommandReal(arguments, 3, &radiusX)
                || !ParseCommandReal(arguments, 4, &radiusY)
                || radiusX <= 0.0 || radiusY <= 0.0)
        {
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
            || command == QStringLiteral("polygon"))
    {
        const QVector<CanvasVector> worldPoints = ParseCommandPoints(arguments, 1);
        if ((command == QStringLiteral("polyline") && worldPoints.size() >= 2)
                || (command == QStringLiteral("polygon") && worldPoints.size() >= 3))
        {
            const QPolygonF viewportPoints = MapPolyline(worldPoints, context);
            if (command == QStringLiteral("polygon"))
            {
                painter.drawPolygon(viewportPoints);
            }
            else
            {
                painter.drawPolyline(viewportPoints);
            }
        }
        return;
    }

    if (command == QStringLiteral("rect"))
    {
        qreal left = 0.0;
        qreal top = 0.0;
        qreal width = 0.0;
        qreal height = 0.0;
        if (arguments.size() < 5
                || !ParseCommandReal(arguments, 1, &left)
                || !ParseCommandReal(arguments, 2, &top)
                || !ParseCommandReal(arguments, 3, &width)
                || !ParseCommandReal(arguments, 4, &height))
        {
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
            || command == QStringLiteral("quad"))
    {
        int closed = 0;
        if (arguments.size() < 4
                || !ParseCommandInteger(arguments, 1, &closed)
                || (closed != 0 && closed != 1))
        {
            return;
        }

        const int valuesPerSegment = command == QStringLiteral("cubic") ? 6 : 4;
        const int coordinateCount = arguments.size() - 4;
        if (coordinateCount < valuesPerSegment
                || coordinateCount % valuesPerSegment != 0)
        {
            return;
        }

        qreal startX = 0.0;
        qreal startY = 0.0;
        if (!ParseCommandReal(arguments, 2, &startX)
                || !ParseCommandReal(arguments, 3, &startY))
        {
            return;
        }
        QPainterPath path(context.MapFromWorld(CanvasVector(startX, startY)));
        for (int index = 4; index < arguments.size();
             index += valuesPerSegment)
        {
            qreal firstX = 0.0;
            qreal firstY = 0.0;
            qreal secondX = 0.0;
            qreal secondY = 0.0;
            qreal endX = 0.0;
            qreal endY = 0.0;
            if (command == QStringLiteral("cubic"))
            {
                if (!ParseCommandReal(arguments, index, &firstX)
                        || !ParseCommandReal(arguments, index + 1, &firstY)
                        || !ParseCommandReal(arguments, index + 2, &secondX)
                        || !ParseCommandReal(arguments, index + 3, &secondY)
                        || !ParseCommandReal(arguments, index + 4, &endX)
                        || !ParseCommandReal(arguments, index + 5, &endY))
                {
                    return;
                }
                path.cubicTo(context.MapFromWorld(CanvasVector(firstX, firstY)),
                             context.MapFromWorld(CanvasVector(secondX, secondY)),
                             context.MapFromWorld(CanvasVector(endX, endY)));
            }
            else
            {
                if (!ParseCommandReal(arguments, index, &firstX)
                        || !ParseCommandReal(arguments, index + 1, &firstY)
                        || !ParseCommandReal(arguments, index + 2, &endX)
                        || !ParseCommandReal(arguments, index + 3, &endY))
                {
                    return;
                }
                path.quadTo(context.MapFromWorld(CanvasVector(firstX, firstY)),
                            context.MapFromWorld(CanvasVector(endX, endY)));
            }
        }
        if (closed == 1)
        {
            path.closeSubpath();
        }
        painter.drawPath(path);
        return;
    }

    if (command == QStringLiteral("text"))
    {
        qreal x = 0.0;
        qreal y = 0.0;
        qreal radians = 0.0;
        int alignment = 0;
        if (arguments.size() < 6
                || !ParseCommandReal(arguments, 2, &x)
                || !ParseCommandReal(arguments, 3, &y)
                || !ParseCommandInteger(arguments, 4, &alignment)
                || !ParseCommandReal(arguments, 5, &radians))
        {
            return;
        }

        const QString text = arguments.at(1);
        const QFontMetricsF metrics(painter.font());
        const qreal descent = metrics.descent();
        const qreal textWidth = metrics.horizontalAdvance(text);
        const qreal textHeight = metrics.height();
        qreal baselineX = 0.0;
        qreal baselineY = 0.0;
        switch (alignment)
        {
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

    if (command == QStringLiteral("image"))
    {
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
                || !ParseCommandInteger(arguments, 7, &followScale))
        {
            return;
        }
        if (scaleX <= 0.0)
        {
            scaleX = 1.0;
        }
        if (scaleY <= 0.0)
        {
            scaleY = 1.0;
        }
        if (followScale == 1)
        {
            const qreal zoom = context.transform.GetZoom();
            scaleX *= zoom;
            scaleY *= zoom;
        }
        else
        {
            scaleX = 1.0;
            scaleY = 1.0;
        }

        QImageReader reader(arguments.at(1));
        reader.setDecideFormatFromContent(true);
        const QImage image = reader.read();
        if (image.isNull())
        {
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
                      bool selected,
                      qreal logicalDpiX,
                      bool lineWidthVisible,
                      qreal dashOffsetPixels = 0.0)
{
    const std::shared_ptr<CanvasData> &data = entity.GetCanvasData();
    if (!data)
    {
        return;
    }

    painter.save();
    painter.setPen(CreateEntityPen(style, selected, logicalDpiX,
                                   lineWidthVisible, context.backgroundColor,
                                   dashOffsetPixels));
    painter.setBrush(Qt::NoBrush);
    detail::RenderCanvasDataCommands(painter, context,
                                     data->CalculateToQStringList());
    painter.restore();
}

qreal GetPolylineDisplayPathLength(
        const QVector<CanvasVector> &worldPoints,
        const CanvasRenderContext &context)
{
    if (worldPoints.size() < 2)
    {
        return 0.0;
    }

    QPointF previousPoint = context.MapFromWorld(worldPoints.first());
    qreal length = 0.0;
    for (int index = 1; index < worldPoints.size(); ++index)
    {
        const QPointF currentPoint = context.MapFromWorld(worldPoints.at(index));
        const qreal segmentLength = QLineF(previousPoint, currentPoint).length();
        if (!IsFinite(segmentLength) || !IsFinite(length + segmentLength))
        {
            return 0.0;
        }
        length += segmentLength;
        previousPoint = currentPoint;
    }
    return length;
}

qreal GetPolylineSegmentDisplayLength(
        const CanvasEntity &segment,
        const CanvasRenderContext &context,
        qreal maximumError,
        QVector<CanvasVector> *tessellatedArcPoints)
{
    if (const CanvasLine *line =
            dynamic_cast<const CanvasLine *>(&segment))
    {
        const QPointF start = context.MapFromWorld(line->GetStartPoint());
        const QPointF end = context.MapFromWorld(line->GetEndPoint());
        const qreal length = QLineF(start, end).length();
        return IsFinite(length) ? length : 0.0;
    }

    if (const CanvasArc *arc =
            dynamic_cast<const CanvasArc *>(&segment))
    {
        if (!arc->IsValid() || !tessellatedArcPoints)
        {
            return 0.0;
        }

        StruArcGeometry geometry;
        geometry.center = arc->GetCenter();
        geometry.radius = arc->GetRadius();
        geometry.startAngle = arc->GetStartAngle();
        geometry.endAngle = arc->GetEndAngle();
        geometry.reversed = arc->IsReversed();
        *tessellatedArcPoints = TessellateArc(geometry, maximumError);
        return GetPolylineDisplayPathLength(*tessellatedArcPoints, context);
    }

    return 0.0;
}

void DrawEntityGeometry(QPainter &painter,
                        const CanvasRenderContext &context,
                        const CanvasEntity &entity,
                        const StruCanvasStyle &style,
                        bool selected,
                        qreal maximumError,
                        qreal logicalDpiX,
                        bool lineWidthVisible,
                        qreal dashOffsetPixels = 0.0,
                        const QVector<CanvasVector> *preTessellatedArcPoints = nullptr)
{
    switch (entity.GetType())
    {
    case CanvasEntityType::Point:
    {
        const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(&entity);
        if (!point || !point->IsValid())
        {
            return;
        }

        const QPointF viewportPosition = context.MapFromWorld(point->GetPosition());
        // 点标记的像素尺寸固定，缩放过小时它会压过周围的小图元。低于该缩放时，所有点统一只画一个像素。
        constexpr qreal kPointSinglePixelZoom = 0.1;
        if (context.transform.GetZoom() < kPointSinglePixelZoom)
        {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(GetDisplayColor(style, selected,
                                                context.backgroundColor), 0.0));
            painter.drawPoint(viewportPosition);
            return;
        }
        if (style.pointShape == CanvasPointShape::Cross)
        {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(CreateEntityPen(style, selected, logicalDpiX,
                                           lineWidthVisible,
                                           context.backgroundColor));
            const qreal halfSize = qMax<qreal>(1.0, style.pointSize) * 0.5;
            painter.drawLine(viewportPosition + QPointF(-halfSize, 0.0),
                             viewportPosition + QPointF(halfSize, 0.0));
            painter.drawLine(viewportPosition + QPointF(0.0, -halfSize),
                             viewportPosition + QPointF(0.0, halfSize));
            return;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(GetDisplayColor(style, selected,
                                         context.backgroundColor));
        const qreal radius = detail::GetDisplayPointSize(
                    style, logicalDpiX, lineWidthVisible) * 0.5;
        painter.drawEllipse(viewportPosition, radius, radius);
        return;
    }
    case CanvasEntityType::Line:
    {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(&entity);
        if (!line || !line->IsValid())
        {
            return;
        }

        painter.setBrush(Qt::NoBrush);
        painter.setPen(CreateEntityPen(style, selected, logicalDpiX,
                                       lineWidthVisible,
                                       context.backgroundColor,
                                       dashOffsetPixels));
        painter.drawLine(context.MapFromWorld(line->GetStartPoint()),
                         context.MapFromWorld(line->GetEndPoint()));
        return;
    }
    case CanvasEntityType::Arc:
    {
        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(&entity);
        if (!arc || !arc->IsValid())
        {
            return;
        }

        StruArcGeometry geometry;
        geometry.center = arc->GetCenter();
        geometry.radius = arc->GetRadius();
        geometry.startAngle = arc->GetStartAngle();
        geometry.endAngle = arc->GetEndAngle();
        geometry.reversed = arc->IsReversed();
        QVector<CanvasVector> generatedPoints;
        const QVector<CanvasVector> *arcPoints = preTessellatedArcPoints;
        if (!arcPoints)
        {
            generatedPoints = TessellateArc(geometry, maximumError);
            arcPoints = &generatedPoints;
        }
        DrawWorldPath(painter, *arcPoints,
                      context, style, selected, false,
                      logicalDpiX, lineWidthVisible, dashOffsetPixels);
        return;
    }
    case CanvasEntityType::Circle:
    {
        const CanvasCircle *circle = dynamic_cast<const CanvasCircle *>(&entity);
        if (!circle || !circle->IsValid())
        {
            return;
        }

        StruCircleGeometry geometry;
        geometry.center = circle->GetCenter();
        geometry.radius = circle->GetRadius();
        DrawWorldPath(painter, TessellateCircle(geometry, maximumError),
                      context, style, selected, true,
                      logicalDpiX, lineWidthVisible, dashOffsetPixels);
        return;
    }
    case CanvasEntityType::Ellipse:
    {
        const CanvasEllipse *ellipse = dynamic_cast<const CanvasEllipse *>(&entity);
        if (!ellipse)
        {
            return;
        }

        bool closed = false;
        const QVector<CanvasVector> points = TessellateEllipse(
                    *ellipse, maximumError, &closed);
        DrawWorldPath(painter, points, context, style, selected, closed,
                      logicalDpiX, lineWidthVisible, dashOffsetPixels);
        return;
    }
    case CanvasEntityType::Polyline:
    {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        if (!polyline || !polyline->IsValid())
        {
            return;
        }

        qreal dashOffsetPixels = 0.0;
        for (int index = 0; index < polyline->GetSegmentCount(); ++index)
        {
            const CanvasEntity *segment = polyline->GetSegment(index);
            if (!segment)
            {
                continue;
            }
            QVector<CanvasVector> tessellatedArcPoints;
            const qreal segmentLength = GetPolylineSegmentDisplayLength(
                        *segment, context, maximumError,
                        &tessellatedArcPoints);
            if (segment->GetStyle().visible)
            {
                const StruCanvasStyle &segmentStyle =
                        segment->HasExplicitStyle()
                        ? segment->GetStyle() : style;
                RenderCanvasData(painter, context, *segment, segmentStyle,
                                 selected,
                                 logicalDpiX, lineWidthVisible,
                                 dashOffsetPixels);
                DrawEntityGeometry(painter, context, *segment, segmentStyle,
                                   selected, maximumError,
                                   logicalDpiX, lineWidthVisible,
                                   dashOffsetPixels, &tessellatedArcPoints);
            }
            if (IsFinite(segmentLength) && segmentLength > 0.0
                    && IsFinite(dashOffsetPixels + segmentLength))
            {
                dashOffsetPixels += segmentLength;
            }
        }
        return;
    }
    case CanvasEntityType::Spline:
    {
        const CanvasSpline *spline = dynamic_cast<const CanvasSpline *>(&entity);
        if (!spline)
        {
            return;
        }

        const QVector<QVector<CanvasVector>> paths = TessellateSpline(
                    *spline, maximumError);
        for (int index = 0; index < paths.size(); ++index)
        {
            DrawWorldPath(painter, paths.at(index), context, style, selected,
                          false, logicalDpiX, lineWidthVisible);
        }
        return;
    }
    case CanvasEntityType::GroupContainer:
    case CanvasEntityType::Invalid:
        return;
    }
}

// 实线图元在视口中小于 1 像素（包围盒两个方向都小于 1 像素），且画笔不超过 1 像素时，
// 可替换成单像素点；虚线、宽笔画、组容器、自定义数据及不可见图元仍走常规绘制。
bool TryGetSubPixelDot(const CanvasDocument &document,
                       const CanvasEntity &entity,
                       const CanvasRenderContext &context,
                       qreal logicalDpiX,
                       bool lineWidthVisible,
                       QPen *pen,
                       QPointF *position)
{
    if (!pen || !position || entity.GetCanvasData() || !entity.GetStyle().visible)
    {
        return false;
    }
    switch (entity.GetType())
    {
    case CanvasEntityType::GroupContainer:
    case CanvasEntityType::Point:
    case CanvasEntityType::Invalid:
        return false;
    default:
        break;
    }

    QRectF bounds;
    if (!TryGetGeometryBounds(entity, &bounds) || bounds.isEmpty())
    {
        return false;
    }
    const QPointF viewportStart = context.MapFromWorld(
                CanvasVector(bounds.left(), bounds.top()));
    const QPointF viewportEnd = context.MapFromWorld(
                CanvasVector(bounds.right(), bounds.bottom()));
    const QRectF viewportBounds = QRectF(viewportStart, viewportEnd).normalized();
    if (!IsFinite(viewportBounds.width()) || !IsFinite(viewportBounds.height())
            || qMax(viewportBounds.width(), viewportBounds.height()) >= 1.0)
    {
        return false;
    }

    const bool selected = entity.GetId() != 0
            && document.IsItemSelected(entity.GetId());
    const QPen basePen = CreateEntityPen(entity.GetStyle(), selected,
                                         logicalDpiX, lineWidthVisible,
                                         context.backgroundColor);
    if (basePen.style() != Qt::SolidLine
            || basePen.color().alpha() == 0
            || basePen.widthF() > 1.0)
    {
        return false;
    }
    // 宽度 0 为 1 像素的修饰线宽，保证点只占一个像素
    *pen = QPen(basePen.color(), 0.0);
    *position = viewportBounds.center();
    return true;
}

// 判断图元能否参与批量绘制：仅限无自定义数据、实线且不透明的直线与圆弧。
// 能则输出其画笔与已映射到视口的路径，返回 true；否则返回 false，由调用方逐个绘制。
// 虚线、半透明、选中（虚线样式）、文字、点等图元不参与批量，以保持虚线相位与叠加效果不变。
bool TryBuildBatchSegment(const CanvasDocument &document,
                          const CanvasEntity &entity,
                          const CanvasRenderContext &context,
                          qreal maximumWorldError,
                          qreal logicalDpiX,
                          bool lineWidthVisible,
                          QPen *pen,
                          QPainterPath *path)
{
    if (!pen || !path || entity.GetCanvasData() || !entity.GetStyle().visible)
    {
        return false;
    }

    const bool isLine = entity.GetType() == CanvasEntityType::Line;
    const bool isArc = entity.GetType() == CanvasEntityType::Arc;
    if (!isLine && !isArc)
    {
        return false;
    }

    const bool selected = entity.GetId() != 0
            && document.IsItemSelected(entity.GetId());
    const QPen candidate = CreateEntityPen(entity.GetStyle(), selected,
                                           logicalDpiX, lineWidthVisible,
                                           context.backgroundColor);
    if (candidate.style() != Qt::SolidLine || candidate.color().alpha() != 255)
    {
        return false;
    }

    if (isLine)
    {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(&entity);
        if (!line || !line->IsValid())
        {
            return false;
        }
        path->moveTo(context.MapFromWorld(line->GetStartPoint()));
        path->lineTo(context.MapFromWorld(line->GetEndPoint()));
    }
    else
    {
        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(&entity);
        if (!arc || !arc->IsValid())
        {
            return false;
        }
        StruArcGeometry geometry;
        geometry.center = arc->GetCenter();
        geometry.radius = arc->GetRadius();
        geometry.startAngle = arc->GetStartAngle();
        geometry.endAngle = arc->GetEndAngle();
        geometry.reversed = arc->IsReversed();
        const QVector<CanvasVector> points = TessellateArc(geometry, maximumWorldError);
        if (points.size() < 2)
        {
            return false;
        }
        path->moveTo(context.MapFromWorld(points.first()));
        for (int index = 1; index < points.size(); ++index)
        {
            path->lineTo(context.MapFromWorld(points.at(index)));
        }
    }

    *pen = candidate;
    return true;
}

void RenderEntity(QPainter &painter,
                  const CanvasRenderContext &context,
                  const CanvasDocument &document,
                  const CanvasEntity *entity,
                  bool inheritedSelection,
                  qreal maximumError,
                  qreal logicalDpiX,
                  bool lineWidthVisible)
{
    if (!entity || !entity->GetStyle().visible)
    {
        return;
    }

    bool selected = inheritedSelection;
    if (!selected && entity->GetId() != 0)
    {
        selected = document.IsItemSelected(entity->GetId());
    }

    if (entity->GetType() == CanvasEntityType::GroupContainer)
    {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(entity);
        if (!group)
        {
            return;
        }
        RenderCanvasData(painter, context, *entity, entity->GetStyle(), selected,
                         logicalDpiX, lineWidthVisible);
        for (int index = 0; index < group->GetChildCount(); ++index)
        {
            RenderEntity(painter, context, document, group->GetChild(index),
                         selected, maximumError,
                         logicalDpiX, lineWidthVisible);
        }
        return;
    }

    RenderCanvasData(painter, context, *entity, entity->GetStyle(), selected,
                     logicalDpiX, lineWidthVisible);
    DrawEntityGeometry(painter, context, *entity, entity->GetStyle(),
                       selected, maximumError,
                       logicalDpiX, lineWidthVisible);
}

bool HasCanvasDataInTree(const CanvasEntity &entity)
{
    if (entity.GetCanvasData())
    {
        return true;
    }

    if (entity.GetType() == CanvasEntityType::GroupContainer)
    {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(&entity);
        if (!group)
        {
            return false;
        }
        for (int index = 0; index < group->GetChildCount(); ++index)
        {
            const CanvasEntity *child = group->GetChild(index);
            if (child && HasCanvasDataInTree(*child))
            {
                return true;
            }
        }
    }
    else if (entity.GetType() == CanvasEntityType::Polyline)
    {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        if (!polyline)
        {
            return false;
        }
        for (int index = 0; index < polyline->GetSegmentCount(); ++index)
        {
            const CanvasEntity *segment = polyline->GetSegment(index);
            if (segment && segment->GetCanvasData())
            {
                return true;
            }
        }
    }
    return false;
}

qreal GetEntityCullingMarginPixels(const CanvasEntity &entity,
                                  qreal logicalDpiX,
                                  bool lineWidthVisible,
                                  qreal zoom)
{
    const StruCanvasStyle &style = entity.GetStyle();
    switch (entity.GetType())
    {
    case CanvasEntityType::Point:
    {
        const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(&entity);
        if (!point || !point->IsValid())
        {
            return 0.0;
        }
        if (zoom < 0.1)
        {
            return 0.5;
        }
        if (style.pointShape == CanvasPointShape::Cross)
        {
            return qMax<qreal>(1.0, style.pointSize) * 0.5
                    + detail::GetDisplayStrokeWidth(
                          style, logicalDpiX, lineWidthVisible) * 0.5;
        }
        return detail::GetDisplayPointSize(
                    style, logicalDpiX, lineWidthVisible) * 0.5;
    }
    case CanvasEntityType::Polyline:
    {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        if (!polyline)
        {
            return 0.0;
        }
        qreal margin = 0.0;
        for (int index = 0; index < polyline->GetSegmentCount(); ++index)
        {
            const CanvasEntity *segment = polyline->GetSegment(index);
            if (!segment || !segment->GetStyle().visible)
            {
                continue;
            }
            const StruCanvasStyle &segmentStyle = segment->HasExplicitStyle()
                    ? segment->GetStyle() : style;
            if (segmentStyle.visible)
            {
                margin = qMax(margin, detail::GetDisplayStrokeWidth(
                                 segmentStyle, logicalDpiX,
                                 lineWidthVisible) * 0.5);
            }
        }
        return margin;
    }
    case CanvasEntityType::GroupContainer:
    {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(&entity);
        if (!group)
        {
            return 0.0;
        }
        qreal margin = 0.0;
        for (int index = 0; index < group->GetChildCount(); ++index)
        {
            const CanvasEntity *child = group->GetChild(index);
            if (child && child->GetStyle().visible)
            {
                margin = qMax(margin, GetEntityCullingMarginPixels(
                                 *child, logicalDpiX, lineWidthVisible, zoom));
            }
        }
        return margin;
    }
    default:
        return detail::GetDisplayStrokeWidth(
                    style, logicalDpiX, lineWidthVisible) * 0.5;
    }
}

} // namespace

namespace detail
{

void RenderCanvasDataCommands(QPainter &painter,
                              const CanvasRenderContext &context,
                              const QStringList &commands,
                              bool isGlobal)
{
    if (commands.isEmpty())
    {
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
    for (const QString &line : commands)
    {
        const QStringList arguments = line.split(QLatin1Char(','), splitBehavior);
        if (!arguments.isEmpty())
        {
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

qreal GetDisplayStrokeWidth(const StruCanvasStyle &style,
                            qreal logicalDpiX,
                            bool lineWidthVisible)
{
    return qMax<qreal>(1.0, GetPenWidth(style, logicalDpiX,
                                        lineWidthVisible));
}

qreal GetDisplayPointSize(const StruCanvasStyle &style,
                          qreal logicalDpiX,
                          bool lineWidthVisible)
{
    return style.strokeWidthUnit == CanvasLineWidthUnit::Millimeters
            ? GetDisplayStrokeWidth(style, logicalDpiX, lineWidthVisible)
            : qMax<qreal>(1.0, style.pointSize);
}

} // namespace detail

void CanvasPainterRenderer::Render(
        QPainter &painter,
        const CanvasRenderContext &context,
        qreal logicalDpiX,
        bool lineWidthVisible) const
{
    const CanvasDocument *document = context.document;
    if (!document)
    {
        return;
    }

    const qreal zoom = qMax(context.transform.GetZoom(),
                            CanvasTransform::GetMinimumZoom());
    const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 视口裁剪：包围盒与可见范围（按像素外扩，覆盖线宽和点标记）不相交的图元直接跳过。
    // 几何缓存的渲染范围即为视口外扩范围，因此裁剪结果与缓存覆盖范围一致。
    constexpr qreal cullingMarginPixels = 32.0;
    const QRectF visibleWorldRect = context.GetVisibleWorldRect();
    const bool cullingEnabled = !visibleWorldRect.isEmpty();
    // 批量绘制：连续且画笔相同的实线直线与圆弧合并为一个路径，一次 drawPath 绘制。
    // 遇到画笔变化或其他类型图元时先提交当前批次，保证绘制顺序与逐个绘制一致。
    QPainterPath batchPath;
    QPen batchPen;
    bool hasBatch = false;
    const auto flushBatch = [&painter, &batchPath, &batchPen, &hasBatch]()
    {
        if (hasBatch)
        {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(batchPen);
            painter.drawPath(batchPath);
            batchPath = QPainterPath();
            hasBatch = false;
        }
    };

    for (const ItemId id : document->GetItemIds())
    {
        const CanvasEntity *entity = document->GetEntity(id);
        QRectF bounds;
        if (cullingEnabled && entity && TryGetGeometryBounds(*entity, &bounds)
                && !HasCanvasDataInTree(*entity))
        {
            const qreal visualMarginPixels = qMax(
                        cullingMarginPixels,
                        GetEntityCullingMarginPixels(*entity, logicalDpiX,
                                                     lineWidthVisible, zoom));
            const qreal cullingMargin = visualMarginPixels / zoom;
            const QRectF cullingRect = visibleWorldRect.adjusted(
                        -cullingMargin, -cullingMargin,
                        cullingMargin, cullingMargin);
            if (bounds.right() < cullingRect.left()
                    || bounds.left() > cullingRect.right()
                    || bounds.bottom() < cullingRect.top()
                    || bounds.top() > cullingRect.bottom())
            {
                continue;
            }
        }

        // 亚像素图元：在视口中小于 1 像素时只画一个像素，跳过细分和线宽绘制
        QPen dotPen;
        QPointF dotPosition;
        if (entity && TryGetSubPixelDot(*document, *entity, context, logicalDpiX,
                                        lineWidthVisible, &dotPen, &dotPosition))
        {
            flushBatch();
            painter.setPen(dotPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPoint(dotPosition);
            continue;
        }

        QPen segmentPen;
        QPainterPath segmentPath;
        if (entity && TryBuildBatchSegment(*document, *entity, context,
                                           maximumWorldError, logicalDpiX,
                                           lineWidthVisible,
                                           &segmentPen, &segmentPath))
        {
            if (!hasBatch || segmentPen != batchPen)
            {
                flushBatch();
                batchPen = segmentPen;
                hasBatch = true;
            }
            batchPath.addPath(segmentPath);
            continue;
        }

        flushBatch();
        RenderEntity(painter, context, *document, entity,
                     false, maximumWorldError,
                     logicalDpiX, lineWidthVisible);
    }
    flushBatch();
    painter.restore();
}

} // namespace mini_graph
