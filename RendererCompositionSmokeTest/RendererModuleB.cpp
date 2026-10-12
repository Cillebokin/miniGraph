#include "RendererModuleB.h"

#include <QColor>
#include <QPainter>
#include <QRectF>

RendererModuleB::RendererModuleB(QStringList *events, QObject *parent)
    : mini_graph::CanvasRenderer(parent)
    , m_Events(events)
{
}

void RendererModuleB::Render(
        QPainter &painter,
        const mini_graph::CanvasRenderContext &context)
{
    if (m_Events) {
        m_Events->append(QStringLiteral("module-b"));
    }

    const QPointF center = context.MapFromWorld(
                mini_graph::CanvasVector(0.0, 0.0));
    painter.fillRect(QRectF(center.x() - 10.0,
                            center.y() - 10.0,
                            20.0,
                            20.0),
                     QColor(38, 139, 210));
}
