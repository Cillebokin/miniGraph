#include "MiniGraph/CanvasGeometry.h"
#include "MiniGraph/CanvasEntities.h"
#include "CanvasGeometryPrivate.h"

#include <QtGlobal>

#include <cmath>
#include <limits>

namespace mini_graph
{

namespace
{

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
    if (normalized < 0.0)
    {
        normalized += TWO_PI;
    }
    return normalized;
}

qreal CalculateDirectedSweep(const StruArcGeometry &geometry)
{
    const qreal directedDelta = geometry.reversed
            ? geometry.startAngle - geometry.endAngle
            : geometry.endAngle - geometry.startAngle;
    if (!IsFinite(directedDelta))
    {
        return std::numeric_limits<qreal>::quiet_NaN();
    }

    qreal sweep = std::fmod(directedDelta, TWO_PI);
    if (!IsFinite(sweep))
    {
        return std::numeric_limits<qreal>::quiet_NaN();
    }
    if (sweep < 0.0)
    {
        sweep += TWO_PI;
    }

    if (qAbs(sweep) <= GEOMETRY_TOLERANCE
            && qAbs(directedDelta) > GEOMETRY_TOLERANCE)
    {
        sweep = TWO_PI;
    }

    return sweep;
}

bool IsAngleOnArcInternal(const StruArcGeometry &geometry, qreal angle)
{
    const qreal sweep = CalculateDirectedSweep(geometry);
    if (sweep >= TWO_PI - GEOMETRY_TOLERANCE)
    {
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
            || radius <= 0.0 || maximumError <= 0.0)
    {
        return 0;
    }

    qreal maximumStep = TWO_PI / 4.0;
    if (maximumError < radius)
    {
        const qreal cosineArgument = qBound<qreal>(-1.0,
                                                   1.0 - maximumError / radius,
                                                   1.0);
        maximumStep = 2.0 * std::acos(cosineArgument);
    }

    if (!IsFinite(maximumStep) || maximumStep <= GEOMETRY_TOLERANCE)
    {
        maximumStep = sweep;
    }

    const int calculatedSegments = static_cast<int>(std::ceil(sweep / maximumStep));
    return qBound(minimumSegments,
                  qMax(1, calculatedSegments),
                  MAX_TESSELLATION_SEGMENTS);
}

class SafeBounds
{
public:
    SafeBounds()
        : m_HasValue(false)
        , m_Left(0.0)
        , m_Right(0.0)
        , m_Top(0.0)
        , m_Bottom(0.0)
    {
    }

    bool IncludePoint(qreal x, qreal y)
    {
        if (!IsFinite(x) || !IsFinite(y))
        {
            return false;
        }

        if (!m_HasValue)
        {
            m_Left = x;
            m_Right = x;
            m_Top = y;
            m_Bottom = y;
            m_HasValue = true;
            return true;
        }

        m_Left = qMin(m_Left, x);
        m_Right = qMax(m_Right, x);
        m_Top = qMin(m_Top, y);
        m_Bottom = qMax(m_Bottom, y);
        return true;
    }

    bool IncludePoint(const CanvasVector &point)
    {
        return point.IsValid() && IncludePoint(point.GetX(), point.GetY());
    }

    bool IncludeExtents(qreal left, qreal top, qreal right, qreal bottom)
    {
        return left <= right && top <= bottom
                && IncludePoint(left, top)
                && IncludePoint(right, bottom);
    }

