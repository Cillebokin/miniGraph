#include "MiniGraph/CanvasView.h"

#include "CanvasPainterRenderer.h"
#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasGeometry.h"
#include "MiniGraph/CanvasGlobalData.h"
#include "MiniGraph/CanvasRenderer.h"
#include "MiniGraph/CanvasSnap.h"

#include <QFont>
#include <QImage>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace mini_graph {

namespace {

const qreal CADLAB_MIN_GRID_PIXEL_SPACING = 10.0;
const qreal CADLAB_MAX_GRID_PIXEL_SPACING = 100.0;
const qreal SELECTION_DRAG_THRESHOLD = 7.0;
const qreal SELECTION_HIT_TOLERANCE_PIXELS = 6.0;
const qreal EDIT_HANDLE_SIZE_PIXELS = 7.0;
const qreal EDIT_HANDLE_HIT_TOLERANCE_PIXELS = 9.0;

const QColor CADLAB_WINDOW_SELECTION_COLOR(9, 9, 255, 60);
const QColor CADLAB_CROSSING_SELECTION_COLOR(9, 255, 9, 60);
const QColor CADLAB_EDIT_HANDLE_COLOR(0, 170, 255);
const QColor CADLAB_ACTIVE_EDIT_HANDLE_COLOR(255, 165, 0);
const QColor CADLAB_SNAP_MARKER_COLOR(255, 220, 0);

bool HasBoundedIntersection(const QLineF &first, const QLineF &second)
{
    QPointF intersection;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return first.intersects(second, &intersection) == QLineF::BoundedIntersection;
#else
    return first.intersect(second, &intersection) == QLineF::BoundedIntersection;
#endif
}

QPointF GetWheelEventPosition(const QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->posF();
#endif
}

qreal CalculateCadLabGridSpacing(qreal baseSpacing, qreal zoom)
{
    qreal spacing = baseSpacing;
    for (int scaleCount = 0;
         scaleCount < 512 && spacing * zoom < CADLAB_MIN_GRID_PIXEL_SPACING;
         ++scaleCount) {
        spacing *= 10.0;
    }
    for (int scaleCount = 0;
         scaleCount < 512 && spacing * zoom >= CADLAB_MAX_GRID_PIXEL_SPACING;
         ++scaleCount) {
        spacing /= 10.0;
    }
    return spacing;
}

qreal DistanceSquaredToSegment(const CanvasVector &point,
                               const CanvasVector &start,
                               const CanvasVector &end)
{
    const CanvasVector direction = end - start;
    const qreal lengthSquared = direction.SquaredLength();
    if (lengthSquared <= 0.0) {
        return point.SquaredDistanceTo(start);
    }

    const CanvasVector pointDelta = point - start;
    const qreal parameter = qBound<qreal>(0.0,
                                          pointDelta.Dot(direction)
                                                  / lengthSquared,
                                          1.0);
    const CanvasVector closestPoint = start + direction * parameter;
    return point.SquaredDistanceTo(closestPoint);
}

bool SegmentIntersectsRect(const CanvasVector &start,
                           const CanvasVector &end,
                           const QRectF &rect)
{
    const QPointF startPoint(start.GetX(), start.GetY());
    const QPointF endPoint(end.GetX(), end.GetY());
    if (rect.contains(startPoint) || rect.contains(endPoint)) {
        return true;
    }

    const QLineF segment(startPoint, endPoint);
    const QLineF edges[] = {
        QLineF(rect.topLeft(), rect.topRight()),
        QLineF(rect.topRight(), rect.bottomRight()),
        QLineF(rect.bottomRight(), rect.bottomLeft()),
        QLineF(rect.bottomLeft(), rect.topLeft())
    };

    for (const QLineF &edge : edges) {
        if (HasBoundedIntersection(segment, edge)) {
            return true;
        }
    }

    return false;
}

bool PolylineIntersectsRect(const QVector<CanvasVector> &points,
                            const QRectF &rect)
{
    if (points.isEmpty()) {
        return false;
    }

    for (const CanvasVector &point : points) {
        if (rect.contains(QPointF(point.GetX(), point.GetY()))) {
            return true;
        }
    }

    for (int index = 1; index < points.size(); ++index) {
        if (SegmentIntersectsRect(points.at(index - 1),
                                  points.at(index),
                                  rect)) {
            return true;
        }
    }

    return false;
}

qreal CalculateHitToleranceSquared(qreal zoom, qreal displaySize)
{
    const qreal halfDisplaySize = qMax<qreal>(0.0, displaySize) * 0.5;
    const qreal pixelTolerance = qMax(SELECTION_HIT_TOLERANCE_PIXELS,
                                      halfDisplaySize);
    const qreal worldTolerance = pixelTolerance / zoom;
    return worldTolerance * worldTolerance;
}

struct EntitySelectionPath
{
    QVector<CanvasVector> points;
    qreal displaySize;
};

void AppendSelectionPath(const QVector<CanvasVector> &points,
                         qreal displaySize,
                         QVector<EntitySelectionPath> *paths)
{
    if (!paths || points.isEmpty()) {
        return;
    }

    EntitySelectionPath path;
    path.points = points;
    path.displaySize = displaySize;
    paths->append(path);
}

void CollectEntitySelectionPaths(const CanvasEntity &entity,
                                 qreal maximumWorldError,
                                 QVector<EntitySelectionPath> *paths)
{
    if (!paths || !entity.GetStyle().visible || !entity.IsValid()) {
        return;
    }

    const StruCanvasStyle &style = entity.GetStyle();
    switch (entity.GetType()) {
    case CanvasEntityType::Point: {
        const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(&entity);
        if (point) {
            AppendSelectionPath(QVector<CanvasVector>() << point->GetPosition(),
                                style.pointSize, paths);
        }
        return;
    }
    case CanvasEntityType::Line: {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(&entity);
        if (line) {
            AppendSelectionPath(QVector<CanvasVector>()
                                << line->GetStartPoint()
                                << line->GetEndPoint(),
                                style.strokeWidth, paths);
        }
        return;
    }
    case CanvasEntityType::Arc: {
        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(&entity);
        if (arc) {
            StruArcGeometry geometry;
            geometry.center = arc->GetCenter();
            geometry.radius = arc->GetRadius();
            geometry.startAngle = arc->GetStartAngle();
            geometry.endAngle = arc->GetEndAngle();
            geometry.reversed = arc->IsReversed();
            AppendSelectionPath(TessellateArc(geometry, maximumWorldError),
                                style.strokeWidth, paths);
        }
        return;
    }
    case CanvasEntityType::Circle: {
        const CanvasCircle *circle = dynamic_cast<const CanvasCircle *>(&entity);
        if (circle) {
            StruCircleGeometry geometry;
            geometry.center = circle->GetCenter();
            geometry.radius = circle->GetRadius();
            AppendSelectionPath(TessellateCircle(geometry, maximumWorldError),
                                style.strokeWidth, paths);
        }
        return;
    }
    case CanvasEntityType::Ellipse: {
        const CanvasEllipse *ellipse =
                dynamic_cast<const CanvasEllipse *>(&entity);
        if (ellipse) {
            AppendSelectionPath(detail::TessellateEllipseForSelection(
                                    *ellipse, maximumWorldError, nullptr),
                                style.strokeWidth, paths);
        }
        return;
    }
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        if (!polyline) {
            return;
        }
        for (int index = 0; index < polyline->GetSegmentCount(); ++index) {
            const CanvasEntity *segment = polyline->GetSegment(index);
            if (const CanvasLine *line =
                    dynamic_cast<const CanvasLine *>(segment)) {
                AppendSelectionPath(QVector<CanvasVector>()
                                    << line->GetStartPoint()
                                    << line->GetEndPoint(),
                                    style.strokeWidth, paths);
            } else if (const CanvasArc *arc =
                       dynamic_cast<const CanvasArc *>(segment)) {
                StruArcGeometry geometry;
                geometry.center = arc->GetCenter();
                geometry.radius = arc->GetRadius();
                geometry.startAngle = arc->GetStartAngle();
                geometry.endAngle = arc->GetEndAngle();
                geometry.reversed = arc->IsReversed();
                AppendSelectionPath(TessellateArc(geometry,
                                                   maximumWorldError),
                                    style.strokeWidth, paths);
            }
        }
        return;
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *spline = dynamic_cast<const CanvasSpline *>(&entity);
        if (!spline) {
            return;
        }
        const QVector<QVector<CanvasVector>> splinePaths =
                detail::TessellateSplineForSelection(*spline,
                                                     maximumWorldError);
        for (const QVector<CanvasVector> &splinePath : splinePaths) {
            AppendSelectionPath(splinePath, style.strokeWidth, paths);
        }
        return;
    }
    case CanvasEntityType::GroupContainer: {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(&entity);
        if (!group) {
            return;
        }
        for (int index = 0; index < group->GetChildCount(); ++index) {
            const CanvasEntity *child = group->GetChild(index);
            if (child) {
                CollectEntitySelectionPaths(*child, maximumWorldError, paths);
            }
        }
        return;
    }
    case CanvasEntityType::Invalid:
        return;
    }
}

qreal DistanceSquaredToPath(const CanvasVector &point,
                            const QVector<CanvasVector> &path)
{
    if (path.isEmpty()) {
        return std::numeric_limits<qreal>::max();
    }
    if (path.size() == 1) {
        return point.SquaredDistanceTo(path.first());
    }

    qreal bestDistanceSquared = std::numeric_limits<qreal>::max();
    for (int index = 1; index < path.size(); ++index) {
        bestDistanceSquared = qMin(
                    bestDistanceSquared,
                    DistanceSquaredToSegment(point,
                                             path.at(index - 1),
                                             path.at(index)));
    }
    return bestDistanceSquared;
}

bool AreEntityPathsContained(const QVector<EntitySelectionPath> &paths,
                             const QRectF &rect)
{
    if (paths.isEmpty()) {
        return false;
    }

    for (const EntitySelectionPath &path : paths) {
        for (const CanvasVector &point : path.points) {
            if (!rect.contains(QPointF(point.GetX(), point.GetY()))) {
                return false;
            }
        }
    }
    return true;
}

