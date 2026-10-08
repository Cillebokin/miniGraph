#pragma once

#include "CanvasTypes.h"

#include <QRectF>
#include <QVector>

namespace mini_graph {

// 判断圆弧几何数据是否有效。
MINIGRAPH_EXPORT bool IsValidGeometry(const StruArcGeometry &geometry);

// 判断圆几何数据是否有效。
MINIGRAPH_EXPORT bool IsValidGeometry(const StruCircleGeometry &geometry);

// 判断多段线的有限坐标顶点数量是否满足其开放或闭合状态的要求。
MINIGRAPH_EXPORT bool IsValidGeometry(const StruPolylineGeometry &geometry);

// 返回按指定方向计算的圆弧扫掠角，单位为弧度。
MINIGRAPH_EXPORT qreal GetArcSweep(const StruArcGeometry &geometry);

// 判断指定角度是否位于有向圆弧上。
MINIGRAPH_EXPORT bool IsAngleOnArc(const StruArcGeometry &geometry,
                                    qreal angle);

// 返回圆弧支撑圆上指定角度对应的世界坐标点。
MINIGRAPH_EXPORT CanvasVector CalculateArcPoint(const StruArcGeometry &geometry,
                                                qreal angle);

// 返回点几何数据的包围矩形。
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruPointGeometry &geometry);

// 返回线段几何数据的包围矩形。
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruLineGeometry &geometry);

// 返回圆弧几何数据的包围矩形。
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruArcGeometry &geometry);

// 返回圆几何数据的包围矩形。
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruCircleGeometry &geometry);

// 返回多段线几何数据的包围矩形。
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruPolylineGeometry &geometry);

// 将圆弧离散为点序列，结果包含起点和终点。
MINIGRAPH_EXPORT QVector<CanvasVector> TessellateArc(
        const StruArcGeometry &geometry,
        qreal maximumError);

// 将圆离散为闭合点序列。
MINIGRAPH_EXPORT QVector<CanvasVector> TessellateCircle(
        const StruCircleGeometry &geometry,
        qreal maximumError);

} // namespace mini_graph
