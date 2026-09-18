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
            const bool selectionChanged = m_SelectedIds.contains(id);
            m_SelectedIds.remove(id);
            if (selectionChanged) {
                emit SelectionChanged();
            }
            emit Changed();
            return true;
        }
    }

    return false;
}

void CanvasDocument::Clear()
{
    const bool contentChanged = !m_Lines.isEmpty();
    const bool selectionChanged = !m_SelectedIds.isEmpty();
    if (!contentChanged && !selectionChanged) {
        return;
    }

    m_Lines.clear();
    m_SelectedIds.clear();
    if (selectionChanged) {
        emit SelectionChanged();
    }
    if (contentChanged) {
        emit Changed();
    }
}

int CanvasDocument::GetItemCount() const
{
    return m_Lines.size();
}

const QVector<StruLinePrimitive> &CanvasDocument::GetLinePrimitives() const
{
    return m_Lines;
}

int CanvasDocument::GetSelectedItemCount() const
{
    return m_SelectedIds.size();
}

QVector<ItemId> CanvasDocument::GetSelectedItemIds() const
{
    QVector<ItemId> selectedIds;
    selectedIds.reserve(m_SelectedIds.size());
    for (const StruLinePrimitive &line : m_Lines) {
        if (m_SelectedIds.contains(line.id)) {
            selectedIds.append(line.id);
        }
    }
    return selectedIds;
}

bool CanvasDocument::IsItemSelected(ItemId id) const
{
    return m_SelectedIds.contains(id);
}

void CanvasDocument::SelectItem(ItemId id)
{
    for (const StruLinePrimitive &line : m_Lines) {
        if (line.id == id && !m_SelectedIds.contains(id)) {
            m_SelectedIds.insert(id);
            emit SelectionChanged();
            return;
        }
    }
}

void CanvasDocument::ToggleItemSelection(ItemId id)
{
    for (const StruLinePrimitive &line : m_Lines) {
        if (line.id != id) {
            continue;
        }

        if (m_SelectedIds.contains(id)) {
            m_SelectedIds.remove(id);
        } else {
            m_SelectedIds.insert(id);
        }
        emit SelectionChanged();
        return;
    }
}

void CanvasDocument::SelectItems(const QVector<ItemId> &ids,
                                 bool append,
                                 bool toggle)
{
    QSet<ItemId> existingIds;
    existingIds.reserve(m_Lines.size());
    for (const StruLinePrimitive &line : m_Lines) {
        existingIds.insert(line.id);
    }

    QSet<ItemId> validIds;
    for (const ItemId id : ids) {
        if (existingIds.contains(id)) {
            validIds.insert(id);
        }
    }

    QSet<ItemId> nextSelection = append ? m_SelectedIds : QSet<ItemId>();
    if (toggle) {
        for (const ItemId id : validIds) {
            if (nextSelection.contains(id)) {
                nextSelection.remove(id);
            } else {
                nextSelection.insert(id);
            }
        }
    } else {
        nextSelection.unite(validIds);
    }

    if (nextSelection == m_SelectedIds) {
        return;
    }

    m_SelectedIds = nextSelection;
    emit SelectionChanged();
}

void CanvasDocument::ToggleItems(const QVector<ItemId> &ids)
{
    SelectItems(ids, true, true);
}

void CanvasDocument::ClearSelection()
{
    if (m_SelectedIds.isEmpty()) {
        return;
    }

    m_SelectedIds.clear();
    emit SelectionChanged();
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