bool DoEntityPathsIntersectRect(const QVector<EntitySelectionPath> &paths,
                                const QRectF &rect)
{
    for (const EntitySelectionPath &path : paths) {
        if (PolylineIntersectsRect(path.points, rect)) {
            return true;
        }
    }
    return false;
}

CanvasVector CalculateEllipsePoint(const CanvasEllipse &ellipse,
                                   qreal parameter)
{
    const CanvasVector majorAxis = ellipse.GetMajorAxis();
    const qreal ratio = ellipse.GetMinorMajorRatio();
    const CanvasVector minorAxis(-majorAxis.GetY() * ratio,
                                 majorAxis.GetX() * ratio);
    return ellipse.GetCenter()
            + majorAxis * std::cos(static_cast<double>(parameter))
            + minorAxis * std::sin(static_cast<double>(parameter));
}

CanvasVector GetFirstEntityReferencePoint(const CanvasEntity &entity)
{
    switch (entity.GetType()) {
    case CanvasEntityType::Point: {
        const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(&entity);
        return point ? point->GetPosition() : CanvasVector();
    }
    case CanvasEntityType::Line: {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(&entity);
        return line ? line->GetStartPoint() : CanvasVector();
    }
    case CanvasEntityType::Arc: {
        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(&entity);
        return arc ? arc->GetStartPoint() : CanvasVector();
    }
    case CanvasEntityType::Circle: {
        const CanvasCircle *circle =
                dynamic_cast<const CanvasCircle *>(&entity);
        return circle ? circle->GetCenter() : CanvasVector();
    }
    case CanvasEntityType::Ellipse: {
        const CanvasEllipse *ellipse =
                dynamic_cast<const CanvasEllipse *>(&entity);
        if (!ellipse) {
            return CanvasVector();
        }
        return ellipse->IsArc()
                ? CalculateEllipsePoint(*ellipse,
                                        ellipse->GetStartParameter())
                : ellipse->GetCenter();
    }
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(&entity);
        const CanvasEntity *firstSegment = polyline
                ? polyline->GetSegment(0) : nullptr;
        return firstSegment
                ? GetFirstEntityReferencePoint(*firstSegment)
                : CanvasVector();
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *spline =
                dynamic_cast<const CanvasSpline *>(&entity);
        return spline && !spline->GetControlPoints().isEmpty()
                ? spline->GetControlPoints().first() : CanvasVector();
    }
    case CanvasEntityType::GroupContainer: {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(&entity);
        const CanvasEntity *firstChild = group
                ? group->GetChild(0) : nullptr;
        return firstChild
                ? GetFirstEntityReferencePoint(*firstChild)
                : CanvasVector();
    }
    case CanvasEntityType::Invalid:
        return CanvasVector();
    }

    return CanvasVector();
}

void CopyEntityProperties(const CanvasEntity &source, CanvasEntity *target)
{
    if (!target) {
        return;
    }
    target->SetStyle(source.GetStyle());
    target->SetLayerName(source.GetLayerName());
    target->SetTag(source.GetTag());
    target->SetBusinessId(source.GetBusinessId());
    target->SetReversed(source.IsReversed());
}

bool IsLineSegment(const CanvasEntity *segment)
{
    return dynamic_cast<const CanvasLine *>(segment) != nullptr;
}

bool IsEditablePolylineVertex(const CanvasPolyline &polyline,
                              int pointIndex)
{
    const int segmentCount = polyline.GetSegmentCount();
    if (segmentCount <= 0) {
        return false;
    }

    if (polyline.IsClosed()) {
        if (pointIndex < 0 || pointIndex >= segmentCount) {
            return false;
        }
        const int previousSegmentIndex =
                (pointIndex + segmentCount - 1) % segmentCount;
        return IsLineSegment(polyline.GetSegment(previousSegmentIndex))
                && IsLineSegment(polyline.GetSegment(pointIndex));
    }

    if (pointIndex < 0 || pointIndex > segmentCount) {
        return false;
    }
    if (pointIndex == 0) {
        return IsLineSegment(polyline.GetSegment(0));
    }
    if (pointIndex == segmentCount) {
        return IsLineSegment(polyline.GetSegment(segmentCount - 1));
    }
    return IsLineSegment(polyline.GetSegment(pointIndex - 1))
            && IsLineSegment(polyline.GetSegment(pointIndex));
}

CanvasVector GetPolylineVertex(const CanvasPolyline &polyline, int pointIndex)
{
    if (!IsEditablePolylineVertex(polyline, pointIndex)) {
        return CanvasVector();
    }

    const int segmentCount = polyline.GetSegmentCount();
    if (polyline.IsClosed() || pointIndex < segmentCount) {
        const int nextSegmentIndex = pointIndex % segmentCount;
        const CanvasLine *nextLine = dynamic_cast<const CanvasLine *>(
                    polyline.GetSegment(nextSegmentIndex));
        return nextLine ? nextLine->GetStartPoint() : CanvasVector();
    }

    const CanvasLine *previousLine = dynamic_cast<const CanvasLine *>(
                polyline.GetSegment(segmentCount - 1));
    return previousLine ? previousLine->GetEndPoint() : CanvasVector();
}

bool TryMovePolylineVertex(const CanvasPolyline &source,
                           int pointIndex,
                           const CanvasVector &position,
                           QSharedPointer<CanvasEntity> *result)
{
    const int segmentCount = source.GetSegmentCount();
    if (!result || !position.IsValid()
            || !IsEditablePolylineVertex(source, pointIndex)) {
        return false;
    }

    CanvasPolyline updated;
    for (int segmentIndex = 0;
         segmentIndex < segmentCount;
         ++segmentIndex) {
        const CanvasEntity *segment = source.GetSegment(segmentIndex);
        if (const CanvasLine *line =
                dynamic_cast<const CanvasLine *>(segment)) {
            CanvasVector startPoint = line->GetStartPoint();
            CanvasVector endPoint = line->GetEndPoint();
            const int endPointIndex = source.IsClosed()
                    ? (segmentIndex + 1) % segmentCount
                    : segmentIndex + 1;
            if (segmentIndex == pointIndex) {
                startPoint = position;
            }
            if (endPointIndex == pointIndex) {
                endPoint = position;
            }

            CanvasLine updatedLine(startPoint, endPoint);
            CopyEntityProperties(*line, &updatedLine);
            if (!updated.AddSegment(updatedLine)) {
                return false;
            }
            continue;
        }

        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(segment);
        if (!arc || !updated.AddSegment(*arc)) {
            return false;
        }
    }

    if (!updated.SetClosed(source.IsClosed())) {
        return false;
    }
    CopyEntityProperties(source, &updated);
    *result = updated.Clone();
    return !result->isNull();
}

void PrepareLayer(QImage *layer, const QSize &pixelSize, qreal devicePixelRatio)
{
    if (!layer) {
        return;
    }

    if (layer->size() != pixelSize
            || layer->format() != QImage::Format_ARGB32_Premultiplied
            || !qFuzzyCompare(layer->devicePixelRatio(), devicePixelRatio)) {
        *layer = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
        layer->setDevicePixelRatio(devicePixelRatio);
    }
    layer->fill(Qt::transparent);
}

} // namespace

class CanvasViewPrivate
{
public:
    CanvasViewPrivate()
        : m_DefaultRenderer(new CanvasPainterRenderer)
    {
    }

    QImage m_BackgroundLayer;
    QImage m_GeometryLayer;
    QImage m_OverlayLayer;
    std::unique_ptr<CanvasPainterRenderer> m_DefaultRenderer;
};

