#pragma once

#include "MiniGraph_global.h"

#include <QColor>
#include <QMetaType>
#include <QPointF>
#include <QtGlobal>

namespace mini_graph {

using ItemId = quint64;

struct StruLinePrimitive
{
    ItemId id = 0;
    QPointF start;
    QPointF end;
    QColor color = QColor(Qt::white);
    qreal width = 1.0;
};

} // namespace mini_graph

Q_DECLARE_METATYPE(mini_graph::StruLinePrimitive)