    bool ToRect(QRectF *rect) const
    {
        if (!rect || !m_HasValue)
        {
            return false;
        }

        qreal width = m_Right - m_Left;
        qreal height = m_Bottom - m_Top;
        if (!IsFinite(width) || !IsFinite(height)
                || width < 0.0 || height < 0.0)
        {
            return false;
        }

        const qreal infinity = std::numeric_limits<qreal>::infinity();
        if (m_Left + width < m_Right)
        {
            width = std::nextafter(width, infinity);
        }
        if (m_Top + height < m_Bottom)
        {
            height = std::nextafter(height, infinity);
        }
        if (!IsFinite(width) || !IsFinite(height))
        {
            return false;
        }

        const QRectF candidate(m_Left, m_Top, width, height);
        if (!IsFinite(candidate.right()) || !IsFinite(candidate.bottom())
                || candidate.right() < m_Right
                || candidate.bottom() < m_Bottom)
        {
            return false;
        }

        *rect = candidate;
        return true;
    }

private:
    bool m_HasValue;
    qreal m_Left;
    qreal m_Right;
    qreal m_Top;
    qreal m_Bottom;
};

bool CalculateDirectedSweep(qreal startAngle,
                            qreal endAngle,
                            bool reversed,
                            qreal *sweep)
{
    if (!sweep || !IsFinite(startAngle) || !IsFinite(endAngle))
    {
        return false;
    }

    const qreal normalizedStart = NormalizeAngle(startAngle);
    const qreal normalizedEnd = NormalizeAngle(endAngle);
    const qreal directedDelta = reversed
            ? normalizedStart - normalizedEnd
            : normalizedEnd - normalizedStart;
    *sweep = NormalizeAngle(directedDelta);
    return IsFinite(*sweep);
}

qreal CalculateDirectedProgress(qreal startAngle,
                                qreal angle,
                                bool reversed)
{
    const qreal normalizedStart = NormalizeAngle(startAngle);
    const qreal normalizedAngle = NormalizeAngle(angle);
    const qreal directedDelta = reversed
            ? normalizedStart - normalizedAngle
            : normalizedAngle - normalizedStart;
    return NormalizeAngle(directedDelta);
}

void CalculateSinCos(qreal angle, qreal *sine, qreal *cosine)
{
    const qreal normalizedAngle = NormalizeAngle(angle);
    if (normalizedAngle == 0.0)
    {
        *sine = 0.0;
        *cosine = 1.0;
    }
    else if (normalizedAngle == TWO_PI / 4.0)
    {
        *sine = 1.0;
        *cosine = 0.0;
    }
    else if (normalizedAngle == TWO_PI / 2.0)
    {
        *sine = 0.0;
        *cosine = -1.0;
    }
    else if (normalizedAngle == TWO_PI * 3.0 / 4.0)
    {
        *sine = -1.0;
        *cosine = 0.0;
    }
    else
    {
        *sine = std::sin(angle);
        *cosine = std::cos(angle);
    }
}

bool IsAngleOnDirectedSweep(qreal startAngle,
                            qreal sweep,
                            qreal angle,
                            bool reversed)
{
    if (!IsFinite(angle))
    {
        return false;
    }
    return CalculateDirectedProgress(startAngle, angle, reversed)
            <= sweep + GEOMETRY_TOLERANCE;
}

bool IncludeFullCircle(const CanvasVector &center,
                       qreal radius,
                       SafeBounds *bounds)
{
    if (!bounds || !center.IsValid() || !IsFinite(radius) || radius <= 0.0)
    {
        return false;
    }
    return bounds->IncludeExtents(center.GetX() - radius,
                                  center.GetY() - radius,
                                  center.GetX() + radius,
                                  center.GetY() + radius);
}

bool IncludeArcBounds(const CanvasArc &arc, SafeBounds *bounds)
{
    if (!bounds || !arc.IsValid())
    {
        return false;
    }

    const CanvasVector startPoint = arc.GetStartPoint();
    const CanvasVector endPoint = arc.GetEndPoint();
    if (!bounds->IncludePoint(startPoint) || !bounds->IncludePoint(endPoint))
    {
        return false;
    }

    qreal sweep = 0.0;
    if (!CalculateDirectedSweep(arc.GetStartAngle(), arc.GetEndAngle(),
                                arc.IsReversed(), &sweep))
    {
        return false;
    }

    if (sweep <= GEOMETRY_TOLERANCE
            || TWO_PI - sweep <= GEOMETRY_TOLERANCE)
    {
        return IncludeFullCircle(arc.GetCenter(), arc.GetRadius(), bounds);
    }

    const CanvasVector center = arc.GetCenter();
    const qreal radius = arc.GetRadius();
    const qreal cardinalX[] = {
        center.GetX() + radius,
        center.GetX(),
        center.GetX() - radius,
        center.GetX()
    };
    const qreal cardinalY[] = {
        center.GetY(),
        center.GetY() + radius,
        center.GetY(),
        center.GetY() - radius
    };
    const qreal cardinalAngles[] = {
        0.0,
        TWO_PI / 4.0,
        TWO_PI / 2.0,
        TWO_PI * 3.0 / 4.0
    };
    for (int index = 0; index < 4; ++index)
    {
        const qreal angle = cardinalAngles[index];
        if (!IsAngleOnDirectedSweep(arc.GetStartAngle(), sweep,
                                    angle, arc.IsReversed()))
        {
            continue;
        }

        if (!bounds->IncludePoint(cardinalX[index], cardinalY[index]))
        {
            return false;
        }
    }
    return true;
}

bool CalculateEllipsePoint(const CanvasEllipse &ellipse,
                           qreal parameter,
                           qreal *x,
                           qreal *y)
{
    if (!x || !y || !ellipse.IsValid() || !IsFinite(parameter))
    {
        return false;
    }

    const CanvasVector center = ellipse.GetCenter();
    const CanvasVector majorAxis = ellipse.GetMajorAxis();
    const qreal ratio = ellipse.GetMinorMajorRatio();
    const qreal minorX = -majorAxis.GetY() * ratio;
    const qreal minorY = majorAxis.GetX() * ratio;
    qreal sine = 0.0;
    qreal cosine = 0.0;
    CalculateSinCos(parameter, &sine, &cosine);
    const qreal offsetX = majorAxis.GetX() * cosine + minorX * sine;
    const qreal offsetY = majorAxis.GetY() * cosine + minorY * sine;
    const qreal pointX = center.GetX() + offsetX;
    const qreal pointY = center.GetY() + offsetY;
    if (!IsFinite(minorX) || !IsFinite(minorY)
            || !IsFinite(offsetX) || !IsFinite(offsetY)
            || !IsFinite(pointX) || !IsFinite(pointY))
    {
        return false;
    }

    *x = pointX;
    *y = pointY;
    return true;
}

bool IncludeEllipseExtents(const CanvasEllipse &ellipse,
                           SafeBounds *bounds)
{
    if (!bounds || !ellipse.IsValid())
    {
        return false;
    }

    const CanvasVector center = ellipse.GetCenter();
    const CanvasVector majorAxis = ellipse.GetMajorAxis();
    const qreal ratio = ellipse.GetMinorMajorRatio();
    const qreal xRadius = std::hypot(majorAxis.GetX(),
                                     majorAxis.GetY() * ratio);
    const qreal yRadius = std::hypot(majorAxis.GetY(),
                                     majorAxis.GetX() * ratio);
    if (!IsFinite(xRadius) || !IsFinite(yRadius))
    {
        return false;
    }

    return bounds->IncludeExtents(center.GetX() - xRadius,
                                  center.GetY() - yRadius,
                                  center.GetX() + xRadius,
                                  center.GetY() + yRadius);
}

bool IncludeEllipseCriticalPoint(const CanvasEllipse &ellipse,
                                 qreal parameter,
                                 bool xIsExact,
                                 bool positiveExtent,
                                 qreal exactExtent,
                                 SafeBounds *bounds)
{
    qreal x = 0.0;
    qreal y = 0.0;
    if (!CalculateEllipsePoint(ellipse, parameter, &x, &y))
    {
        return false;
    }

    const qreal centerCoordinate = xIsExact
            ? ellipse.GetCenter().GetX() : ellipse.GetCenter().GetY();
    const qreal exactCoordinate = positiveExtent
            ? centerCoordinate + exactExtent
            : centerCoordinate - exactExtent;
    if (!IsFinite(exactCoordinate))
    {
        return false;
    }
    if (xIsExact)
    {
        x = exactCoordinate;
    }
    else
    {
        y = exactCoordinate;
    }
    return bounds->IncludePoint(x, y);
}

bool IncludeEllipseArcBounds(const CanvasEllipse &ellipse, SafeBounds *bounds)
{
    if (!bounds || !ellipse.IsValid())
    {
        return false;
    }

    if (!ellipse.IsArc())
    {
        return IncludeEllipseExtents(ellipse, bounds);
    }

    qreal sweep = 0.0;
    if (!CalculateDirectedSweep(ellipse.GetStartParameter(),
                                ellipse.GetEndParameter(),
                                ellipse.IsReversed(), &sweep))
    {
        return false;
    }
    if (sweep <= GEOMETRY_TOLERANCE
            || TWO_PI - sweep <= GEOMETRY_TOLERANCE)
    {
        return IncludeEllipseExtents(ellipse, bounds);
    }

    qreal startX = 0.0;
    qreal startY = 0.0;
    qreal endX = 0.0;
    qreal endY = 0.0;
    if (!CalculateEllipsePoint(ellipse, ellipse.GetStartParameter(),
                               &startX, &startY)
            || !CalculateEllipsePoint(ellipse, ellipse.GetEndParameter(),
                                      &endX, &endY)
            || !bounds->IncludePoint(startX, startY)
            || !bounds->IncludePoint(endX, endY))
    {
        return false;
    }

    const CanvasVector majorAxis = ellipse.GetMajorAxis();
    const qreal ratio = ellipse.GetMinorMajorRatio();
    const qreal minorX = -majorAxis.GetY() * ratio;
    const qreal minorY = majorAxis.GetX() * ratio;
    const qreal xRadius = std::hypot(majorAxis.GetX(), minorX);
    const qreal yRadius = std::hypot(majorAxis.GetY(), minorY);
    if (!IsFinite(minorX) || !IsFinite(minorY)
            || !IsFinite(xRadius) || !IsFinite(yRadius))
    {
        return false;
    }

    const qreal xMaximumParameter = std::atan2(minorX,
                                                majorAxis.GetX());
    const qreal yMaximumParameter = std::atan2(minorY,
                                                majorAxis.GetY());
    const qreal candidateParameters[] = {
        xMaximumParameter,
        xMaximumParameter + TWO_PI / 2.0,
        yMaximumParameter,
        yMaximumParameter + TWO_PI / 2.0
    };
    for (int index = 0; index < 4; ++index)
    {
        const qreal parameter = candidateParameters[index];
        if (!IsAngleOnDirectedSweep(ellipse.GetStartParameter(), sweep,
                                    parameter, ellipse.IsReversed()))
        {
            continue;
        }

        const bool xIsExact = index < 2;
        const bool positiveExtent = index == 0 || index == 2;
        if (!IncludeEllipseCriticalPoint(ellipse, parameter, xIsExact,
                                         positiveExtent,
                                         xIsExact ? xRadius : yRadius,
                                         bounds))
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool detail::IsValidArcSweep(qreal startAngle, qreal endAngle)
{
    if (!IsFinite(startAngle) || !IsFinite(endAngle))
    {
        return false;
    }

    const qreal angleDifference = endAngle - startAngle;
    if (!IsFinite(angleDifference))
    {
        return false;
    }

    const qreal normalizedDifference = std::remainder(angleDifference, TWO_PI);
    return IsFinite(normalizedDifference)
            && qAbs(normalizedDifference) > GEOMETRY_TOLERANCE;
}

bool detail::EvaluateSplineInSpan(const CanvasSpline &spline,
                                  int knotSpan,
                                  qreal parameter,
                                  CanvasVector *point)
{
    if (!point || !spline.IsValid() || !IsFinite(parameter))
    {
        return false;
    }

    const int degree = spline.GetDegree();
    const QVector<CanvasVector> &controlPoints = spline.GetControlPoints();
    const QVector<qreal> &knots = spline.GetKnots();
    const QVector<qreal> &weights = spline.GetWeights();
    if (knotSpan < degree || knotSpan >= controlPoints.size())
    {
        return false;
    }

    qreal basis[4] = {0.0, 0.0, 0.0, 0.0};
    qreal left[4] = {0.0, 0.0, 0.0, 0.0};
    qreal right[4] = {0.0, 0.0, 0.0, 0.0};
    basis[0] = 1.0;
    for (int order = 1; order <= degree; ++order)
    {
        left[order] = parameter - knots.at(knotSpan + 1 - order);
        right[order] = knots.at(knotSpan + order) - parameter;
        if (!IsFinite(left[order]) || !IsFinite(right[order]))
        {
            return false;
        }

        qreal saved = 0.0;
        for (int index = 0; index < order; ++index)
        {
            const qreal denominator = right[index + 1]
                    + left[order - index];
            if (!IsFinite(denominator))
            {
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
    for (int index = 0; index <= degree; ++index)
    {
        if (!IsFinite(basis[index]) || basis[index] < 0.0)
        {
            return false;
        }
        if (basis[index] == 0.0)
        {
            continue;
        }

        const int controlIndex = knotSpan - degree + index;
        const qreal weight = weights.isEmpty() ? 1.0
                                               : weights.at(controlIndex);
        if (!IsFinite(weight) || weight <= 0.0)
        {
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

    if (maximumExponent == std::numeric_limits<int>::min())
    {
        return false;
    }

    qreal normalizedTerms[4] = {0.0, 0.0, 0.0, 0.0};
    qreal normalizedWeightSum = 0.0;
    for (int index = 0; index <= degree; ++index)
    {
        if (termMantissas[index] == 0.0)
        {
            continue;
        }
        normalizedTerms[index] = std::ldexp(
                    termMantissas[index],
                    termExponents[index] - maximumExponent);
        normalizedWeightSum += normalizedTerms[index];
    }
    if (!IsFinite(normalizedWeightSum) || normalizedWeightSum <= 0.0)
    {
        return false;
    }

    qreal resultX = 0.0;
    qreal resultY = 0.0;
    for (int index = 0; index <= degree; ++index)
    {
        if (normalizedTerms[index] == 0.0)
        {
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
    if (!candidate.IsValid())
    {
        return false;
    }
    *point = candidate;
    return true;
}

bool detail::TryGetSplineEndpoints(const CanvasSpline &spline,
                                   CanvasVector *startPoint,
                                   CanvasVector *endPoint)
{
    if (!startPoint || !endPoint || !spline.IsValid())
    {
        return false;
    }

    const QVector<CanvasVector> &controlPoints = spline.GetControlPoints();
    const QVector<qreal> &knots = spline.GetKnots();
    const int degree = spline.GetDegree();
    const qreal domainStart = knots.at(degree);
    const qreal domainEnd = knots.at(controlPoints.size());
    if (!IsFinite(domainStart) || !IsFinite(domainEnd)
            || domainStart >= domainEnd)
    {
        return false;
    }

    int startSpan = -1;
    int endSpan = -1;
    for (int knotSpan = degree;
         knotSpan < controlPoints.size();
         ++knotSpan)
    {
        if (startSpan < 0 && knots.at(knotSpan) <= domainStart
                && domainStart < knots.at(knotSpan + 1))
        {
            startSpan = knotSpan;
        }
        if (knots.at(knotSpan) < domainEnd
                && domainEnd <= knots.at(knotSpan + 1))
        {
            endSpan = knotSpan;
        }
    }
    if (startSpan < 0 || endSpan < 0)
    {
        return false;
    }

    CanvasVector candidateStart;
    CanvasVector candidateEnd;
    if (!EvaluateSplineInSpan(spline, startSpan, domainStart, &candidateStart)
            || !EvaluateSplineInSpan(spline, endSpan, domainEnd, &candidateEnd))
    {
        return false;
    }
    *startPoint = candidateStart;
    *endPoint = candidateEnd;
    return true;
}

bool IsValidGeometry(const StruArcGeometry &geometry)
{
    if (!geometry.center.IsValid()
            || !IsFinite(geometry.radius)
            || geometry.radius <= 0.0
            || !IsFinite(geometry.startAngle)
            || !IsFinite(geometry.endAngle))
    {
        return false;
    }

    return detail::IsValidArcSweep(geometry.startAngle, geometry.endAngle);
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
    if (geometry.vertices.size() < minimumVertexCount)
    {
        return false;
    }

    for (const CanvasVector &vertex : geometry.vertices)
    {
        if (!vertex.IsValid())
        {
            return false;
        }
    }

    return true;
}

qreal GetArcSweep(const StruArcGeometry &geometry)
{
    if (!IsValidGeometry(geometry))
    {
        return 0.0;
    }

    return CalculateDirectedSweep(geometry);
}

bool IsAngleOnArc(const StruArcGeometry &geometry, qreal angle)
{
    if (!IsValidGeometry(geometry) || !IsFinite(angle))
    {
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
    if (!IsValidGeometry(geometry))
    {
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
    for (const qreal cardinalAngle : cardinalAngles)
    {
        if (IsAngleOnArcInternal(geometry, cardinalAngle))
        {
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
    if (!IsValidGeometry(geometry))
    {
        return QRectF();
    }

    return QRectF(geometry.center.GetX() - geometry.radius,
                  geometry.center.GetY() - geometry.radius,
                  geometry.radius * 2.0,
                  geometry.radius * 2.0);
}

QRectF GetGeometryBounds(const StruPolylineGeometry &geometry)
{
    if (!IsValidGeometry(geometry))
    {
        return QRectF();
    }

    qreal left = geometry.vertices.first().GetX();
    qreal right = left;
    qreal top = geometry.vertices.first().GetY();
    qreal bottom = top;
    for (int index = 1; index < geometry.vertices.size(); ++index)
    {
        IncludePoint(&left, &right, &top, &bottom, geometry.vertices.at(index));
    }

    return QRectF(left, top, right - left, bottom - top);
}

bool TryGetGeometryBounds(const CanvasEntity &entity, QRectF *bounds)
{
    if (!bounds)
    {
        return false;
    }

    SafeBounds accumulatedBounds;
    QVector<const CanvasEntity *> pendingEntities;
    pendingEntities.append(&entity);

    while (!pendingEntities.isEmpty())
    {
        const CanvasEntity *current = pendingEntities.takeLast();
        if (!current)
        {
            return false;
        }

        switch (current->GetType())
        {
        case CanvasEntityType::Point:
        {
            const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(current);
            if (!point || !point->IsValid()
                    || !accumulatedBounds.IncludePoint(point->GetPosition()))
            {
                return false;
            }
            break;
        }
        case CanvasEntityType::Line:
        {
            const CanvasLine *line = dynamic_cast<const CanvasLine *>(current);
            if (!line || !line->IsValid()
                    || !accumulatedBounds.IncludePoint(line->GetStartPoint())
                    || !accumulatedBounds.IncludePoint(line->GetEndPoint()))
            {
                return false;
            }
            break;
        }
        case CanvasEntityType::Arc:
        {
            const CanvasArc *arc = dynamic_cast<const CanvasArc *>(current);
            if (!arc || !IncludeArcBounds(*arc, &accumulatedBounds))
            {
                return false;
            }
            break;
        }
        case CanvasEntityType::Circle:
        {
            const CanvasCircle *circle =
                    dynamic_cast<const CanvasCircle *>(current);
            if (!circle || !circle->IsValid())
            {
                return false;
            }
            const CanvasVector center = circle->GetCenter();
            const qreal radius = circle->GetRadius();
            if (!accumulatedBounds.IncludeExtents(
                        center.GetX() - radius,
                        center.GetY() - radius,
                        center.GetX() + radius,
                        center.GetY() + radius))
            {
                return false;
            }
            break;
        }
        case CanvasEntityType::Ellipse:
        {
            const CanvasEllipse *ellipse =
                    dynamic_cast<const CanvasEllipse *>(current);
            if (!ellipse || !IncludeEllipseArcBounds(*ellipse,
                                                      &accumulatedBounds))
            {
                return false;
            }
            break;
        }
        case CanvasEntityType::Polyline:
        {
            const CanvasPolyline *polyline =
                    dynamic_cast<const CanvasPolyline *>(current);
            if (!polyline || !polyline->IsValid()
                    || polyline->GetSegmentCount() <= 0)
            {
                return false;
            }
            for (int index = 0; index < polyline->GetSegmentCount(); ++index)
            {
                pendingEntities.append(polyline->GetSegment(index));
            }
            break;
        }
        case CanvasEntityType::Spline:
        {
            const CanvasSpline *spline =
                    dynamic_cast<const CanvasSpline *>(current);
            if (!spline || !spline->IsValid()
                    || spline->GetControlPoints().isEmpty())
            {
                return false;
            }
            for (int index = 0;
                 index < spline->GetControlPoints().size(); ++index)
            {
                if (!accumulatedBounds.IncludePoint(
                            spline->GetControlPoints().at(index)))
                {
                    return false;
                }
            }
            break;
        }
        case CanvasEntityType::GroupContainer:
        {
            const CanvasGroupContainer *group =
                    dynamic_cast<const CanvasGroupContainer *>(current);
            if (!group || group->GetChildCount() <= 0)
            {
                return false;
            }
            for (int index = 0; index < group->GetChildCount(); ++index)
            {
                pendingEntities.append(group->GetChild(index));
            }
            break;
        }
        default:
            return false;
        }
    }

    QRectF candidateBounds;
    if (!accumulatedBounds.ToRect(&candidateBounds))
    {
        return false;
    }

    *bounds = candidateBounds;
    return true;
}

QVector<CanvasVector> TessellateArc(const StruArcGeometry &geometry,
                                    qreal maximumError)
{
    QVector<CanvasVector> points;
    if (!IsValidGeometry(geometry) || !IsFinite(maximumError)
            || maximumError <= 0.0)
    {
        return points;
    }

    const qreal sweep = CalculateDirectedSweep(geometry);
    const int segmentCount = CalculateSegmentCount(sweep,
                                                   geometry.radius,
                                                   maximumError,
                                                   MIN_ARC_SEGMENTS);
    if (segmentCount <= 0)
    {
        return points;
    }

    points.reserve(segmentCount + 1);
    const bool fullSweep = sweep >= TWO_PI - GEOMETRY_TOLERANCE;
    for (int index = 0; index <= segmentCount; ++index)
    {
        if (fullSweep && index == segmentCount)
        {
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
            || maximumError <= 0.0)
    {
        return points;
    }

    const int segmentCount = CalculateSegmentCount(TWO_PI,
                                                   geometry.radius,
                                                   maximumError,
                                                   MIN_CIRCLE_SEGMENTS);
    if (segmentCount <= 0)
    {
        return points;
    }

    points.reserve(segmentCount + 1);
    for (int index = 0; index < segmentCount; ++index)
    {
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
