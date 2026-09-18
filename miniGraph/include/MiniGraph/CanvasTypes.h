#pragma once

#include "MiniGraph_global.h"

#include <QColor>
#include <QMetaType>
#include <QPointF>
#include <QtGlobal>

namespace mini_graph {

// 画布图元的唯一标识。
using ItemId = quint64;

// 画布中的线段图元数据。
// 坐标使用画布世界坐标系表示，颜色和线宽用于图元绘制。
struct StruLinePrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 线段起点的世界坐标。
    QPointF start;

    // 线段终点的世界坐标。
    QPointF end;

    // 线段颜色，默认使用白色。
    QColor color = QColor(Qt::white);

    // 线段宽度，默认值为 1.0。
    qreal width = 1.0;
};

} // namespace mini_graph

Q_DECLARE_METATYPE(mini_graph::StruLinePrimitive)
