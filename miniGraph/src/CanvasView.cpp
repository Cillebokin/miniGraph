#include "MiniGraph/CanvasView.h"

#include "MiniGraph/CanvasDocument.h"

#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QFont>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QDebug>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace mini_graph {

namespace {

struct StruLineVertex
{
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

const qreal CADLAB_MIN_GRID_PIXEL_SPACING = 10.0;
const qreal CADLAB_MAX_GRID_PIXEL_SPACING = 100.0;
const qreal SELECTION_DRAG_THRESHOLD = 7.0;
const qreal SELECTION_HIT_TOLERANCE_PIXELS = 6.0;

const QColor CADLAB_WINDOW_SELECTION_COLOR(9, 9, 255, 60);
const QColor CADLAB_CROSSING_SELECTION_COLOR(9, 255, 9, 60);
const QColor CADLAB_SELECTED_ITEM_COLOR(165, 71, 71);

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
        QPointF intersection;
        if (segment.intersect(edge, &intersection) == QLineF::BoundedIntersection) {
            return true;
        }
    }

    return false;
}

const char *LINE_VERTEX_SHADER =
        "attribute vec2 a_position;\n"
        "attribute vec4 a_color;\n"
        "uniform mat4 u_matrix;\n"
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    gl_Position = u_matrix * vec4(a_position, 0.0, 1.0);\n"
        "    v_color = a_color;\n"
        "}\n";

const char *LINE_FRAGMENT_SHADER =
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec4 v_color;\n"
        "void main() {\n"
        "    gl_FragColor = v_color;\n"
        "}\n";

} // namespace

class CanvasViewPrivate
{
public:
    CanvasViewPrivate()
        : m_LineBuffer(QOpenGLBuffer::VertexBuffer)
    {
    }

    QOpenGLFunctions *m_GL = nullptr;
    QOpenGLShaderProgram m_LineProgram;
    QOpenGLBuffer m_LineBuffer;
    bool m_LineProgramReady = false;
    bool m_LineBufferDirty = true;
};

