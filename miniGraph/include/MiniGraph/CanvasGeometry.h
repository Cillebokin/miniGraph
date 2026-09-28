#pragma once

#include "CanvasTypes.h"

#include <QRectF>
#include <QVector>

namespace mini_graph {

// Returns whether the arc geometry is valid.
MINIGRAPH_EXPORT bool IsValidGeometry(const StruArcGeometry &geometry);

// Returns whether the circle geometry is valid.
MINIGRAPH_EXPORT bool IsValidGeometry(const StruCircleGeometry &geometry);

// Returns whether the polyline has enough finite vertices for its open/closed state.
MINIGRAPH_EXPORT bool IsValidGeometry(const StruPolylineGeometry &geometry);

// Returns the directed arc sweep in radians.
MINIGRAPH_EXPORT qreal GetArcSweep(const StruArcGeometry &geometry);

// Returns whether an angle lies on the directed arc.
MINIGRAPH_EXPORT bool IsAngleOnArc(const StruArcGeometry &geometry,
                                    qreal angle);

// Returns a world-space point on the arc's supporting circle.
MINIGRAPH_EXPORT QPointF CalculateArcPoint(const StruArcGeometry &geometry,
                                           qreal angle);

// Returns the point geometry bounds.
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruPointGeometry &geometry);

// Returns the line geometry bounds.
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruLineGeometry &geometry);

// Returns the arc geometry bounds.
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruArcGeometry &geometry);

// Returns the circle geometry bounds.
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruCircleGeometry &geometry);

// Returns the polyline geometry bounds.
MINIGRAPH_EXPORT QRectF GetGeometryBounds(const StruPolylineGeometry &geometry);

// Tessellates an arc into a point sequence including both endpoints.
MINIGRAPH_EXPORT QVector<QPointF> TessellateArc(const StruArcGeometry &geometry,
                                                qreal maximumError);

// Tessellates a circle into a closed point sequence.
MINIGRAPH_EXPORT QVector<QPointF> TessellateCircle(const StruCircleGeometry &geometry,
                                                   qreal maximumError);

} // namespace mini_graph
