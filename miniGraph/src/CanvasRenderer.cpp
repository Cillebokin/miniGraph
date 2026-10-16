#include "MiniGraph/CanvasRenderer.h"

#include <QtGlobal>

namespace mini_graph
{

CanvasRenderContext::CanvasRenderContext()
    : document(nullptr)
    , transform()
    , viewportSize()
    , backgroundColor(Qt::black)
{
}

CanvasRenderContext::CanvasRenderContext(const CanvasDocument *documentValue,
                                         const CanvasTransform &transformValue,
                                         const QSize &viewportSizeValue,
                                         const QColor &backgroundColorValue)
    : document(documentValue)
    , transform(transformValue)
    , viewportSize(viewportSizeValue)
    , backgroundColor(backgroundColorValue)
{
}

CanvasVector CanvasRenderContext::MapToWorld(const QPointF &viewportPosition) const
{
    return transform.MapToWorld(viewportPosition, QSizeF(viewportSize));
}

QPointF CanvasRenderContext::MapFromWorld(const CanvasVector &worldPosition) const
{
    return transform.MapFromWorld(worldPosition, QSizeF(viewportSize));
}

QRectF CanvasRenderContext::GetVisibleWorldRect() const
{
    if (viewportSize.isEmpty())
    {
        return QRectF();
    }

    const CanvasVector topLeft = MapToWorld(QPointF(0.0, 0.0));
    const CanvasVector bottomRight = MapToWorld(QPointF(viewportSize.width(),
                                                        viewportSize.height()));
    return QRectF(QPointF(topLeft.GetX(), topLeft.GetY()),
                  QPointF(bottomRight.GetX(), bottomRight.GetY())).normalized();
}

CanvasRenderer::CanvasRenderer(QObject *parent)
    : QObject(parent)
{
}

CanvasRenderer::~CanvasRenderer()
{
}

void CanvasRenderer::RequestUpdate()
{
    emit UpdateRequested();
}

void CanvasRenderer::RenderBackground(QPainter &painter,
                                      const CanvasRenderContext &context)
{
    Q_UNUSED(painter)
    Q_UNUSED(context)
}

void CanvasRenderer::RenderOverlay(QPainter &painter,
                                   const CanvasRenderContext &context)
{
    Q_UNUSED(painter)
    Q_UNUSED(context)
}

} // namespace mini_graph
