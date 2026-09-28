#pragma once

#include "MiniGraph/CanvasRenderer.h"

class QPainter;

namespace mini_graph {

// Built-in QPainter renderer for the document geometry.
class CanvasPainterRenderer
{
public:
    void Render(QPainter &painter,
                const CanvasRenderContext &context) const;
};

} // namespace mini_graph
