#include "MiniGraph/CanvasTransform.h"

#include <QtGlobal>

namespace mini_graph {

namespace {
const qreal MINIMUM_ZOOM = 0.01;
const qreal MAXIMUM_ZOOM = 100000.0;
}

CanvasTransform::CanvasTransform()
    : m_Zoom(1.0)
    , m_Center(0.0, 0.0)
{
}

qreal CanvasTransform::GetZoom() const
{
    return m_Zoom;
}

void CanvasTransform::SetZoom(qreal zoom)
{
    if (zoom <= 0.0) {
        return;
    }

    m_Zoom = qBound(MINIMUM_ZOOM, zoom, MAXIMUM_ZOOM);
}

QPointF CanvasTransform::GetCenter() const
{
    return m_Center;
}

void CanvasTransform::SetCenter(const QPointF &center)
{
    m_Center = center;
}

QPointF CanvasTransform::MapToWorld(const QPointF &viewportPoint,
                                    const QSizeF &viewportSize) const
{
    const QPointF viewportCenter(viewportSize.width() * 0.5,
                                 viewportSize.height() * 0.5);
    const QPointF delta = viewportPoint - viewportCenter;
    return m_Center + QPointF(delta.x() / m_Zoom, -delta.y() / m_Zoom);
}

QPointF CanvasTransform::MapFromWorld(const QPointF &worldPoint,
                                      const QSizeF &viewportSize) const
{
    const QPointF viewportCenter(viewportSize.width() * 0.5,
                                 viewportSize.height() * 0.5);
    const QPointF delta = worldPoint - m_Center;
    return viewportCenter + QPointF(delta.x() * m_Zoom, -delta.y() * m_Zoom);
}

void CanvasTransform::PanBy(const QPointF &viewportDelta)
{
    m_Center += QPointF(-viewportDelta.x() / m_Zoom,
                        viewportDelta.y() / m_Zoom);
}

void CanvasTransform::ZoomAt(const QPointF &viewportPoint,
                             const QSizeF &viewportSize,
                             qreal factor)
{
    if (factor <= 0.0) {
        return;
    }

    const QPointF worldPoint = MapToWorld(viewportPoint, viewportSize);
    SetZoom(m_Zoom * factor);
    m_Center += worldPoint - MapToWorld(viewportPoint, viewportSize);
}

void CanvasTransform::Reset()
{
    m_Zoom = 1.0;
    m_Center = QPointF(0.0, 0.0);
}

qreal CanvasTransform::GetMinimumZoom()
{
    return MINIMUM_ZOOM;
}

qreal CanvasTransform::GetMaximumZoom()
{
    return MAXIMUM_ZOOM;
}

} // namespace mini_graph
