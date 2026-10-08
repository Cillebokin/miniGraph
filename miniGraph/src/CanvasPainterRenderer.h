#pragma once

#include "MiniGraph/CanvasEntities.h"
#include "MiniGraph/CanvasRenderer.h"

#include <QVector>

class QPainter;

namespace mini_graph {

namespace detail {

// Share the built-in renderer's curve flattening with view hit testing.
QVector<CanvasVector> TessellateEllipseForSelection(
        const CanvasEllipse &ellipse, qreal maximumError, bool *closed);
QVector<QVector<CanvasVector>> TessellateSplineForSelection(
        const CanvasSpline &spline, qreal maximumError);

} // namespace detail

// Built-in QPainter renderer for the document geometry.
class CanvasPainterRenderer
{
public:
    void Render(QPainter &painter,
                const CanvasRenderContext &context) const;
};

} // namespace mini_graph
