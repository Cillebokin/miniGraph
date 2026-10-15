#pragma once

#include "MiniGraph_global.h"
#include "CanvasVector.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace mini_graph {

// 管理画布世界坐标与视口坐标之间的变换。
// 变换以中心点、缩放比例和 X/Y 轴显示方向描述视图状态；默认世界坐标
// X 轴向右、Y 轴向上，也支持分别翻转两个轴的显示方向。
class MINIGRAPH_EXPORT CanvasTransform
{
public:
    // 创建一个缩放比例为 1、中心点为世界原点的变换。
    CanvasTransform();

    // 返回当前缩放比例。
    qreal GetZoom() const;

    // 设置缩放比例，最终会限制在最小和最大缩放范围内；非有限或非正输入不改变当前值。
    void SetZoom(qreal zoom);

    // 返回当前视图中心对应的世界坐标。
    CanvasVector GetCenter() const;

    // 设置视图中心对应的世界坐标；无效中心不改变当前值。
    void SetCenter(const CanvasVector &center);

    // 返回 X 轴是否按 CADLab 视图语义翻转。
    bool GetFlipX() const;

    // 设置 X 轴视图翻转；切换时保持世界原点的视口位置不变。
    void SetFlipX(bool flipX);

    // 返回 Y 轴是否按 CADLab 视图语义翻转。
    bool GetFlipY() const;

    // 设置 Y 轴视图翻转；切换时保持世界原点的视口位置不变。
    void SetFlipY(bool flipY);

    // 将视口坐标转换为世界坐标，viewportPoint 的原点位于视口左上角。
    // 输入坐标或视口尺寸无效时返回无效的 CanvasVector。
    CanvasVector MapToWorld(const QPointF &viewportPoint,
                            const QSizeF &viewportSize) const;

    // 将世界坐标转换为视口坐标。
    // 输入无效或计算结果非有限时返回默认 QPointF()。
    QPointF MapFromWorld(const CanvasVector &worldPoint,
                         const QSizeF &viewportSize) const;

    // 按视口像素增量平移画布；输入或计算结果无效时不改变当前状态。
    void PanBy(const QPointF &viewportDelta);

    // 围绕指定视口位置缩放，并保持该位置下的世界坐标不变。
    // 输入坐标、缩放因子或视口尺寸无效时不修改变换状态。
    void ZoomAt(const QPointF &viewportPoint, const QSizeF &viewportSize, qreal factor);

    // 调整缩放比例和视图中心，使世界坐标矩形适配到视口内。
    // marginPixels 为视口边缘预留的像素边距；任一输入无效时返回 false，且不修改状态。
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
    CanvasVector m_Center;

    // 世界坐标 X 轴是否映射到视口负方向。
    bool m_FlipX;

    // 世界坐标 Y 轴是否映射到视口正方向。
    bool m_FlipY;
};

} // namespace mini_graph
