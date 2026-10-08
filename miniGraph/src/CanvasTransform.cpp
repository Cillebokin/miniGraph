#include "MiniGraph/CanvasTransform.h"

#include <QtGlobal>

#include <cmath>

namespace mini_graph {

namespace {
const qreal MINIMUM_ZOOM = 0.000001;
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

CanvasVector CanvasTransform::GetCenter() const
{
    return m_Center;
}

void CanvasTransform::SetCenter(const CanvasVector &center)
{
    if (center.IsValid()) {
        m_Center = center;
    }
}

CanvasVector CanvasTransform::MapToWorld(const QPointF &viewportPoint,
                                         const QSizeF &viewportSize) const
{
    const QPointF viewportCenter(viewportSize.width() * 0.5,
                                 viewportSize.height() * 0.5);
    const QPointF delta = viewportPoint - viewportCenter;
    return m_Center + CanvasVector(delta.x() / m_Zoom, -delta.y() / m_Zoom);
}

QPointF CanvasTransform::MapFromWorld(const CanvasVector &worldPoint,
                                      const QSizeF &viewportSize) const
{
    if (!worldPoint.IsValid() || !m_Center.IsValid()) {
        return QPointF();
    }

    const QPointF viewportCenter(viewportSize.width() * 0.5,
                                 viewportSize.height() * 0.5);
    const CanvasVector delta = worldPoint - m_Center;
    return viewportCenter + QPointF(delta.GetX() * m_Zoom,
                                    -delta.GetY() * m_Zoom);
}

void CanvasTransform::PanBy(const QPointF &viewportDelta)
{
    m_Center += CanvasVector(-viewportDelta.x() / m_Zoom,
                             viewportDelta.y() / m_Zoom);
}

void CanvasTransform::ZoomAt(const QPointF &viewportPoint,
                             const QSizeF &viewportSize,
                             qreal factor)
{
    if (factor <= 0.0) {
        return;
    }

    const CanvasVector worldPoint = MapToWorld(viewportPoint, viewportSize);
    SetZoom(m_Zoom * factor);
    m_Center += worldPoint - MapToWorld(viewportPoint, viewportSize);
}

bool CanvasTransform::FitRect(const QRectF &worldRect,
                              const QSizeF &viewportSize,
                              qreal marginPixels)
{
    if (!std::isfinite(static_cast<double>(viewportSize.width()))
            || !std::isfinite(static_cast<double>(viewportSize.height()))
            || viewportSize.width() <= 0.0
            || viewportSize.height() <= 0.0) {
        return false;
    }

    const qreal coordinates[] = {
        worldRect.left(),
        worldRect.top(),
        worldRect.right(),
        worldRect.bottom()
    };
    for (const qreal coordinate : coordinates) {
        if (!std::isfinite(static_cast<double>(coordinate))) {
            return false;
        }
    }

    const QRectF normalizedRect = worldRect.normalized();
    const qreal requestedMargin = std::isfinite(static_cast<double>(marginPixels))
            ? qMax<qreal>(0.0, marginPixels)
            : 0.0;
    const qreal maximumMargin = qMin(viewportSize.width(),
                                     viewportSize.height()) * 0.49;
    const qreal effectiveMargin = qMin(requestedMargin, maximumMargin);
    const qreal availableWidth = qMax<qreal>(1.0,
                                             viewportSize.width()
                                             - effectiveMargin * 2.0);
    const qreal availableHeight = qMax<qreal>(1.0,
                                              viewportSize.height()
                                              - effectiveMargin * 2.0);
    const qreal worldWidth = qMax<qreal>(1.0, normalizedRect.width());
    const qreal worldHeight = qMax<qreal>(1.0, normalizedRect.height());

    const QPointF rectCenter = normalizedRect.center();
    m_Center = CanvasVector(rectCenter.x(), rectCenter.y());
    SetZoom(qMin(availableWidth / worldWidth,
                 availableHeight / worldHeight));
    return true;
}

void CanvasTransform::Reset()
{
    m_Zoom = 1.0;
    m_Center = CanvasVector(0.0, 0.0);
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
