#pragma once

#include "MiniGraph_global.h"

#include <QStringList>

#include <memory>

namespace mini_graph {

// 文档级扩展数据；文档通过共享指针持有，同一实例也可以被多个文档引用。
// CanvasView 会在 GUI 线程同步读取并绘制该数据；本类不提供并发保护，数据应在 GUI
// 线程访问和修改，禁止与绘制并发读写。
class MINIGRAPH_EXPORT CanvasGlobalData
{
public:
    virtual ~CanvasGlobalData() = default;

    // 创建独立的数据副本；必须返回非空且不同于当前对象的实例，并隔离可变状态。
    // CanvasDocument 和 CanvasView 不会自动调用此方法；需要独立文档级数据时由调用方显式调用。
    virtual std::shared_ptr<CanvasGlobalData> Clone() = 0;

    // 生成与 CanvasData::CalculateToQStringList() 相同语法的前置绘制指令；
    // 参数格式及限制见项目根目录 CanvasData命令格式.md。
    // 此方法在画布绘制过程中同步调用，应快速返回且不得执行阻塞操作。
    virtual QStringList CalToQStringListPre() = 0;

    // 生成与 CanvasData::CalculateToQStringList() 相同语法的后置绘制指令；
    // 参数格式及限制见项目根目录 CanvasData命令格式.md。
    // 此方法在画布绘制过程中同步调用，应快速返回且不得执行阻塞操作。
    virtual QStringList CalToQStringListPost() = 0;
};

} // namespace mini_graph
