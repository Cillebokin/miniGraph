#pragma once

#include "MiniGraph_global.h"

#include <QSharedPointer>

class QPainter;

namespace mini_graph {

struct CanvasRenderContext;

// 附着在单个画布文档上的全局扩展数据。
class MINIGRAPH_EXPORT CanvasGlobalData
{
public:
    virtual ~CanvasGlobalData() = default;

    // 创建独立的数据副本。
    virtual QSharedPointer<CanvasGlobalData> Clone() const = 0;

    // 在普通图元及自定义几何之前绘制全局内容。
    virtual void RenderBefore(QPainter &, const CanvasRenderContext &) {}

    // 在普通图元及自定义几何之后绘制全局内容。
    virtual void RenderAfter(QPainter &, const CanvasRenderContext &) {}
};

} // namespace mini_graph
