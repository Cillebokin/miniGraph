#pragma once

#include "MiniGraph_global.h"

#include <QStringList>

#include <memory>

namespace mini_graph {

class CanvasEntity;

// 附着在单个画布图元上的扩展数据。
// 数据会随图元克隆；克隆后通过 Update() 重新绑定到目标图元。
class MINIGRAPH_EXPORT CanvasData
{
public:
    virtual ~CanvasData() = default;

    // 创建独立的数据副本；派生类应避免共享可变状态。
    virtual std::shared_ptr<CanvasData> Clone() = 0;

    // 生成 CADLab CAMData 绘制指令列表。
    // 支持 Pen、Brush、Font 状态及 Point、Line、Arc、Ellipse、Polyline、
    // Polygon、Rect、Cubic、Quad、Text、Image；Brush 使用 Qt::BrushStyle，
    // 包含横线、竖线、交叉线等 hatch pattern 填充样式。
    virtual QStringList CalculateToQStringList() = 0;

    // 将数据对象绑定或重新绑定到图元。
    virtual void Update(CanvasEntity *entity) = 0;

};

} // namespace mini_graph
