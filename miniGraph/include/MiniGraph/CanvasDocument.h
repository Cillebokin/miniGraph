#pragma once

#include "CanvasEntities.h"
#include "CanvasTypes.h"

#include <QObject>
#include <QHash>
#include <QSharedPointer>
#include <QRectF>
#include <QSet>
#include <QVector>

#include <memory>

namespace mini_graph {

class CanvasData;
class CanvasGlobalData;

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

    // 克隆并登记一个有效图元；文档会分配唯一 ID，失败时返回 0。
    ItemId AddEntity(const CanvasEntity &entity);

    // 返回文档所持图元的只读借用指针；该图元被替换、删除或文档清空后指针失效。
    const CanvasEntity *GetEntity(ItemId id) const;

    // 克隆并替换指定图元，保留其 ID、文档顺序和选择状态。
    // 替换图元允许使用不同类型；图元不存在或新图元无效时返回 false。
    bool ReplaceEntity(ItemId id, const CanvasEntity &entity);

    // 向文档中添加一条线段。
    // start 和 end 为线段端点的世界坐标。
    // color 为线段颜色，width 为线段宽度，小于 0 的宽度会被限制为 0。
    // 返回新建图元的唯一标识。
    ItemId AddLine(const CanvasVector &start,
                   const CanvasVector &end,
                   const QColor &color = QColor(Qt::white),
                   qreal width = 1.0);

    // 按几何数据向文档中添加一条线段。
    // color 和 width 仅用于兼容当前线段绘制，width 小于 0 时会被限制为 0。
    ItemId AddLine(const StruLineGeometry &geometry,
                   const QColor &color = QColor(Qt::white),
                   qreal width = 1.0);

    // 向文档中添加一个点。
    // 返回新建图元的唯一标识。
    ItemId AddPoint(const CanvasVector &position);

    // 按几何数据向文档中添加一个点。
    // 返回新建图元的唯一标识。
    ItemId AddPoint(const StruPointGeometry &geometry);

    // 向文档中添加一段圆弧。
    // 半径必须大于 0，起止角不能相同；非法几何返回 0。
    ItemId AddArc(const CanvasVector &center,
                  qreal radius,
                  qreal startAngle,
                  qreal endAngle,
                  bool reversed = false);

    // 按几何数据向文档中添加一段圆弧。
    // 半径必须大于 0，起止角不能相同；非法几何返回 0。
    ItemId AddArc(const StruArcGeometry &geometry);

    // 向文档中添加一个圆。
    // 半径必须大于 0；非法几何返回 0。
    ItemId AddCircle(const CanvasVector &center,
                     qreal radius);

    // 按几何数据向文档中添加一个圆。
    // 半径必须大于 0；非法几何返回 0。
    ItemId AddCircle(const StruCircleGeometry &geometry);

    // 按顶点列表向文档中添加一条多段线。
    // 开放多段线至少需要 2 个有限顶点，闭合多段线至少需要 3 个有限顶点。
    ItemId AddPolyline(const QVector<CanvasVector> &vertices,
                       bool closed = false);

    // 按几何数据向文档中添加一条多段线；非法几何返回 0。
    ItemId AddPolyline(const StruPolylineGeometry &geometry);

    // 按图元标识删除图元，找到并删除时返回 true，否则返回 false。
    bool RemoveItem(ItemId id);

    // 按世界坐标增量平移指定图元。
    // ids 中不存在的图元会被忽略；发生实际变化时发出 Changed()。
    bool TranslateItems(const QVector<ItemId> &ids,
                        const CanvasVector &delta);

    // 清空文档中的全部图元。
    void Clear();

    // 返回当前文档中的图元数量。
    int GetItemCount() const;

    // 返回旧式线段图元兼容视图；引用在文档内容修改后可能失效。
    const QVector<StruLinePrimitive> &GetLinePrimitives() const;

    // 返回旧式点图元兼容视图；引用在文档内容修改后可能失效。
    const QVector<StruPointPrimitive> &GetPointPrimitives() const;

    // 返回旧式圆弧图元兼容视图；引用在文档内容修改后可能失效。
    const QVector<StruArcPrimitive> &GetArcPrimitives() const;

    // 返回旧式圆图元兼容视图；引用在文档内容修改后可能失效。
    const QVector<StruCirclePrimitive> &GetCirclePrimitives() const;

    // 返回仅由直线段组成的旧式多段线兼容视图；引用在内容修改后可能失效。
    const QVector<StruPolylinePrimitive> &GetPolylinePrimitives() const;

    // 返回全部图元标识，结果按照图元添加顺序排列。
    QVector<ItemId> GetItemIds() const;

    // 返回指定图元的几何类型，不存在时返回 Invalid。
    CanvasEntityType GetItemType(ItemId id) const;

    // 返回指定图元的显示样式。
    // 图元不存在或 style 为空时返回 false。
    bool GetItemStyle(ItemId id, StruCanvasStyle *style) const;

    // 设置指定图元的显示样式。
    // 图元不存在或样式参数非法时返回 false；样式发生变化时发出 Changed()。
    bool SetItemStyle(ItemId id, const StruCanvasStyle &style);

