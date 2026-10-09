#pragma once

#include "MiniGraph/CanvasEntities.h"

namespace mini_graph {

namespace detail {

// 判断圆弧起止角是否形成非零且非整周的有效扫掠。
bool IsValidArcSweep(qreal startAngle, qreal endAngle);

// Evaluate one valid knot span of a spline without flattening the curve.
bool EvaluateSplineInSpan(const CanvasSpline &spline,
                          int knotSpan,
                          qreal parameter,
                          CanvasVector *point);

// Return the exact start and end points of the active spline parameter domain.
bool TryGetSplineEndpoints(const CanvasSpline &spline,
                           CanvasVector *startPoint,
                           CanvasVector *endPoint);

} // namespace detail

} // namespace mini_graph
