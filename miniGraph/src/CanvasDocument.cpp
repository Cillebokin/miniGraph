#include "MiniGraph/CanvasDocument.h"

#include <QtGlobal>

namespace mini_graph {

CanvasDocument::CanvasDocument(QObject *parent)
    : QObject(parent)
    , m_NextId(1)
{
}

ItemId CanvasDocument::AddLine(const QPointF &start,
                               const QPointF &end,
                               const QColor &color,
                               qreal width)
{
    StruLinePrimitive line;
    line.id = m_NextId++;
    line.start = start;
    line.end = end;
    line.color = color;
    line.width = qMax<qreal>(0.0, width);
    m_Lines.append(line);
    emit Changed();
    return line.id;
}

bool CanvasDocument::RemoveItem(ItemId id)
{
    for (int lineIndex = 0; lineIndex < m_Lines.size(); ++lineIndex) {
        if (m_Lines.at(lineIndex).id == id) {
            m_Lines.remove(lineIndex);
            emit Changed();
            return true;
        }
    }

    return false;
}

void CanvasDocument::Clear()
{
    if (m_Lines.isEmpty()) {
        return;
    }

    m_Lines.clear();
    emit Changed();
}

int CanvasDocument::GetItemCount() const
{
    return m_Lines.size();
}

const QVector<StruLinePrimitive> &CanvasDocument::GetLinePrimitives() const
{
    return m_Lines;
}

QRectF CanvasDocument::GetBounds() const
{
    if (m_Lines.isEmpty()) {
        return QRectF();
    }

    QRectF result;
    bool hasBounds = false;
    for (const StruLinePrimitive &line : m_Lines) {
        const QRectF lineBounds(QPointF(qMin(line.start.x(), line.end.x()),
                                        qMin(line.start.y(), line.end.y())),
                                QPointF(qMax(line.start.x(), line.end.x()),
                                        qMax(line.start.y(), line.end.y())));
        if (!hasBounds) {
            result = lineBounds;
            hasBounds = true;
        } else {
            result = result.united(lineBounds);
        }
    }

    return result;
}

} // namespace mini_graph
