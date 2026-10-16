#pragma once

#include "CanvasTypes.h"
#include "MiniGraph_global.h"

namespace mini_graph
{

class CanvasDocument;

// 画布捕捉支持的点类型。
enum class CanvasSnapType : quint8
{
    None,
    Grid,
    Point,
    LineEndpoint,
    ArcEndpoint,
    ArcCenter,
    CircleCenter,
    PolylineVertex,
    EllipseCenter,
    EllipseEndpoint,
    EllipseAxisPoint,
    SplineEndpoint
};

// 画布捕捉查询的结果。
struct CanvasSnapResult
{
    // 捕捉结果的类型。
    CanvasSnapType type = CanvasSnapType::None;

    // 捕捉来源图元的标识；网格捕捉结果为 0。
    ItemId itemId = 0;

    // 捕捉位置的世界坐标。
    CanvasVector position;

    // 返回结果是否包含有效的捕捉点。
    bool IsValid() const
    {
        return type != CanvasSnapType::None;
    }

    // 比较两个捕捉结果是否相同。
    bool operator==(const CanvasSnapResult &other) const
    {
        return type == other.type
                && itemId == other.itemId
                && position == other.position;
    }

    // 比较两个捕捉结果是否不同。
    bool operator!=(const CanvasSnapResult &other) const
    {
        return !(*this == other);
    }
};

// 无状态捕捉算法，供 CanvasView 和后续图元创建工具使用。
class MINIGRAPH_EXPORT CanvasSnapper
{
public:
    // 查找最近的可见几何捕捉点或网格捕捉点。
    // 位于 toleranceWorld 容差范围内的几何点优先于网格点。
    // 网格捕捉使用 gridSpacing 作为世界坐标间距。
    static CanvasSnapResult FindSnap(const CanvasDocument *document,
                                     const CanvasVector &worldPosition,
                                     qreal toleranceWorld,
                                     qreal gridSpacing,
                                     bool geometrySnapEnabled,
                                     bool gridSnapEnabled);
};

} // namespace mini_graph

Q_DECLARE_METATYPE(mini_graph::CanvasSnapType)
Q_DECLARE_METATYPE(mini_graph::CanvasSnapResult)