    // 返回指定图元的附加数据；图元不存在或未设置数据时返回空指针。
    std::shared_ptr<CanvasData> GetItemCanvasData(ItemId id) const;

    // 设置指定图元的附加数据。
    // 图元不存在或数据已绑定到其他图元时返回 false，且不发出 Changed()；成功时发出 Changed()。
    bool SetItemCanvasData(ItemId id,
                           const std::shared_ptr<CanvasData> &data);

    // 调用方原地修改图元附加数据后，通知文档刷新关联视图。
    // 图元不存在或未设置 CanvasData 时返回 false。
    bool NotifyItemCanvasDataChanged(ItemId id);

    // 返回当前文档的全局附加数据；未设置时返回空指针。
    std::shared_ptr<CanvasGlobalData> GetCanvasGlobalData() const;

    // 设置当前文档的全局附加数据，并发出 Changed()。
    void SetCanvasGlobalData(const std::shared_ptr<CanvasGlobalData> &data);

    // 调用方原地修改全局附加数据后，通知文档刷新关联视图。
    // 当前未设置 CanvasGlobalData 时返回 false。
    bool NotifyCanvasGlobalDataChanged();

    // 返回指定点图元的几何数据，图元类型不匹配或 geometry 为空时返回 false。
    bool GetPointGeometry(ItemId id, StruPointGeometry *geometry) const;

    // 更新指定点图元的几何数据，数据发生变化时发出 Changed()。
    bool SetPointGeometry(ItemId id, const StruPointGeometry &geometry);

    // 返回指定线段图元的几何数据，图元类型不匹配或 geometry 为空时返回 false。
    bool GetLineGeometry(ItemId id, StruLineGeometry *geometry) const;

    // 更新指定线段图元的几何数据，数据发生变化时发出 Changed()。
    bool SetLineGeometry(ItemId id, const StruLineGeometry &geometry);

    // 返回指定圆弧图元的几何数据，图元类型不匹配或 geometry 为空时返回 false。
    bool GetArcGeometry(ItemId id, StruArcGeometry *geometry) const;

    // 更新指定圆弧图元的几何数据，非法数据或类型不匹配时返回 false。
    bool SetArcGeometry(ItemId id, const StruArcGeometry &geometry);

    // 返回指定圆图元的几何数据，图元类型不匹配或 geometry 为空时返回 false。
    bool GetCircleGeometry(ItemId id, StruCircleGeometry *geometry) const;

    // 更新指定圆图元的几何数据，非法数据或类型不匹配时返回 false。
    bool SetCircleGeometry(ItemId id, const StruCircleGeometry &geometry);

    // 返回指定多段线图元的几何数据，类型不匹配或 geometry 为空时返回 false。
    bool GetPolylineGeometry(ItemId id, StruPolylineGeometry *geometry) const;

    // 更新指定多段线图元的几何数据，非法数据或类型不匹配时返回 false。
    bool SetPolylineGeometry(ItemId id, const StruPolylineGeometry &geometry);

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

    // 计算指定图元集合的包围矩形。
    // 不存在的图元会被忽略，ids 为空时返回空矩形。
    QRectF GetBounds(const QVector<ItemId> &ids) const;

signals:
    // 图元集合或图元数据提交变化后发出；处理信号时可以读取到更新后的文档状态。
    // 依赖文档图元数据的缓存应在收到该信号时失效；该粗粒度信号不提供具体图元 ID。
    void Changed();

    // 文档中的图元选择状态发生变化时发出。
    void SelectionChanged();

private:
    // 查找指定图元的可写对象。
    CanvasEntity *FindEntity(ItemId id);

    // 查找指定图元的只读对象。
    const CanvasEntity *FindEntity(ItemId id) const;

    // 标记旧式图元兼容视图需要从实体数据重新生成。
    void InvalidatePrimitiveViews();

    // 按文档顺序重建旧式图元兼容视图。
    void EnsurePrimitiveViews() const;

    // 文档唯一持有的实体对象；所有旧式视图均由此数据派生。
    QHash<ItemId, QSharedPointer<CanvasEntity>> m_Entities;

    // 当前文档中全部图元的添加顺序。
    QVector<ItemId> m_ItemOrder;

    // 下一次添加图元时使用的标识值。
    ItemId m_NextId;

    // 当前文档持有的全局附加数据。
    std::shared_ptr<CanvasGlobalData> m_CanvasGlobalData;

    // 当前被选中的图元标识集合。
    QSet<ItemId> m_SelectedIds;

    // 为旧式公开接口缓存的兼容视图，不作为文档权威数据。
    mutable QVector<StruPointPrimitive> m_PointPrimitiveView;
    mutable QVector<StruLinePrimitive> m_LinePrimitiveView;
    mutable QVector<StruArcPrimitive> m_ArcPrimitiveView;
    mutable QVector<StruCirclePrimitive> m_CirclePrimitiveView;
    mutable QVector<StruPolylinePrimitive> m_PolylinePrimitiveView;
    mutable bool m_PrimitiveViewsDirty;
};

} // namespace mini_graph