CanvasView::CanvasView(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_GridVisible(true)
    , m_GridSpacing(10.0)
    , m_AxisVisible(true)
    , m_RulerVisible(false)
    , m_OrthogonalMode(false)
    , m_PanState(PanState::Idle)
    , m_PanStartPosition(0.0, 0.0)
    , m_LastMousePosition(0.0, 0.0)
    , m_SelectionState(SelectionState::Idle)
    , m_SelectionStartPosition(0.0, 0.0)
    , m_SelectionCurrentPosition(0.0, 0.0)
    , m_SelectionModifiers(Qt::NoModifier)
    , m_PrivateData(new CanvasViewPrivate)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

CanvasView::~CanvasView()
{
    CleanupGL();
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

    QObject::disconnect(m_DocumentConnection);
    QObject::disconnect(m_DocumentSelectionConnection);
    m_Document = document;
    m_PrivateData->m_LineBufferDirty = true;
    m_SelectionState = SelectionState::Idle;

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

void CanvasView::ResetView()
{
    m_Transform.Reset();
    emit ZoomChanged(m_Transform.GetZoom());
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

void CanvasView::initializeGL()
{
    m_PrivateData->m_GL = context()->functions();
    m_PrivateData->m_GL->initializeOpenGLFunctions();
    m_PrivateData->m_LineProgramReady = false;
    if (m_PrivateData->m_LineBuffer.isCreated()) {
        m_PrivateData->m_LineBuffer.destroy();
    }
    m_PrivateData->m_LineProgram.removeAllShaders();

    connect(context(),
            &QOpenGLContext::aboutToBeDestroyed,
            this,
            &CanvasView::CleanupGL,
            Qt::DirectConnection);

    m_PrivateData->m_LineProgramReady = m_PrivateData->m_LineProgram.addShaderFromSourceCode(
                QOpenGLShader::Vertex,
                LINE_VERTEX_SHADER)
            && m_PrivateData->m_LineProgram.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                                     LINE_FRAGMENT_SHADER)
            && m_PrivateData->m_LineProgram.link();

    if (!m_PrivateData->m_LineProgramReady) {
        qWarning("MiniGraph: failed to initialize the line shader: %s",
                 qPrintable(m_PrivateData->m_LineProgram.log()));
        m_PrivateData->m_LineProgram.removeAllShaders();
        return;
    }

    m_PrivateData->m_LineBuffer.create();
    m_PrivateData->m_LineBufferDirty = true;
}

void CanvasView::resizeGL(int width, int height)
{
    Q_UNUSED(width)
    Q_UNUSED(height)
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift) {
        if (!event->isAutoRepeat()) {
            SetOrthogonalModeInternal(true);
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        m_PanState = PanState::Idle;
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        SetOrthogonalModeInternal(false);
        unsetCursor();
        emit InteractionCancelled();
        update();
        event->accept();
        return;
    }

    QOpenGLWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift) {
        if (!event->isAutoRepeat()) {
            SetOrthogonalModeInternal(false);
        }
        event->accept();
        return;
    }

    QOpenGLWidget::keyReleaseEvent(event);
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    emit MouseWorldPositionChanged(MapToWorld(event->localPos()));

    if (event->button() == Qt::MiddleButton) {
        if (isVisible()) {
            setFocus(Qt::MouseFocusReason);
        }
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
        m_PanState = PanState::Pressed;
        m_PanStartPosition = event->localPos();
        m_LastMousePosition = event->localPos();
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        m_SelectionState = SelectionState::Idle;
        m_SelectionModifiers = Qt::NoModifier;
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
        m_SelectionState = SelectionState::Pressed;
        m_SelectionStartPosition = event->localPos();
        m_SelectionCurrentPosition = event->localPos();
        m_SelectionModifiers = event->modifiers();
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF currentMousePosition = event->localPos();
    emit MouseWorldPositionChanged(MapToWorld(currentMousePosition));

    if (m_PanState == PanState::Idle) {
        if (m_SelectionState != SelectionState::Idle) {
            if (!(event->buttons() & Qt::LeftButton)) {
                m_SelectionState = SelectionState::Idle;
                m_SelectionModifiers = Qt::NoModifier;
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
                    m_SelectionState = SelectionState::Dragging;
                }
            }

            update();
            event->accept();
            return;
        }

        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    if (!(event->buttons() & Qt::MiddleButton)) {
        m_PanState = PanState::Idle;
        unsetCursor();
        QOpenGLWidget::mouseMoveEvent(event);
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
    update();
    event->accept();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    emit MouseWorldPositionChanged(MapToWorld(event->localPos()));

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
            && m_SelectionState != SelectionState::Idle) {
        m_SelectionCurrentPosition = event->localPos();
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
        update();
        event->accept();
        return;
    }

    QOpenGLWidget::mouseReleaseEvent(event);
}

void CanvasView::paintGL()
{
    if (!m_PrivateData->m_GL) {
        return;
    }

    m_PrivateData->m_GL->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    m_PrivateData->m_GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_GridVisible) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        DrawBackgroundLayer(painter);
    }

    DrawLines();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    DrawOverlayLayer(painter);
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
        m_Transform.ZoomAt(event->posF(), QSizeF(size()), factor);
        emit ZoomChanged(m_Transform.GetZoom());
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
        update();
        event->accept();
        return;
    }

    if (!pixelDelta.isNull()) {
        m_Transform.PanBy(QPointF(pixelDelta.x(), -pixelDelta.y()));
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
    m_Transform.ZoomAt(event->posF(), QSizeF(size()), factor);
    emit ZoomChanged(m_Transform.GetZoom());
    update();
    event->accept();
}

void CanvasView::OnDocumentChanged()
{
    m_PrivateData->m_LineBufferDirty = true;
    update();
}

