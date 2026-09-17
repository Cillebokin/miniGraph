#pragma once

#include "CanvasTypes.h"

#include <QObject>
#include <QRectF>
#include <QVector>

namespace mini_graph {

class MINIGRAPH_EXPORT CanvasDocument : public QObject
{
    Q_OBJECT

public:
    explicit CanvasDocument(QObject *parent = nullptr);

    ItemId AddLine(const QPointF &start,
                   const QPointF &end,
                   const QColor &color = QColor(Qt::white),
                   qreal width = 1.0);
    bool RemoveItem(ItemId id);
    void Clear();

    int GetItemCount() const;
    const QVector<StruLinePrimitive> &GetLinePrimitives() const;
    QRectF GetBounds() const;

signals:
    void Changed();

private:
    QVector<StruLinePrimitive> m_Lines;
    ItemId m_NextId;
};

} // namespace mini_graph
