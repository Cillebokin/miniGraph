#include "MiniGraph/CanvasView.h"

#include "MiniGraph/CanvasDocument.h"

#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
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
    , m_IsPanning(false)
    , m_LastMousePosition(0.0, 0.0)
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
    m_Document = document;
    m_PrivateData->m_LineBufferDirty = true;

    if (m_Document) {
        m_DocumentConnection = connect(m_Document.data(),
                                       &CanvasDocument::Changed,
                                       this,
                                       &CanvasView::OnDocumentChanged);
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

void CanvasView::ResetView()
{
    m_Transform.Reset();
    emit ZoomChanged(m_Transform.GetZoom());
    update();
}

void CanvasView::initializeGL()
{
    m_PrivateData->m_GL = context()->functions();
    m_PrivateData->m_GL->initializeOpenGLFunctions();

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

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_IsPanning = true;
        m_LastMousePosition = event->localPos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_IsPanning) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF currentMousePosition = event->localPos();
    m_Transform.PanBy(currentMousePosition - m_LastMousePosition);
    m_LastMousePosition = currentMousePosition;
    update();
    event->accept();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_IsPanning) {
        m_IsPanning = false;
        unsetCursor();
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

    m_PrivateData->m_GL->glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    m_PrivateData->m_GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_GridVisible) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        DrawGrid(painter);
    }

    DrawLines();
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0 || size().isEmpty()) {
        event->ignore();
        return;
    }

    const qreal steps = static_cast<qreal>(delta) / 120.0;
    const qreal factor = std::pow(1.2, steps);
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

void CanvasView::CleanupGL()
{
    if (!context() || !m_PrivateData->m_GL) {
        return;
    }

    makeCurrent();
    if (m_PrivateData->m_LineBuffer.isCreated()) {
        m_PrivateData->m_LineBuffer.destroy();
    }
    m_PrivateData->m_LineProgram.removeAllShaders();
    m_PrivateData->m_LineProgramReady = false;
    m_PrivateData->m_GL = nullptr;
    doneCurrent();
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

    qreal spacing = m_GridSpacing;
    while ((right - left) / spacing > 160.0) {
        spacing *= 2.0;
    }
    while ((right - left) / spacing < 40.0 && spacing > m_GridSpacing / 1024.0) {
        spacing *= 0.5;
    }

    const int firstX = static_cast<int>(std::floor(left / spacing)) - 1;
    const int lastX = static_cast<int>(std::ceil(right / spacing)) + 1;
    const int firstY = static_cast<int>(std::floor(bottom / spacing)) - 1;
    const int lastY = static_cast<int>(std::ceil(top / spacing)) + 1;

    painter.setPen(QPen(QColor(48, 52, 60), 1.0));
    for (int x = firstX; x <= lastX; ++x) {
        const qreal worldX = x * spacing;
        painter.drawLine(QLineF(m_Transform.MapFromWorld(QPointF(worldX, bottom), viewportSize),
                                m_Transform.MapFromWorld(QPointF(worldX, top), viewportSize)));
    }
    for (int y = firstY; y <= lastY; ++y) {
        const qreal worldY = y * spacing;
        painter.drawLine(QLineF(m_Transform.MapFromWorld(QPointF(left, worldY), viewportSize),
                                m_Transform.MapFromWorld(QPointF(right, worldY), viewportSize)));
    }

    painter.setPen(QPen(QColor(82, 90, 105), 1.0));
    const QPointF origin = m_Transform.MapFromWorld(QPointF(0.0, 0.0), viewportSize);
    if (origin.x() >= 0.0 && origin.x() <= width()) {
        painter.drawLine(QLineF(origin.x(), 0.0, origin.x(), height()));
    }
    if (origin.y() >= 0.0 && origin.y() <= height()) {
        painter.drawLine(QLineF(0.0, origin.y(), width(), origin.y()));
    }
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
        const QColor color = line.color.isValid() ? line.color : QColor(Qt::white);
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

} // namespace mini_graph
