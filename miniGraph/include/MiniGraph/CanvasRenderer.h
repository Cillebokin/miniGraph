#pragma once

#include "CanvasTransform.h"
#include "CanvasVector.h"
#include "MiniGraph_global.h"

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSize>

class QPainter;

namespace mini_graph
{

class CanvasDocument;

// 自定义绘制时使用的画布渲染上下文。
struct MINIGRAPH_EXPORT CanvasRenderContext
{
    // 创建一个空的渲染上下文。
    CanvasRenderContext();

    // 按当前画布状态创建渲染上下文。
    CanvasRenderContext(const CanvasDocument *document,
                        const CanvasTransform &transform,
                        const QSize &viewportSize,
                        const QColor &backgroundColor = QColor(Qt::black));

    // 当前画布文档，只读访问；可能为空。
    const CanvasDocument *document;

    // 当前世界坐标变换。
    CanvasTransform transform;

    // 当前视口尺寸。
    QSize viewportSize;

    // 当前视图的背景颜色。
    QColor backgroundColor;

    // 将视口坐标转换为世界坐标。
    CanvasVector MapToWorld(const QPointF &viewportPosition) const;

    // 将世界坐标转换为视口坐标。
    QPointF MapFromWorld(const CanvasVector &worldPosition) const;

    // 返回当前视口覆盖的世界坐标矩形。
    QRectF GetVisibleWorldRect() const;
};

// 画布自定义渲染器接口。
// CanvasView 按注册顺序调用各渲染器的背景、几何和叠加层回调；
// 各回调接收当前视图的 CanvasRenderContext，并拥有独立的 QPainter 状态。
class MINIGRAPH_EXPORT CanvasRenderer : public QObject
{
    Q_OBJECT

public:
    // 创建自定义渲染器，parent 可用于管理渲染器生命周期。
    explicit CanvasRenderer(QObject *parent = nullptr);

    // 释放自定义渲染器。
    ~CanvasRenderer() override;

    // 请求当前画布重新绘制。
    // 每个绑定该渲染器的 CanvasView 都会自动响应此请求。
    void RequestUpdate();

    // 绘制自定义 QPainter 背景内容。
    // 该回调在所有内置绘制之前执行；多个渲染器按注册顺序调用。
    virtual void RenderBackground(QPainter &painter,
                                  const CanvasRenderContext &context);

    // 绘制自定义几何内容。
    // 该回调在内置图元之后、内置叠加层之前执行；多个渲染器按注册顺序调用。
    virtual void Render(QPainter &painter,
                        const CanvasRenderContext &context) = 0;

    // 绘制自定义 QPainter 叠加层内容。
    // 该回调在内置叠加层之后执行；多个渲染器按注册顺序调用。
    // 默认实现不执行任何操作。
    virtual void RenderOverlay(QPainter &painter,
                               const CanvasRenderContext &context);

signals:
    // 请求附加该渲染器的画布重新绘制。
    void UpdateRequested();
};

} // namespace mini_graph