void CanvasView::OnSelectionChanged()
{
    m_PrivateData->m_LineBufferDirty = true;
    update();
}

void CanvasView::CleanupGL()
{
    QOpenGLContext *glContext = context();
    if (!glContext || !m_PrivateData->m_GL) {
        return;
    }

    if (!glContext->isValid()) {
        m_PrivateData->m_LineProgramReady = false;
        m_PrivateData->m_GL = nullptr;
        return;
    }

    const bool madeCurrent = QOpenGLContext::currentContext() != glContext;
    if (madeCurrent) {
        makeCurrent();
    }
    if (QOpenGLContext::currentContext() != glContext) {
        m_PrivateData->m_LineProgramReady = false;
        m_PrivateData->m_GL = nullptr;
        return;
    }

    if (m_PrivateData->m_LineBuffer.isCreated()) {
        m_PrivateData->m_LineBuffer.destroy();
    }
    m_PrivateData->m_LineProgram.removeAllShaders();
    m_PrivateData->m_LineProgramReady = false;
    m_PrivateData->m_GL = nullptr;
    if (madeCurrent) {
        doneCurrent();
    }
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
    DrawSelectionBox(painter);

    if (m_AxisVisible) {
        DrawAxes(painter);
    }

    if (m_RulerVisible) {
        DrawRulerLabels(painter);
    }
}

