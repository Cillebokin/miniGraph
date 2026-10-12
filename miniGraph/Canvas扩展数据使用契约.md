# Canvas 扩展数据使用契约

本文说明 `CanvasData`、`CanvasGlobalData` 与 `CanvasDocument` 的线程、刷新通知和克隆约定。相关类不提供并发保护；本契约不代表库会自动切换线程或自动复制数据。

## 线程约定

- `CanvasView` 是 Qt GUI 控件。与它关联的 `CanvasDocument`、图元附加的 `CanvasData` 和文档附加的 `CanvasGlobalData`，应在 GUI 线程访问和修改。
- 这些类不提供互斥锁。绘制时，画布会在 GUI 线程同步调用扩展数据的绘制指令生成方法；禁止工作线程同时修改或读取同一个绑定数据对象。
- `QObject` 的线程亲和性不会让普通成员函数调用自动排队到对象线程。工作线程只处理独立的数据或计算结果，再通过排队信号槽交回 GUI 线程，之后再修改文档或绑定数据。
- 绘制指令生成方法应快速返回，不应执行文件 I/O、网络访问、等待锁或其他阻塞操作。

## 变更与刷新通知

替换扩展数据时使用文档接口：

```cpp
document.SetItemCanvasData(itemId, itemData);
document.SetCanvasGlobalData(globalData);
```

成功设置会发出 `CanvasDocument::Changed()`。设置接口保存传入的 `shared_ptr`，不会自动调用 `Clone()`。

如果修改的是已经附着的数据对象本身，应先在 GUI 线程完成一组字段修改，再显式通知文档：

```cpp
const std::shared_ptr<mini_graph::CanvasData> data =
        document.GetItemCanvasData(itemId);
// 将 data 转换为调用方自己的派生类型，并在 GUI 线程修改它。
document.NotifyItemCanvasDataChanged(itemId);

const std::shared_ptr<mini_graph::CanvasGlobalData> globalData =
        document.GetCanvasGlobalData();
// 在 GUI 线程修改 globalData 后通知。
document.NotifyCanvasGlobalDataChanged();
```

通知只表示数据已变化，不代替数据修改。成功通知会发出 `Changed()`，关联视图据此刷新；若多个字段属于同一个数据对象，应先全部修改，再通知一次。若同一个 `CanvasGlobalData` 实例被多个文档共享，修改后需要分别通知每个文档；需要文档间状态隔离时，应使用独立副本。

## 克隆约定

### `CanvasData`

`CanvasData::Clone()` 会在图元克隆时由实体克隆流程调用。实现必须返回非空、不同于源对象的实例，并复制所需业务状态、隔离可变数据。返回空指针或源对象本身会使实体克隆失败。克隆后的数据由框架绑定到新图元并调用 `Update(newEntity)`；派生类不应把源图元的绑定关系复制到副本中。不可变资源可以按调用方定义共享，但共享的可变状态不满足独立克隆契约。

### `CanvasGlobalData`

`CanvasGlobalData::Clone()` 目前不会由 `CanvasDocument` 或 `CanvasView` 自动调用。只有调用方需要为另一个文档创建独立的全局数据状态时才显式克隆；若直接把同一个 `shared_ptr` 设置给多个文档，它们共享的是同一个可变对象，而不是副本。

## 最小用法

基础示例中的 `BasicCanvasData` 和 `BasicCanvasGlobalData` 提供可运行的派生实现；工具栏里的 **Update item data** 和 **Update global data** 会原地修改数据并调用相应通知。

```cpp
#include <memory>
#include "MiniGraph/MiniGraph.h"
#include "BasicCanvasData.h"

void DemonstrateExtensionData(mini_graph::CanvasDocument &document)
{
    // 本函数及其文档操作均在 GUI 线程执行。
    mini_graph::CanvasPoint sourcePoint(mini_graph::CanvasVector(0.0, 0.0));
    sourcePoint.SetCanvasData(std::make_shared<BasicCanvasData>());

    // AddEntity 克隆图元，因此会调用 CanvasData::Clone()。
    const mini_graph::ItemId itemId = document.AddEntity(sourcePoint);
    const std::shared_ptr<BasicCanvasData> itemData =
            std::dynamic_pointer_cast<BasicCanvasData>(
                document.GetItemCanvasData(itemId));
    if (itemData) {
        itemData->AdvanceRevision();
        document.NotifyItemCanvasDataChanged(itemId);
    }

    const std::shared_ptr<BasicCanvasGlobalData> globalData =
            std::make_shared<BasicCanvasGlobalData>();
    document.SetCanvasGlobalData(globalData);
    globalData->AdvanceRevision();
    document.NotifyCanvasGlobalDataChanged();

    // 只有需要独立状态时才显式克隆全局数据。
    mini_graph::CanvasDocument anotherDocument;
    anotherDocument.SetCanvasGlobalData(globalData->Clone());
}
```

完整实现见 [BasicCanvasData.h](examples/basic/BasicCanvasData.h)、[BasicCanvasData.cpp](examples/basic/BasicCanvasData.cpp) 和 [Main.cpp](examples/basic/Main.cpp)。