CanvasView::CanvasView(QWidget *parent)
    : QWidget(parent)
    , m_InteractionMode(InteractionMode::Edit)
    , m_GridVisible(true)
    , m_GridSpacing(10.0)
    , m_AxisVisible(true)
    , m_AxisFlipX(false)
    , m_AxisFlipY(false)
    , m_RulerVisible(false)
    , m_OrthogonalMode(false)
    , m_PanState(PanState::Idle)
    , m_PanStartPosition(0.0, 0.0)
    , m_LastMousePosition(0.0, 0.0)
    , m_SelectionState(SelectionState::Idle)
    , m_SelectionStartPosition(0.0, 0.0)
    , m_SelectionCurrentPosition(0.0, 0.0)
    , m_SelectionModifiers(Qt::NoModifier)
    , m_PressedItemId(0)
    , m_MoveLastWorldPosition(0.0, 0.0)
    , m_MoveTotalWorldDelta(0.0, 0.0)
    , m_SnapEnabled(false)
    , m_GridSnapEnabled(true)
    , m_GeometrySnapEnabled(true)
    , m_CurrentMousePosition(0.0, 0.0)
    , m_EditHandleType(EditHandleType::None)
    , m_EditPointIndex(-1)
    , m_EditItemId(0)
    , m_EditEntityType(CanvasEntityType::Invalid)
    , m_EditGeometryChanged(false)
    , m_PrivateData(new CanvasViewPrivate)
{
    qRegisterMetaType<CanvasVector>("mini_graph::CanvasVector");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

CanvasView::~CanvasView()
{
}

void CanvasView::SetInteractionMode(InteractionMode mode)
{
    if (m_InteractionMode == mode) {
        return;
    }

    CancelEdit();
    CancelMove();
    m_PanState = PanState::Idle;
    m_SelectionState = SelectionState::Idle;
    m_SelectionModifiers = Qt::NoModifier;
    m_PressedItemId = 0;
    SetOrthogonalModeInternal(false);
    unsetCursor();
    m_InteractionMode = mode;
    if (mode == InteractionMode::PreviewNavigation && m_Document) {
        m_Document->ClearSelection();
    }
    const CanvasSnapResult emptySnap;
    if (m_CurrentSnapResult != emptySnap) {
        m_CurrentSnapResult = emptySnap;
        emit SnapChanged(m_CurrentSnapResult);
    }
    update();
}

CanvasView::InteractionMode CanvasView::GetInteractionMode() const
{
    return m_InteractionMode;
}

CanvasDocument *CanvasView::GetDocument() const
{
    return m_Document.data();
}

void CanvasView::SetDocument(CanvasDocument *document)
{
    if (m_Document.data() == document) {
        return;
    }

    CancelEdit();
    CancelMove();
    QObject::disconnect(m_DocumentConnection);
    QObject::disconnect(m_DocumentSelectionConnection);
    m_Document = document;
    m_SelectionState = SelectionState::Idle;
    m_PressedItemId = 0;
    m_SelectionModifiers = Qt::NoModifier;

    if (m_InteractionMode == InteractionMode::PreviewNavigation && m_Document) {
        m_Document->ClearSelection();
    }

    if (m_Document) {
        m_DocumentConnection = connect(m_Document.data(),
                                       &CanvasDocument::Changed,
                                       this,
                                       &CanvasView::OnDocumentChanged);
        m_DocumentSelectionConnection = connect(m_Document.data(),
                                                &CanvasDocument::SelectionChanged,
                                                this,
                                                &CanvasView::OnSelectionChanged);
    }

    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
}

CanvasRenderer *CanvasView::GetCustomRenderer() const
{
    return m_CustomRenderer.data();
}

void CanvasView::SetCustomRenderer(CanvasRenderer *renderer)
{
    if (m_CustomRenderer.data() == renderer) {
        return;
    }

    QObject::disconnect(m_CustomRendererDestroyedConnection);
    QObject::disconnect(m_CustomRendererUpdateConnection);
    m_CustomRenderer = renderer;

    if (m_CustomRenderer) {
        m_CustomRendererDestroyedConnection = connect(
                    m_CustomRenderer.data(),
                    &QObject::destroyed,
                    this,
            [this]() {
            m_CustomRenderer = nullptr;
            m_CustomRendererDestroyedConnection = QMetaObject::Connection();
            m_CustomRendererUpdateConnection = QMetaObject::Connection();
            update();
        });
        m_CustomRendererUpdateConnection = connect(
                    m_CustomRenderer.data(),
                    &CanvasRenderer::UpdateRequested,
                    this,
                    [this]() {
            update();
        });
    } else {
        m_CustomRendererDestroyedConnection = QMetaObject::Connection();
        m_CustomRendererUpdateConnection = QMetaObject::Connection();
    }
    update();
}

bool CanvasView::IsGridVisible() const
{
    return m_GridVisible;
}

void CanvasView::SetGridVisible(bool visible)
{
    if (m_GridVisible == visible) {
        return;
    }

    m_GridVisible = visible;
    emit GridVisibleChanged(m_GridVisible);
    update();
}

qreal CanvasView::GetGridSpacing() const
{
    return m_GridSpacing;
}

void CanvasView::SetGridSpacing(qreal spacing)
{
    if (spacing <= 0.0 || qFuzzyCompare(m_GridSpacing, spacing)) {
        return;
    }

    m_GridSpacing = spacing;
    emit GridSpacingChanged(m_GridSpacing);
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
}

qreal CanvasView::GetZoom() const
{
    return m_Transform.GetZoom();
}

CanvasTransform CanvasView::GetTransform() const
{
    return m_Transform;
}

CanvasVector CanvasView::MapToWorld(const QPointF &viewportPosition) const
{
    if (size().isEmpty()) {
        return CanvasVector();
    }

    return m_Transform.MapToWorld(viewportPosition, QSizeF(size()));
}

QPointF CanvasView::MapFromWorld(const CanvasVector &worldPosition) const
{
    if (size().isEmpty()) {
        return QPointF();
    }

    return m_Transform.MapFromWorld(worldPosition, QSizeF(size()));
}

bool CanvasView::IsAxisVisible() const
{
    return m_AxisVisible;
}

void CanvasView::SetAxisVisible(bool visible)
{
    if (m_AxisVisible == visible) {
        return;
    }

    m_AxisVisible = visible;
    emit AxisVisibleChanged(m_AxisVisible);
    update();
}

void CanvasView::SetAxisFlips(bool flipX, bool flipY)
{
    if (m_AxisFlipX == flipX && m_AxisFlipY == flipY) {
        return;
    }

    m_AxisFlipX = flipX;
    m_AxisFlipY = flipY;
    update();
}

bool CanvasView::IsRulerVisible() const
{
    return m_RulerVisible;
}

void CanvasView::SetRulerVisible(bool visible)
{
    if (m_RulerVisible == visible) {
        return;
    }

    m_RulerVisible = visible;
    emit RulerVisibleChanged(m_RulerVisible);
    update();
}

bool CanvasView::IsOrthogonalMode() const
{
    return m_OrthogonalMode;
}

bool CanvasView::IsSnapEnabled() const
{
    return m_SnapEnabled;
}

void CanvasView::SetSnapEnabled(bool enabled)
{
    if (m_SnapEnabled == enabled) {
        return;
    }

    m_SnapEnabled = enabled;
    emit SnapEnabledChanged(m_SnapEnabled);
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
}

bool CanvasView::IsGridSnapEnabled() const
{
    return m_GridSnapEnabled;
}

void CanvasView::SetGridSnapEnabled(bool enabled)
{
    if (m_GridSnapEnabled == enabled) {
        return;
    }

    m_GridSnapEnabled = enabled;
    emit GridSnapEnabledChanged(m_GridSnapEnabled);
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
}

bool CanvasView::IsGeometrySnapEnabled() const
{
    return m_GeometrySnapEnabled;
}

void CanvasView::SetGeometrySnapEnabled(bool enabled)
{
    if (m_GeometrySnapEnabled == enabled) {
        return;
    }

    m_GeometrySnapEnabled = enabled;
    emit GeometrySnapEnabledChanged(m_GeometrySnapEnabled);
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
}

CanvasSnapResult CanvasView::GetCurrentSnapResult() const
{
    return m_CurrentSnapResult;
}

void CanvasView::ResetView()
{
    m_Transform.Reset();
    emit ZoomChanged(m_Transform.GetZoom());
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
}

bool CanvasView::FitAll(qreal marginPixels)
{
    if (!m_Document || m_Document->GetItemCount() <= 0) {
        return false;
    }

    return FitWorldRect(m_Document->GetBounds(), marginPixels);
}

bool CanvasView::FitSelection(qreal marginPixels)
{
    if (!m_Document) {
        return false;
    }

    const QVector<ItemId> selectedIds = m_Document->GetSelectedItemIds();
    if (selectedIds.isEmpty()) {
        return false;
    }

    return FitWorldRect(m_Document->GetBounds(selectedIds), marginPixels);
}

void CanvasView::CenterOnWorld(const CanvasVector &worldPosition)
{
    if (m_Transform.GetCenter() == worldPosition) {
        return;
    }

    m_Transform.SetCenter(worldPosition);
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
}

void CanvasView::SetOrthogonalModeInternal(bool enabled)
{
    if (m_OrthogonalMode == enabled) {
        return;
    }

    m_OrthogonalMode = enabled;
    emit OrthogonalModeChanged(m_OrthogonalMode);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (m_InteractionMode == InteractionMode::PreviewNavigation) {
        if (event->key() == Qt::Key_Escape) {
            m_PanState = PanState::Idle;
            unsetCursor();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Delete
                || event->key() == Qt::Key_Backspace
                || event->key() == Qt::Key_Shift) {
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Shift) {
        if (!event->isAutoRepeat()) {
            SetOrthogonalModeInternal(true);
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        CancelEdit();
        CancelMove();
        m_PanState = PanState::Idle;
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        m_PressedItemId = 0;
        if (m_Document) {
            m_Document->ClearSelection();
        }
        SetOrthogonalModeInternal(false);
        unsetCursor();
        emit InteractionCancelled();
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete
            || event->key() == Qt::Key_Backspace) {
        CancelEdit();
        CancelMove();
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        m_PressedItemId = 0;
        if (m_Document) {
            const QVector<ItemId> selectedIds = m_Document->GetSelectedItemIds();
            for (const ItemId id : selectedIds) {
                m_Document->RemoveItem(id);
            }
        }
        unsetCursor();
        update();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (m_InteractionMode == InteractionMode::PreviewNavigation
            && event->key() == Qt::Key_Shift) {
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Shift) {
        if (!event->isAutoRepeat()) {
            SetOrthogonalModeInternal(false);
        }
        event->accept();
        return;
    }

    QWidget::keyReleaseEvent(event);
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    m_CurrentMousePosition = event->localPos();
    const CanvasVector mouseWorldPosition = MapToWorld(m_CurrentMousePosition);
    emit MouseWorldPositionChanged(mouseWorldPosition);
    UpdateSnap(mouseWorldPosition);

    if (m_InteractionMode == InteractionMode::PreviewNavigation) {
        if (event->button() == Qt::RightButton) {
            event->ignore();
            return;
        }
        if (event->button() == Qt::LeftButton
                || event->button() == Qt::MiddleButton) {
            if (m_PanState != PanState::Idle) {
                event->ignore();
                return;
            }
            if (isVisible()) {
                setFocus(Qt::MouseFocusReason);
            }
            m_PanState = PanState::Pressed;
            m_PanStartPosition = event->localPos();
            m_LastMousePosition = event->localPos();
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        event->ignore();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        if (isVisible()) {
            setFocus(Qt::MouseFocusReason);
        }
        CancelEdit();
        ResetMoveState();
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        m_PressedItemId = 0;
        m_PanState = PanState::Pressed;
        m_PanStartPosition = event->localPos();
        m_LastMousePosition = event->localPos();
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        CancelEdit();
        ResetMoveState();
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        m_PressedItemId = 0;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_PanState != PanState::Idle) {
            event->ignore();
            return;
        }
        if (isVisible()) {
            setFocus(Qt::MouseFocusReason);
        }

        if (m_Document && m_Document->GetSelectedItemCount() == 1) {
            const QVector<ItemId> selectedIds = m_Document->GetSelectedItemIds();
            int pointIndex = -1;
            const EditHandleType handleType = HitTestEditHandle(event->localPos(),
                                                                 &pointIndex);
            if (selectedIds.size() == 1
                    && handleType == EditHandleType::GroupReferencePoint) {
                m_SelectionState = SelectionState::Pressed;
                m_SelectionStartPosition = event->localPos();
                m_SelectionCurrentPosition = event->localPos();
                m_SelectionModifiers = event->modifiers();
                m_PressedItemId = selectedIds.first();
                event->accept();
                return;
            }
            if (selectedIds.size() == 1
                    && handleType != EditHandleType::None
                    && BeginEdit(selectedIds.first(), handleType, pointIndex)) {
                m_SelectionState = SelectionState::Editing;
                m_SelectionStartPosition = event->localPos();
                m_SelectionCurrentPosition = event->localPos();
                m_SelectionModifiers = event->modifiers();
                m_PressedItemId = selectedIds.first();
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
        }

        m_SelectionState = SelectionState::Pressed;
        m_SelectionStartPosition = event->localPos();
        m_SelectionCurrentPosition = event->localPos();
        m_SelectionModifiers = event->modifiers();
        m_PressedItemId = HitTestItem(event->localPos());
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF currentMousePosition = event->localPos();
    m_CurrentMousePosition = currentMousePosition;
    const CanvasVector mouseWorldPosition = MapToWorld(currentMousePosition);
    emit MouseWorldPositionChanged(mouseWorldPosition);
    UpdateSnap(mouseWorldPosition);

    if (m_InteractionMode == InteractionMode::PreviewNavigation) {
        if ((event->buttons() & Qt::RightButton)
                || m_PanState == PanState::Idle) {
            event->ignore();
            return;
        }
        if (!(event->buttons() & (Qt::LeftButton | Qt::MiddleButton))) {
            m_PanState = PanState::Idle;
            unsetCursor();
            event->ignore();
            return;
        }
        if (m_PanState == PanState::Pressed) {
            const QPointF delta = currentMousePosition - m_PanStartPosition;
            if (QPointF::dotProduct(delta, delta) < 49.0) {
                event->accept();
                return;
            }
            m_PanState = PanState::Dragging;
            setCursor(Qt::ClosedHandCursor);
        }
        m_Transform.PanBy(currentMousePosition - m_LastMousePosition);
        m_LastMousePosition = currentMousePosition;
        update();
        event->accept();
        return;
    }

    if (m_PanState == PanState::Idle) {
        if (m_SelectionState == SelectionState::Editing) {
            if (!(event->buttons() & Qt::LeftButton)) {
                CancelEdit();
                m_SelectionState = SelectionState::Idle;
                m_SelectionModifiers = Qt::NoModifier;
                m_PressedItemId = 0;
                unsetCursor();
                update();
                event->accept();
                return;
            }

            m_SelectionCurrentPosition = currentMousePosition;
            UpdateEdit(MapToWorld(currentMousePosition));
            update();
            event->accept();
            return;
        }

        if (m_SelectionState != SelectionState::Idle) {
            if (!(event->buttons() & Qt::LeftButton)) {
                ResetMoveState();
                m_SelectionState = SelectionState::Idle;
                m_SelectionModifiers = Qt::NoModifier;
                m_PressedItemId = 0;
                unsetCursor();
                update();
                event->accept();
                return;
            }

            m_SelectionCurrentPosition = currentMousePosition;
            if (m_SelectionState == SelectionState::Pressed) {
                const QPointF delta = currentMousePosition - m_SelectionStartPosition;
                const qreal distanceSquared = QPointF::dotProduct(delta, delta);
                if (distanceSquared >= SELECTION_DRAG_THRESHOLD
                        * SELECTION_DRAG_THRESHOLD) {
                    if (m_PressedItemId != 0 && m_Document) {
                        if (!m_Document->IsItemSelected(m_PressedItemId)) {
                            const bool append = (m_SelectionModifiers
                                                 & Qt::ShiftModifier) != 0;
                            ApplySingleSelection(m_PressedItemId, append);
                        }

                        if (m_Document->IsItemSelected(m_PressedItemId)) {
                            m_MoveItemIds = m_Document->GetSelectedItemIds();
                            m_MoveLastWorldPosition = MapToWorld(
                                        m_SelectionStartPosition);
                            m_MoveTotalWorldDelta = CanvasVector(0.0, 0.0);
                            m_SelectionState = SelectionState::Moving;
                            setCursor(Qt::SizeAllCursor);
                        } else {
                            m_SelectionState = SelectionState::Dragging;
                        }
                    } else {
                        m_SelectionState = SelectionState::Dragging;
                    }
                }
            }

            if (m_SelectionState == SelectionState::Moving) {
                const CanvasVector currentWorldPosition = MapToWorld(
                            currentMousePosition);
                const CanvasVector moveDelta = currentWorldPosition
                        - m_MoveLastWorldPosition;
                if (moveDelta.GetX() != 0.0 || moveDelta.GetY() != 0.0) {
                    if (m_Document) {
                        m_Document->TranslateItems(m_MoveItemIds, moveDelta);
                    }
                    m_MoveTotalWorldDelta += moveDelta;
                    m_MoveLastWorldPosition = currentWorldPosition;
                }
            }

            update();
            event->accept();
            return;
        }

        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!(event->buttons() & Qt::MiddleButton)) {
        m_PanState = PanState::Idle;
        unsetCursor();
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_PanState == PanState::Pressed) {
        const QPointF delta = currentMousePosition - m_PanStartPosition;
        const qreal distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
        if (distanceSquared < 49.0) {
            event->accept();
            return;
        }

        m_PanState = PanState::Dragging;
        setCursor(Qt::ClosedHandCursor);
    }

    m_Transform.PanBy(currentMousePosition - m_LastMousePosition);
    m_LastMousePosition = currentMousePosition;
    UpdateSnap(MapToWorld(currentMousePosition));
    update();
    event->accept();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    m_CurrentMousePosition = event->localPos();
    const CanvasVector mouseWorldPosition = MapToWorld(m_CurrentMousePosition);
    emit MouseWorldPositionChanged(mouseWorldPosition);
    UpdateSnap(mouseWorldPosition);

    if (m_InteractionMode == InteractionMode::PreviewNavigation) {
        if (event->button() == Qt::LeftButton
                || event->button() == Qt::MiddleButton) {
            m_PanState = PanState::Idle;
            unsetCursor();
            event->accept();
            return;
        }
        event->ignore();
        return;
    }

    if (event->button() == Qt::MiddleButton && m_PanState != PanState::Idle) {
        m_PanState = PanState::Idle;
        unsetCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        emit ContextMenuRequested(MapToWorld(event->localPos()), event->pos());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton
            && m_SelectionState == SelectionState::Editing) {
        ResetEditState();
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        m_PressedItemId = 0;
        unsetCursor();
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton
            && m_SelectionState != SelectionState::Idle) {
        m_SelectionCurrentPosition = event->localPos();
        if (m_SelectionState == SelectionState::Moving) {
            ResetMoveState();
            m_SelectionState = SelectionState::Idle;
            m_SelectionModifiers = Qt::NoModifier;
            m_PressedItemId = 0;
            unsetCursor();
            update();
            event->accept();
            return;
        }

        const bool append = (m_SelectionModifiers & Qt::ShiftModifier) != 0;
        if (m_SelectionState == SelectionState::Dragging) {
            ApplyWindowSelection(QRectF(m_SelectionStartPosition,
                                        m_SelectionCurrentPosition),
                                 append);
        } else {
            ApplySingleSelection(HitTestItem(m_SelectionCurrentPosition), append);
        }

        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        m_PressedItemId = 0;
        update();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_InteractionMode == InteractionMode::PreviewNavigation
            && event->button() == Qt::LeftButton) {
        FitAll(10.0);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (size().isEmpty()) {
        return;
    }

    const CanvasRenderContext renderContext(m_Document.data(),
                                            m_Transform,
                                            size());
    const qreal devicePixelRatio = devicePixelRatioF();
    const QSize layerSize(qMax(1, qRound(width() * devicePixelRatio)),
                          qMax(1, qRound(height() * devicePixelRatio)));
    PrepareLayer(&m_PrivateData->m_BackgroundLayer, layerSize,
                 devicePixelRatio);
    {
        QPainter painter(&m_PrivateData->m_BackgroundLayer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (m_CustomRenderer) {
            painter.save();
            m_CustomRenderer->RenderBackground(painter, renderContext);
            painter.restore();
        }
        if (m_GridVisible) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, false);
            DrawBackgroundLayer(painter);
            painter.restore();
        }
    }

    PrepareLayer(&m_PrivateData->m_GeometryLayer, layerSize,
                 devicePixelRatio);
    {
        QPainter painter(&m_PrivateData->m_GeometryLayer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const std::shared_ptr<CanvasGlobalData> globalData = m_Document
                ? m_Document->GetCanvasGlobalData()
                : std::shared_ptr<CanvasGlobalData>();
        if (globalData) {
            painter.save();
            detail::RenderCanvasDataCommands(
                        painter, renderContext,
                        globalData->CalToQStringListPre(), true);
            painter.restore();
        }
        m_PrivateData->m_DefaultRenderer->Render(painter, renderContext);
        if (m_CustomRenderer) {
            painter.save();
            m_CustomRenderer->Render(painter, renderContext);
            painter.restore();
        }
        if (globalData) {
            painter.save();
            detail::RenderCanvasDataCommands(
                        painter, renderContext,
                        globalData->CalToQStringListPost(), true);
            painter.restore();
        }
    }

    PrepareLayer(&m_PrivateData->m_OverlayLayer, layerSize,
                 devicePixelRatio);
    {
        QPainter painter(&m_PrivateData->m_OverlayLayer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        DrawOverlayLayer(painter);
        if (m_CustomRenderer) {
            painter.save();
            m_CustomRenderer->RenderOverlay(painter, renderContext);
            painter.restore();
        }
    }

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.drawImage(QPoint(0, 0), m_PrivateData->m_BackgroundLayer);
    painter.drawImage(QPoint(0, 0), m_PrivateData->m_GeometryLayer);
    painter.drawImage(QPoint(0, 0), m_PrivateData->m_OverlayLayer);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    if (size().isEmpty()) {
        event->ignore();
        return;
    }

    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();
    const Qt::KeyboardModifiers modifiers = event->modifiers();

    if (m_InteractionMode == InteractionMode::PreviewNavigation) {
        if (!pixelDelta.isNull()) {
            if (modifiers == Qt::ControlModifier) {
                const qreal steps = -static_cast<qreal>(pixelDelta.y()) / 100.0;
                if (qFuzzyIsNull(steps)) {
                    event->ignore();
                    return;
                }
                const qreal factor = steps > 0.0
                        ? 1.0 + steps : 1.0 / (1.0 - steps);
                m_Transform.ZoomAt(GetWheelEventPosition(event), QSizeF(size()), factor);
                emit ZoomChanged(m_Transform.GetZoom());
            } else {
                m_Transform.PanBy(QPointF(pixelDelta.x(), pixelDelta.y()));
            }
        } else {
            const bool horizontal = qAbs(static_cast<qint64>(angleDelta.x()))
                    > qAbs(static_cast<qint64>(angleDelta.y()));
            const int delta = horizontal ? angleDelta.x() : angleDelta.y();
            if (delta == 0) {
                event->ignore();
                return;
            }
            if (modifiers == Qt::NoModifier) {
                const qreal factor = std::pow(1.137,
                                               static_cast<qreal>(delta) / 120.0);
                m_Transform.ZoomAt(GetWheelEventPosition(event), QSizeF(size()), factor);
                emit ZoomChanged(m_Transform.GetZoom());
            } else if (modifiers == Qt::ControlModifier) {
                m_Transform.PanBy(horizontal
                                  ? QPointF(-angleDelta.x() / 4.0, 0.0)
                                  : QPointF(0.0, angleDelta.y() / 4.0));
            } else if (modifiers == Qt::ShiftModifier) {
                m_Transform.PanBy(horizontal
                                  ? QPointF(0.0, angleDelta.x() / 4.0)
                                  : QPointF(-angleDelta.y() / 4.0, 0.0));
            } else {
                event->ignore();
                return;
            }
        }
        UpdateSnap(MapToWorld(m_CurrentMousePosition));
        update();
        event->accept();
        return;
    }

    if (modifiers == Qt::ControlModifier) {
        qreal zoomSteps = 0.0;
        if (!pixelDelta.isNull()) {
            zoomSteps = -static_cast<qreal>(pixelDelta.y()) / 100.0;
        } else {
            zoomSteps = static_cast<qreal>(angleDelta.y()) / 120.0;
        }

        if (qFuzzyIsNull(zoomSteps)) {
            event->ignore();
            return;
        }

        const qreal factor = !pixelDelta.isNull()
                ? (zoomSteps > 0.0 ? 1.0 + zoomSteps : 1.0 / (1.0 - zoomSteps))
                : std::pow(1.137, zoomSteps);
        m_Transform.ZoomAt(GetWheelEventPosition(event), QSizeF(size()), factor);
        emit ZoomChanged(m_Transform.GetZoom());
        UpdateSnap(MapToWorld(m_CurrentMousePosition));
        update();
        event->accept();
        return;
    }

    if (modifiers == Qt::ShiftModifier) {
        const QPoint delta = !pixelDelta.isNull() ? pixelDelta : angleDelta / 4;
        if (delta.isNull()) {
            event->ignore();
            return;
        }

        const qreal horizontalDelta = !pixelDelta.isNull() && delta.x() != 0
                ? static_cast<qreal>(delta.x())
                : static_cast<qreal>(delta.y());
        m_Transform.PanBy(QPointF(-horizontalDelta, 0.0));
        UpdateSnap(MapToWorld(m_CurrentMousePosition));
        update();
        event->accept();
        return;
    }

    if (!pixelDelta.isNull()) {
        m_Transform.PanBy(QPointF(pixelDelta.x(), -pixelDelta.y()));
        UpdateSnap(MapToWorld(m_CurrentMousePosition));
        update();
        event->accept();
        return;
    }

    const qreal steps = static_cast<qreal>(angleDelta.y()) / 120.0;
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }

    const qreal factor = std::pow(1.137, steps);
    m_Transform.ZoomAt(GetWheelEventPosition(event), QSizeF(size()), factor);
    emit ZoomChanged(m_Transform.GetZoom());
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
    event->accept();
}

void CanvasView::OnDocumentChanged()
{
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
}

void CanvasView::OnSelectionChanged()
{
    update();
}

void CanvasView::UpdateSnap(const CanvasVector &worldPosition)
{
    CanvasSnapResult result;
    if (m_InteractionMode == InteractionMode::Edit
            && m_SnapEnabled && !size().isEmpty()) {
        const qreal zoom = qMax<qreal>(m_Transform.GetZoom(),
                                       CanvasTransform::GetMinimumZoom());
        const qreal toleranceWorld = 10.0 / zoom;
        const qreal gridSpacing = CalculateCadLabGridSpacing(m_GridSpacing,
                                                              zoom);
        result = CanvasSnapper::FindSnap(m_Document.data(),
                                         worldPosition,
                                         toleranceWorld,
                                         gridSpacing,
                                         m_GeometrySnapEnabled,
                                         m_GridSnapEnabled);
    }

    if (result == m_CurrentSnapResult) {
        return;
    }

    m_CurrentSnapResult = result;
    emit SnapChanged(m_CurrentSnapResult);
    update();
}

bool CanvasView::FitWorldRect(const QRectF &worldRect, qreal marginPixels)
{
    const qreal oldZoom = m_Transform.GetZoom();
    if (!m_Transform.FitRect(worldRect,
                             QSizeF(size()),
                             marginPixels)) {
        return false;
    }

    if (!qFuzzyCompare(oldZoom + 1.0, m_Transform.GetZoom() + 1.0)) {
        emit ZoomChanged(m_Transform.GetZoom());
    }
    UpdateSnap(MapToWorld(m_CurrentMousePosition));
    update();
    return true;
}

void CanvasView::DrawBackgroundLayer(QPainter &painter) const
{
    DrawGrid(painter);
}

void CanvasView::DrawGrid(QPainter &painter) const
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const QSizeF viewportSize(size());
    const CanvasVector first = m_Transform.MapToWorld(QPointF(0.0, 0.0), viewportSize);
    const CanvasVector last = m_Transform.MapToWorld(QPointF(width(), height()), viewportSize);
    const qreal left = qMin(first.GetX(), last.GetX());
    const qreal right = qMax(first.GetX(), last.GetX());
    const qreal bottom = qMin(first.GetY(), last.GetY());
    const qreal top = qMax(first.GetY(), last.GetY());

    const qreal spacing = CalculateCadLabGridSpacing(m_GridSpacing,
                                                      m_Transform.GetZoom());
    const qreal metaSpacing = spacing * 10.0;
    const int firstX = static_cast<int>(std::floor(left / metaSpacing)) - 1;
    const int lastX = static_cast<int>(std::ceil(right / metaSpacing)) + 1;
    const int firstY = static_cast<int>(std::floor(bottom / metaSpacing)) - 1;
    const int lastY = static_cast<int>(std::ceil(top / metaSpacing)) + 1;

    painter.setPen(QPen(QColor(64, 64, 64), 1.0, Qt::DashLine));
    for (int x = firstX; x <= lastX; ++x) {
        const qreal worldX = x * metaSpacing;
        painter.drawLine(QLineF(m_Transform.MapFromWorld(CanvasVector(worldX, bottom), viewportSize),
                                m_Transform.MapFromWorld(CanvasVector(worldX, top), viewportSize)));
    }
    for (int y = firstY; y <= lastY; ++y) {
        const qreal worldY = y * metaSpacing;
        painter.drawLine(QLineF(m_Transform.MapFromWorld(CanvasVector(left, worldY), viewportSize),
                                m_Transform.MapFromWorld(CanvasVector(right, worldY), viewportSize)));
    }
}

void CanvasView::DrawOverlayLayer(QPainter &painter) const
{
    if (m_InteractionMode == InteractionMode::Edit) {
        DrawSelectionBox(painter);
    }

    if (m_AxisVisible) {
        DrawAxes(painter);
    }

    if (m_RulerVisible) {
        DrawRulerLabels(painter);
    }

    if (m_InteractionMode == InteractionMode::Edit) {
        DrawEditHandles(painter);
        DrawSnapMarker(painter);
    }
}

void CanvasView::DrawSelectionBox(QPainter &painter) const
{
    if (m_InteractionMode != InteractionMode::Edit
            || m_SelectionState != SelectionState::Dragging) {
        return;
    }

    const QRectF selectionRect = QRectF(m_SelectionStartPosition,
                                       m_SelectionCurrentPosition).normalized();
    if (selectionRect.isNull()) {
        return;
    }

    const bool windowSelection = m_SelectionCurrentPosition.x()
            >= m_SelectionStartPosition.x();
    const QColor fillColor = windowSelection
            ? CADLAB_WINDOW_SELECTION_COLOR
            : CADLAB_CROSSING_SELECTION_COLOR;
    const Qt::PenStyle borderStyle = windowSelection ? Qt::SolidLine : Qt::DashLine;

    painter.save();
    painter.setPen(QPen(Qt::white, 1.0, borderStyle));
    painter.fillRect(selectionRect, fillColor);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(selectionRect);
    painter.restore();
}

void CanvasView::DrawAxes(QPainter &painter) const
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const QSizeF viewportSize(size());
    const QPointF origin = m_Transform.MapFromWorld(CanvasVector(0.0, 0.0), viewportSize);
    const qreal viewportWidth = static_cast<qreal>(width());
    const qreal viewportHeight = static_cast<qreal>(height());

    const QColor axisXColor(255, 0, 0, 80);
    const QColor axisYColor(0, 255, 0, 80);
    const QColor axisArrowColor(220, 220, 220);

    if (origin.y() >= 0.0 && origin.y() <= viewportHeight) {
        painter.setPen(QPen(axisXColor, 1.0, Qt::SolidLine));
        painter.drawLine(QLineF(0.0, origin.y(), viewportWidth, origin.y()));
    }

    if (origin.x() >= 0.0 && origin.x() <= viewportWidth) {
        painter.setPen(QPen(axisYColor, 1.0, Qt::SolidLine));
        painter.drawLine(QLineF(origin.x(), 0.0, origin.x(), viewportHeight));
    }

    const qreal axisXDirection = m_AxisFlipX ? -1.0 : 1.0;
    const qreal axisYDirection = m_AxisFlipY ? 1.0 : -1.0;
    // The coordinate indicator belongs to the world origin.  Do not move it
    // to a viewport corner when the origin or its labels approach an edge;
    // that would make the indicator screen anchored while geometry keeps
    // following the world transform during panning.
    const QPointF coordinateOrigin = origin;

    const QPointF axisXBase(coordinateOrigin.x() + axisXDirection * 70.0,
                            coordinateOrigin.y());
    const QPointF axisXTip(coordinateOrigin.x() + axisXDirection * 90.0,
                           coordinateOrigin.y());
    const QPointF axisYBase(coordinateOrigin.x(),
                            coordinateOrigin.y() + axisYDirection * 70.0);
    const QPointF axisYTip(coordinateOrigin.x(),
                           coordinateOrigin.y() + axisYDirection * 90.0);

    painter.save();
    painter.setPen(QPen(axisArrowColor, 1.0, Qt::SolidLine));
    painter.drawLine(QLineF(coordinateOrigin.x() - axisXDirection * 8.0,
                            coordinateOrigin.y(),
                            axisXBase.x(),
                            axisXBase.y()));
    painter.drawLine(QLineF(coordinateOrigin.x(),
                            coordinateOrigin.y() - axisYDirection * 8.0,
                            axisYBase.x(),
                            axisYBase.y()));
    painter.drawRect(QRectF(coordinateOrigin.x() - 8.0,
                            coordinateOrigin.y() - 8.0,
                            16.0,
                            16.0));

    painter.drawLine(QLineF(axisYTip, QPointF(axisYBase.x() - 7.0, axisYBase.y())));
    painter.drawLine(QLineF(axisYTip, QPointF(axisYBase.x() + 7.0, axisYBase.y())));
    painter.drawLine(QLineF(axisYBase.x() - 7.0,
                            axisYBase.y(),
                            axisYBase.x() + 7.0,
                            axisYBase.y()));

    painter.drawLine(QLineF(axisXTip, QPointF(axisXBase.x(), axisXBase.y() + 7.0)));
    painter.drawLine(QLineF(axisXTip, QPointF(axisXBase.x(), axisXBase.y() - 7.0)));
    painter.drawLine(QLineF(axisXBase.x(),
                            axisXBase.y() + 7.0,
                            axisXBase.x(),
                            axisXBase.y() - 7.0));

    painter.setPen(QPen(QColor(axisArrowColor.red() / 2,
                               axisArrowColor.green() / 2,
                               axisArrowColor.blue() / 2),
                        1.0,
                        Qt::SolidLine));
    painter.drawLine(QLineF(QPointF(axisYBase.x() - 3.0, axisYBase.y()), axisYTip));
    painter.drawLine(QLineF(QPointF(axisYBase.x() + 3.0, axisYBase.y()), axisYTip));
    painter.drawLine(QLineF(QPointF(axisXBase.x(), axisXBase.y() - 3.0), axisXTip));
    painter.drawLine(QLineF(QPointF(axisXBase.x(), axisXBase.y() + 3.0), axisXTip));

    painter.setPen(axisArrowColor);
    QFont coordinateFont(QStringLiteral("Microsoft YaHei"));
    coordinateFont.setPointSize(18);
    painter.setFont(coordinateFont);
    painter.drawText(QPointF(coordinateOrigin.x() - 6.0,
                             coordinateOrigin.y() + axisYDirection * 98.0),
                     QStringLiteral("Y"));
    painter.drawText(QPointF(coordinateOrigin.x() + axisXDirection * 98.0,
                             coordinateOrigin.y() + 10.0),
                     QStringLiteral("X"));
    painter.restore();
}

void CanvasView::DrawRulerLabels(QPainter &painter) const
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const QSizeF viewportSize(size());
    const CanvasVector first = m_Transform.MapToWorld(QPointF(0.0, 0.0), viewportSize);
    const CanvasVector last = m_Transform.MapToWorld(QPointF(width(), height()), viewportSize);
    const qreal left = qMin(first.GetX(), last.GetX());
    const qreal right = qMax(first.GetX(), last.GetX());
    const qreal bottom = qMin(first.GetY(), last.GetY());
    const qreal top = qMax(first.GetY(), last.GetY());

    const qreal spacing = CalculateCadLabGridSpacing(m_GridSpacing,
                                                      m_Transform.GetZoom());
    const qreal rulerSpacing = spacing * 10.0;
    const int firstX = static_cast<int>(std::floor(left / rulerSpacing)) - 1;
    const int lastX = static_cast<int>(std::ceil(right / rulerSpacing)) + 1;
    const int firstY = static_cast<int>(std::floor(bottom / rulerSpacing)) - 1;
    const int lastY = static_cast<int>(std::ceil(top / rulerSpacing)) + 1;

    painter.save();
    painter.setPen(QColor(255, 255, 0));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 14));

    for (int x = firstX; x <= lastX; ++x) {
        const qreal worldX = x * rulerSpacing;
        const qreal viewportX = m_Transform.MapFromWorld(CanvasVector(worldX, 0.0),
                                                          viewportSize).x();
        painter.drawText(QRectF(viewportX - 50.0,
                                static_cast<qreal>(height() - 20),
                                100.0,
                                20.0),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         QString::number(worldX));
    }

    for (int y = firstY; y <= lastY; ++y) {
        const qreal worldY = y * rulerSpacing;
        const qreal viewportY = m_Transform.MapFromWorld(CanvasVector(0.0, worldY),
                                                          viewportSize).y();
        painter.drawText(QRectF(0.0,
                                viewportY - 10.0,
                                100.0,
                                20.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(worldY));
    }

    painter.restore();
}

void CanvasView::DrawEditHandles(QPainter &painter) const
{
    if (m_InteractionMode != InteractionMode::Edit
            || !m_Document || m_Document->GetSelectedItemCount() != 1
            || size().isEmpty()) {
        return;
    }

    const QVector<ItemId> selectedIds = m_Document->GetSelectedItemIds();
    if (selectedIds.size() != 1) {
        return;
    }

    const ItemId itemId = selectedIds.first();
    const QVector<EditHandlePosition> handles = GetEditHandlePositions(itemId);
    if (handles.isEmpty()) {
        return;
    }

    const QSizeF viewportSize(size());
    const qreal halfSize = EDIT_HANDLE_SIZE_PIXELS * 0.5;
    painter.save();
    painter.setPen(QPen(Qt::white, 1.0, Qt::SolidLine));

    for (const EditHandlePosition &handle : handles) {
        const QPointF viewportPosition = m_Transform.MapFromWorld(
                    handle.worldPosition,
                    viewportSize);
        const bool active = m_SelectionState == SelectionState::Editing
                && m_EditItemId == itemId
                && m_EditHandleType == handle.type
                && (handle.type != EditHandleType::PolylineVertex
                    && handle.type != EditHandleType::SplineControlPoint
                    || m_EditPointIndex == handle.pointIndex);
        painter.setBrush(active
                         ? CADLAB_ACTIVE_EDIT_HANDLE_COLOR
                         : CADLAB_EDIT_HANDLE_COLOR);

        const QRectF handleRect(viewportPosition.x() - halfSize,
                                viewportPosition.y() - halfSize,
                                EDIT_HANDLE_SIZE_PIXELS,
                                EDIT_HANDLE_SIZE_PIXELS);
        if (handle.type == EditHandleType::CircleRadius) {
            painter.drawEllipse(handleRect);
        } else {
            painter.drawRect(handleRect);
        }
    }

    painter.restore();
}

void CanvasView::DrawSnapMarker(QPainter &painter) const
{
    if (m_InteractionMode != InteractionMode::Edit
            || !m_CurrentSnapResult.IsValid() || size().isEmpty()) {
        return;
    }

    const QPointF viewportPosition = m_Transform.MapFromWorld(
                m_CurrentSnapResult.position,
                QSizeF(size()));
    const qreal markerSize = 8.0;
    const QRectF markerRect(viewportPosition.x() - markerSize * 0.5,
                            viewportPosition.y() - markerSize * 0.5,
                            markerSize,
                            markerSize);

    painter.save();
    painter.setPen(QPen(Qt::white, 1.0, Qt::SolidLine));
    painter.setBrush(CADLAB_SNAP_MARKER_COLOR);
    if (m_CurrentSnapResult.type == CanvasSnapType::Grid) {
        painter.drawRect(markerRect);
    } else {
        painter.drawEllipse(markerRect);
    }
    painter.restore();
}

QVector<CanvasView::EditHandlePosition> CanvasView::GetEditHandlePositions(
        ItemId id) const
{
    QVector<EditHandlePosition> handles;
    if (!m_Document || id == 0) {
        return handles;
    }

    const CanvasEntity *entity = m_Document->GetEntity(id);
    if (!entity || !entity->GetStyle().visible || !entity->IsValid()) {
        return handles;
    }

    switch (entity->GetType()) {
    case CanvasEntityType::Point: {
        const CanvasPoint *point = dynamic_cast<const CanvasPoint *>(entity);
        if (point) {
            handles.append({EditHandleType::PointCenter,
                            point->GetPosition()});
        }
        break;
    }
    case CanvasEntityType::Line: {
        const CanvasLine *line = dynamic_cast<const CanvasLine *>(entity);
        if (line) {
            handles.append({EditHandleType::LineStart,
                            line->GetStartPoint()});
            handles.append({EditHandleType::LineEnd,
                            line->GetEndPoint()});
        }
        break;
    }
    case CanvasEntityType::Arc: {
        const CanvasArc *arc = dynamic_cast<const CanvasArc *>(entity);
        if (arc) {
            handles.append({EditHandleType::ArcCenter,
                            arc->GetCenter()});
            handles.append({EditHandleType::ArcStart,
                            arc->GetStartPoint()});
            handles.append({EditHandleType::ArcEnd,
                            arc->GetEndPoint()});
        }
        break;
    }
    case CanvasEntityType::Circle: {
        const CanvasCircle *circle =
                dynamic_cast<const CanvasCircle *>(entity);
        if (circle) {
            handles.append({EditHandleType::CircleCenter,
                            circle->GetCenter()});
            handles.append({EditHandleType::CircleRadius,
                            circle->GetCenter()
                            + CanvasVector(circle->GetRadius(), 0.0)});
        }
        break;
    }
    case CanvasEntityType::Ellipse: {
        const CanvasEllipse *ellipse =
                dynamic_cast<const CanvasEllipse *>(entity);
        if (!ellipse) {
            break;
        }

        const CanvasVector center = ellipse->GetCenter();
        const CanvasVector majorAxis = ellipse->GetMajorAxis();
        const CanvasVector minorAxis(-majorAxis.GetY()
                                     * ellipse->GetMinorMajorRatio(),
                                     majorAxis.GetX()
                                     * ellipse->GetMinorMajorRatio());
        handles.append({EditHandleType::EllipseCenter, center});
        handles.append({EditHandleType::EllipseMajorAxis,
                        center + majorAxis});
        handles.append({EditHandleType::EllipseMinorAxis,
                        center + minorAxis});
        if (ellipse->IsArc()) {
            handles.append({EditHandleType::EllipseStart,
                            CalculateEllipsePoint(*ellipse,
                                                  ellipse->GetStartParameter())});
            handles.append({EditHandleType::EllipseEnd,
                            CalculateEllipsePoint(*ellipse,
                                                  ellipse->GetEndParameter())});
        }
        break;
    }
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(entity);
        if (!polyline) {
            break;
        }
        const int vertexCount = polyline->IsClosed()
                ? polyline->GetSegmentCount()
                : polyline->GetSegmentCount() + 1;
        for (int index = 0; index < vertexCount; ++index) {
            if (!IsEditablePolylineVertex(*polyline, index)) {
                continue;
            }
            const CanvasVector position = GetPolylineVertex(*polyline, index);
            if (position.IsValid()) {
                handles.append({EditHandleType::PolylineVertex,
                                position,
                                index});
            }
        }
        break;
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *spline =
                dynamic_cast<const CanvasSpline *>(entity);
        if (spline) {
            const QVector<CanvasVector> &controlPoints =
                    spline->GetControlPoints();
            for (int index = 0; index < controlPoints.size(); ++index) {
                handles.append({EditHandleType::SplineControlPoint,
                                controlPoints.at(index),
                                index});
            }
        }
        break;
    }
    case CanvasEntityType::GroupContainer: {
        const CanvasGroupContainer *group =
                dynamic_cast<const CanvasGroupContainer *>(entity);
        if (group) {
            const CanvasVector referencePoint =
                    GetFirstEntityReferencePoint(*group);
            if (referencePoint.IsValid()) {
                handles.append({EditHandleType::GroupReferencePoint,
                                referencePoint});
            }
        }
        break;
    }
    case CanvasEntityType::Invalid:
        break;
    }

    return handles;
}

CanvasView::EditHandleType CanvasView::HitTestEditHandle(
        const QPointF &viewportPosition,
        int *pointIndex) const
{
    if (pointIndex) {
        *pointIndex = -1;
    }
    if (!m_Document || m_Document->GetSelectedItemCount() != 1
            || size().isEmpty()) {
        return EditHandleType::None;
    }

    const QVector<ItemId> selectedIds = m_Document->GetSelectedItemIds();
    if (selectedIds.size() != 1) {
        return EditHandleType::None;
    }

    const QVector<EditHandlePosition> handles =
            GetEditHandlePositions(selectedIds.first());
    const qreal toleranceSquared = EDIT_HANDLE_HIT_TOLERANCE_PIXELS
            * EDIT_HANDLE_HIT_TOLERANCE_PIXELS;
    qreal bestDistanceSquared = std::numeric_limits<qreal>::max();
    EditHandleType hitType = EditHandleType::None;
    int hitPointIndex = -1;
    const QSizeF viewportSize(size());

    for (int index = handles.size() - 1; index >= 0; --index) {
        const EditHandlePosition &handle = handles.at(index);
        const QPointF handlePosition = m_Transform.MapFromWorld(
                    handle.worldPosition,
                    viewportSize);
        const QPointF delta = viewportPosition - handlePosition;
        const qreal distanceSquared = QPointF::dotProduct(delta, delta);
        if (distanceSquared <= toleranceSquared
                && distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            hitType = handle.type;
            hitPointIndex = handle.pointIndex;
        }
    }

    if (pointIndex) {
        *pointIndex = hitPointIndex;
    }
    return hitType;
}

bool CanvasView::BeginEdit(ItemId id,
                           EditHandleType handleType,
                           int pointIndex)
{
    if (!m_Document || id == 0 || handleType == EditHandleType::None
            || !m_Document->IsItemSelected(id)) {
        return false;
    }

    const CanvasEntity *entity = m_Document->GetEntity(id);
    if (!entity || !entity->IsValid()) {
        return false;
    }

    m_EditOriginalEntity = entity->Clone();
    if (m_EditOriginalEntity.isNull()
            || m_EditOriginalEntity->GetType() != entity->GetType()) {
        ResetEditState();
        return false;
    }

    m_EditItemId = id;
    m_EditEntityType = entity->GetType();
    m_EditHandleType = handleType;
    m_EditPointIndex = pointIndex;
    m_EditGeometryChanged = false;

    bool isHandleValid = false;
    switch (m_EditEntityType) {
    case CanvasEntityType::Point:
        isHandleValid = handleType == EditHandleType::PointCenter;
        break;
    case CanvasEntityType::Line:
        isHandleValid = handleType == EditHandleType::LineStart
                || handleType == EditHandleType::LineEnd;
        break;
    case CanvasEntityType::Arc:
        isHandleValid = handleType == EditHandleType::ArcCenter
                || handleType == EditHandleType::ArcStart
                || handleType == EditHandleType::ArcEnd;
        break;
    case CanvasEntityType::Circle:
        isHandleValid = handleType == EditHandleType::CircleCenter
                || handleType == EditHandleType::CircleRadius;
        break;
    case CanvasEntityType::Ellipse:
        isHandleValid = handleType == EditHandleType::EllipseCenter
                || handleType == EditHandleType::EllipseMajorAxis
                || handleType == EditHandleType::EllipseMinorAxis
                || handleType == EditHandleType::EllipseStart
                || handleType == EditHandleType::EllipseEnd;
        break;
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *polyline =
                dynamic_cast<const CanvasPolyline *>(
                    m_EditOriginalEntity.data());
        isHandleValid = polyline
                && handleType == EditHandleType::PolylineVertex
                && IsEditablePolylineVertex(*polyline, pointIndex);
        break;
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *spline =
                dynamic_cast<const CanvasSpline *>(m_EditOriginalEntity.data());
        isHandleValid = spline
                && handleType == EditHandleType::SplineControlPoint
                && pointIndex >= 0
                && pointIndex < spline->GetControlPoints().size();
        break;
    }
    case CanvasEntityType::GroupContainer:
    case CanvasEntityType::Invalid:
        break;
    }

    if (!isHandleValid) {
        ResetEditState();
        return false;
    }

    return true;
}

bool CanvasView::UpdateEdit(const CanvasVector &worldPosition)
{
    if (!m_Document || m_EditItemId == 0
            || m_EditHandleType == EditHandleType::None
            || m_EditOriginalEntity.isNull()
            || !worldPosition.IsValid()) {
        return false;
    }

    QSharedPointer<CanvasEntity> updated = m_EditOriginalEntity->Clone();
    if (updated.isNull()) {
        return false;
    }

    bool geometryUpdated = false;
    switch (m_EditEntityType) {
    case CanvasEntityType::Point: {
        CanvasPoint *point = dynamic_cast<CanvasPoint *>(updated.data());
        geometryUpdated = point
                && m_EditHandleType == EditHandleType::PointCenter
                && point->SetPosition(worldPosition);
        break;
    }
    case CanvasEntityType::Line: {
        const CanvasLine *original = dynamic_cast<const CanvasLine *>(
                    m_EditOriginalEntity.data());
        CanvasLine *line = dynamic_cast<CanvasLine *>(updated.data());
        if (original && line) {
            CanvasVector startPoint = original->GetStartPoint();
            CanvasVector endPoint = original->GetEndPoint();
            if (m_EditHandleType == EditHandleType::LineStart) {
                startPoint = worldPosition;
            } else if (m_EditHandleType == EditHandleType::LineEnd) {
                endPoint = worldPosition;
            }
            geometryUpdated = line->SetGeometry(startPoint, endPoint);
        }
        break;
    }
    case CanvasEntityType::Arc: {
        const CanvasArc *original = dynamic_cast<const CanvasArc *>(
                    m_EditOriginalEntity.data());
        CanvasArc *arc = dynamic_cast<CanvasArc *>(updated.data());
        if (original && arc) {
            CanvasVector center = original->GetCenter();
            qreal radius = original->GetRadius();
            qreal startAngle = original->GetStartAngle();
            qreal endAngle = original->GetEndAngle();
            if (m_EditHandleType == EditHandleType::ArcCenter) {
                center = worldPosition;
            } else if (m_EditHandleType == EditHandleType::ArcStart
                       || m_EditHandleType == EditHandleType::ArcEnd) {
                const CanvasVector delta = worldPosition - center;
                radius = delta.Length();
                if (radius <= 0.0
                        || !std::isfinite(static_cast<double>(radius))) {
                    return false;
                }
                const qreal angle = std::atan2(delta.GetY(), delta.GetX());
                if (m_EditHandleType == EditHandleType::ArcStart) {
                    startAngle = angle;
                } else {
                    endAngle = angle;
                }
            }
            geometryUpdated = arc->SetGeometry(center, radius,
                                                startAngle, endAngle);
        }
        break;
    }
    case CanvasEntityType::Circle: {
        const CanvasCircle *original = dynamic_cast<const CanvasCircle *>(
                    m_EditOriginalEntity.data());
        CanvasCircle *circle = dynamic_cast<CanvasCircle *>(updated.data());
        if (original && circle) {
            CanvasVector center = original->GetCenter();
            qreal radius = original->GetRadius();
            if (m_EditHandleType == EditHandleType::CircleCenter) {
                center = worldPosition;
            } else if (m_EditHandleType == EditHandleType::CircleRadius) {
                radius = (worldPosition - center).Length();
                if (radius <= 0.0
                        || !std::isfinite(static_cast<double>(radius))) {
                    return false;
                }
            }
            geometryUpdated = circle->SetGeometry(center, radius);
        }
        break;
    }
    case CanvasEntityType::Ellipse: {
        const CanvasEllipse *original = dynamic_cast<const CanvasEllipse *>(
                    m_EditOriginalEntity.data());
        CanvasEllipse *ellipse = dynamic_cast<CanvasEllipse *>(updated.data());
        if (!original || !ellipse) {
            break;
        }

        CanvasVector center = original->GetCenter();
        CanvasVector majorAxis = original->GetMajorAxis();
        qreal ratio = original->GetMinorMajorRatio();
        qreal startParameter = original->GetStartParameter();
        qreal endParameter = original->GetEndParameter();
        if (m_EditHandleType == EditHandleType::EllipseCenter) {
            center = worldPosition;
        } else if (m_EditHandleType == EditHandleType::EllipseMajorAxis) {
            majorAxis = worldPosition - center;
        } else if (m_EditHandleType == EditHandleType::EllipseMinorAxis) {
            const qreal majorLength = majorAxis.Length();
            if (majorLength <= 0.0
                    || !std::isfinite(static_cast<double>(majorLength))) {
                return false;
            }
            const CanvasVector unitMinorAxis(
                        -majorAxis.GetY() / majorLength,
                        majorAxis.GetX() / majorLength);
            const qreal minorLength =
                    (worldPosition - center).Dot(unitMinorAxis);
            ratio = minorLength / majorLength;
        } else if (m_EditHandleType == EditHandleType::EllipseStart
                   || m_EditHandleType == EditHandleType::EllipseEnd) {
            const qreal majorLengthSquared = majorAxis.SquaredLength();
            const CanvasVector minorAxis(-majorAxis.GetY() * ratio,
                                         majorAxis.GetX() * ratio);
            const qreal minorLengthSquared = minorAxis.SquaredLength();
            if (majorLengthSquared <= 0.0 || minorLengthSquared <= 0.0) {
                return false;
            }
            const CanvasVector delta = worldPosition - center;
            const qreal majorCoordinate =
                    delta.Dot(majorAxis) / majorLengthSquared;
            const qreal minorCoordinate =
                    delta.Dot(minorAxis) / minorLengthSquared;
            if (!std::isfinite(static_cast<double>(majorCoordinate))
                    || !std::isfinite(static_cast<double>(minorCoordinate))) {
                return false;
            }
            const qreal parameter = std::atan2(minorCoordinate,
                                                majorCoordinate);
            if (m_EditHandleType == EditHandleType::EllipseStart) {
                startParameter = parameter;
            } else {
                endParameter = parameter;
            }
        }
        geometryUpdated = ellipse->SetGeometry(center, majorAxis, ratio,
                                               original->IsArc(),
                                               startParameter,
                                               endParameter);
        break;
    }
    case CanvasEntityType::Polyline: {
        const CanvasPolyline *polyline = dynamic_cast<const CanvasPolyline *>(
                    m_EditOriginalEntity.data());
        if (polyline
                && m_EditHandleType == EditHandleType::PolylineVertex) {
            geometryUpdated = TryMovePolylineVertex(*polyline,
                                                    m_EditPointIndex,
                                                    worldPosition,
                                                    &updated);
        }
        break;
    }
    case CanvasEntityType::Spline: {
        const CanvasSpline *original = dynamic_cast<const CanvasSpline *>(
                    m_EditOriginalEntity.data());
        CanvasSpline *spline = dynamic_cast<CanvasSpline *>(updated.data());
        if (original && spline
                && m_EditHandleType == EditHandleType::SplineControlPoint
                && m_EditPointIndex >= 0
                && m_EditPointIndex < original->GetControlPoints().size()) {
            QVector<CanvasVector> controlPoints = original->GetControlPoints();
            controlPoints[m_EditPointIndex] = worldPosition;
            geometryUpdated = spline->SetData(original->GetDegree(),
                                              controlPoints,
                                              original->GetKnots(),
                                              original->GetWeights(),
                                              original->IsClosed(),
                                              original->IsPeriodic());
        }
        break;
    }
    case CanvasEntityType::GroupContainer:
        return false;
    case CanvasEntityType::Invalid:
        return false;
    }

    if (!geometryUpdated || !updated->IsValid()
            || !m_Document->ReplaceEntity(m_EditItemId, *updated)) {
        return false;
    }
    m_EditGeometryChanged = true;
    return true;
}

void CanvasView::ResetEditState()
{
    m_EditHandleType = EditHandleType::None;
    m_EditPointIndex = -1;
    m_EditItemId = 0;
    m_EditEntityType = CanvasEntityType::Invalid;
    m_EditGeometryChanged = false;
    m_EditOriginalEntity.clear();
}

void CanvasView::CancelEdit()
{
    if (m_Document && m_EditItemId != 0 && m_EditGeometryChanged
            && !m_EditOriginalEntity.isNull()) {
        m_Document->ReplaceEntity(m_EditItemId, *m_EditOriginalEntity);
    }

    ResetEditState();
}

ItemId CanvasView::HitTestItem(const QPointF &viewportPosition) const
{
    if (!m_Document || size().isEmpty()) {
        return 0;
    }

    const CanvasVector worldPosition = MapToWorld(viewportPosition);
    const qreal zoom = qMax<qreal>(m_Transform.GetZoom(),
                                   CanvasTransform::GetMinimumZoom());
    const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);
    qreal bestDistanceSquared = std::numeric_limits<qreal>::max();
    ItemId hitId = 0;

    const auto considerHit = [&bestDistanceSquared, &hitId](ItemId id,
                                                             qreal distanceSquared,
                                                             qreal toleranceSquared) {
        if (distanceSquared > toleranceSquared) {
            return;
        }

        if (hitId == 0 || distanceSquared < bestDistanceSquared
                || (qFuzzyCompare(distanceSquared, bestDistanceSquared) && id > hitId)) {
            bestDistanceSquared = distanceSquared;
            hitId = id;
        }
    };

    const QVector<ItemId> ids = m_Document->GetItemIds();
    for (int itemIndex = ids.size() - 1; itemIndex >= 0; --itemIndex) {
        const ItemId id = ids.at(itemIndex);
        const CanvasEntity *entity = m_Document->GetEntity(id);
        if (!entity) {
            continue;
        }
        QVector<EntitySelectionPath> paths;
        CollectEntitySelectionPaths(*entity, maximumWorldError, &paths);
        for (const EntitySelectionPath &path : paths) {
            const qreal distanceSquared = DistanceSquaredToPath(worldPosition,
                                                                 path.points);
            considerHit(id, distanceSquared,
                        CalculateHitToleranceSquared(zoom, path.displaySize));
        }
    }

    return hitId;
}

