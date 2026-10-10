#pragma once

#include "MiniGraph_global.h"

#include <QStringList>

#include <memory>

namespace mini_graph {

class CanvasEntity;

// 附着在单个画布图元上的扩展数据。
// 数据会随图元克隆；克隆后通过 Update() 重新绑定到目标图元。
// 同一个 CanvasData 实例一次只能绑定到一个图元；Update(nullptr) 表示解绑。
class MINIGRAPH_EXPORT CanvasData
{
public:
    // 创建未绑定到任何图元的数据基类状态。
    CanvasData()
        : m_BoundEntity(nullptr)
    {
    }

    // 复制派生数据时不继承源对象的图元绑定关系。
    CanvasData(const CanvasData &)
        : m_BoundEntity(nullptr)
    {
    }

    // 赋值不转移或覆盖当前对象的图元绑定关系。
    CanvasData &operator=(const CanvasData &)
    {
        return *this;
    }

    virtual ~CanvasData() = default;

    // 创建独立的数据副本；派生类应避免共享可变状态。
    virtual std::shared_ptr<CanvasData> Clone() = 0;

    // 生成绘制指令列表；每个列表项是一条逗号分隔的命令。
    // 命令签名、参数限制及兼容边界见项目根目录 CanvasData命令格式.md。
    // 当前格式不支持字段转义或逗号/空字段；未知或无效命令会被忽略。
    virtual QStringList CalculateToQStringList() = 0;

    // 由图元绑定或解绑时调用；派生类不要自行改变绑定关系。
    virtual void Update(CanvasEntity *entity) = 0;

private:
    friend class CanvasEntity;
    CanvasEntity *m_BoundEntity;
};

} // namespace mini_graph
