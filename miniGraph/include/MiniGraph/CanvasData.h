#pragma once

#include "MiniGraph_global.h"

#include <QSharedPointer>

class QPainter;

namespace mini_graph {

class CanvasEntity;
struct CanvasRenderContext;

// 附着在单个画布图元上的扩展数据。
// 数据会随图元克隆；克隆后通过 Update() 重新绑定到目标图元。
class MINIGRAPH_EXPORT CanvasData
{
public:
    virtual ~CanvasData() = default;

    // 创建独立的数据副本；派生类应避免共享可变状态。
    virtual QSharedPointer<CanvasData> Clone() const = 0;

    // 将数据对象绑定或重新绑定到图元。
    virtual void Update(CanvasEntity *entity) = 0;

    // 在所属图元的基础几何之后绘制附加内容。
    // painter 使用视口坐标；需要世界坐标时可通过 context 执行换算。
    virtual void Render(QPainter &painter,
                        const CanvasRenderContext &context) = 0;
};

} // namespace mini_graph
