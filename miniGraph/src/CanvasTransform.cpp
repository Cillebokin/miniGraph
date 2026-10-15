#include "MiniGraph/CanvasTransform.h"

#include <QtGlobal>

#include <cmath>

namespace mini_graph {

namespace {
const qreal MINIMUM_ZOOM = 0.000001;
const qreal MAXIMUM_ZOOM = 100000.0;

bool IsFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

bool IsFinitePoint(const QPointF &point)
{
    return IsFinite(point.x()) && IsFinite(point.y());
}

bool IsValidViewportSize(const QSizeF &viewportSize)
{
    return IsFinite(viewportSize.width())
            && IsFinite(viewportSize.height())
            && viewportSize.width() > 0.0
            && viewportSize.height() > 0.0;
}
}

CanvasTransform::CanvasTransform()
    : m_Zoom(1.0)
    , m_Center(0.0, 0.0)
    , m_FlipX(false)
    , m_FlipY(false)
{
}

qreal CanvasTransform::GetZoom() const
{
    return m_Zoom;
}

void CanvasTransform::SetZoom(qreal zoom)
{
    if (!IsFinite(zoom) || zoom <= 0.0) {
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

bool CanvasTransform::GetFlipX() const
{
    return m_FlipX;
}

void CanvasTransform::SetFlipX(bool flipX)
{
    if (m_FlipX == flipX) {
        return;
    }

    const CanvasVector nextCenter(-m_Center.GetX(), m_Center.GetY());
    if (!nextCenter.IsValid()) {
        return;
    }

    m_Center = nextCenter;
    m_FlipX = flipX;
}

bool CanvasTransform::GetFlipY() const
{
    return m_FlipY;
}

void CanvasTransform::SetFlipY(bool flipY)
{
    if (m_FlipY == flipY) {
        return;
    }

    const CanvasVector nextCenter(m_Center.GetX(), -m_Center.GetY());
    if (!nextCenter.IsValid()) {
        return;
    }

    m_Center = nextCenter;
    m_FlipY = flipY;
}

CanvasVector CanvasTransform::MapToWorld(const QPointF &viewportPoint,
                                         const QSizeF &viewportSize) const
{
    if (!IsFinitePoint(viewportPoint) || !IsValidViewportSize(viewportSize)
            || !m_Center.IsValid()) {
        return CanvasVector();
    }

    const QPointF viewportCenter(viewportSize.width() * 0.5,
                                 viewportSize.height() * 0.5);
    const QPointF delta = viewportPoint - viewportCenter;
    const qreal axisX = m_FlipX ? -1.0 : 1.0;
    const qreal axisY = m_FlipY ? 1.0 : -1.0;
    const CanvasVector worldPoint = m_Center
            + CanvasVector(delta.x() / (axisX * m_Zoom),
                           delta.y() / (axisY * m_Zoom));
    return worldPoint.IsValid() ? worldPoint : CanvasVector();
}

QPointF CanvasTransform::MapFromWorld(const CanvasVector &worldPoint,
                                      const QSizeF &viewportSize) const
{
    if (!worldPoint.IsValid() || !m_Center.IsValid()
            || !IsValidViewportSize(viewportSize)) {
        return QPointF();
    }

    const QPointF viewportCenter(viewportSize.width() * 0.5,
                                 viewportSize.height() * 0.5);
    const CanvasVector delta = worldPoint - m_Center;
    if (!delta.IsValid()) {
        return QPointF();
    }

    const qreal axisX = m_FlipX ? -1.0 : 1.0;
    const qreal axisY = m_FlipY ? 1.0 : -1.0;
    const QPointF viewportPoint(viewportCenter.x()
                                + axisX * delta.GetX() * m_Zoom,
                                viewportCenter.y()
                                + axisY * delta.GetY() * m_Zoom);
    return IsFinitePoint(viewportPoint) ? viewportPoint : QPointF();
}

void CanvasTransform::PanBy(const QPointF &viewportDelta)
{
    if (!IsFinite(viewportDelta.x()) || !IsFinite(viewportDelta.y())) {
        return;
    }

    const qreal axisX = m_FlipX ? -1.0 : 1.0;
    const qreal axisY = m_FlipY ? 1.0 : -1.0;
    const CanvasVector nextCenter = m_Center
            + CanvasVector(-viewportDelta.x() / (axisX * m_Zoom),
                           -viewportDelta.y() / (axisY * m_Zoom));
    if (nextCenter.IsValid()) {
        m_Center = nextCenter;
    }
}

void CanvasTransform::ZoomAt(const QPointF &viewportPoint,
                             const QSizeF &viewportSize,
                             qreal factor)
{
    if (!IsFinitePoint(viewportPoint) || !IsValidViewportSize(viewportSize)
            || !IsFinite(factor) || factor <= 0.0) {
        return;
    }

    const CanvasVector worldPoint = MapToWorld(viewportPoint, viewportSize);
    const qreal requestedZoom = m_Zoom * factor;
    if (!worldPoint.IsValid() || !IsFinite(requestedZoom)) {
        return;
    }

    const qreal nextZoom = qBound(MINIMUM_ZOOM,
                                  requestedZoom,
                                  MAXIMUM_ZOOM);
    const QPointF viewportCenter(viewportSize.width() * 0.5,
                                 viewportSize.height() * 0.5);
    const QPointF viewportDelta = viewportPoint - viewportCenter;
    const qreal axisX = m_FlipX ? -1.0 : 1.0;
    const qreal axisY = m_FlipY ? 1.0 : -1.0;
    const CanvasVector worldPointAfterZoom = m_Center
            + CanvasVector(viewportDelta.x() / (axisX * nextZoom),
                           viewportDelta.y() / (axisY * nextZoom));
    const CanvasVector centerAdjustment = worldPoint - worldPointAfterZoom;
    const CanvasVector nextCenter = m_Center + centerAdjustment;
    if (!worldPointAfterZoom.IsValid() || !nextCenter.IsValid()) {
        return;
    }

    m_Zoom = nextZoom;
    m_Center = nextCenter;
}

bool CanvasTransform::FitRect(const QRectF &worldRect,
                              const QSizeF &viewportSize,
                              qreal marginPixels)
{
    if (!IsValidViewportSize(viewportSize) || !IsFinite(marginPixels)) {
        return false;
    }

    const qreal coordinates[] = {
        worldRect.left(),
        worldRect.top(),
        worldRect.right(),
        worldRect.bottom()
    };
    for (const qreal coordinate : coordinates) {
        if (!IsFinite(coordinate)) {
            return false;
        }
    }

    const QRectF normalizedRect = worldRect.normalized();
    const qreal worldWidth = normalizedRect.width();
    const qreal worldHeight = normalizedRect.height();
    const QPointF rectCenter = normalizedRect.center();
    if (!IsFinite(worldWidth) || !IsFinite(worldHeight)
            || !IsFinitePoint(rectCenter)) {
        return false;
    }

    const qreal requestedMargin = qMax<qreal>(0.0, marginPixels);
    const qreal maximumMargin = qMin(viewportSize.width(),
                                     viewportSize.height()) * 0.49;
    const qreal effectiveMargin = qMin(requestedMargin, maximumMargin);
    const qreal availableWidth = qMax<qreal>(1.0,
                                             viewportSize.width()
                                             - effectiveMargin * 2.0);
    const qreal availableHeight = qMax<qreal>(1.0,
                                              viewportSize.height()
                                              - effectiveMargin * 2.0);
    const qreal effectiveWorldWidth = qMax<qreal>(1.0, worldWidth);
    const qreal effectiveWorldHeight = qMax<qreal>(1.0, worldHeight);
    const qreal requestedZoom = qMin(availableWidth / effectiveWorldWidth,
                                     availableHeight / effectiveWorldHeight);
    const qreal nextZoom = qBound(MINIMUM_ZOOM, requestedZoom, MAXIMUM_ZOOM);
    const CanvasVector nextCenter(rectCenter.x(), rectCenter.y());
    if (!IsFinite(requestedZoom) || requestedZoom <= 0.0
            || !IsFinite(nextZoom) || nextZoom <= 0.0
            || !nextCenter.IsValid()) {
        return false;
    }

    m_Center = nextCenter;
    m_Zoom = nextZoom;
    return true;
}

void CanvasTransform::Reset()
{
    m_Zoom = 1.0;
    m_Center = CanvasVector(0.0, 0.0);
    m_FlipX = false;
    m_FlipY = false;
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
