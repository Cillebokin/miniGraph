#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasData.h"
#include "MiniGraph/CanvasGeometry.h"
#include "MiniGraph/CanvasGlobalData.h"

#include <QtGlobal>

#include <cmath>
#include <limits>

namespace mini_graph {

namespace {

bool IsValidEntityTree(const CanvasEntity &entity)
{
    if (entity.GetType() == CanvasEntityType::Invalid
            || !entity.IsValid()
            || !entity.GetStyle().IsValid()) {
        return false;
    }

    if (entity.GetType() == CanvasEntityType::Polyline) {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        if (!polyline) {
            return false;
        }
        for (int index = 0; index < polyline->GetSegmentCount(); ++index) {
            const CanvasEntity *segment = polyline->GetSegment(index);
            if (!segment || !IsValidEntityTree(*segment)) {
                return false;
            }
        }
    } else if (entity.GetType() == CanvasEntityType::GroupContainer) {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(&entity);
        if (!group) {
            return false;
        }
        for (int index = 0; index < group->GetChildCount(); ++index) {
            const CanvasEntity *child = group->GetChild(index);
            if (!child || !IsValidEntityTree(*child)) {
                return false;
            }
        }
    }

    return true;
}

bool IsUsableDocumentEntity(const CanvasEntity &entity)
{
    if (!IsValidEntityTree(entity)) {
        return false;
    }
    QRectF bounds;
    return TryGetGeometryBounds(entity, &bounds)
            && std::isfinite(static_cast<double>(bounds.x()))
            && std::isfinite(static_cast<double>(bounds.y()))
            && std::isfinite(static_cast<double>(bounds.width()))
            && std::isfinite(static_cast<double>(bounds.height()));
}

bool CopyCommonProperties(const CanvasEntity &source, CanvasEntity *target)
{
    if (!target) {
        return false;
    }
    target->SetStyle(source.GetStyle());
    target->SetLayerName(source.GetLayerName());
    target->SetTag(source.GetTag());
    target->SetBusinessId(source.GetBusinessId());
    target->SetReversed(source.IsReversed());
    const std::shared_ptr<CanvasData> &sourceData = source.GetCanvasData();
    const std::shared_ptr<CanvasData> copiedData =
            sourceData ? sourceData->Clone() : std::shared_ptr<CanvasData>();
    if (sourceData
            && (!copiedData || copiedData.get() == sourceData.get())) {
        return false;
    }
    return target->TrySetCanvasData(copiedData);
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

bool BuildPolylineEntity(const StruPolylineGeometry &geometry,
                         CanvasPolyline *polyline)
{
    if (!polyline || !IsValidGeometry(geometry)) {
        return false;
    }

    const int edgeCount = geometry.closed
            ? geometry.vertices.size() : geometry.vertices.size() - 1;
    for (int index = 0; index < edgeCount; ++index) {
        const CanvasVector &start = geometry.vertices.at(index);
        const CanvasVector &end = geometry.vertices.at(
                    (index + 1) % geometry.vertices.size());
        if (!polyline->AddSegment(CanvasLine(start, end))) {
            return false;
        }
    }
    return polyline->SetClosed(geometry.closed) && polyline->IsValid();
}

bool ExtractPolylineGeometry(const CanvasPolyline &polyline,
                             StruPolylineGeometry *geometry)
{
    if (!geometry || polyline.GetSegmentCount() == 0) {
        return false;
    }

    StruPolylineGeometry candidate;
    candidate.closed = polyline.IsClosed();
    for (int index = 0; index < polyline.GetSegmentCount(); ++index) {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(
                    polyline.GetSegment(index));
        if (!line) {
            return false;
        }
        if (index == 0) {
            candidate.vertices.append(line->GetStartPoint());
        }
        if (!candidate.closed || index + 1 < polyline.GetSegmentCount()) {
            candidate.vertices.append(line->GetEndPoint());
        }
    }
    if (!IsValidGeometry(candidate)) {
        return false;
    }
    *geometry = candidate;
    return true;
}

bool TranslateEntity(const CanvasEntity &source,
                     const CanvasVector &delta,
                     QSharedPointer<CanvasEntity> *translated)
{
    if (!translated || !delta.IsValid() || !IsUsableDocumentEntity(source)) {
        return false;
    }

    QSharedPointer<CanvasEntity> candidate = source.Clone();
    if (!candidate || candidate->GetType() != source.GetType()) {
        return false;
    }

    switch (source.GetType()) {
    case CanvasEntityType::Point: {
        const CanvasPoint *original = dynamic_cast<const CanvasPoint *>(&source);
        CanvasPoint *updated = dynamic_cast<CanvasPoint *>(candidate.data());
        if (!original || !updated
                || !updated->SetPosition(original->GetPosition() + delta)) {
            return false;
        }
        break;
    }
    case CanvasEntityType::Line: {
        const CanvasLine *original = dynamic_cast<const CanvasLine *>(&source);
        CanvasLine *updated = dynamic_cast<CanvasLine *>(candidate.data());
        if (!original || !updated
                || !updated->SetGeometry(original->GetStartPoint() + delta,
                                         original->GetEndPoint() + delta)) {
            return false;
        }
        break;
    }
    case CanvasEntityType::Arc: {
        const CanvasArc *original = dynamic_cast<const CanvasArc *>(&source);
        CanvasArc *updated = dynamic_cast<CanvasArc *>(candidate.data());
        if (!original || !updated
                || !updated->SetGeometry(original->GetCenter() + delta,
                                         original->GetRadius(),
                                         original->GetStartAngle(),
                                         original->GetEndAngle())) {
            return false;
        }
        break;
    }
    case CanvasEntityType::Circle: {
        const CanvasCircle *original = dynamic_cast<const CanvasCircle *>(&source);
        CanvasCircle *updated = dynamic_cast<CanvasCircle *>(candidate.data());
        if (!original || !updated
                || !updated->SetGeometry(original->GetCenter() + delta,
                                         original->GetRadius())) {
            return false;
        }
        break;
    }
    case CanvasEntityType::Ellipse: {
        const CanvasEllipse *original = dynamic_cast<const CanvasEllipse *>(&source);
        CanvasEllipse *updated = dynamic_cast<CanvasEllipse *>(candidate.data());
        if (!original || !updated
                || !updated->SetGeometry(original->GetCenter() + delta,
                                         original->GetMajorAxis(),
                                         original->GetMinorMajorRatio(),
                                         original->IsArc(),
                                         original->GetStartParameter(),
                                         original->GetEndParameter())) {
            return false;
        }
        break;
    }
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *original =
                dynamic_cast<const CanvasPolyline *>(&source);
        if (!original) {
            return false;
        }

        QSharedPointer<CanvasPolyline> updated(new CanvasPolyline);
        for (int index = 0; index < original->GetSegmentCount(); ++index) {
            const CanvasEntity *segment = original->GetSegment(index);
            if (const CanvasLine *line =
                    dynamic_cast<const CanvasLine *>(segment)) {
                CanvasLine translatedLine(line->GetStartPoint() + delta,
                                          line->GetEndPoint() + delta);
                if (!CopyCommonProperties(*line, &translatedLine)) {
                    return false;
                }
                if (!updated->AddSegment(translatedLine)) {
                    return false;
                }
            } else if (const CanvasArc *arc =
                       dynamic_cast<const CanvasArc *>(segment)) {
                CanvasArc translatedArc(arc->GetCenter() + delta,
                                        arc->GetRadius(),
                                        arc->GetStartAngle(),
                                        arc->GetEndAngle(),
                                        arc->IsReversed());
                if (!CopyCommonProperties(*arc, &translatedArc)) {
                    return false;
                }
                if (!updated->AddSegment(translatedArc)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        if (!updated->SetClosed(original->IsClosed())) {
            return false;
        }
        if (!CopyCommonProperties(source, updated.data())) {
            return false;
        }
        candidate = updated;
        break;
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *original = dynamic_cast<const CanvasSpline *>(&source);
        CanvasSpline *updated = dynamic_cast<CanvasSpline *>(candidate.data());
        if (!original || !updated) {
            return false;
        }
        QVector<CanvasVector> controlPoints;
        controlPoints.reserve(original->GetControlPoints().size());
        for (const CanvasVector &point : original->GetControlPoints()) {
            controlPoints.append(point + delta);
        }
        if (!updated->SetData(original->GetDegree(), controlPoints,
                              original->GetKnots(), original->GetWeights(),
                              original->IsClosed(), original->IsPeriodic())) {
            return false;
        }
        break;
    }
    case CanvasEntityType::GroupContainer: {
        const CanvasGroupContainer *original =
                dynamic_cast<const CanvasGroupContainer *>(&source);
        CanvasGroupContainer *updated =
                dynamic_cast<CanvasGroupContainer *>(candidate.data());
        if (!original || !updated) {
            return false;
        }
        for (int index = 0; index < original->GetChildCount(); ++index) {
            const CanvasEntity *child = original->GetChild(index);
            QSharedPointer<CanvasEntity> translatedChild;
            if (!child || !TranslateEntity(*child, delta, &translatedChild)
                    || !updated->ReplaceChild(index, *translatedChild)) {
                return false;
            }
        }
        break;
    }
    case CanvasEntityType::Invalid:
    default:
        return false;
    }

    if (!IsUsableDocumentEntity(*candidate)) {
        return false;
    }
    *translated = candidate;
    return true;
}

} // namespace

CanvasDocument::CanvasDocument(QObject *parent)
    : QObject(parent)
    , m_NextId(1)
    , m_PrimitiveViewsDirty(true)
{
}

ItemId CanvasDocument::AddEntity(const CanvasEntity &entity)
{
    if (m_NextId == 0 || !IsUsableDocumentEntity(entity)) {
        return 0;
    }

    QSharedPointer<CanvasEntity> clone = entity.Clone();
    if (!clone || clone->GetType() != entity.GetType()
            || !IsUsableDocumentEntity(*clone)) {
        return 0;
    }

    const ItemId id = m_NextId;
    clone->SetId(id);
    m_Entities.insert(id, clone);
    m_ItemOrder.append(id);
    m_NextId = id == std::numeric_limits<ItemId>::max() ? 0 : id + 1;
    InvalidatePrimitiveViews();
    emit Changed();
    return id;
}

const CanvasEntity *CanvasDocument::GetEntity(ItemId id) const
{
    return FindEntity(id);
}

bool CanvasDocument::ReplaceEntity(ItemId id, const CanvasEntity &entity)
{
    CanvasEntity *current = FindEntity(id);
    if (!current || !IsUsableDocumentEntity(entity)) {
        return false;
    }

    QSharedPointer<CanvasEntity> clone = entity.Clone();
    if (!clone || clone->GetType() != entity.GetType()
            || !IsUsableDocumentEntity(*clone)) {
        return false;
    }

    clone->SetId(id);
    m_Entities[id] = clone;
    InvalidatePrimitiveViews();
    emit Changed();
    return true;
}

ItemId CanvasDocument::AddLine(const CanvasVector &start,
                               const CanvasVector &end,
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
    if (!std::isfinite(static_cast<double>(width))) {
        return 0;
    }

    CanvasLine line(geometry.startPoint, geometry.endPoint);
    if (!line.IsValid()) {
        return 0;
    }
    StruCanvasStyle style = line.GetStyle();
    style.strokeColor = color.isValid() ? color : QColor(Qt::white);
    style.strokeWidth = qMax<qreal>(0.0, width);
    if (!line.SetStyle(style)) {
        return 0;
    }
    return AddEntity(line);
}

ItemId CanvasDocument::AddPoint(const CanvasVector &position)
{
    if (!position.IsValid()) {
        return 0;
    }
    return AddEntity(CanvasPoint(position));
}

ItemId CanvasDocument::AddPoint(const StruPointGeometry &geometry)
{
    return AddPoint(geometry.position);
}

ItemId CanvasDocument::AddArc(const CanvasVector &center,
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
    CanvasArc arc(geometry.center, geometry.radius, geometry.startAngle,
                  geometry.endAngle, geometry.reversed);
    arc.SetReversed(geometry.reversed);
    return AddEntity(arc);
}

ItemId CanvasDocument::AddCircle(const CanvasVector &center, qreal radius)
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
    return AddEntity(CanvasCircle(geometry.center, geometry.radius));
}

ItemId CanvasDocument::AddPolyline(const QVector<CanvasVector> &vertices,
                                   bool closed)
{
    StruPolylineGeometry geometry;
    geometry.vertices = vertices;
    geometry.closed = closed;
    return AddPolyline(geometry);
}

ItemId CanvasDocument::AddPolyline(const StruPolylineGeometry &geometry)
{
    CanvasPolyline polyline;
    if (!BuildPolylineEntity(geometry, &polyline)) {
        return 0;
    }
    return AddEntity(polyline);
}

bool CanvasDocument::RemoveItem(ItemId id)
{
    if (!m_Entities.remove(id)) {
        return false;
    }

    m_ItemOrder.removeAll(id);
    const bool selectionChanged = m_SelectedIds.contains(id);
    m_SelectedIds.remove(id);
    InvalidatePrimitiveViews();
    if (selectionChanged) {
        emit SelectionChanged();
    }
    emit Changed();
    return true;
}

bool CanvasDocument::TranslateItems(const QVector<ItemId> &ids,
                                    const CanvasVector &delta)
{
    if (ids.isEmpty() || !delta.IsValid()
            || (delta.GetX() == 0.0 && delta.GetY() == 0.0)) {
        return false;
    }

    QSet<ItemId> requestedIds;
    requestedIds.reserve(ids.size());
    for (const ItemId id : ids) {
        if (FindEntity(id)) {
            requestedIds.insert(id);
        }
    }
    if (requestedIds.isEmpty()) {
        return false;
    }

    QHash<ItemId, QSharedPointer<CanvasEntity>> translatedEntities;
    translatedEntities.reserve(requestedIds.size());
    for (const ItemId id : m_ItemOrder) {
        if (!requestedIds.contains(id)) {
            continue;
        }
        const CanvasEntity *entity = FindEntity(id);
        QSharedPointer<CanvasEntity> translated;
        if (!entity || !TranslateEntity(*entity, delta, &translated)) {
            return false;
        }
        translated->SetId(id);
        translatedEntities.insert(id, translated);
    }

    for (const ItemId id : m_ItemOrder) {
        const auto translated = translatedEntities.constFind(id);
        if (translated != translatedEntities.constEnd()) {
            m_Entities[id] = translated.value();
        }
    }
    InvalidatePrimitiveViews();
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

    m_Entities.clear();
    m_ItemOrder.clear();
    m_SelectedIds.clear();
    InvalidatePrimitiveViews();
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
    EnsurePrimitiveViews();
    return m_PointPrimitiveView;
}

const QVector<StruLinePrimitive> &CanvasDocument::GetLinePrimitives() const
{
    EnsurePrimitiveViews();
    return m_LinePrimitiveView;
}

const QVector<StruArcPrimitive> &CanvasDocument::GetArcPrimitives() const
{
    EnsurePrimitiveViews();
    return m_ArcPrimitiveView;
}

const QVector<StruCirclePrimitive> &CanvasDocument::GetCirclePrimitives() const
{
    EnsurePrimitiveViews();
    return m_CirclePrimitiveView;
}

const QVector<StruPolylinePrimitive> &CanvasDocument::GetPolylinePrimitives() const
{
    EnsurePrimitiveViews();
    return m_PolylinePrimitiveView;
}

QVector<ItemId> CanvasDocument::GetItemIds() const
{
    return m_ItemOrder;
}

CanvasEntityType CanvasDocument::GetItemType(ItemId id) const
{
    const CanvasEntity *entity = FindEntity(id);
    return entity ? entity->GetType() : CanvasEntityType::Invalid;
}

bool CanvasDocument::GetItemStyle(ItemId id, StruCanvasStyle *style) const
{
    const CanvasEntity *entity = FindEntity(id);
    if (!entity || !style) {
        return false;
    }
    *style = entity->GetStyle();
    return true;
}

bool CanvasDocument::SetItemStyle(ItemId id, const StruCanvasStyle &style)
{
    CanvasEntity *entity = FindEntity(id);
    if (!entity || entity->GetStyle() == style || !entity->SetStyle(style)) {
        return false;
    }
    InvalidatePrimitiveViews();
    emit Changed();
    return true;
}

std::shared_ptr<CanvasData> CanvasDocument::GetItemCanvasData(ItemId id) const
{
    const CanvasEntity *entity = FindEntity(id);
    return entity ? entity->GetCanvasData() : std::shared_ptr<CanvasData>();
}

bool CanvasDocument::SetItemCanvasData(
        ItemId id, const std::shared_ptr<CanvasData> &data)
{
    CanvasEntity *entity = FindEntity(id);
    if (!entity) {
        return false;
    }
    if (!entity->TrySetCanvasData(data)) {
        return false;
    }
    emit Changed();
    return true;
}

bool CanvasDocument::NotifyItemCanvasDataChanged(ItemId id)
{
    const CanvasEntity *entity = FindEntity(id);
    if (!entity || !entity->GetCanvasData()) {
        return false;
    }
    emit Changed();
    return true;
}

std::shared_ptr<CanvasGlobalData> CanvasDocument::GetCanvasGlobalData() const
{
    return m_CanvasGlobalData;
}

void CanvasDocument::SetCanvasGlobalData(
        const std::shared_ptr<CanvasGlobalData> &data)
{
    m_CanvasGlobalData = data;
    emit Changed();
}

bool CanvasDocument::NotifyCanvasGlobalDataChanged()
{
    if (!m_CanvasGlobalData) {
        return false;
    }
    emit Changed();
    return true;
}

bool CanvasDocument::GetPointGeometry(ItemId id,
                                      StruPointGeometry *geometry) const
{
    const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(FindEntity(id));
    if (!point || !geometry) {
        return false;
    }
    geometry->position = point->GetPosition();
    return true;
}

bool CanvasDocument::SetPointGeometry(ItemId id,
                                      const StruPointGeometry &geometry)
{
    const CanvasPoint *current =
            dynamic_cast<const CanvasPoint *>(FindEntity(id));
    if (!current || !geometry.position.IsValid()
            || current->GetPosition() == geometry.position) {
        return false;
    }
    QSharedPointer<CanvasEntity> clone = current->Clone();
    CanvasPoint *updated = clone
            ? dynamic_cast<CanvasPoint *>(clone.data()) : nullptr;
    return updated && updated->SetPosition(geometry.position)
            && ReplaceEntity(id, *updated);
}

bool CanvasDocument::GetLineGeometry(ItemId id,
                                     StruLineGeometry *geometry) const
{
    const CanvasLine *line = dynamic_cast<const CanvasLine *>(FindEntity(id));
    if (!line || !geometry) {
        return false;
    }
    geometry->startPoint = line->GetStartPoint();
    geometry->endPoint = line->GetEndPoint();
    return true;
}

bool CanvasDocument::SetLineGeometry(ItemId id,
                                     const StruLineGeometry &geometry)
{
    const CanvasLine *current =
            dynamic_cast<const CanvasLine *>(FindEntity(id));
    if (!current
            || (current->GetStartPoint() == geometry.startPoint
                && current->GetEndPoint() == geometry.endPoint)) {
        return false;
    }
    QSharedPointer<CanvasEntity> clone = current->Clone();
    CanvasLine *updated = clone
            ? dynamic_cast<CanvasLine *>(clone.data()) : nullptr;
    return updated && updated->SetGeometry(geometry.startPoint,
                                           geometry.endPoint)
            && ReplaceEntity(id, *updated);
}

bool CanvasDocument::GetArcGeometry(ItemId id,
                                    StruArcGeometry *geometry) const
{
    const CanvasArc *arc = dynamic_cast<const CanvasArc *>(FindEntity(id));
    if (!arc || !geometry) {
        return false;
    }
    geometry->center = arc->GetCenter();
    geometry->radius = arc->GetRadius();
    geometry->startAngle = arc->GetStartAngle();
    geometry->endAngle = arc->GetEndAngle();
    geometry->reversed = arc->IsReversed();
    return true;
}

bool CanvasDocument::SetArcGeometry(ItemId id,
                                    const StruArcGeometry &geometry)
{
    const CanvasArc *current =
            dynamic_cast<const CanvasArc *>(FindEntity(id));
    if (!current || !IsValidGeometry(geometry)) {
        return false;
    }
    StruArcGeometry currentGeometry;
    currentGeometry.center = current->GetCenter();
    currentGeometry.radius = current->GetRadius();
    currentGeometry.startAngle = current->GetStartAngle();
    currentGeometry.endAngle = current->GetEndAngle();
    currentGeometry.reversed = current->IsReversed();
    if (AreSameArcGeometry(currentGeometry, geometry)) {
        return false;
    }
    QSharedPointer<CanvasEntity> clone = current->Clone();
    CanvasArc *updated = clone
            ? dynamic_cast<CanvasArc *>(clone.data()) : nullptr;
    if (!updated || !updated->SetGeometry(geometry.center, geometry.radius,
                                          geometry.startAngle,
                                          geometry.endAngle)) {
        return false;
    }
    updated->SetReversed(geometry.reversed);
    return ReplaceEntity(id, *updated);
}

bool CanvasDocument::GetCircleGeometry(ItemId id,
                                       StruCircleGeometry *geometry) const
{
    const CanvasCircle *circle =
            dynamic_cast<const CanvasCircle *>(FindEntity(id));
    if (!circle || !geometry) {
        return false;
    }
    geometry->center = circle->GetCenter();
    geometry->radius = circle->GetRadius();
    return true;
}

bool CanvasDocument::SetCircleGeometry(ItemId id,
                                       const StruCircleGeometry &geometry)
{
    const CanvasCircle *current =
            dynamic_cast<const CanvasCircle *>(FindEntity(id));
    if (!current || !IsValidGeometry(geometry)) {
        return false;
    }
    StruCircleGeometry currentGeometry;
    currentGeometry.center = current->GetCenter();
    currentGeometry.radius = current->GetRadius();
    if (AreSameCircleGeometry(currentGeometry, geometry)) {
        return false;
    }
    QSharedPointer<CanvasEntity> clone = current->Clone();
    CanvasCircle *updated = clone
            ? dynamic_cast<CanvasCircle *>(clone.data()) : nullptr;
    return updated && updated->SetGeometry(geometry.center, geometry.radius)
            && ReplaceEntity(id, *updated);
}

bool CanvasDocument::GetPolylineGeometry(ItemId id,
                                         StruPolylineGeometry *geometry) const
{
    const CanvasPolyline *polyline =
            dynamic_cast<const CanvasPolyline *>(FindEntity(id));
    return polyline && ExtractPolylineGeometry(*polyline, geometry);
}

bool CanvasDocument::SetPolylineGeometry(ItemId id,
                                         const StruPolylineGeometry &geometry)
{
    const CanvasPolyline *current =
            dynamic_cast<const CanvasPolyline *>(FindEntity(id));
    if (!current || !IsValidGeometry(geometry)) {
        return false;
    }
    StruPolylineGeometry currentGeometry;
    if (!ExtractPolylineGeometry(*current, &currentGeometry)
            || AreSamePolylineGeometry(currentGeometry, geometry)) {
        return false;
    }

    CanvasPolyline updated;
    if (!BuildPolylineEntity(geometry, &updated)) {
        return false;
    }
    const int segmentsToPreserve = qMin(current->GetSegmentCount(),
                                        updated.GetSegmentCount());
    for (int index = 0; index < segmentsToPreserve; ++index) {
        const CanvasLine *currentSegment = dynamic_cast<const CanvasLine *>(
                    current->GetSegment(index));
        const CanvasLine *updatedSegment = dynamic_cast<const CanvasLine *>(
                    updated.GetSegment(index));
        if (!currentSegment || !updatedSegment) {
            return false;
        }

        CanvasLine segmentWithProperties(updatedSegment->GetStartPoint(),
                                         updatedSegment->GetEndPoint());
        if (!CopyCommonProperties(*currentSegment, &segmentWithProperties)
                || !updated.SetSegment(index, segmentWithProperties)) {
            return false;
        }
    }
    if (!CopyCommonProperties(*current, &updated)) {
        return false;
    }
    return ReplaceEntity(id, updated);
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
    if (!FindEntity(id) || m_SelectedIds.contains(id)) {
        return;
    }
    m_SelectedIds.insert(id);
    emit SelectionChanged();
}

void CanvasDocument::ToggleItemSelection(ItemId id)
{
    if (!FindEntity(id)) {
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
        if (FindEntity(id)) {
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
    for (const ItemId id : m_ItemOrder) {
        if (!requestedIds.contains(id)) {
            continue;
        }
        const CanvasEntity *entity = FindEntity(id);
        QRectF entityBounds;
        if (!entity || !TryGetGeometryBounds(*entity, &entityBounds)) {
            continue;
        }
        if (!hasBounds) {
            result = entityBounds;
            hasBounds = true;
            continue;
        }

        const qreal left = qMin(result.left(), entityBounds.left());
        const qreal top = qMin(result.top(), entityBounds.top());
        const qreal right = qMax(result.right(), entityBounds.right());
        const qreal bottom = qMax(result.bottom(), entityBounds.bottom());
        const qreal width = right - left;
        const qreal height = bottom - top;
        if (!std::isfinite(static_cast<double>(left))
                || !std::isfinite(static_cast<double>(top))
                || !std::isfinite(static_cast<double>(width))
                || !std::isfinite(static_cast<double>(height))) {
            return QRectF();
        }
        result = QRectF(left, top, width, height);
    }
    return hasBounds ? result : QRectF();
}

CanvasEntity *CanvasDocument::FindEntity(ItemId id)
{
    const auto entity = m_Entities.find(id);
    return entity == m_Entities.end() ? nullptr : entity.value().data();
}

const CanvasEntity *CanvasDocument::FindEntity(ItemId id) const
{
    const auto entity = m_Entities.constFind(id);
    return entity == m_Entities.constEnd() ? nullptr : entity.value().data();
}

void CanvasDocument::InvalidatePrimitiveViews()
{
    m_PrimitiveViewsDirty = true;
}

void CanvasDocument::EnsurePrimitiveViews() const
{
    if (!m_PrimitiveViewsDirty) {
        return;
    }

    m_PointPrimitiveView.clear();
    m_LinePrimitiveView.clear();
    m_ArcPrimitiveView.clear();
    m_CirclePrimitiveView.clear();
    m_PolylinePrimitiveView.clear();

    for (const ItemId id : m_ItemOrder) {
        const CanvasEntity *entity = FindEntity(id);
        if (!entity) {
            continue;
        }
        switch (entity->GetType()) {
        case CanvasEntityType::Point: {
            const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(entity);
            if (!point) {
                break;
            }
            StruPointPrimitive primitive;
            primitive.id = id;
            primitive.geometry.position = point->GetPosition();
            primitive.style = point->GetStyle();
            m_PointPrimitiveView.append(primitive);
            break;
        }
        case CanvasEntityType::Line: {
            const CanvasLine *line = dynamic_cast<const CanvasLine *>(entity);
            if (!line) {
                break;
            }
            StruLinePrimitive primitive;
            primitive.id = id;
            primitive.geometry.startPoint = line->GetStartPoint();
            primitive.geometry.endPoint = line->GetEndPoint();
            primitive.style = line->GetStyle();
            primitive.color = primitive.style.strokeColor;
            primitive.width = primitive.style.strokeWidth;
            m_LinePrimitiveView.append(primitive);
            break;
        }
        case CanvasEntityType::Arc: {
            const CanvasArc *arc = dynamic_cast<const CanvasArc *>(entity);
            if (!arc) {
                break;
            }
            StruArcPrimitive primitive;
            primitive.id = id;
            primitive.geometry.center = arc->GetCenter();
            primitive.geometry.radius = arc->GetRadius();
            primitive.geometry.startAngle = arc->GetStartAngle();
            primitive.geometry.endAngle = arc->GetEndAngle();
            primitive.geometry.reversed = arc->IsReversed();
            primitive.style = arc->GetStyle();
            m_ArcPrimitiveView.append(primitive);
            break;
        }
        case CanvasEntityType::Circle: {
            const CanvasCircle *circle =
                    dynamic_cast<const CanvasCircle *>(entity);
            if (!circle) {
                break;
            }
            StruCirclePrimitive primitive;
            primitive.id = id;
            primitive.geometry.center = circle->GetCenter();
            primitive.geometry.radius = circle->GetRadius();
            primitive.style = circle->GetStyle();
            m_CirclePrimitiveView.append(primitive);
            break;
        }
        case CanvasEntityType::Polyline: {
            const CanvasPolyline *polyline =
                    dynamic_cast<const CanvasPolyline *>(entity);
            if (!polyline) {
                break;
            }
            StruPolylinePrimitive primitive;
            if (!ExtractPolylineGeometry(*polyline, &primitive.geometry)) {
                break;
            }
            primitive.id = id;
            primitive.style = polyline->GetStyle();
            m_PolylinePrimitiveView.append(primitive);
            break;
        }
        case CanvasEntityType::Ellipse:
        case CanvasEntityType::Spline:
        case CanvasEntityType::GroupContainer:
        case CanvasEntityType::Invalid:
            break;
        }
    }
    m_PrimitiveViewsDirty = false;
}

} // namespace mini_graph
