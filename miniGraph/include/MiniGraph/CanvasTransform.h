#pragma once

#include "MiniGraph_global.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace mini_graph {

// 管理画布世界坐标与视口坐标之间的变换。
// 变换以中心点和缩放比例描述视图状态，屏幕坐标的 Y 轴方向会转换为
// 世界坐标系中向上的 Y 轴方向。
class MINIGRAPH_EXPORT CanvasTransform
{
public:
    // 创建一个缩放比例为 1、中心点为世界原点的变换。
    CanvasTransform();

    // 返回当前缩放比例。
    qreal GetZoom() const;

    // 设置缩放比例，最终会限制在最小和最大缩放范围内。
    void SetZoom(qreal zoom);

    // 返回当前视图中心对应的世界坐标。
    QPointF GetCenter() const;

    // 设置视图中心对应的世界坐标。
    void SetCenter(const QPointF &center);

    // 将视口坐标转换为世界坐标，viewportPoint 的原点位于视口左上角。
    QPointF MapToWorld(const QPointF &viewportPoint, const QSizeF &viewportSize) const;

    // 将世界坐标转换为视口坐标。
    QPointF MapFromWorld(const QPointF &worldPoint, const QSizeF &viewportSize) const;

    // 按视口像素增量平移画布。
    void PanBy(const QPointF &viewportDelta);

    // 围绕指定视口位置缩放，并保持该位置下的世界坐标不变。
    void ZoomAt(const QPointF &viewportPoint, const QSizeF &viewportSize, qreal factor);

    // 调整缩放比例和视图中心，使世界坐标矩形适配到视口内。
    // marginPixels 为视口边缘预留的像素边距；视口无效时返回 false。
    bool FitRect(const QRectF &worldRect,
                 const QSizeF &viewportSize,
                 qreal marginPixels = 40.0);

    // 将变换恢复为默认状态。
    void Reset();

    // 返回允许使用的最小缩放比例。
    static qreal GetMinimumZoom();

    // 返回允许使用的最大缩放比例。
    static qreal GetMaximumZoom();

private:
    // 当前世界坐标到视口坐标的缩放比例。
    qreal m_Zoom;

    // 当前视口中心对应的世界坐标。
    QPointF m_Center;
};

} // namespace mini_graph
