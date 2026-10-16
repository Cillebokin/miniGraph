#pragma once

#include "MiniGraph_global.h"

#include <QObject>
#include <QPointer>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace mini_graph
{

class CanvasView;
struct CanvasRenderContext;

// 面向调用层的画布交互 Action；CanvasView 负责接管已安装 Action 的所有权。
class MINIGRAPH_EXPORT CanvasAction : public QObject
{
    Q_OBJECT

public:
    // 创建尚未附着到画布的 Action。
    explicit CanvasAction(QObject *parent = nullptr);

    // 释放 Action。
    ~CanvasAction() override;

    // 返回当前附着的画布；未安装时返回 nullptr。
    CanvasView *GetCanvasView() const;

    // 请求附着该 Action 的画布重新绘制。
    void RequestUpdate();

    // 请求结束当前 Action 并恢复默认交互。
    // 请求在当前事件回调返回后处理，避免回调执行期间销毁 Action。
    void Finish();

    // 处理鼠标按下事件。
    virtual void MousePressEvent(QMouseEvent *event);

    // 处理鼠标移动事件。
    virtual void MouseMoveEvent(QMouseEvent *event);

    // 处理鼠标释放事件。
    virtual void MouseReleaseEvent(QMouseEvent *event);

    // 处理鼠标双击事件。
    virtual void MouseDoubleClickEvent(QMouseEvent *event);

    // 处理键盘按下事件。
    virtual void KeyPressEvent(QKeyEvent *event);

    // 处理键盘释放事件。
    virtual void KeyReleaseEvent(QKeyEvent *event);

    // 绘制 Action 的临时预览；默认不绘制。
    virtual void RenderOverlay(QPainter &painter,
                               const CanvasRenderContext &context);

    // Action 被取消或替换时调用，默认不执行操作。
    virtual void Cancel();

signals:
    // 请求附着该 Action 的画布重新绘制。
    void UpdateRequested();

    // 请求结束当前 Action；由 CanvasView 延迟处理。
    void FinishRequested();

private:
    friend class CanvasView;

    void SetCanvasView(CanvasView *view);

    QPointer<CanvasView> m_CanvasView;
};

} // namespace mini_graph