QVector<ItemId> CanvasView::CollectWindowSelection(const QRectF &worldRect,
                                                   bool crossing) const
{
    QVector<ItemId> selectedIds;
    if (!m_Document) {
        return selectedIds;
    }

    const QRectF normalizedRect = worldRect.normalized();
    const qreal zoom = qMax<qreal>(m_Transform.GetZoom(),
                                   CanvasTransform::GetMinimumZoom());
    const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);
    const QVector<ItemId> ids = m_Document->GetItemIds();
    for (const ItemId id : ids) {
        const CanvasEntity *entity = m_Document->GetEntity(id);
        if (!entity) {
            continue;
        }

        QVector<EntitySelectionPath> paths;
        CollectEntitySelectionPaths(*entity, maximumWorldError, &paths);
        const bool selected = crossing
                ? DoEntityPathsIntersectRect(paths, normalizedRect)
                : AreEntityPathsContained(paths, normalizedRect);
        if (selected) {
            selectedIds.append(id);
        }
    }

    return selectedIds;
}

void CanvasView::ApplySingleSelection(ItemId id, bool append)
{
    if (!m_Document || id == 0) {
        return;
    }

    if (append || m_Document->IsItemSelected(id)) {
        m_Document->ToggleItemSelection(id);
        return;
    }

    // A normal click on another unselected item keeps the existing selection.
    // Clicking an already selected item above remains the deselect operation.
    m_Document->SelectItem(id);
}

