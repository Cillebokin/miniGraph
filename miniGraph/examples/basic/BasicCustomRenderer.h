#pragma once

#include "MiniGraph/CanvasRenderer.h"

#include <QPointF>

// Custom canvas renderer used by the basic example.
// It demonstrates background, QPainter geometry, and overlay layers.
class BasicCustomRenderer : public mini_graph::CanvasRenderer
{
public:
    // Creates the example renderer.
    explicit BasicCustomRenderer(QObject *parent = nullptr);

    // Advances the example shape and requests a canvas refresh.
    void Advance();

    // Draws the example background layer.
    void RenderBackground(QPainter &painter,
                          const mini_graph::CanvasRenderContext &context) override;

    // Draws the example QPainter geometry.
    void Render(QPainter &painter,
                const mini_graph::CanvasRenderContext &context) override;

    // Draws the example overlay.
    void RenderOverlay(QPainter &painter,
                       const mini_graph::CanvasRenderContext &context) override;

private:
    // World-space offset of the example shape.
    QPointF m_Offset;
};
