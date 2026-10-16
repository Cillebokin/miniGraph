#include "MiniGraph/CanvasEntities.h"

#include "MiniGraph/CanvasData.h"

#include "CanvasGeometryPrivate.h"

#include <cmath>
#include <limits>

namespace mini_graph
{
namespace
{

const qreal kPolylineConnectionTolerance = 1e-4;

bool IsFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

struct GroupTraversalFrame
{
    const CanvasGroupContainer *group;
    int nextChildIndex;
};

bool HasGroupNestingDepthAtMost(const CanvasEntity &entity, int maximumDepth)
{
    if (entity.GetType() != CanvasEntityType::GroupContainer)
    {
        return true;
    }
    if (maximumDepth < 1)
    {
        return false;
    }

    const CanvasGroupContainer *root =
            dynamic_cast<const CanvasGroupContainer *>(&entity);
    if (!root)
    {
        return false;
    }

    QVector<GroupTraversalFrame> pending;
    pending.append(GroupTraversalFrame{root, 0});
    while (!pending.isEmpty())
    {
        GroupTraversalFrame &frame = pending.last();
        if (frame.nextChildIndex >= frame.group->GetChildCount())
        {
            pending.removeLast();
            continue;
        }

        const CanvasEntity *child =
                frame.group->GetChild(frame.nextChildIndex);
        ++frame.nextChildIndex;
        if (!child)
        {
            return false;
        }
        if (child->GetType() != CanvasEntityType::GroupContainer)
        {
            continue;
        }
        if (pending.size() >= maximumDepth)
        {
            return false;
        }

        const CanvasGroupContainer *nestedGroup =
                dynamic_cast<const CanvasGroupContainer *>(child);
        if (!nestedGroup)
        {
            return false;
        }
        pending.append(GroupTraversalFrame{nestedGroup, 0});
    }
    return true;
}

qreal NormalizeAngle(qreal angle)
{
    const qreal fullTurn = 2.0 * std::acos(-1.0);
    qreal normalized = std::fmod(angle, fullTurn);
    if (normalized < 0.0)
    {
        normalized += fullTurn;
    }
    return normalized == fullTurn ? 0.0 : normalized;
}

bool HasNonzeroSweep(qreal startAngle, qreal endAngle)
{
    if (!IsFinite(startAngle) || !IsFinite(endAngle))
    {
        return false;
    }

    const qreal fullTurn = 2.0 * std::acos(-1.0);
    qreal angleDifference = endAngle - startAngle;
    if (!IsFinite(angleDifference))
    {
        angleDifference = NormalizeAngle(endAngle)
                - NormalizeAngle(startAngle);
    }

    const qreal precisionTolerance =
            4.0 * std::numeric_limits<qreal>::epsilon() * fullTurn;
    return std::abs(std::remainder(angleDifference, fullTurn))
            > precisionTolerance;
}

bool IsValidArcData(const CanvasVector &center, qreal radius,
                    qreal startAngle, qreal endAngle)
{
    if (!center.IsValid() || !IsFinite(radius) || radius <= 0.0
            || !detail::IsValidArcSweep(startAngle, endAngle))
    {
        return false;
    }

    const CanvasVector startPoint = center
            + CanvasVector::FromPolar(radius, startAngle);
    const CanvasVector endPoint = center
            + CanvasVector::FromPolar(radius, endAngle);
    return startPoint.IsValid() && endPoint.IsValid();
}

bool IsValidEllipseData(const CanvasVector &center,
                        const CanvasVector &majorAxis,
                        qreal minorMajorRatio, bool isArc,
                        qreal startParameter, qreal endParameter)
{
    if (!center.IsValid() || !majorAxis.IsValid()
            || !IsFinite(minorMajorRatio)
            || minorMajorRatio <= 0.0 || minorMajorRatio > 1.0)
    {
        return false;
    }

    const qreal majorLength = majorAxis.Length();
    if (!IsFinite(majorLength) || majorLength <= 0.0)
    {
        return false;
    }

    if (!isArc)
    {
        return startParameter == 0.0 && endParameter == 0.0;
    }
    return HasNonzeroSweep(startParameter, endParameter);
}

bool GetSegmentEndpoints(const CanvasEntity *entity,
                         CanvasVector *startPoint,
                         CanvasVector *endPoint)
{
    if (!entity || !startPoint || !endPoint || !entity->IsValid())
    {
        return false;
    }

    if (const CanvasLine *line = dynamic_cast<const CanvasLine *>(entity))
    {
        *startPoint = line->GetStartPoint();
        *endPoint = line->GetEndPoint();
        return true;
    }
    if (const CanvasArc *arc = dynamic_cast<const CanvasArc *>(entity))
    {
        *startPoint = arc->GetStartPoint();
        *endPoint = arc->GetEndPoint();
        return startPoint->IsValid() && endPoint->IsValid();
    }
    return false;
}

bool IsValidPolyline(const QVector<QSharedPointer<CanvasEntity> > &segments,
                     bool closed)
{
    const int minimumCount = closed ? 2 : 1;
    if (segments.size() < minimumCount)
    {
        return false;
    }

    CanvasVector firstStart;
    CanvasVector previousEnd;
    for (int index = 0; index < segments.size(); ++index)
    {
        CanvasVector startPoint;
        CanvasVector endPoint;
        if (!GetSegmentEndpoints(segments.at(index).data(),
                                 &startPoint, &endPoint))
        {
            return false;
        }
        if (index == 0)
        {
            firstStart = startPoint;
        }
        else if (!previousEnd.IsNearTo(startPoint,
                                         kPolylineConnectionTolerance))
        {
            return false;
        }
        previousEnd = endPoint;
    }

    return !closed || previousEnd.IsNearTo(firstStart,
                                           kPolylineConnectionTolerance);
}

bool IsValidSplineData(int degree,
                       const QVector<CanvasVector> &controlPoints,
                       const QVector<qreal> &knots,
                       const QVector<qreal> &weights)
{
    if (degree < 1 || degree > 3
            || controlPoints.size() < degree + 1
            || knots.size() != controlPoints.size() + degree + 1)
    {
        return false;
    }

    for (int index = 0; index < controlPoints.size(); ++index)
    {
        if (!controlPoints.at(index).IsValid())
        {
            return false;
        }
    }

    int knotMultiplicity = 0;
    for (int index = 0; index < knots.size(); ++index)
    {
        if (!IsFinite(knots.at(index))
                || (index > 0 && knots.at(index) < knots.at(index - 1)))
        {
            return false;
        }
        knotMultiplicity = index > 0
                && knots.at(index) == knots.at(index - 1)
                ? knotMultiplicity + 1 : 1;
        if (knotMultiplicity > degree + 1)
        {
            return false;
        }
    }
    if (knots.at(degree) >= knots.at(controlPoints.size()))
    {
        return false;
    }

    if (!weights.isEmpty() && weights.size() != controlPoints.size())
    {
        return false;
    }
    for (int index = 0; index < weights.size(); ++index)
    {
        if (!IsFinite(weights.at(index)) || weights.at(index) <= 0.0)
        {
            return false;
        }
    }
    return true;
}

} // namespace

CanvasEntity::CanvasEntity()
    : m_Id(0)
    , m_HasExplicitStyle(false)
    , m_BusinessId(0)
    , m_Reversed(false)
{
}

CanvasEntity::~CanvasEntity()
{
    if (m_CanvasData && m_CanvasData->m_BoundEntity == this)
    {
        m_CanvasData->m_BoundEntity = nullptr;
        m_CanvasData->Update(nullptr);
    }
}

ItemId CanvasEntity::GetId() const
{
    return m_Id;
}

const StruCanvasStyle &CanvasEntity::GetStyle() const
{
    return m_Style;
}

bool CanvasEntity::HasExplicitStyle() const
{
    return m_HasExplicitStyle;
}

bool CanvasEntity::SetStyle(const StruCanvasStyle &style)
{
    if (!style.IsValid())
    {
        return false;
    }
    m_Style = style;
    m_HasExplicitStyle = true;
    return true;
}

const std::shared_ptr<CanvasData> &CanvasEntity::GetCanvasData() const
{
    return m_CanvasData;
}

void CanvasEntity::SetCanvasData(const std::shared_ptr<CanvasData> &data)
{
    TrySetCanvasData(data);
}

bool CanvasEntity::TrySetCanvasData(const std::shared_ptr<CanvasData> &data)
{
    if (m_CanvasData == data)
    {
        return true;
    }
    if (data && data->m_BoundEntity && data->m_BoundEntity != this)
    {
        return false;
    }
    if (m_CanvasData)
    {
        if (m_CanvasData->m_BoundEntity == this)
        {
            m_CanvasData->m_BoundEntity = nullptr;
        }
        m_CanvasData->Update(nullptr);
    }
    m_CanvasData = data;
    if (m_CanvasData)
    {
        m_CanvasData->m_BoundEntity = this;
        m_CanvasData->Update(this);
    }
    return true;
}

QString CanvasEntity::GetLayerName() const
{
    return m_LayerName;
}

void CanvasEntity::SetLayerName(const QString &name)
{
    m_LayerName = name;
}

QString CanvasEntity::GetTag() const
{
    return m_Tag;
}

void CanvasEntity::SetTag(const QString &tag)
{
    m_Tag = tag;
}

qint64 CanvasEntity::GetBusinessId() const
{
    return m_BusinessId;
}

void CanvasEntity::SetBusinessId(qint64 id)
{
    m_BusinessId = id;
}

bool CanvasEntity::IsReversed() const
{
    return m_Reversed;
}

void CanvasEntity::SetReversed(bool reversed)
{
    m_Reversed = reversed;
}

bool CanvasEntity::CopyCommonPropertiesTo(CanvasEntity *target) const
{
    if (!target)
    {
        return false;
    }
    target->m_Style = m_Style;
    target->m_HasExplicitStyle = m_HasExplicitStyle;
    target->m_LayerName = m_LayerName;
    target->m_Tag = m_Tag;
    target->m_BusinessId = m_BusinessId;
    target->m_Reversed = m_Reversed;
    if (m_CanvasData)
    {
        const std::shared_ptr<CanvasData> clonedData = m_CanvasData->Clone();
        if (!clonedData || clonedData.get() == m_CanvasData.get())
        {
            return false;
        }
        if (!target->TrySetCanvasData(clonedData))
        {
            return false;
        }
    }
    return true;
}

void CanvasEntity::SetId(ItemId id)
{
    m_Id = id;
    if (m_CanvasData)
    {
        m_CanvasData->Update(this);
    }
}

CanvasPoint::CanvasPoint(const CanvasVector &position)
    : m_Position()
{
    SetPosition(position);
}

CanvasVector CanvasPoint::GetPosition() const
{
    return m_Position;
}

bool CanvasPoint::SetPosition(const CanvasVector &position)
{
    if (!position.IsValid())
    {
        return false;
    }
    m_Position = position;
    return true;
}

CanvasEntityType CanvasPoint::GetType() const
{
    return CanvasEntityType::Point;
}

bool CanvasPoint::IsValid() const
{
    return m_Position.IsValid();
}

QSharedPointer<CanvasEntity> CanvasPoint::Clone() const
{
    QSharedPointer<CanvasPoint> clone(new CanvasPoint(m_Position));
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

CanvasLine::CanvasLine(const CanvasVector &startPoint,
                       const CanvasVector &endPoint)
    : m_StartPoint()
    , m_EndPoint()
{
    SetGeometry(startPoint, endPoint);
}

CanvasVector CanvasLine::GetStartPoint() const
{
    return m_StartPoint;
}

CanvasVector CanvasLine::GetEndPoint() const
{
    return m_EndPoint;
}

bool CanvasLine::SetGeometry(const CanvasVector &startPoint,
                             const CanvasVector &endPoint)
{
    if (!startPoint.IsValid() || !endPoint.IsValid())
    {
        return false;
    }
    m_StartPoint = startPoint;
    m_EndPoint = endPoint;
    return true;
}

CanvasEntityType CanvasLine::GetType() const
{
    return CanvasEntityType::Line;
}

bool CanvasLine::IsValid() const
{
    return m_StartPoint.IsValid() && m_EndPoint.IsValid();
}

QSharedPointer<CanvasEntity> CanvasLine::Clone() const
{
    QSharedPointer<CanvasLine> clone(
                new CanvasLine(m_StartPoint, m_EndPoint));
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

CanvasArc::CanvasArc(const CanvasVector &center, qreal radius,
                     qreal startAngle, qreal endAngle, bool reversed)
    : m_Center()
    , m_Radius(0.0)
    , m_StartAngle(0.0)
    , m_EndAngle(0.0)
{
    SetReversed(reversed);
    SetGeometry(center, radius, startAngle, endAngle);
}

CanvasVector CanvasArc::GetCenter() const
{
    return m_Center;
}

qreal CanvasArc::GetRadius() const
{
    return m_Radius;
}

qreal CanvasArc::GetStartAngle() const
{
    return m_StartAngle;
}

qreal CanvasArc::GetEndAngle() const
{
    return m_EndAngle;
}

bool CanvasArc::SetGeometry(const CanvasVector &center, qreal radius,
                            qreal startAngle, qreal endAngle)
{
    if (!IsValidArcData(center, radius, startAngle, endAngle))
    {
        return false;
    }
    m_Center = center;
    m_Radius = radius;
    m_StartAngle = startAngle;
    m_EndAngle = endAngle;
    return true;
}

CanvasVector CanvasArc::GetStartPoint() const
{
    if (!IsValid())
    {
        return CanvasVector();
    }
    return m_Center + CanvasVector::FromPolar(m_Radius, m_StartAngle);
}

CanvasVector CanvasArc::GetEndPoint() const
{
    if (!IsValid())
    {
        return CanvasVector();
    }
    return m_Center + CanvasVector::FromPolar(m_Radius, m_EndAngle);
}

CanvasEntityType CanvasArc::GetType() const
{
    return CanvasEntityType::Arc;
}

bool CanvasArc::IsValid() const
{
    return IsValidArcData(m_Center, m_Radius, m_StartAngle, m_EndAngle);
}

QSharedPointer<CanvasEntity> CanvasArc::Clone() const
{
    QSharedPointer<CanvasArc> clone(new CanvasArc);
    clone->m_Center = m_Center;
    clone->m_Radius = m_Radius;
    clone->m_StartAngle = m_StartAngle;
    clone->m_EndAngle = m_EndAngle;
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

CanvasCircle::CanvasCircle(const CanvasVector &center, qreal radius)
    : m_Center()
    , m_Radius(0.0)
{
    SetGeometry(center, radius);
}

CanvasVector CanvasCircle::GetCenter() const
{
    return m_Center;
}

qreal CanvasCircle::GetRadius() const
{
    return m_Radius;
}

bool CanvasCircle::SetGeometry(const CanvasVector &center, qreal radius)
{
    if (!center.IsValid() || !IsFinite(radius) || radius <= 0.0)
    {
        return false;
    }
    m_Center = center;
    m_Radius = radius;
    return true;
}

CanvasEntityType CanvasCircle::GetType() const
{
    return CanvasEntityType::Circle;
}

bool CanvasCircle::IsValid() const
{
    return m_Center.IsValid() && IsFinite(m_Radius) && m_Radius > 0.0;
}

QSharedPointer<CanvasEntity> CanvasCircle::Clone() const
{
    QSharedPointer<CanvasCircle> clone(new CanvasCircle);
    clone->m_Center = m_Center;
    clone->m_Radius = m_Radius;
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

CanvasEllipse::CanvasEllipse()
    : m_Center()
    , m_MajorAxis()
    , m_MinorMajorRatio(0.0)
    , m_IsArc(false)
    , m_StartParameter(0.0)
    , m_EndParameter(0.0)
{
}

CanvasVector CanvasEllipse::GetCenter() const
{
    return m_Center;
}

CanvasVector CanvasEllipse::GetMajorAxis() const
{
    return m_MajorAxis;
}

qreal CanvasEllipse::GetMinorMajorRatio() const
{
    return m_MinorMajorRatio;
}

bool CanvasEllipse::IsArc() const
{
    return m_IsArc;
}

qreal CanvasEllipse::GetStartParameter() const
{
    return m_StartParameter;
}

qreal CanvasEllipse::GetEndParameter() const
{
    return m_EndParameter;
}

bool CanvasEllipse::SetGeometry(const CanvasVector &center,
                                const CanvasVector &majorAxis,
                                qreal minorMajorRatio, bool isArc,
                                qreal startParameter, qreal endParameter)
{
    if (!isArc)
    {
        startParameter = 0.0;
        endParameter = 0.0;
    }
    if (!IsValidEllipseData(center, majorAxis, minorMajorRatio,
                            isArc, startParameter, endParameter))
    {
        return false;
    }
    m_Center = center;
    m_MajorAxis = majorAxis;
    m_MinorMajorRatio = minorMajorRatio;
    m_IsArc = isArc;
    m_StartParameter = startParameter;
    m_EndParameter = endParameter;
    return true;
}

CanvasEntityType CanvasEllipse::GetType() const
{
    return CanvasEntityType::Ellipse;
}

bool CanvasEllipse::IsValid() const
{
    return IsValidEllipseData(m_Center, m_MajorAxis, m_MinorMajorRatio,
                              m_IsArc, m_StartParameter, m_EndParameter);
}

QSharedPointer<CanvasEntity> CanvasEllipse::Clone() const
{
    QSharedPointer<CanvasEllipse> clone(new CanvasEllipse);
    clone->m_Center = m_Center;
    clone->m_MajorAxis = m_MajorAxis;
    clone->m_MinorMajorRatio = m_MinorMajorRatio;
    clone->m_IsArc = m_IsArc;
    clone->m_StartParameter = m_StartParameter;
    clone->m_EndParameter = m_EndParameter;
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

CanvasPolyline::CanvasPolyline()
    : m_Closed(false)
{
}

int CanvasPolyline::GetSegmentCount() const
{
    return m_Segments.size();
}

const CanvasEntity *CanvasPolyline::GetSegment(int index) const
{
    return index >= 0 && index < m_Segments.size()
            ? m_Segments.at(index).data() : nullptr;
}

bool CanvasPolyline::AddSegment(const CanvasLine &line)
{
    return AddSegmentClone(line);
}

bool CanvasPolyline::AddSegment(const CanvasArc &arc)
{
    return AddSegmentClone(arc);
}

bool CanvasPolyline::AddSegmentClone(const CanvasEntity &entity)
{
    if (!entity.IsValid())
    {
        return false;
    }
    const QSharedPointer<CanvasEntity> clone = entity.Clone();
    if (!clone || !clone->IsValid())
    {
        return false;
    }
    QVector<QSharedPointer<CanvasEntity> > candidate = m_Segments;
    candidate.append(clone);
    if (!IsValidPolyline(candidate, m_Closed))
    {
        return false;
    }
    m_Segments = candidate;
    return true;
}

bool CanvasPolyline::SetSegment(int index, const CanvasLine &line)
{
    return SetSegmentClone(index, line);
}

bool CanvasPolyline::SetSegment(int index, const CanvasArc &arc)
{
    return SetSegmentClone(index, arc);
}

bool CanvasPolyline::SetSegmentClone(int index, const CanvasEntity &entity)
{
    if (index < 0 || index >= m_Segments.size() || !entity.IsValid())
    {
        return false;
    }
    const QSharedPointer<CanvasEntity> clone = entity.Clone();
    if (!clone || !clone->IsValid())
    {
        return false;
    }
    QVector<QSharedPointer<CanvasEntity> > candidate = m_Segments;
    candidate[index] = clone;
    if (!IsValidPolyline(candidate, m_Closed))
    {
        return false;
    }
    m_Segments = candidate;
    return true;
}

bool CanvasPolyline::RemoveSegment(int index)
{
    if (index < 0 || index >= m_Segments.size())
    {
        return false;
    }
    QVector<QSharedPointer<CanvasEntity> > candidate = m_Segments;
    candidate.removeAt(index);
    if (!IsValidPolyline(candidate, m_Closed))
    {
        return false;
    }
    m_Segments = candidate;
    return true;
}

bool CanvasPolyline::IsClosed() const
{
    return m_Closed;
}

bool CanvasPolyline::SetClosed(bool closed)
{
    if (closed == m_Closed)
    {
        return true;
    }
    if (!IsValidPolyline(m_Segments, closed))
    {
        return false;
    }
    m_Closed = closed;
    return true;
}

CanvasEntityType CanvasPolyline::GetType() const
{
    return CanvasEntityType::Polyline;
}

bool CanvasPolyline::IsValid() const
{
    return IsValidPolyline(m_Segments, m_Closed);
}

QSharedPointer<CanvasEntity> CanvasPolyline::Clone() const
{
    QSharedPointer<CanvasPolyline> clone(new CanvasPolyline);
    clone->m_Closed = m_Closed;
    for (int index = 0; index < m_Segments.size(); ++index)
    {
        const QSharedPointer<CanvasEntity> &segment = m_Segments.at(index);
        if (!segment)
        {
            return QSharedPointer<CanvasEntity>();
        }
        const QSharedPointer<CanvasEntity> segmentClone = segment->Clone();
        if (!segmentClone)
        {
            return QSharedPointer<CanvasEntity>();
        }
        clone->m_Segments.append(segmentClone);
    }
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

CanvasSpline::CanvasSpline()
    : m_Degree(0)
    , m_Closed(false)
    , m_Periodic(false)
{
}

int CanvasSpline::GetDegree() const
{
    return m_Degree;
}

const QVector<CanvasVector> &CanvasSpline::GetControlPoints() const
{
    return m_ControlPoints;
}

const QVector<qreal> &CanvasSpline::GetKnots() const
{
    return m_Knots;
}

const QVector<qreal> &CanvasSpline::GetWeights() const
{
    return m_Weights;
}

bool CanvasSpline::IsClosed() const
{
    return m_Closed;
}

bool CanvasSpline::IsPeriodic() const
{
    return m_Periodic;
}

bool CanvasSpline::SetData(int degree,
                           const QVector<CanvasVector> &controlPoints,
                           const QVector<qreal> &knots,
                           const QVector<qreal> &weights,
                           bool closed, bool periodic)
{
    if (!IsValidSplineData(degree, controlPoints, knots, weights))
    {
        return false;
    }
    m_Degree = degree;
    m_ControlPoints = controlPoints;
    m_Knots = knots;
    m_Weights = weights;
    m_Closed = closed;
    m_Periodic = periodic;
    return true;
}

CanvasEntityType CanvasSpline::GetType() const
{
    return CanvasEntityType::Spline;
}

bool CanvasSpline::IsValid() const
{
    return IsValidSplineData(m_Degree, m_ControlPoints,
                             m_Knots, m_Weights);
}

QSharedPointer<CanvasEntity> CanvasSpline::Clone() const
{
    QSharedPointer<CanvasSpline> clone(new CanvasSpline);
    clone->m_Degree = m_Degree;
    clone->m_ControlPoints = m_ControlPoints;
    clone->m_Knots = m_Knots;
    clone->m_Weights = m_Weights;
    clone->m_Closed = m_Closed;
    clone->m_Periodic = m_Periodic;
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

CanvasGroupContainer::CanvasGroupContainer()
    : m_AllowExplode(true)
{
}

int CanvasGroupContainer::GetChildCount() const
{
    return m_Children.size();
}

const CanvasEntity *CanvasGroupContainer::GetChild(int index) const
{
    return index >= 0 && index < m_Children.size()
            ? m_Children.at(index).data() : nullptr;
}

bool CanvasGroupContainer::AddChild(const CanvasEntity &entity)
{
    if (&entity == this
            || !HasGroupNestingDepthAtMost(
                entity, MaximumNestingDepth - 1)
            || !entity.IsValid())
    {
        return false;
    }
    const QSharedPointer<CanvasEntity> clone = entity.Clone();
    if (!clone || !clone->IsValid())
    {
        return false;
    }
    m_Children.append(clone);
    return true;
}

bool CanvasGroupContainer::ReplaceChild(int index,
                                        const CanvasEntity &entity)
{
    if (index < 0 || index >= m_Children.size()
            || &entity == this
            || !HasGroupNestingDepthAtMost(
                entity, MaximumNestingDepth - 1)
            || !entity.IsValid())
    {
        return false;
    }
    const QSharedPointer<CanvasEntity> clone = entity.Clone();
    if (!clone || !clone->IsValid())
    {
        return false;
    }
    m_Children[index] = clone;
    return true;
}

bool CanvasGroupContainer::RemoveChild(int index)
{
    if (index < 0 || index >= m_Children.size())
    {
        return false;
    }
    m_Children.removeAt(index);
    return true;
}

bool CanvasGroupContainer::CanExplode() const
{
    return m_AllowExplode;
}

void CanvasGroupContainer::SetAllowExplode(bool allow)
{
    m_AllowExplode = allow;
}

CanvasEntityType CanvasGroupContainer::GetType() const
{
    return CanvasEntityType::GroupContainer;
}

bool CanvasGroupContainer::IsValid() const
{
    if (m_Children.isEmpty())
    {
        return false;
    }
    for (int index = 0; index < m_Children.size(); ++index)
    {
        if (!m_Children.at(index) || !m_Children.at(index)->IsValid())
        {
            return false;
        }
    }
    return true;
}

QSharedPointer<CanvasEntity> CanvasGroupContainer::Clone() const
{
    QSharedPointer<CanvasGroupContainer> clone(new CanvasGroupContainer);
    clone->m_AllowExplode = m_AllowExplode;
    for (int index = 0; index < m_Children.size(); ++index)
    {
        const QSharedPointer<CanvasEntity> &child = m_Children.at(index);
        if (!child)
        {
            return QSharedPointer<CanvasEntity>();
        }
        const QSharedPointer<CanvasEntity> childClone = child->Clone();
        if (!childClone)
        {
            return QSharedPointer<CanvasEntity>();
        }
        clone->m_Children.append(childClone);
    }
    if (!CopyCommonPropertiesTo(clone.data()))
    {
        return QSharedPointer<CanvasEntity>();
    }
    return clone;
}

} // namespace mini_graph
