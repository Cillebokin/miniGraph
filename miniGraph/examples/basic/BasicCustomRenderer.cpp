#include "BasicCustomRenderer.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

BasicCustomRenderer::BasicCustomRenderer(QObject *parent)
    : mini_graph::CanvasRenderer(parent)
    , m_Offset(0.0, 0.0)
{
}

void BasicCustomRenderer::Advance()
{
    m_Offset += mini_graph::CanvasVector(10.0, 0.0);
    RequestUpdate();
}

void BasicCustomRenderer::RenderBackground(
        QPainter &painter,
        const mini_graph::CanvasRenderContext &context)
{
    const QPointF anchor = context.MapFromWorld(
                m_Offset + mini_graph::CanvasVector(0.0, 25.0));
    const QPointF radiusPoint = context.MapFromWorld(
                m_Offset + mini_graph::CanvasVector(30.0, 25.0));
    const qreal radius = qMax<qreal>(12.0,
                                     qAbs(radiusPoint.x() - anchor.x()));

    painter.save();
    painter.setPen(QPen(QColor(80, 180, 240, 120), 1.0));
    painter.setBrush(QColor(30, 120, 200, 35));
    painter.drawEllipse(anchor, radius, radius);
    painter.restore();
}

void BasicCustomRenderer::Render(
        QPainter &painter,
        const mini_graph::CanvasRenderContext &context)
{
    const mini_graph::CanvasVector worldPoints[] = {
        mini_graph::CanvasVector(-30.0, 25.0),
        mini_graph::CanvasVector(-15.0, 40.0),
        mini_graph::CanvasVector(0.0, 25.0),
        mini_graph::CanvasVector(15.0, 40.0),
        mini_graph::CanvasVector(30.0, 25.0)
    };

    QPolygonF viewportPoints;
    for (const mini_graph::CanvasVector &worldPoint : worldPoints)
    {
        viewportPoints.append(context.MapFromWorld(worldPoint + m_Offset));
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(64, 217, 255), 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(viewportPoints);
    painter.restore();
}

void BasicCustomRenderer::RenderOverlay(
        QPainter &painter,
        const mini_graph::CanvasRenderContext &context)
{
    Q_UNUSED(context)

    painter.save();
    painter.setPen(QPen(QColor(120, 220, 255), 2.0));
    painter.drawLine(QPointF(12.0, 22.0), QPointF(36.0, 22.0));
    painter.setPen(QColor(180, 225, 245));
    painter.drawText(QPointF(44.0, 26.0),
                     QStringLiteral("Custom renderer"));
    painter.restore();
}
