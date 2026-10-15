#pragma once

#include "MiniGraph/CanvasEntities.h"
#include "MiniGraph/CanvasRenderer.h"

#include <QStringList>
#include <QVector>

class QPainter;

namespace mini_graph {

namespace detail {

void RenderCanvasDataCommands(QPainter &painter,
                              const CanvasRenderContext &context,
                              const QStringList &commands,
                              bool isGlobal = false);

// 返回样式在线宽显示设置下实际占用的逻辑像素宽度。
qreal GetDisplayStrokeWidth(const StruCanvasStyle &style,
                            qreal logicalDpiX,
                            bool lineWidthVisible);

// 返回点图元实际绘制的逻辑像素直径。
qreal GetDisplayPointSize(const StruCanvasStyle &style,
                          qreal logicalDpiX,
                          bool lineWidthVisible);

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
                const CanvasRenderContext &context,
                qreal logicalDpiX,
                bool lineWidthVisible) const;
};

} // namespace mini_graph
