#include "BasicCircleCreationAction.h"

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasRenderer.h"
#include "MiniGraph/CanvasSnap.h"
#include "MiniGraph/CanvasView.h"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <cmath>

BasicCircleCreationAction::BasicCircleCreationAction(QObject *parent)
    : mini_graph::CanvasAction(parent)
    , m_HasCenter(false)
    , m_HasPreviewRadiusPoint(false)
{
}

void BasicCircleCreationAction::MousePressEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton)
    {
        return;
    }

    mini_graph::CanvasView *view = GetCanvasView();
    mini_graph::CanvasDocument *document = view ? view->GetDocument() : nullptr;
    if (!view || !document)
    {
        return;
    }

    const mini_graph::CanvasSnapResult snap = view->GetCurrentSnapResult();
    const mini_graph::CanvasVector position = snap.IsValid()
            ? snap.position
            : view->MapToWorld(event->localPos());
    if (!position.IsValid())
    {
        return;
    }

    if (!m_HasCenter)
    {
        m_Center = position;
        m_HasCenter = true;
        m_HasPreviewRadiusPoint = false;
        RequestUpdate();
        return;
    }

    m_PreviewRadiusPoint = position;
    m_HasPreviewRadiusPoint = true;
    const qreal deltaX = position.GetX() - m_Center.GetX();
    const qreal deltaY = position.GetY() - m_Center.GetY();
    const qreal radius = std::hypot(deltaX, deltaY);
    if (!std::isfinite(radius) || radius <= 0.0)
    {
        RequestUpdate();
        return;
    }

    if (document->AddCircle(m_Center, radius) != 0)
    {
        m_Center = mini_graph::CanvasVector();
        m_PreviewRadiusPoint = mini_graph::CanvasVector();
        m_HasCenter = false;
        m_HasPreviewRadiusPoint = false;
        Finish();
    }
}

void BasicCircleCreationAction::MouseDoubleClickEvent(QMouseEvent *event)
{
    MousePressEvent(event);
}

void BasicCircleCreationAction::MouseMoveEvent(QMouseEvent *event)
{
    if (!event || !m_HasCenter)
    {
        return;
    }

    mini_graph::CanvasView *view = GetCanvasView();
    if (!view)
    {
        return;
    }

    const mini_graph::CanvasSnapResult snap = view->GetCurrentSnapResult();
    const mini_graph::CanvasVector position = snap.IsValid()
            ? snap.position
            : view->MapToWorld(event->localPos());
    if (!position.IsValid()
            || (m_HasPreviewRadiusPoint && position == m_PreviewRadiusPoint))
    {
        return;
    }

    m_PreviewRadiusPoint = position;
    m_HasPreviewRadiusPoint = true;
    RequestUpdate();
}

void BasicCircleCreationAction::RenderOverlay(
        QPainter &painter,
        const mini_graph::CanvasRenderContext &context)
{
    if (!m_HasCenter || !m_HasPreviewRadiusPoint)
    {
        return;
    }

    const QPointF centerPosition = context.MapFromWorld(m_Center);
    const QPointF radiusPosition = context.MapFromWorld(m_PreviewRadiusPoint);
    const qreal radiusInPixels = QLineF(centerPosition, radiusPosition).length();
    if (!std::isfinite(radiusInPixels) || radiusInPixels <= 0.0)
    {
        return;
    }

    painter.save();
    QPen previewPen(QColor(0, 255, 255), 2.0, Qt::SolidLine);
    previewPen.setCosmetic(true);
    painter.setPen(previewPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(centerPosition, radiusInPixels, radiusInPixels);
    painter.restore();
}

void BasicCircleCreationAction::Cancel()
{
    m_Center = mini_graph::CanvasVector();
    m_PreviewRadiusPoint = mini_graph::CanvasVector();
    m_HasCenter = false;
    m_HasPreviewRadiusPoint = false;
    RequestUpdate();
}
