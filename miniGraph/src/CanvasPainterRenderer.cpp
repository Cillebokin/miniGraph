#include "CanvasPainterRenderer.h"

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasGeometry.h"

#include <QColor>
#include <QHash>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <QtGlobal>

namespace mini_graph {

namespace {

QColor GetDisplayColor(const StruCanvasStyle &style, bool selected)
{
    return selected ? style.selectedStrokeColor : style.strokeColor;
}

QPolygonF MapPolyline(const QVector<QPointF> &worldPoints,
                      const CanvasRenderContext &context)
{
    QPolygonF viewportPoints;
    viewportPoints.reserve(worldPoints.size());
    for (const QPointF &worldPoint : worldPoints) {
        viewportPoints.append(context.MapFromWorld(worldPoint));
    }
    return viewportPoints;
}

} // namespace

void CanvasPainterRenderer::Render(
        QPainter &painter,
        const CanvasRenderContext &context) const
{
    const CanvasDocument *document = context.document;
    if (!document) {
        return;
    }

    const qreal zoom = qMax(context.transform.GetZoom(),
                            CanvasTransform::GetMinimumZoom());
    const qreal maximumWorldError = qMax<qreal>(1.0e-6, 0.5 / zoom);
    const QVector<StruPointPrimitive> &points = document->GetPointPrimitives();
    const QVector<StruLinePrimitive> &lines = document->GetLinePrimitives();
    const QVector<StruArcPrimitive> &arcs = document->GetArcPrimitives();
    const QVector<StruCirclePrimitive> &circles = document->GetCirclePrimitives();
    const QVector<StruPolylinePrimitive> &polylines =
            document->GetPolylinePrimitives();

    QHash<ItemId, const StruPointPrimitive *> pointById;
    QHash<ItemId, const StruLinePrimitive *> lineById;
    QHash<ItemId, const StruArcPrimitive *> arcById;
    QHash<ItemId, const StruCirclePrimitive *> circleById;
    QHash<ItemId, const StruPolylinePrimitive *> polylineById;
    pointById.reserve(points.size());
    lineById.reserve(lines.size());
    arcById.reserve(arcs.size());
    circleById.reserve(circles.size());
    polylineById.reserve(polylines.size());
    for (const StruPointPrimitive &point : points) {
        pointById.insert(point.id, &point);
    }
    for (const StruLinePrimitive &line : lines) {
        lineById.insert(line.id, &line);
    }
    for (const StruArcPrimitive &arc : arcs) {
        arcById.insert(arc.id, &arc);
    }
    for (const StruCirclePrimitive &circle : circles) {
        circleById.insert(circle.id, &circle);
    }
    for (const StruPolylinePrimitive &polyline : polylines) {
        polylineById.insert(polyline.id, &polyline);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const ItemId id : document->GetItemIds()) {
        switch (document->GetItemType(id)) {
        case CanvasEntityType::Point: {
            const StruPointPrimitive *point = pointById.value(id, nullptr);
            if (!point || !point->style.visible) {
                break;
            }

            painter.setPen(Qt::NoPen);
            painter.setBrush(GetDisplayColor(point->style,
                                             document->IsItemSelected(point->id)));
            const QPointF viewportPosition = context.MapFromWorld(
                        point->geometry.position);
            const qreal radius = qMax<qreal>(1.0, point->style.pointSize) * 0.5;
            painter.drawEllipse(viewportPosition, radius, radius);
            break;
        }
        case CanvasEntityType::Line: {
            const StruLinePrimitive *line = lineById.value(id, nullptr);
            if (!line || !line->style.visible) {
                break;
            }

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(GetDisplayColor(
                                    line->style,
                                    document->IsItemSelected(line->id)),
                                qMax<qreal>(1.0, line->style.strokeWidth)));
            painter.drawLine(context.MapFromWorld(line->geometry.startPoint),
                             context.MapFromWorld(line->geometry.endPoint));
            break;
        }
        case CanvasEntityType::Arc: {
            const StruArcPrimitive *arc = arcById.value(id, nullptr);
            if (!arc || !arc->style.visible) {
                break;
            }

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(GetDisplayColor(
                                    arc->style,
                                    document->IsItemSelected(arc->id)),
                                qMax<qreal>(1.0, arc->style.strokeWidth)));
            painter.drawPolyline(MapPolyline(
                                     TessellateArc(arc->geometry,
                                                   maximumWorldError),
                                     context));
            break;
        }
        case CanvasEntityType::Circle: {
            const StruCirclePrimitive *circle = circleById.value(id, nullptr);
            if (!circle || !circle->style.visible) {
                break;
            }

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(GetDisplayColor(
                                    circle->style,
                                    document->IsItemSelected(circle->id)),
                                qMax<qreal>(1.0, circle->style.strokeWidth)));
            painter.drawPolyline(MapPolyline(
                                     TessellateCircle(circle->geometry,
                                                      maximumWorldError),
                                     context));
            break;
        }
        case CanvasEntityType::Polyline: {
            const StruPolylinePrimitive *polyline = polylineById.value(id, nullptr);
            if (!polyline || !polyline->style.visible) {
                break;
            }

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(GetDisplayColor(
                                    polyline->style,
                                    document->IsItemSelected(polyline->id)),
                                qMax<qreal>(1.0,
                                            polyline->style.strokeWidth)));
            const QPolygonF viewportPolyline = MapPolyline(
                        polyline->geometry.vertices,
                        context);
            if (polyline->geometry.closed) {
                painter.drawPolygon(viewportPolyline);
            } else {
                painter.drawPolyline(viewportPolyline);
            }
            break;
        }
        case CanvasEntityType::Invalid:
            break;
        }
    }

    painter.restore();
}

} // namespace mini_graph
