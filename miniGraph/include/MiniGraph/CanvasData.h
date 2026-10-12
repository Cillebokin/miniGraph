#pragma once

#include "MiniGraph_global.h"

#include <QStringList>

#include <memory>

namespace mini_graph {

class CanvasEntity;

// 附着在单个画布图元上的扩展数据。
// 同一个实例一次只能绑定到一个图元；实体克隆时会调用 Clone() 复制数据，随后通过
// Update() 将副本绑定到新图元。CanvasView 会在 GUI 线程同步读取并绘制该数据；本类不
// 提供并发保护，绑定期间应在 GUI 线程访问和修改，禁止与绘制并发读写。
// Update(nullptr) 表示解绑。
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

    // 创建独立的数据副本；必须返回非空且不同于当前对象的实例，并隔离可变状态。
    // 实体克隆流程会在此后重新绑定副本；返回空指针或当前对象会使实体克隆失败。
    virtual std::shared_ptr<CanvasData> Clone() = 0;

    // 生成绘制指令列表；每个列表项是一条逗号分隔的命令。
    // 命令签名、参数限制及兼容边界见项目根目录 CanvasData命令格式.md。
    // 当前格式不支持字段转义或逗号/空字段；未知或无效命令会被忽略。
    // 此方法在画布绘制过程中同步调用，应快速返回且不得执行阻塞操作。
    virtual QStringList CalculateToQStringList() = 0;

    // 由图元绑定或解绑时在所属 GUI 线程调用；派生类不要自行改变绑定关系。
    virtual void Update(CanvasEntity *entity) = 0;

private:
    friend class CanvasEntity;
    CanvasEntity *m_BoundEntity;
};

} // namespace mini_graph
