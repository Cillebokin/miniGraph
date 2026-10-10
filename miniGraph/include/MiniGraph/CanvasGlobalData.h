#pragma once

#include "MiniGraph_global.h"

#include <QStringList>

#include <memory>

namespace mini_graph {

// 附着在单个画布文档上的全局扩展数据。
class MINIGRAPH_EXPORT CanvasGlobalData
{
public:
    virtual ~CanvasGlobalData() = default;

    // 创建独立的数据副本。
    virtual std::shared_ptr<CanvasGlobalData> Clone() = 0;

    // 生成与 CanvasData::CalculateToQStringList() 相同语法的前置绘制指令；
    // 参数格式及限制见项目根目录 CanvasData命令格式.md。
    virtual QStringList CalToQStringListPre() = 0;

    // 生成与 CanvasData::CalculateToQStringList() 相同语法的后置绘制指令；
    // 参数格式及限制见项目根目录 CanvasData命令格式.md。
    virtual QStringList CalToQStringListPost() = 0;
};

} // namespace mini_graph
