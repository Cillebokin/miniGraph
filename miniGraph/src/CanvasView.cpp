#include "MiniGraph/CanvasView.h"

#include "CanvasPainterRenderer.h"
#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasGeometry.h"
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

qreal DistanceSquaredToSegment(const QPointF &point,
                               const QPointF &start,
                               const QPointF &end)
{
    const QPointF direction = end - start;
    const qreal lengthSquared = QPointF::dotProduct(direction, direction);
    if (lengthSquared <= 0.0) {
        const QPointF delta = point - start;
        return QPointF::dotProduct(delta, delta);
    }

    const QPointF pointDelta = point - start;
    const qreal parameter = qBound<qreal>(0.0,
                                          QPointF::dotProduct(pointDelta, direction)
                                                  / lengthSquared,
                                          1.0);
    const QPointF closestPoint = start + direction * parameter;
    const QPointF delta = point - closestPoint;
    return QPointF::dotProduct(delta, delta);
}

bool SegmentIntersectsRect(const QPointF &start,
                           const QPointF &end,
                           const QRectF &rect)
{
    if (rect.contains(start) || rect.contains(end)) {
        return true;
    }

    const QLineF segment(start, end);
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

bool PolylineIntersectsRect(const QVector<QPointF> &points,
                            const QRectF &rect,
                            bool closed = false)
{
    if (points.isEmpty()) {
        return false;
    }

    for (const QPointF &point : points) {
        if (rect.contains(point)) {
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

    if (closed && points.size() > 2
            && SegmentIntersectsRect(points.last(), points.first(), rect)) {
        return true;
    }

    return false;
}

bool IsGeometryBoundsContained(const QRectF &geometryBounds,
                               const QRectF &selectionRect)
{
    return selectionRect.contains(geometryBounds.topLeft())
            && selectionRect.contains(geometryBounds.bottomRight());
}

qreal DistanceSquaredToArc(const QPointF &point,
                           const StruArcGeometry &geometry)
{
    const QPointF delta = point - geometry.center;
    const qreal distance = std::hypot(delta.x(), delta.y());
    if (distance > 0.0 && IsAngleOnArc(geometry, std::atan2(delta.y(), delta.x()))) {
        const qreal radialDelta = distance - geometry.radius;
        return radialDelta * radialDelta;
    }

    const QPointF startPoint = CalculateArcPoint(geometry, geometry.startAngle);
    const QPointF endPoint = CalculateArcPoint(geometry, geometry.endAngle);
    const QPointF startDelta = point - startPoint;
    const QPointF endDelta = point - endPoint;
    return qMin(QPointF::dotProduct(startDelta, startDelta),
                QPointF::dotProduct(endDelta, endDelta));
}

qreal DistanceSquaredToCircle(const QPointF &point,
                              const StruCircleGeometry &geometry)
{
    const QPointF delta = point - geometry.center;
    const qreal distance = std::hypot(delta.x(), delta.y());
    const qreal radialDelta = distance - geometry.radius;
    return radialDelta * radialDelta;
}

qreal DistanceSquaredToPolyline(const QPointF &point,
                                const StruPolylineGeometry &geometry)
{
    qreal bestDistanceSquared = std::numeric_limits<qreal>::max();
    for (int index = 1; index < geometry.vertices.size(); ++index) {
        bestDistanceSquared = qMin(
                    bestDistanceSquared,
                    DistanceSquaredToSegment(point,
                                             geometry.vertices.at(index - 1),
                                             geometry.vertices.at(index)));
    }

    if (geometry.closed && geometry.vertices.size() > 2) {
        bestDistanceSquared = qMin(
                    bestDistanceSquared,
                    DistanceSquaredToSegment(point,
                                             geometry.vertices.last(),
                                             geometry.vertices.first()));
    }

    return bestDistanceSquared;
}

qreal CalculateHitToleranceSquared(qreal zoom, qreal displaySize)
{
    const qreal halfDisplaySize = qMax<qreal>(0.0, displaySize) * 0.5;
    const qreal pixelTolerance = qMax(SELECTION_HIT_TOLERANCE_PIXELS,
                                      halfDisplaySize);
    const qreal worldTolerance = pixelTolerance / zoom;
    return worldTolerance * worldTolerance;
}

} // namespace

class CanvasViewPrivate
{
public:
    CanvasViewPrivate()
        : m_DefaultRenderer(new CanvasPainterRenderer)
    {
    }

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
    , m_EditVertexIndex(-1)
    , m_EditItemId(0)
    , m_EditEntityType(CanvasEntityType::Invalid)
    , m_EditGeometryChanged(false)
    , m_PrivateData(new CanvasViewPrivate)
{
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

QPointF CanvasView::MapToWorld(const QPointF &viewportPosition) const
{
    if (size().isEmpty()) {
        return QPointF();
    }

    return m_Transform.MapToWorld(viewportPosition, QSizeF(size()));
}

QPointF CanvasView::MapFromWorld(const QPointF &worldPosition) const
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

void CanvasView::CenterOnWorld(const QPointF &worldPosition)
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
    const QPointF mouseWorldPosition = MapToWorld(m_CurrentMousePosition);
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
            int vertexIndex = -1;
            const EditHandleType handleType = HitTestEditHandle(event->localPos(),
                                                                 &vertexIndex);
            if (selectedIds.size() == 1
                    && handleType != EditHandleType::None
                    && BeginEdit(selectedIds.first(), handleType, vertexIndex)) {
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
    const QPointF mouseWorldPosition = MapToWorld(currentMousePosition);
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
                            m_MoveTotalWorldDelta = QPointF(0.0, 0.0);
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
                const QPointF currentWorldPosition = MapToWorld(
                            currentMousePosition);
                const QPointF moveDelta = currentWorldPosition
                        - m_MoveLastWorldPosition;
                if (moveDelta.x() != 0.0 || moveDelta.y() != 0.0) {
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
    const QPointF mouseWorldPosition = MapToWorld(m_CurrentMousePosition);
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
    const auto createLayer = [layerSize, devicePixelRatio]() {
        QImage layer(layerSize, QImage::Format_ARGB32_Premultiplied);
        layer.setDevicePixelRatio(devicePixelRatio);
        layer.fill(Qt::transparent);
        return layer;
    };

    QImage backgroundLayer = createLayer();
    {
        QPainter painter(&backgroundLayer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (m_CustomRenderer) {
            m_CustomRenderer->RenderBackground(painter, renderContext);
        }
        if (m_GridVisible) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, false);
            DrawBackgroundLayer(painter);
            painter.restore();
        }
    }

    QImage geometryLayer = createLayer();
    {
        QPainter painter(&geometryLayer);
        m_PrivateData->m_DefaultRenderer->Render(painter, renderContext);
        if (m_CustomRenderer) {
            m_CustomRenderer->Render(painter, renderContext);
        }
    }

    QImage overlayLayer = createLayer();
    {
        QPainter painter(&overlayLayer);
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
    painter.drawImage(QPoint(0, 0), backgroundLayer);
    painter.drawImage(QPoint(0, 0), geometryLayer);
    painter.drawImage(QPoint(0, 0), overlayLayer);
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

void CanvasView::UpdateSnap(const QPointF &worldPosition)
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
    const QPointF first = m_Transform.MapToWorld(QPointF(0.0, 0.0), viewportSize);
    const QPointF last = m_Transform.MapToWorld(QPointF(width(), height()), viewportSize);
    const qreal left = qMin(first.x(), last.x());
    const qreal right = qMax(first.x(), last.x());
    const qreal bottom = qMin(first.y(), last.y());
    const qreal top = qMax(first.y(), last.y());

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
        painter.drawLine(QLineF(m_Transform.MapFromWorld(QPointF(worldX, bottom), viewportSize),
                                m_Transform.MapFromWorld(QPointF(worldX, top), viewportSize)));
    }
    for (int y = firstY; y <= lastY; ++y) {
        const qreal worldY = y * metaSpacing;
        painter.drawLine(QLineF(m_Transform.MapFromWorld(QPointF(left, worldY), viewportSize),
                                m_Transform.MapFromWorld(QPointF(right, worldY), viewportSize)));
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
    const QPointF origin = m_Transform.MapFromWorld(QPointF(0.0, 0.0), viewportSize);
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
    const QPointF first = m_Transform.MapToWorld(QPointF(0.0, 0.0), viewportSize);
    const QPointF last = m_Transform.MapToWorld(QPointF(width(), height()), viewportSize);
    const qreal left = qMin(first.x(), last.x());
    const qreal right = qMax(first.x(), last.x());
    const qreal bottom = qMin(first.y(), last.y());
    const qreal top = qMax(first.y(), last.y());

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
        const qreal viewportX = m_Transform.MapFromWorld(QPointF(worldX, 0.0),
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
        const qreal viewportY = m_Transform.MapFromWorld(QPointF(0.0, worldY),
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
                    || m_EditVertexIndex == handle.vertexIndex);
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

    StruCanvasStyle style;
    if (!m_Document->GetItemStyle(id, &style) || !style.visible) {
        return handles;
    }

    const CanvasEntityType type = m_Document->GetItemType(id);
    switch (type) {
    case CanvasEntityType::Point: {
        for (const StruPointPrimitive &point : m_Document->GetPointPrimitives()) {
            if (point.id == id) {
                handles.append({EditHandleType::PointCenter,
                                point.geometry.position});
                break;
            }
        }
        break;
    }
    case CanvasEntityType::Line: {
        for (const StruLinePrimitive &line : m_Document->GetLinePrimitives()) {
            if (line.id == id) {
                handles.append({EditHandleType::LineStart,
                                line.geometry.startPoint});
                handles.append({EditHandleType::LineEnd,
                                line.geometry.endPoint});
                break;
            }
        }
        break;
    }
    case CanvasEntityType::Arc: {
        for (const StruArcPrimitive &arc : m_Document->GetArcPrimitives()) {
            if (arc.id == id) {
                handles.append({EditHandleType::ArcCenter,
                                arc.geometry.center});
                handles.append({EditHandleType::ArcStart,
                                CalculateArcPoint(arc.geometry,
                                                  arc.geometry.startAngle)});
                handles.append({EditHandleType::ArcEnd,
                                CalculateArcPoint(arc.geometry,
                                                  arc.geometry.endAngle)});
                break;
            }
        }
        break;
    }
    case CanvasEntityType::Circle: {
        for (const StruCirclePrimitive &circle : m_Document->GetCirclePrimitives()) {
            if (circle.id == id) {
                handles.append({EditHandleType::CircleCenter,
                                circle.geometry.center});
                handles.append({EditHandleType::CircleRadius,
                                circle.geometry.center
                                + QPointF(circle.geometry.radius, 0.0)});
                break;
            }
        }
        break;
    }
    case CanvasEntityType::Polyline: {
        for (const StruPolylinePrimitive &polyline : m_Document->GetPolylinePrimitives()) {
            if (polyline.id != id) {
                continue;
            }
            for (int index = 0; index < polyline.geometry.vertices.size(); ++index) {
                handles.append({EditHandleType::PolylineVertex,
                                polyline.geometry.vertices.at(index),
                                index});
            }
            break;
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
        int *vertexIndex) const
{
    if (vertexIndex) {
        *vertexIndex = -1;
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
    int hitVertexIndex = -1;
    const QSizeF viewportSize(size());

    for (const EditHandlePosition &handle : handles) {
        const QPointF handlePosition = m_Transform.MapFromWorld(
                    handle.worldPosition,
                    viewportSize);
        const QPointF delta = viewportPosition - handlePosition;
        const qreal distanceSquared = QPointF::dotProduct(delta, delta);
        if (distanceSquared <= toleranceSquared
                && distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            hitType = handle.type;
            hitVertexIndex = handle.vertexIndex;
        }
    }

    if (vertexIndex) {
        *vertexIndex = hitVertexIndex;
    }
    return hitType;
}

bool CanvasView::BeginEdit(ItemId id,
                           EditHandleType handleType,
                           int vertexIndex)
{
    if (!m_Document || id == 0 || handleType == EditHandleType::None
            || !m_Document->IsItemSelected(id)) {
        return false;
    }

    m_EditItemId = id;
    m_EditEntityType = m_Document->GetItemType(id);
    m_EditHandleType = handleType;
    m_EditVertexIndex = vertexIndex;
    m_EditGeometryChanged = false;

    bool loaded = false;
    switch (m_EditEntityType) {
    case CanvasEntityType::Point:
        loaded = handleType == EditHandleType::PointCenter
                && m_Document->GetPointGeometry(id,
                                                &m_EditOriginalPointGeometry);
        break;
    case CanvasEntityType::Line:
        loaded = (handleType == EditHandleType::LineStart
                  || handleType == EditHandleType::LineEnd)
                && m_Document->GetLineGeometry(id,
                                               &m_EditOriginalLineGeometry);
        break;
    case CanvasEntityType::Arc:
        loaded = (handleType == EditHandleType::ArcCenter
                  || handleType == EditHandleType::ArcStart
                  || handleType == EditHandleType::ArcEnd)
                && m_Document->GetArcGeometry(id,
                                              &m_EditOriginalArcGeometry);
        break;
    case CanvasEntityType::Circle:
        loaded = (handleType == EditHandleType::CircleCenter
                  || handleType == EditHandleType::CircleRadius)
                && m_Document->GetCircleGeometry(id,
                                                 &m_EditOriginalCircleGeometry);
        break;
    case CanvasEntityType::Polyline:
        loaded = handleType == EditHandleType::PolylineVertex
                && m_Document->GetPolylineGeometry(
                    id,
                    &m_EditOriginalPolylineGeometry)
                && vertexIndex >= 0
                && vertexIndex < m_EditOriginalPolylineGeometry.vertices.size();
        break;
    case CanvasEntityType::Invalid:
        break;
    }

    if (!loaded) {
        ResetEditState();
        return false;
    }

    return true;
}

bool CanvasView::UpdateEdit(const QPointF &worldPosition)
{
    if (!m_Document || m_EditItemId == 0
            || m_EditHandleType == EditHandleType::None) {
        return false;
    }

    bool changed = false;
    switch (m_EditEntityType) {
    case CanvasEntityType::Point: {
        if (m_EditHandleType != EditHandleType::PointCenter) {
            return false;
        }

        StruPointGeometry geometry = m_EditOriginalPointGeometry;
        geometry.position = worldPosition;
        changed = m_Document->SetPointGeometry(m_EditItemId, geometry);
        break;
    }
    case CanvasEntityType::Line: {
        StruLineGeometry geometry = m_EditOriginalLineGeometry;
        if (m_EditHandleType == EditHandleType::LineStart) {
            geometry.startPoint = worldPosition;
        } else if (m_EditHandleType == EditHandleType::LineEnd) {
            geometry.endPoint = worldPosition;
        } else {
            return false;
        }
        changed = m_Document->SetLineGeometry(m_EditItemId, geometry);
        break;
    }
    case CanvasEntityType::Arc: {
        StruArcGeometry geometry = m_EditOriginalArcGeometry;
        if (m_EditHandleType == EditHandleType::ArcCenter) {
            geometry.center = worldPosition;
        } else {
            const QPointF delta = worldPosition - geometry.center;
            const qreal radius = std::hypot(delta.x(), delta.y());
            if (radius <= 0.0) {
                return false;
            }

            geometry.radius = radius;
            const qreal angle = std::atan2(delta.y(), delta.x());
            if (m_EditHandleType == EditHandleType::ArcStart) {
                geometry.startAngle = angle;
            } else if (m_EditHandleType == EditHandleType::ArcEnd) {
                geometry.endAngle = angle;
            } else {
                return false;
            }
        }
        changed = m_Document->SetArcGeometry(m_EditItemId, geometry);
        break;
    }
    case CanvasEntityType::Circle: {
        StruCircleGeometry geometry = m_EditOriginalCircleGeometry;
        if (m_EditHandleType == EditHandleType::CircleCenter) {
            geometry.center = worldPosition;
        } else if (m_EditHandleType == EditHandleType::CircleRadius) {
            const QPointF delta = worldPosition - geometry.center;
            const qreal radius = std::hypot(delta.x(), delta.y());
            if (radius <= 0.0) {
                return false;
            }
            geometry.radius = radius;
        } else {
            return false;
        }
        changed = m_Document->SetCircleGeometry(m_EditItemId, geometry);
        break;
    }
    case CanvasEntityType::Polyline: {
        if (m_EditHandleType != EditHandleType::PolylineVertex
                || m_EditVertexIndex < 0
                || m_EditVertexIndex >= m_EditOriginalPolylineGeometry.vertices.size()) {
            return false;
        }
        StruPolylineGeometry geometry = m_EditOriginalPolylineGeometry;
        geometry.vertices[m_EditVertexIndex] = worldPosition;
        changed = m_Document->SetPolylineGeometry(m_EditItemId, geometry);
        break;
    }
    case CanvasEntityType::Invalid:
        return false;
    }

    if (changed) {
        m_EditGeometryChanged = true;
    }
    return changed;
}

void CanvasView::ResetEditState()
{
    m_EditHandleType = EditHandleType::None;
    m_EditVertexIndex = -1;
    m_EditItemId = 0;
    m_EditEntityType = CanvasEntityType::Invalid;
    m_EditGeometryChanged = false;
}

void CanvasView::CancelEdit()
{
    if (m_Document && m_EditItemId != 0 && m_EditGeometryChanged) {
        switch (m_EditEntityType) {
        case CanvasEntityType::Point:
            m_Document->SetPointGeometry(m_EditItemId,
                                         m_EditOriginalPointGeometry);
            break;
        case CanvasEntityType::Line:
            m_Document->SetLineGeometry(m_EditItemId,
                                        m_EditOriginalLineGeometry);
            break;
        case CanvasEntityType::Arc:
            m_Document->SetArcGeometry(m_EditItemId,
                                       m_EditOriginalArcGeometry);
            break;
        case CanvasEntityType::Circle:
            m_Document->SetCircleGeometry(m_EditItemId,
                                          m_EditOriginalCircleGeometry);
            break;
        case CanvasEntityType::Polyline:
            m_Document->SetPolylineGeometry(m_EditItemId,
                                            m_EditOriginalPolylineGeometry);
            break;
        case CanvasEntityType::Invalid:
            break;
        }
    }

    ResetEditState();
}

ItemId CanvasView::HitTestItem(const QPointF &viewportPosition) const
{
    if (!m_Document || size().isEmpty()) {
        return 0;
    }

    const QPointF worldPosition = MapToWorld(viewportPosition);
    const qreal zoom = qMax<qreal>(m_Transform.GetZoom(),
                                   CanvasTransform::GetMinimumZoom());
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

    const QVector<StruPointPrimitive> &points = m_Document->GetPointPrimitives();
    for (int pointIndex = points.size() - 1; pointIndex >= 0; --pointIndex) {
        const StruPointPrimitive &point = points.at(pointIndex);
        if (!point.style.visible) {
            continue;
        }
        const QPointF delta = worldPosition - point.geometry.position;
        considerHit(point.id,
                    QPointF::dotProduct(delta, delta),
                    CalculateHitToleranceSquared(zoom, point.style.pointSize));
    }

    const QVector<StruLinePrimitive> &lines = m_Document->GetLinePrimitives();
    for (int lineIndex = lines.size() - 1; lineIndex >= 0; --lineIndex) {
        const StruLinePrimitive &line = lines.at(lineIndex);
        if (!line.style.visible) {
            continue;
        }
        const qreal distanceSquared = DistanceSquaredToSegment(worldPosition,
                                                                line.geometry.startPoint,
                                                                line.geometry.endPoint);
        considerHit(line.id,
                    distanceSquared,
                    CalculateHitToleranceSquared(zoom, line.style.strokeWidth));
    }

    const QVector<StruArcPrimitive> &arcs = m_Document->GetArcPrimitives();
    for (int arcIndex = arcs.size() - 1; arcIndex >= 0; --arcIndex) {
        const StruArcPrimitive &arc = arcs.at(arcIndex);
        if (!arc.style.visible) {
            continue;
        }
        considerHit(arc.id,
                    DistanceSquaredToArc(worldPosition, arc.geometry),
                    CalculateHitToleranceSquared(zoom, arc.style.strokeWidth));
    }

    const QVector<StruCirclePrimitive> &circles = m_Document->GetCirclePrimitives();
    for (int circleIndex = circles.size() - 1; circleIndex >= 0; --circleIndex) {
        const StruCirclePrimitive &circle = circles.at(circleIndex);
        if (!circle.style.visible) {
            continue;
        }
        considerHit(circle.id,
                    DistanceSquaredToCircle(worldPosition, circle.geometry),
                    CalculateHitToleranceSquared(zoom, circle.style.strokeWidth));
    }

    const QVector<StruPolylinePrimitive> &polylines =
            m_Document->GetPolylinePrimitives();
    for (int polylineIndex = polylines.size() - 1;
         polylineIndex >= 0;
         --polylineIndex) {
        const StruPolylinePrimitive &polyline = polylines.at(polylineIndex);
        if (!polyline.style.visible) {
            continue;
        }
        considerHit(polyline.id,
                    DistanceSquaredToPolyline(worldPosition,
                                              polyline.geometry),
                    CalculateHitToleranceSquared(zoom,
                                                 polyline.style.strokeWidth));
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

    const auto isPointSelected = [&normalizedRect](const QPointF &point) {
        return normalizedRect.contains(point);
    };

    const auto isArcSelected = [this, &normalizedRect, crossing](
            const StruArcGeometry &geometry) {
        const QRectF geometryBounds = GetGeometryBounds(geometry);
        if (geometryBounds.isEmpty()
                || !geometryBounds.intersects(normalizedRect)) {
            return false;
        }

        if (!crossing) {
            return IsGeometryBoundsContained(geometryBounds, normalizedRect);
        }

        const qreal zoom = qMax<qreal>(m_Transform.GetZoom(),
                                       CanvasTransform::GetMinimumZoom());
        const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);
        return PolylineIntersectsRect(TessellateArc(geometry,
                                                   maximumWorldError),
                                      normalizedRect);
    };

    const auto isCircleSelected = [this, &normalizedRect, crossing](
            const StruCircleGeometry &geometry) {
        const QRectF geometryBounds = GetGeometryBounds(geometry);
        if (geometryBounds.isEmpty()
                || !geometryBounds.intersects(normalizedRect)) {
            return false;
        }

        if (!crossing) {
            return IsGeometryBoundsContained(geometryBounds, normalizedRect);
        }

        const qreal zoom = qMax<qreal>(m_Transform.GetZoom(),
                                       CanvasTransform::GetMinimumZoom());
        const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);
        return PolylineIntersectsRect(TessellateCircle(geometry,
                                                      maximumWorldError),
                                      normalizedRect);
    };

    const auto isPolylineSelected = [&normalizedRect, crossing](
            const StruPolylineGeometry &geometry) {
        if (crossing) {
            return PolylineIntersectsRect(geometry.vertices,
                                          normalizedRect,
                                          geometry.closed);
        }

        for (const QPointF &vertex : geometry.vertices) {
            if (!normalizedRect.contains(vertex)) {
                return false;
            }
        }
        return true;
    };

    const QVector<StruPointPrimitive> &points = m_Document->GetPointPrimitives();
    for (const StruPointPrimitive &point : points) {
        if (!point.style.visible) {
            continue;
        }
        if (isPointSelected(point.geometry.position)) {
            selectedIds.append(point.id);
        }
    }

    const QVector<StruLinePrimitive> &lines = m_Document->GetLinePrimitives();
    for (const StruLinePrimitive &line : lines) {
        if (!line.style.visible) {
            continue;
        }
        const bool selected = crossing
                ? SegmentIntersectsRect(line.geometry.startPoint,
                                        line.geometry.endPoint,
                                        normalizedRect)
                : (normalizedRect.contains(line.geometry.startPoint)
                   && normalizedRect.contains(line.geometry.endPoint));
        if (selected) {
            selectedIds.append(line.id);
        }
    }

    const QVector<StruArcPrimitive> &arcs = m_Document->GetArcPrimitives();
    for (const StruArcPrimitive &arc : arcs) {
        if (!arc.style.visible) {
            continue;
        }
        if (isArcSelected(arc.geometry)) {
            selectedIds.append(arc.id);
        }
    }

    const QVector<StruCirclePrimitive> &circles = m_Document->GetCirclePrimitives();
    for (const StruCirclePrimitive &circle : circles) {
        if (!circle.style.visible) {
            continue;
        }
        if (isCircleSelected(circle.geometry)) {
            selectedIds.append(circle.id);
        }
    }

    const QVector<StruPolylinePrimitive> &polylines =
            m_Document->GetPolylinePrimitives();
    for (const StruPolylinePrimitive &polyline : polylines) {
        if (!polyline.style.visible) {
            continue;
        }
        if (isPolylineSelected(polyline.geometry)) {
            selectedIds.append(polyline.id);
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

    const QPointF worldStart = MapToWorld(viewportRect.topLeft());
    const QPointF worldEnd = MapToWorld(viewportRect.bottomRight());
    const bool crossing = m_SelectionCurrentPosition.x()
            < m_SelectionStartPosition.x();
    const QVector<ItemId> selectedIds = CollectWindowSelection(QRectF(worldStart,
                                                                       worldEnd),
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
    m_MoveLastWorldPosition = QPointF(0.0, 0.0);
    m_MoveTotalWorldDelta = QPointF(0.0, 0.0);
}

void CanvasView::CancelMove()
{
    if (m_SelectionState == SelectionState::Moving
            && m_Document
            && (m_MoveTotalWorldDelta.x() != 0.0
                || m_MoveTotalWorldDelta.y() != 0.0)) {
        m_Document->TranslateItems(m_MoveItemIds,
                                   QPointF(-m_MoveTotalWorldDelta.x(),
                                           -m_MoveTotalWorldDelta.y()));
    }

    ResetMoveState();
}

} // namespace mini_graph
