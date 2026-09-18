#pragma once

#include "MiniGraph_global.h"

#include <QColor>
#include <QMetaType>
#include <QPointF>
#include <QtGlobal>

namespace mini_graph {

// 画布图元的唯一标识。
using ItemId = quint64;

// 画布图元的几何类型。
enum class CanvasEntityType : quint8
{
    Invalid,
    Point,
    Line,
    Arc,
    Circle
};

// 画布中的点几何数据。
struct StruPointGeometry
{
    // 创建位于世界坐标原点的点几何数据。
    StruPointGeometry()
        : position(0.0, 0.0)
    {
    }

    // 点的世界坐标。
    QPointF position;
};

// 画布中的线段几何数据。
struct StruLineGeometry
{
    // 创建起点和终点均位于世界坐标原点的线段几何数据。
    StruLineGeometry()
        : startPoint(0.0, 0.0)
        , endPoint(0.0, 0.0)
    {
    }

    // 线段起点的世界坐标。
    QPointF startPoint;

    // 线段终点的世界坐标。
    QPointF endPoint;
};

// 画布中的圆弧几何数据。
struct StruArcGeometry
{
    // 创建位于原点、半径为 0、角度为 0 的圆弧几何数据。
    StruArcGeometry()
        : center(0.0, 0.0)
        , radius(0.0)
        , startAngle(0.0)
        , endAngle(0.0)
        , reversed(false)
    {
    }

    // 圆弧圆心的世界坐标。
    QPointF center;

    // 圆弧半径，必须大于 0 才能加入文档。
    qreal radius;

    // 圆弧起始角，单位为弧度。
    qreal startAngle;

    // 圆弧终止角，单位为弧度。
    qreal endAngle;

    // 是否沿顺时针方向扫掠，false 表示逆时针方向。
    bool reversed;
};

// 画布中的圆几何数据。
struct StruCircleGeometry
{
    // 创建位于原点、半径为 0 的圆几何数据。
    StruCircleGeometry()
        : center(0.0, 0.0)
        , radius(0.0)
    {
    }

    // 圆心的世界坐标。
    QPointF center;

    // 圆半径，必须大于 0 才能加入文档。
    qreal radius;
};

// 画布中的点图元存储数据。
struct StruPointPrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 点的几何数据。
    StruPointGeometry geometry;
};

// 画布中的线段图元存储数据。
// 坐标使用画布世界坐标系表示，颜色和线宽用于兼容现有线段绘制。
struct StruLinePrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 线段几何数据。
    StruLineGeometry geometry;

    // 线段颜色，默认使用白色。
    QColor color = QColor(Qt::white);

    // 线段宽度，默认值为 1.0。
    qreal width = 1.0;
};

// 画布中的圆弧图元存储数据。
struct StruArcPrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 圆弧的几何数据。
    StruArcGeometry geometry;
};

// 画布中的圆图元存储数据。
struct StruCirclePrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 圆的几何数据。
    StruCircleGeometry geometry;
};

} // namespace mini_graph

Q_DECLARE_METATYPE(mini_graph::CanvasEntityType)
Q_DECLARE_METATYPE(mini_graph::StruPointGeometry)
Q_DECLARE_METATYPE(mini_graph::StruLineGeometry)
Q_DECLARE_METATYPE(mini_graph::StruArcGeometry)
Q_DECLARE_METATYPE(mini_graph::StruCircleGeometry)
Q_DECLARE_METATYPE(mini_graph::StruPointPrimitive)
Q_DECLARE_METATYPE(mini_graph::StruLinePrimitive)
Q_DECLARE_METATYPE(mini_graph::StruArcPrimitive)
Q_DECLARE_METATYPE(mini_graph::StruCirclePrimitive)
