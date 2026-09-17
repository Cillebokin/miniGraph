#pragma once

#include "MiniGraph_global.h"

#include <QPointF>
#include <QSizeF>

namespace mini_graph {

class MINIGRAPH_EXPORT CanvasTransform
{
public:
    CanvasTransform();

    qreal GetZoom() const;
    void SetZoom(qreal zoom);

    QPointF GetCenter() const;
    void SetCenter(const QPointF &center);

    QPointF MapToWorld(const QPointF &viewportPoint, const QSizeF &viewportSize) const;
    QPointF MapFromWorld(const QPointF &worldPoint, const QSizeF &viewportSize) const;

    void PanBy(const QPointF &viewportDelta);
    void ZoomAt(const QPointF &viewportPoint, const QSizeF &viewportSize, qreal factor);
    void Reset();

    static qreal GetMinimumZoom();
    static qreal GetMaximumZoom();

private:
    qreal m_Zoom;
    QPointF m_Center;
};

} // namespace mini_graph