void CanvasView::DrawSelectionBox(QPainter &painter) const
{
    if (m_SelectionState != SelectionState::Dragging) {
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

    const qreal axisXDirection = 1.0;
    const qreal axisYDirection = -1.0;
    const qreal axisXLabelPosition = origin.x() + axisXDirection * 120.0;
    const qreal axisYLabelPosition = origin.y() + axisYDirection * 120.0;

    QPointF coordinateOrigin = origin;
    if (coordinateOrigin.x() - 8.0 < 0.0 || coordinateOrigin.x() + 8.0 > viewportWidth
            || axisXLabelPosition < 0.0 || axisXLabelPosition > viewportWidth
            || coordinateOrigin.y() - 8.0 < 0.0 || coordinateOrigin.y() + 8.0 > viewportHeight
            || axisYLabelPosition < 0.0 || axisYLabelPosition > viewportHeight) {
        coordinateOrigin.setX(axisXDirection > 0.0 ? 30.0 : viewportWidth - 30.0);
        coordinateOrigin.setY(axisYDirection < 0.0 ? viewportHeight - 30.0 : 30.0);
    }

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
    painter.drawText(QPointF(coordinateOrigin.x() + 98.0,
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

void CanvasView::UploadLineBuffer()
{
    if (!m_PrivateData->m_LineProgramReady || !m_PrivateData->m_LineBuffer.isCreated()
            || !m_Document) {
        return;
    }

    const QVector<StruLinePrimitive> &lines = m_Document->GetLinePrimitives();
    QVector<StruLineVertex> vertices;
    vertices.reserve(lines.size() * 2);

    for (const StruLinePrimitive &line : lines) {
        const QColor color = m_Document->IsItemSelected(line.id)
                ? CADLAB_SELECTED_ITEM_COLOR
                : (line.color.isValid() ? line.color : QColor(Qt::white));
        const float red = color.redF();
        const float green = color.greenF();
        const float blue = color.blueF();
        const float alpha = color.alphaF();
        vertices.append({static_cast<float>(line.start.x()),
                         static_cast<float>(line.start.y()),
                         red, green, blue, alpha});
        vertices.append({static_cast<float>(line.end.x()),
                         static_cast<float>(line.end.y()),
                         red, green, blue, alpha});
    }

    m_PrivateData->m_LineBuffer.bind();
    m_PrivateData->m_LineBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_PrivateData->m_LineBuffer.allocate(vertices.constData(),
                                        static_cast<int>(vertices.size()
                                                         * sizeof(StruLineVertex)));
    m_PrivateData->m_LineBuffer.release();
    m_PrivateData->m_LineBufferDirty = false;
}

void CanvasView::DrawLines()
{
    if (!m_PrivateData->m_LineProgramReady || !m_PrivateData->m_LineBuffer.isCreated()
            || !m_Document) {
        return;
    }

    if (m_PrivateData->m_LineBufferDirty) {
        UploadLineBuffer();
    }

    const int vertexCount = m_Document->GetLinePrimitives().size() * 2;
    if (vertexCount == 0) {
        return;
    }

    const qreal viewportWidth = qMax(1, width());
    const qreal viewportHeight = qMax(1, height());
    const qreal zoom = m_Transform.GetZoom();
    const QPointF center = m_Transform.GetCenter();

    QMatrix4x4 matrix;
    matrix.setToIdentity();
    matrix(0, 0) = static_cast<float>(2.0 * zoom / viewportWidth);
    matrix(0, 3) = static_cast<float>(-2.0 * zoom * center.x() / viewportWidth);
    matrix(1, 1) = static_cast<float>(-2.0 * zoom / viewportHeight);
    matrix(1, 3) = static_cast<float>(2.0 * zoom * center.y() / viewportHeight);

    m_PrivateData->m_LineProgram.bind();
    m_PrivateData->m_LineProgram.setUniformValue("u_matrix", matrix);
    m_PrivateData->m_LineBuffer.bind();
    m_PrivateData->m_LineProgram.enableAttributeArray("a_position");
    m_PrivateData->m_LineProgram.setAttributeBuffer("a_position",
                                                     GL_FLOAT,
                                                     0,
                                                     2,
                                                     sizeof(StruLineVertex));
    m_PrivateData->m_LineProgram.enableAttributeArray("a_color");
    m_PrivateData->m_LineProgram.setAttributeBuffer("a_color",
                                                     GL_FLOAT,
                                                     static_cast<int>(sizeof(float) * 2),
                                                     4,
                                                     sizeof(StruLineVertex));
    m_PrivateData->m_GL->glDrawArrays(GL_LINES, 0, vertexCount);
    m_PrivateData->m_LineProgram.disableAttributeArray("a_position");
    m_PrivateData->m_LineProgram.disableAttributeArray("a_color");
    m_PrivateData->m_LineBuffer.release();
    m_PrivateData->m_LineProgram.release();
}

ItemId CanvasView::HitTestItem(const QPointF &viewportPosition) const
{
    if (!m_Document || size().isEmpty()) {
        return 0;
    }

    const QPointF worldPosition = MapToWorld(viewportPosition);
    const qreal zoom = qMax<qreal>(m_Transform.GetZoom(),
                                   CanvasTransform::GetMinimumZoom());
    const qreal tolerance = SELECTION_HIT_TOLERANCE_PIXELS / zoom;
    const qreal toleranceSquared = tolerance * tolerance;
    qreal bestDistanceSquared = toleranceSquared;
    ItemId hitId = 0;

    const QVector<StruLinePrimitive> &lines = m_Document->GetLinePrimitives();
    for (int lineIndex = lines.size() - 1; lineIndex >= 0; --lineIndex) {
        const StruLinePrimitive &line = lines.at(lineIndex);
        const qreal distanceSquared = DistanceSquaredToSegment(worldPosition,
                                                                line.start,
                                                                line.end);
        if (distanceSquared <= bestDistanceSquared) {
            if (hitId == 0 || distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                hitId = line.id;
            }
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
    const QVector<StruLinePrimitive> &lines = m_Document->GetLinePrimitives();
    for (const StruLinePrimitive &line : lines) {
        const bool selected = crossing
                ? SegmentIntersectsRect(line.start, line.end, normalizedRect)
                : (normalizedRect.contains(line.start)
                   && normalizedRect.contains(line.end));
        if (selected) {
            selectedIds.append(line.id);
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

    m_Document->SelectItems(QVector<ItemId>() << id, false, false);
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

} // namespace mini_graph
