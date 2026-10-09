#include "BasicLineCreationAction.h"

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasRenderer.h"
#include "MiniGraph/CanvasSnap.h"
#include "MiniGraph/CanvasView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>

BasicLineCreationAction::BasicLineCreationAction(QObject *parent)
    : mini_graph::CanvasAction(parent)
    , m_HasStartPoint(false)
    , m_HasPreviewEndPoint(false)
{
}

void BasicLineCreationAction::MousePressEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton) {
        return;
    }

    mini_graph::CanvasView *view = GetCanvasView();
    mini_graph::CanvasDocument *document = view ? view->GetDocument() : nullptr;
    if (!view || !document) {
        return;
    }

    const mini_graph::CanvasSnapResult snap = view->GetCurrentSnapResult();
    const mini_graph::CanvasVector position = snap.IsValid()
            ? snap.position
            : view->MapToWorld(event->localPos());
    if (!position.IsValid()) {
        return;
    }

    if (!m_HasStartPoint) {
        m_StartPoint = position;
        m_HasStartPoint = true;
        m_HasPreviewEndPoint = false;
        RequestUpdate();
        return;
    }

    if (document->AddLine(m_StartPoint, position) != 0) {
        m_HasStartPoint = false;
        m_HasPreviewEndPoint = false;
        Finish();
    }
}

void BasicLineCreationAction::MouseDoubleClickEvent(QMouseEvent *event)
{
    MousePressEvent(event);
}

void BasicLineCreationAction::MouseMoveEvent(QMouseEvent *event)
{
    if (!event || !m_HasStartPoint) {
        return;
    }

    mini_graph::CanvasView *view = GetCanvasView();
    if (!view) {
        return;
    }

    const mini_graph::CanvasSnapResult snap = view->GetCurrentSnapResult();
    const mini_graph::CanvasVector position = snap.IsValid()
            ? snap.position
            : view->MapToWorld(event->localPos());
    if (!position.IsValid()
            || (m_HasPreviewEndPoint && position == m_PreviewEndPoint)) {
        return;
    }

    m_PreviewEndPoint = position;
    m_HasPreviewEndPoint = true;
    RequestUpdate();
}

void BasicLineCreationAction::RenderOverlay(
        QPainter &painter,
        const mini_graph::CanvasRenderContext &context)
{
    if (!m_HasStartPoint || !m_HasPreviewEndPoint) {
        return;
    }

    const QPointF startPosition = context.MapFromWorld(m_StartPoint);
    const QPointF endPosition = context.MapFromWorld(m_PreviewEndPoint);

    painter.save();
    QPen previewPen(QColor(0, 255, 255), 2.0, Qt::SolidLine);
    previewPen.setCosmetic(true);
    painter.setPen(previewPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(startPosition, endPosition);
    painter.restore();
}

void BasicLineCreationAction::Cancel()
{
    m_StartPoint = mini_graph::CanvasVector();
    m_PreviewEndPoint = mini_graph::CanvasVector();
    m_HasStartPoint = false;
    m_HasPreviewEndPoint = false;
    RequestUpdate();
}
