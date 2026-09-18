#pragma once

#include "CanvasTypes.h"

#include <QObject>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QVector>

namespace mini_graph {

// 画布图元文档。
// CanvasDocument 负责保存和管理画布中的基础图元，并在内容发生变化时
// 通过 Changed() 通知关联的视图或其他观察者。
class MINIGRAPH_EXPORT CanvasDocument : public QObject
{
    Q_OBJECT

public:
    // 构造一个空的画布文档。
    // parent 为 Qt 对象父级，默认为 nullptr。
    explicit CanvasDocument(QObject *parent = nullptr);

    // 向文档中添加一条线段。
    // start 和 end 为线段端点的世界坐标。
    // color 为线段颜色，width 为线段宽度，小于 0 的宽度会被限制为 0。
    // 返回新建图元的唯一标识。
    ItemId AddLine(const QPointF &start,
                   const QPointF &end,
                   const QColor &color = QColor(Qt::white),
                   qreal width = 1.0);

    // 按几何数据向文档中添加一条线段。
    // color 和 width 仅用于兼容当前线段绘制，width 小于 0 时会被限制为 0。
    ItemId AddLine(const StruLineGeometry &geometry,
                   const QColor &color = QColor(Qt::white),
                   qreal width = 1.0);

    // 向文档中添加一个点。
    // 返回新建图元的唯一标识。
    ItemId AddPoint(const StruPointGeometry &geometry);

    // 向文档中添加一段圆弧。
    // 半径必须大于 0，起止角不能相同；非法几何返回 0。
    ItemId AddArc(const StruArcGeometry &geometry);

    // 向文档中添加一个圆。
    // 半径必须大于 0；非法几何返回 0。
    ItemId AddCircle(const StruCircleGeometry &geometry);

    // 按图元标识删除图元，找到并删除时返回 true，否则返回 false。
    bool RemoveItem(ItemId id);

    // 清空文档中的全部图元。
    void Clear();

    // 返回当前文档中的图元数量。
    int GetItemCount() const;

    // 返回当前保存的线段图元列表。
    // 返回值是文档内部容器的只读引用，文档发生修改后引用内容可能变化。
    const QVector<StruLinePrimitive> &GetLinePrimitives() const;

    // 返回当前保存的点图元列表。
    // 返回值是文档内部容器的只读引用，文档发生修改后引用内容可能变化。
    const QVector<StruPointPrimitive> &GetPointPrimitives() const;

    // 返回当前保存的圆弧图元列表。
    // 返回值是文档内部容器的只读引用，文档发生修改后引用内容可能变化。
    const QVector<StruArcPrimitive> &GetArcPrimitives() const;

    // 返回当前保存的圆图元列表。
    // 返回值是文档内部容器的只读引用，文档发生修改后引用内容可能变化。
    const QVector<StruCirclePrimitive> &GetCirclePrimitives() const;

    // 返回全部图元标识，结果按照图元添加顺序排列。
    QVector<ItemId> GetItemIds() const;

    // 返回指定图元的几何类型，不存在时返回 Invalid。
    CanvasEntityType GetItemType(ItemId id) const;

    // 返回当前被选中的图元数量。
    int GetSelectedItemCount() const;

    // 返回当前被选中的图元标识，结果按照图元在文档中的顺序排列。
    QVector<ItemId> GetSelectedItemIds() const;

    // 返回指定图元当前是否处于选中状态。
    bool IsItemSelected(ItemId id) const;

    // 选中指定图元，图元不存在或已经选中时不产生变化。
    void SelectItem(ItemId id);

    // 切换指定图元的选中状态。
    // 图元不存在时不产生变化。
    void ToggleItemSelection(ItemId id);

    // 根据图元标识替换当前选择集合。
    // ids 中不存在的图元标识会被忽略。
    void SelectItems(const QVector<ItemId> &ids,
                    bool append,
                    bool toggle);

    // 对 ids 中的图元切换选中状态。
    // 图元不存在时会被忽略。
    void ToggleItems(const QVector<ItemId> &ids);

    // 清空当前选择状态。
    void ClearSelection();

    // 计算所有图元的包围矩形。
    // 文档非空时返回包围矩形，文档为空时返回空矩形。
    QRectF GetBounds() const;

signals:
    // 文档中的图元集合或图元数据发生变化时发出。
    void Changed();

    // 文档中的图元选择状态发生变化时发出。
    void SelectionChanged();

private:
    // 当前文档保存的点图元。
    QVector<StruPointPrimitive> m_Points;

    // 当前文档保存的线段图元。
    QVector<StruLinePrimitive> m_Lines;

    // 当前文档保存的圆弧图元。
    QVector<StruArcPrimitive> m_Arcs;

    // 当前文档保存的圆图元。
    QVector<StruCirclePrimitive> m_Circles;

    // 当前文档中全部图元的添加顺序。
    QVector<ItemId> m_ItemOrder;

    // 图元标识到几何类型的索引。
    QHash<ItemId, CanvasEntityType> m_ItemTypes;

    // 下一次添加图元时使用的标识值。
    ItemId m_NextId;

    // 当前被选中的图元标识集合。
    QSet<ItemId> m_SelectedIds;
};

} // namespace mini_graph