void CanvasView::ApplyWindowSelection(const QRectF &viewportRect, bool append)
{
    if (!m_Document) {
        return;
    }

    const CanvasVector worldStart = MapToWorld(viewportRect.topLeft());
    const CanvasVector worldEnd = MapToWorld(viewportRect.bottomRight());
    const bool crossing = m_SelectionCurrentPosition.x()
            < m_SelectionStartPosition.x();
    const QVector<ItemId> selectedIds = CollectWindowSelection(
                QRectF(QPointF(worldStart.GetX(), worldStart.GetY()),
                       QPointF(worldEnd.GetX(), worldEnd.GetY())),
                                                               crossing);
    if (selectedIds.isEmpty()) {
        return;
    }

    if (append) {
        m_Document->ToggleItems(selectedIds);
    } else {
        m_Document->SelectItems(selectedIds, false, false);
    }
}

void CanvasView::ResetMoveState()
{
    m_MoveItemIds.clear();
    m_MoveLastWorldPosition = CanvasVector(0.0, 0.0);
    m_MoveTotalWorldDelta = CanvasVector(0.0, 0.0);
}

void CanvasView::CancelMove()
{
    if (m_SelectionState == SelectionState::Moving
            && m_Document
            && (m_MoveTotalWorldDelta.GetX() != 0.0
                || m_MoveTotalWorldDelta.GetY() != 0.0)) {
        m_Document->TranslateItems(m_MoveItemIds,
                                   -m_MoveTotalWorldDelta);
    }

    ResetMoveState();
}

} // namespace mini_graph
