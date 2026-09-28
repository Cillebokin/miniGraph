#pragma once

#include "CanvasTypes.h"
#include "MiniGraph_global.h"

#include <QPointF>

namespace mini_graph {

class CanvasDocument;

// Types of points that can be used by canvas snapping.
enum class CanvasSnapType : quint8
{
    None,
    Grid,
    Point,
    LineEndpoint,
    ArcEndpoint,
    ArcCenter,
    CircleCenter,
    PolylineVertex
};

// Result returned by a canvas snap query.
struct CanvasSnapResult
{
    // The kind of snap result.
    CanvasSnapType type = CanvasSnapType::None;

    // The source item, or zero for a grid result.
    ItemId itemId = 0;

    // The snapped world coordinate.
    QPointF position;

    // Return whether this result contains a snap point.
    bool IsValid() const
    {
        return type != CanvasSnapType::None;
    }

    // Compare two snap results.
    bool operator==(const CanvasSnapResult &other) const
    {
        return type == other.type
                && itemId == other.itemId
                && position == other.position;
    }

    // Compare two snap results for inequality.
    bool operator!=(const CanvasSnapResult &other) const
    {
        return !(*this == other);
    }
};

// Stateless snapping algorithm used by CanvasView and future creation tools.
class MINIGRAPH_EXPORT CanvasSnapper
{
public:
    // Find the nearest visible geometry snap point and grid point.
    // Geometry points within toleranceWorld take priority over the grid.
    // Grid snapping uses gridSpacing as its world-space unit.
    static CanvasSnapResult FindSnap(const CanvasDocument *document,
                                     const QPointF &worldPosition,
                                     qreal toleranceWorld,
                                     qreal gridSpacing,
                                     bool geometrySnapEnabled,
                                     bool gridSnapEnabled);
};

} // namespace mini_graph

Q_DECLARE_METATYPE(mini_graph::CanvasSnapType)
Q_DECLARE_METATYPE(mini_graph::CanvasSnapResult)
