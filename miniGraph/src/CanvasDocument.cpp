#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasGeometry.h"

#include <QtGlobal>

#include <cmath>

namespace mini_graph {

namespace {

bool IsValidCanvasStyle(const StruCanvasStyle &style)
{
    return style.strokeColor.isValid()
            && style.selectedStrokeColor.isValid()
            && std::isfinite(static_cast<double>(style.strokeWidth))
            && style.strokeWidth >= 0.0
            && std::isfinite(static_cast<double>(style.pointSize))
            && style.pointSize > 0.0;
}

bool AreSameArcGeometry(const StruArcGeometry &left,
                        const StruArcGeometry &right)
{
    return left.center == right.center
            && left.radius == right.radius
            && left.startAngle == right.startAngle
            && left.endAngle == right.endAngle
            && left.reversed == right.reversed;
}

bool AreSameCircleGeometry(const StruCircleGeometry &left,
                           const StruCircleGeometry &right)
{
    return left.center == right.center
            && left.radius == right.radius;
}

bool AreSamePolylineGeometry(const StruPolylineGeometry &left,
                             const StruPolylineGeometry &right)
{
    return left.vertices == right.vertices
            && left.closed == right.closed;
}

} // namespace

CanvasDocument::CanvasDocument(QObject *parent)
    : QObject(parent)
    , m_NextId(1)
{
}

ItemId CanvasDocument::AddPoint(const QPointF &position)
{
    StruPointGeometry geometry;
    geometry.position = position;
    return AddPoint(geometry);
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
    line.style.strokeColor = color.isValid() ? color : QColor(Qt::white);
    line.style.strokeWidth = qMax<qreal>(0.0, width);
    line.color = line.style.strokeColor;
    line.width = line.style.strokeWidth;
    m_Lines.append(line);
    m_ItemOrder.append(line.id);
    m_ItemTypes.insert(line.id, CanvasEntityType::Line);
    emit Changed();
    return line.id;
}

ItemId CanvasDocument::AddArc(const QPointF &center,
                              qreal radius,
                              qreal startAngle,
                              qreal endAngle,
                              bool reversed)
{
    StruArcGeometry geometry;
    geometry.center = center;
    geometry.radius = radius;
    geometry.startAngle = startAngle;
    geometry.endAngle = endAngle;
    geometry.reversed = reversed;
    return AddArc(geometry);
}

ItemId CanvasDocument::AddArc(const StruArcGeometry &geometry)
{
    if (!IsValidGeometry(geometry)) {
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

ItemId CanvasDocument::AddCircle(const QPointF &center,
                                 qreal radius)
{
    StruCircleGeometry geometry;
    geometry.center = center;
    geometry.radius = radius;
    return AddCircle(geometry);
}

ItemId CanvasDocument::AddCircle(const StruCircleGeometry &geometry)
{
    if (!IsValidGeometry(geometry)) {
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

ItemId CanvasDocument::AddPolyline(const QVector<QPointF> &vertices,
                                   bool closed)
{
    StruPolylineGeometry geometry;
    geometry.vertices = vertices;
    geometry.closed = closed;
    return AddPolyline(geometry);
}

ItemId CanvasDocument::AddPolyline(const StruPolylineGeometry &geometry)
{
    if (!IsValidGeometry(geometry)) {
        return 0;
    }

    StruPolylinePrimitive polyline;
    polyline.id = m_NextId++;
    polyline.geometry = geometry;
    m_Polylines.append(polyline);
    m_ItemOrder.append(polyline.id);
    m_ItemTypes.insert(polyline.id, CanvasEntityType::Polyline);
    emit Changed();
    return polyline.id;
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
    case CanvasEntityType::Polyline:
        for (int index = 0; index < m_Polylines.size(); ++index) {
            if (m_Polylines.at(index).id == id) {
                m_Polylines.remove(index);
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

bool CanvasDocument::TranslateItems(const QVector<ItemId> &ids,
                                    const QPointF &delta)
{
    if (ids.isEmpty()
            || !std::isfinite(static_cast<double>(delta.x()))
            || !std::isfinite(static_cast<double>(delta.y()))
            || (delta.x() == 0.0 && delta.y() == 0.0)) {
        return false;
    }

    QSet<ItemId> itemIds;
    itemIds.reserve(ids.size());
    for (const ItemId id : ids) {
        if (GetItemType(id) != CanvasEntityType::Invalid) {
            itemIds.insert(id);
        }
    }

    if (itemIds.isEmpty()) {
        return false;
    }

    for (StruPointPrimitive &point : m_Points) {
        if (itemIds.contains(point.id)) {
            point.geometry.position += delta;
        }
    }

    for (StruLinePrimitive &line : m_Lines) {
        if (itemIds.contains(line.id)) {
            line.geometry.startPoint += delta;
            line.geometry.endPoint += delta;
        }
    }

    for (StruArcPrimitive &arc : m_Arcs) {
        if (itemIds.contains(arc.id)) {
            arc.geometry.center += delta;
        }
    }

    for (StruCirclePrimitive &circle : m_Circles) {
        if (itemIds.contains(circle.id)) {
            circle.geometry.center += delta;
        }
    }

    for (StruPolylinePrimitive &polyline : m_Polylines) {
        if (!itemIds.contains(polyline.id)) {
            continue;
        }
        for (QPointF &vertex : polyline.geometry.vertices) {
            vertex += delta;
        }
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
    m_Polylines.clear();
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

const QVector<StruPolylinePrimitive> &CanvasDocument::GetPolylinePrimitives() const
{
    return m_Polylines;
}

QVector<ItemId> CanvasDocument::GetItemIds() const
{
    return m_ItemOrder;
}

CanvasEntityType CanvasDocument::GetItemType(ItemId id) const
{
    return m_ItemTypes.value(id, CanvasEntityType::Invalid);
}

bool CanvasDocument::GetItemStyle(ItemId id, StruCanvasStyle *style) const
{
    if (!style) {
        return false;
    }

    const StruCanvasStyle *storedStyle = FindItemStyle(id);
    if (!storedStyle) {
        return false;
    }

    *style = *storedStyle;
    return true;
}

bool CanvasDocument::SetItemStyle(ItemId id, const StruCanvasStyle &style)
{
    if (!IsValidCanvasStyle(style)) {
        return false;
    }

    StruCanvasStyle *storedStyle = FindItemStyle(id);
    if (!storedStyle || *storedStyle == style) {
        return false;
    }

    *storedStyle = style;
    if (GetItemType(id) == CanvasEntityType::Line) {
        for (StruLinePrimitive &line : m_Lines) {
            if (line.id == id) {
                line.color = style.strokeColor;
                line.width = style.strokeWidth;
                break;
            }
        }
    }

    emit Changed();
    return true;
}

bool CanvasDocument::GetPointGeometry(ItemId id,
                                      StruPointGeometry *geometry) const
{
    if (!geometry || GetItemType(id) != CanvasEntityType::Point) {
        return false;
    }

    for (const StruPointPrimitive &point : m_Points) {
        if (point.id == id) {
            *geometry = point.geometry;
            return true;
        }
    }
    return false;
}

bool CanvasDocument::SetPointGeometry(ItemId id,
                                      const StruPointGeometry &geometry)
{
    if (GetItemType(id) != CanvasEntityType::Point) {
        return false;
    }

    for (StruPointPrimitive &point : m_Points) {
        if (point.id == id) {
            if (point.geometry.position == geometry.position) {
                return false;
            }
            point.geometry = geometry;
            emit Changed();
            return true;
        }
    }
    return false;
}

bool CanvasDocument::GetLineGeometry(ItemId id,
                                     StruLineGeometry *geometry) const
{
    if (!geometry || GetItemType(id) != CanvasEntityType::Line) {
        return false;
    }

    for (const StruLinePrimitive &line : m_Lines) {
        if (line.id == id) {
            *geometry = line.geometry;
            return true;
        }
    }
    return false;
}

bool CanvasDocument::SetLineGeometry(ItemId id,
                                     const StruLineGeometry &geometry)
{
    if (GetItemType(id) != CanvasEntityType::Line) {
        return false;
    }

    for (StruLinePrimitive &line : m_Lines) {
        if (line.id == id) {
            if (line.geometry.startPoint == geometry.startPoint
                    && line.geometry.endPoint == geometry.endPoint) {
                return false;
            }
            line.geometry = geometry;
            emit Changed();
            return true;
        }
    }
    return false;
}

bool CanvasDocument::GetArcGeometry(ItemId id,
                                    StruArcGeometry *geometry) const
{
    if (!geometry || GetItemType(id) != CanvasEntityType::Arc) {
        return false;
    }

    for (const StruArcPrimitive &arc : m_Arcs) {
        if (arc.id == id) {
            *geometry = arc.geometry;
            return true;
        }
    }
    return false;
}

bool CanvasDocument::SetArcGeometry(ItemId id,
                                    const StruArcGeometry &geometry)
{
    if (!IsValidGeometry(geometry)
            || GetItemType(id) != CanvasEntityType::Arc) {
        return false;
    }

    for (StruArcPrimitive &arc : m_Arcs) {
        if (arc.id == id) {
            if (AreSameArcGeometry(arc.geometry, geometry)) {
                return false;
            }
            arc.geometry = geometry;
            emit Changed();
            return true;
        }
    }
    return false;
}

bool CanvasDocument::GetCircleGeometry(ItemId id,
                                       StruCircleGeometry *geometry) const
{
    if (!geometry || GetItemType(id) != CanvasEntityType::Circle) {
        return false;
    }

    for (const StruCirclePrimitive &circle : m_Circles) {
        if (circle.id == id) {
            *geometry = circle.geometry;
            return true;
        }
    }
    return false;
}

bool CanvasDocument::SetCircleGeometry(ItemId id,
                                       const StruCircleGeometry &geometry)
{
    if (!IsValidGeometry(geometry)
            || GetItemType(id) != CanvasEntityType::Circle) {
        return false;
    }

    for (StruCirclePrimitive &circle : m_Circles) {
        if (circle.id == id) {
            if (AreSameCircleGeometry(circle.geometry, geometry)) {
                return false;
            }
            circle.geometry = geometry;
            emit Changed();
            return true;
        }
    }
    return false;
}

bool CanvasDocument::GetPolylineGeometry(ItemId id,
                                         StruPolylineGeometry *geometry) const
{
    if (!geometry || GetItemType(id) != CanvasEntityType::Polyline) {
        return false;
    }

    for (const StruPolylinePrimitive &polyline : m_Polylines) {
        if (polyline.id == id) {
            *geometry = polyline.geometry;
            return true;
        }
    }
    return false;
}

bool CanvasDocument::SetPolylineGeometry(ItemId id,
                                         const StruPolylineGeometry &geometry)
{
    if (!IsValidGeometry(geometry)
            || GetItemType(id) != CanvasEntityType::Polyline) {
        return false;
    }

    for (StruPolylinePrimitive &polyline : m_Polylines) {
        if (polyline.id == id) {
            if (AreSamePolylineGeometry(polyline.geometry, geometry)) {
                return false;
            }
            polyline.geometry = geometry;
            emit Changed();
            return true;
        }
    }
    return false;
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
    return GetBounds(m_ItemOrder);
}

QRectF CanvasDocument::GetBounds(const QVector<ItemId> &ids) const
{
    if (ids.isEmpty()) {
        return QRectF();
    }

    QSet<ItemId> requestedIds;
    requestedIds.reserve(ids.size());
    for (const ItemId id : ids) {
        requestedIds.insert(id);
    }

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
        if (requestedIds.contains(point.id)) {
            mergeBounds(GetGeometryBounds(point.geometry));
        }
    }
    for (const StruLinePrimitive &line : m_Lines) {
        if (requestedIds.contains(line.id)) {
            mergeBounds(GetGeometryBounds(line.geometry));
        }
    }
    for (const StruArcPrimitive &arc : m_Arcs) {
        if (requestedIds.contains(arc.id)) {
            mergeBounds(GetGeometryBounds(arc.geometry));
        }
    }
    for (const StruCirclePrimitive &circle : m_Circles) {
        if (requestedIds.contains(circle.id)) {
            mergeBounds(GetGeometryBounds(circle.geometry));
        }
    }
    for (const StruPolylinePrimitive &polyline : m_Polylines) {
        if (requestedIds.contains(polyline.id)) {
            mergeBounds(GetGeometryBounds(polyline.geometry));
        }
    }

    return hasBounds ? result : QRectF();
}

StruCanvasStyle *CanvasDocument::FindItemStyle(ItemId id)
{
    switch (GetItemType(id)) {
    case CanvasEntityType::Point:
        for (StruPointPrimitive &point : m_Points) {
            if (point.id == id) {
                return &point.style;
            }
        }
        break;
    case CanvasEntityType::Line:
        for (StruLinePrimitive &line : m_Lines) {
            if (line.id == id) {
                return &line.style;
            }
        }
        break;
    case CanvasEntityType::Arc:
        for (StruArcPrimitive &arc : m_Arcs) {
            if (arc.id == id) {
                return &arc.style;
            }
        }
        break;
    case CanvasEntityType::Circle:
        for (StruCirclePrimitive &circle : m_Circles) {
            if (circle.id == id) {
                return &circle.style;
            }
        }
        break;
    case CanvasEntityType::Polyline:
        for (StruPolylinePrimitive &polyline : m_Polylines) {
            if (polyline.id == id) {
                return &polyline.style;
            }
        }
        break;
    case CanvasEntityType::Invalid:
        break;
    }

    return nullptr;
}

const StruCanvasStyle *CanvasDocument::FindItemStyle(ItemId id) const
{
    switch (GetItemType(id)) {
    case CanvasEntityType::Point:
        for (const StruPointPrimitive &point : m_Points) {
            if (point.id == id) {
                return &point.style;
            }
        }
        break;
    case CanvasEntityType::Line:
        for (const StruLinePrimitive &line : m_Lines) {
            if (line.id == id) {
                return &line.style;
            }
        }
        break;
    case CanvasEntityType::Arc:
        for (const StruArcPrimitive &arc : m_Arcs) {
            if (arc.id == id) {
                return &arc.style;
            }
        }
        break;
    case CanvasEntityType::Circle:
        for (const StruCirclePrimitive &circle : m_Circles) {
            if (circle.id == id) {
                return &circle.style;
            }
        }
        break;
    case CanvasEntityType::Polyline:
        for (const StruPolylinePrimitive &polyline : m_Polylines) {
            if (polyline.id == id) {
                return &polyline.style;
            }
        }
        break;
    case CanvasEntityType::Invalid:
        break;
    }

    return nullptr;
}

} // namespace mini_graph
