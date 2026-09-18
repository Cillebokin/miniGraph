#include "MiniGraph/CanvasDocument.h"

#include <QtGlobal>

#include <cmath>

namespace mini_graph {

namespace {

const qreal GEOMETRY_TOLERANCE = 1.0e-12;
const qreal TWO_PI = 6.28318530717958647692;

qreal NormalizeAngle(qreal angle)
{
    qreal normalized = std::fmod(angle, TWO_PI);
    if (normalized < 0.0) {
        normalized += TWO_PI;
    }
    return normalized;
}

qreal CalculateArcSweep(const StruArcGeometry &geometry)
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

bool IsValidArc(const StruArcGeometry &geometry)
{
    return std::isfinite(static_cast<double>(geometry.radius))
            && geometry.radius > 0.0
            && std::isfinite(static_cast<double>(geometry.startAngle))
            && std::isfinite(static_cast<double>(geometry.endAngle))
            && qAbs(geometry.endAngle - geometry.startAngle)
            > GEOMETRY_TOLERANCE;
}

QPointF GetCirclePoint(const StruArcGeometry &geometry, qreal angle)
{
    return geometry.center
            + QPointF(geometry.radius * std::cos(angle),
                      geometry.radius * std::sin(angle));
}

bool IsAngleOnArc(const StruArcGeometry &geometry, qreal angle)
{
    const qreal sweep = CalculateArcSweep(geometry);
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

QRectF GetPointBounds(const StruPointGeometry &geometry)
{
    return QRectF(geometry.position.x(), geometry.position.y(), 0.0, 0.0);
}

QRectF GetLineBounds(const StruLineGeometry &geometry)
{
    return QRectF(QPointF(qMin(geometry.startPoint.x(), geometry.endPoint.x()),
                          qMin(geometry.startPoint.y(), geometry.endPoint.y())),
                  QPointF(qMax(geometry.startPoint.x(), geometry.endPoint.x()),
                          qMax(geometry.startPoint.y(), geometry.endPoint.y())));
}

QRectF GetArcBounds(const StruArcGeometry &geometry)
{
    const QPointF startPoint = GetCirclePoint(geometry, geometry.startAngle);
    const QPointF endPoint = GetCirclePoint(geometry, geometry.endAngle);
    qreal left = qMin(startPoint.x(), endPoint.x());
    qreal right = qMax(startPoint.x(), endPoint.x());
    qreal top = qMin(startPoint.y(), endPoint.y());
    qreal bottom = qMax(startPoint.y(), endPoint.y());

    const auto includePoint = [&left, &right, &top, &bottom](const QPointF &point) {
        left = qMin(left, point.x());
        right = qMax(right, point.x());
        top = qMin(top, point.y());
        bottom = qMax(bottom, point.y());
    };

    const qreal cardinalAngles[] = {
        0.0,
        TWO_PI / 4.0,
        TWO_PI / 2.0,
        TWO_PI * 3.0 / 4.0
    };
    for (const qreal cardinalAngle : cardinalAngles) {
        if (!IsAngleOnArc(geometry, cardinalAngle)) {
            continue;
        }

        const QPointF cardinalPoint = GetCirclePoint(geometry, cardinalAngle);
        includePoint(cardinalPoint);
    }

    return QRectF(left, top, right - left, bottom - top);
}

QRectF GetCircleBounds(const StruCircleGeometry &geometry)
{
    return QRectF(geometry.center.x() - geometry.radius,
                  geometry.center.y() - geometry.radius,
                  geometry.radius * 2.0,
                  geometry.radius * 2.0);
}

} // namespace

CanvasDocument::CanvasDocument(QObject *parent)
    : QObject(parent)
    , m_NextId(1)
{
}

ItemId CanvasDocument::AddPoint(const StruPointGeometry &geometry)
{
    StruPointPrimitive point;
    point.id = m_NextId++;
    point.geometry = geometry;
    m_Points.append(point);
    m_ItemOrder.append(point.id);
    m_ItemTypes.insert(point.id, CanvasEntityType::Point);
    emit Changed();
    return point.id;
}

ItemId CanvasDocument::AddLine(const QPointF &start,
                               const QPointF &end,
                               const QColor &color,
                               qreal width)
{
    StruLineGeometry geometry;
    geometry.startPoint = start;
    geometry.endPoint = end;
    return AddLine(geometry, color, width);
}

ItemId CanvasDocument::AddLine(const StruLineGeometry &geometry,
                               const QColor &color,
                               qreal width)
{
    StruLinePrimitive line;
    line.id = m_NextId++;
    line.geometry = geometry;
    line.color = color;
    line.width = qMax<qreal>(0.0, width);
    m_Lines.append(line);
    m_ItemOrder.append(line.id);
    m_ItemTypes.insert(line.id, CanvasEntityType::Line);
    emit Changed();
    return line.id;
}

ItemId CanvasDocument::AddArc(const StruArcGeometry &geometry)
{
    if (!IsValidArc(geometry)) {
        return 0;
    }

    StruArcPrimitive arc;
    arc.id = m_NextId++;
    arc.geometry = geometry;
    m_Arcs.append(arc);
    m_ItemOrder.append(arc.id);
    m_ItemTypes.insert(arc.id, CanvasEntityType::Arc);
    emit Changed();
    return arc.id;
}

ItemId CanvasDocument::AddCircle(const StruCircleGeometry &geometry)
{
    if (!std::isfinite(static_cast<double>(geometry.radius))
            || geometry.radius <= 0.0) {
        return 0;
    }

    StruCirclePrimitive circle;
    circle.id = m_NextId++;
    circle.geometry = geometry;
    m_Circles.append(circle);
    m_ItemOrder.append(circle.id);
    m_ItemTypes.insert(circle.id, CanvasEntityType::Circle);
    emit Changed();
    return circle.id;
}

bool CanvasDocument::RemoveItem(ItemId id)
{
    const CanvasEntityType type = GetItemType(id);
    if (type == CanvasEntityType::Invalid) {
        return false;
    }

    bool removed = false;
    switch (type) {
    case CanvasEntityType::Point:
        for (int index = 0; index < m_Points.size(); ++index) {
            if (m_Points.at(index).id == id) {
                m_Points.remove(index);
                removed = true;
                break;
            }
        }
        break;
    case CanvasEntityType::Line:
        for (int index = 0; index < m_Lines.size(); ++index) {
            if (m_Lines.at(index).id == id) {
                m_Lines.remove(index);
                removed = true;
                break;
            }
        }
        break;
    case CanvasEntityType::Arc:
        for (int index = 0; index < m_Arcs.size(); ++index) {
            if (m_Arcs.at(index).id == id) {
                m_Arcs.remove(index);
                removed = true;
                break;
            }
        }
        break;
    case CanvasEntityType::Circle:
        for (int index = 0; index < m_Circles.size(); ++index) {
            if (m_Circles.at(index).id == id) {
                m_Circles.remove(index);
                removed = true;
                break;
            }
        }
        break;
    case CanvasEntityType::Invalid:
        break;
    }

    if (!removed) {
        return false;
    }

    m_ItemTypes.remove(id);
    m_ItemOrder.removeAll(id);
    const bool selectionChanged = m_SelectedIds.contains(id);
    m_SelectedIds.remove(id);
    if (selectionChanged) {
        emit SelectionChanged();
    }
    emit Changed();
    return true;
}

void CanvasDocument::Clear()
{
    const bool contentChanged = !m_ItemOrder.isEmpty();
    const bool selectionChanged = !m_SelectedIds.isEmpty();
    if (!contentChanged && !selectionChanged) {
        return;
    }

    m_Points.clear();
    m_Lines.clear();
    m_Arcs.clear();
    m_Circles.clear();
    m_ItemOrder.clear();
    m_ItemTypes.clear();
    m_SelectedIds.clear();
    if (selectionChanged) {
        emit SelectionChanged();
    }
    if (contentChanged) {
        emit Changed();
    }
}

int CanvasDocument::GetItemCount() const
{
    return m_ItemOrder.size();
}

const QVector<StruPointPrimitive> &CanvasDocument::GetPointPrimitives() const
{
    return m_Points;
}

const QVector<StruLinePrimitive> &CanvasDocument::GetLinePrimitives() const
{
    return m_Lines;
}

const QVector<StruArcPrimitive> &CanvasDocument::GetArcPrimitives() const
{
    return m_Arcs;
}

const QVector<StruCirclePrimitive> &CanvasDocument::GetCirclePrimitives() const
{
    return m_Circles;
}

QVector<ItemId> CanvasDocument::GetItemIds() const
{
    return m_ItemOrder;
}

CanvasEntityType CanvasDocument::GetItemType(ItemId id) const
{
    return m_ItemTypes.value(id, CanvasEntityType::Invalid);
}

int CanvasDocument::GetSelectedItemCount() const
{
    return m_SelectedIds.size();
}

QVector<ItemId> CanvasDocument::GetSelectedItemIds() const
{
    QVector<ItemId> selectedIds;
    selectedIds.reserve(m_SelectedIds.size());
    for (const ItemId id : m_ItemOrder) {
        if (m_SelectedIds.contains(id)) {
            selectedIds.append(id);
        }
    }
    return selectedIds;
}

bool CanvasDocument::IsItemSelected(ItemId id) const
{
    return m_SelectedIds.contains(id);
}

void CanvasDocument::SelectItem(ItemId id)
{
    if (GetItemType(id) == CanvasEntityType::Invalid
            || m_SelectedIds.contains(id)) {
        return;
    }

    m_SelectedIds.insert(id);
    emit SelectionChanged();
}

void CanvasDocument::ToggleItemSelection(ItemId id)
{
    if (GetItemType(id) == CanvasEntityType::Invalid) {
        return;
    }

    if (m_SelectedIds.contains(id)) {
        m_SelectedIds.remove(id);
    } else {
        m_SelectedIds.insert(id);
    }
    emit SelectionChanged();
}

void CanvasDocument::SelectItems(const QVector<ItemId> &ids,
                                 bool append,
                                 bool toggle)
{
    QSet<ItemId> validIds;
    validIds.reserve(ids.size());
    for (const ItemId id : ids) {
        if (GetItemType(id) != CanvasEntityType::Invalid) {
            validIds.insert(id);
        }
    }

    QSet<ItemId> nextSelection = append ? m_SelectedIds : QSet<ItemId>();
    if (toggle) {
        for (const ItemId id : validIds) {
            if (nextSelection.contains(id)) {
                nextSelection.remove(id);
            } else {
                nextSelection.insert(id);
            }
        }
    } else {
        nextSelection.unite(validIds);
    }

    if (nextSelection == m_SelectedIds) {
        return;
    }

    m_SelectedIds = nextSelection;
    emit SelectionChanged();
}

void CanvasDocument::ToggleItems(const QVector<ItemId> &ids)
{
    SelectItems(ids, true, true);
}

void CanvasDocument::ClearSelection()
{
    if (m_SelectedIds.isEmpty()) {
        return;
    }

    m_SelectedIds.clear();
    emit SelectionChanged();
}

QRectF CanvasDocument::GetBounds() const
{
    QRectF result;
    bool hasBounds = false;
    const auto mergeBounds = [&result, &hasBounds](const QRectF &bounds) {
        if (!hasBounds) {
            result = bounds;
            hasBounds = true;
        } else {
            const QRectF normalizedResult = result.normalized();
            const QRectF normalizedBounds = bounds.normalized();
            const qreal left = qMin(normalizedResult.left(), normalizedBounds.left());
            const qreal top = qMin(normalizedResult.top(), normalizedBounds.top());
            const qreal right = qMax(normalizedResult.right(), normalizedBounds.right());
            const qreal bottom = qMax(normalizedResult.bottom(), normalizedBounds.bottom());
            result = QRectF(left, top, right - left, bottom - top);
        }
    };

    for (const StruPointPrimitive &point : m_Points) {
        mergeBounds(GetPointBounds(point.geometry));
    }
    for (const StruLinePrimitive &line : m_Lines) {
        mergeBounds(GetLineBounds(line.geometry));
    }
    for (const StruArcPrimitive &arc : m_Arcs) {
        mergeBounds(GetArcBounds(arc.geometry));
    }
    for (const StruCirclePrimitive &circle : m_Circles) {
        mergeBounds(GetCircleBounds(circle.geometry));
    }

    return hasBounds ? result : QRectF();
}

} // namespace mini_graph
