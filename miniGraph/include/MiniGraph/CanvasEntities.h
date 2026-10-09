#pragma once

#include "CanvasTypes.h"

#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <memory>

namespace mini_graph {

class CanvasData;

// 所有画布图元的公共数据和多态接口。
class MINIGRAPH_EXPORT CanvasEntity
{
public:
    virtual ~CanvasEntity();

    // 实体通过 Clone() 复制，禁止普通复制以避免复制登记 ID 或共享容器子项。
    CanvasEntity(const CanvasEntity &) = delete;
    CanvasEntity &operator=(const CanvasEntity &) = delete;

    // 返回由画布文档分配的图元标识，未登记时为 0。
    ItemId GetId() const;

    // 返回图元的只读显示样式。
    const StruCanvasStyle &GetStyle() const;

    // 设置显示样式；线宽和点尺寸必须是有限的非负数。
    bool SetStyle(const StruCanvasStyle &style);

    // 返回图元附加数据；未设置时为空。
    const std::shared_ptr<CanvasData> &GetCanvasData() const;

    // 设置图元附加数据；如果数据已绑定到其他图元，本操作会被忽略。
    void SetCanvasData(const std::shared_ptr<CanvasData> &data);

    // 尝试设置图元附加数据；已绑定到其他图元时返回 false 且不改变当前状态。
    // 替换或移除时会先以 nullptr 解绑旧数据；传入对象由调用方和图元共享持有。
    bool TrySetCanvasData(const std::shared_ptr<CanvasData> &data);

    // 返回图元所属图层名称。
    QString GetLayerName() const;

    // 设置图元所属图层名称。
    void SetLayerName(const QString &name);

    // 返回调用方自定义标签。
    QString GetTag() const;

    // 设置调用方自定义标签。
    void SetTag(const QString &tag);

    // 返回调用方关联的业务编号。
    qint64 GetBusinessId() const;

    // 设置调用方关联的业务编号。
    void SetBusinessId(qint64 id);

    // 返回图元的方向反转状态。
    bool IsReversed() const;

    // 设置图元的方向反转状态。
    void SetReversed(bool reversed);

    // 返回图元几何类型。
    virtual CanvasEntityType GetType() const = 0;

    // 验证图元存储数据；后续派生边界或屏幕坐标仍须由相应算法检查范围。
    virtual bool IsValid() const = 0;

    // 创建不共享可变状态且标识为 0 的图元副本。
    virtual QSharedPointer<CanvasEntity> Clone() const = 0;

protected:
    CanvasEntity();

    // 将公共属性复制到目标对象，但不复制图元标识。
    bool CopyCommonPropertiesTo(CanvasEntity *target) const;

private:
    friend class CanvasDocument;
    void SetId(ItemId id);

    ItemId m_Id;
    StruCanvasStyle m_Style;
    QString m_LayerName;
    QString m_Tag;
    qint64 m_BusinessId;
    bool m_Reversed;
    std::shared_ptr<CanvasData> m_CanvasData;
};

// 以世界坐标保存的点图元。
class MINIGRAPH_EXPORT CanvasPoint final : public CanvasEntity
{
public:
    explicit CanvasPoint(const CanvasVector &position = CanvasVector());

    // 返回点的世界坐标。
    CanvasVector GetPosition() const;

    // 设置点坐标；无效坐标会被拒绝且不改变原值。
    bool SetPosition(const CanvasVector &position);

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    CanvasVector m_Position;
};

// 以两个端点保存的直线段图元。
class MINIGRAPH_EXPORT CanvasLine final : public CanvasEntity
{
public:
    CanvasLine(const CanvasVector &startPoint = CanvasVector(),
               const CanvasVector &endPoint = CanvasVector());

    // 返回直线段起点。
    CanvasVector GetStartPoint() const;

    // 返回直线段终点。
    CanvasVector GetEndPoint() const;

    // 同时设置两个端点；任一端点无效时会保留原几何数据。
    bool SetGeometry(const CanvasVector &startPoint,
                     const CanvasVector &endPoint);

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    CanvasVector m_StartPoint;
    CanvasVector m_EndPoint;
};

// 以圆心、半径和弧度角范围保存的圆弧图元。
class MINIGRAPH_EXPORT CanvasArc final : public CanvasEntity
{
public:
    CanvasArc(const CanvasVector &center = CanvasVector(), qreal radius = 0.0,
              qreal startAngle = 0.0, qreal endAngle = 0.0,
              bool reversed = false);

    // 返回圆弧圆心。
    CanvasVector GetCenter() const;

    // 返回圆弧半径。
    qreal GetRadius() const;

    // 返回圆弧起始角，单位为弧度。
    qreal GetStartAngle() const;

    // 返回圆弧终止角，单位为弧度。
    qreal GetEndAngle() const;

    // 设置圆弧几何数据；角差对 2π 取余后绝对值不大于 1e-12 弧度时拒绝。
    bool SetGeometry(const CanvasVector &center, qreal radius,
                     qreal startAngle, qreal endAngle);

    // 根据圆心、半径和起始角计算圆弧起点。
    CanvasVector GetStartPoint() const;

    // 根据圆心、半径和终止角计算圆弧终点。
    CanvasVector GetEndPoint() const;

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    CanvasVector m_Center;
    qreal m_Radius;
    qreal m_StartAngle;
    qreal m_EndAngle;
};

// 以圆心和半径保存的圆图元。
class MINIGRAPH_EXPORT CanvasCircle final : public CanvasEntity
{
public:
    CanvasCircle(const CanvasVector &center = CanvasVector(),
                 qreal radius = 0.0);

    // 返回圆心。
    CanvasVector GetCenter() const;

    // 返回圆半径。
    qreal GetRadius() const;

    // 设置圆几何数据；圆心必须有效且半径必须大于 0。
    bool SetGeometry(const CanvasVector &center, qreal radius);

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    CanvasVector m_Center;
    qreal m_Radius;
};

// 以圆心、长轴向量和短长轴比例保存的椭圆或椭圆弧图元。
class MINIGRAPH_EXPORT CanvasEllipse final : public CanvasEntity
{
public:
    CanvasEllipse();

    // 返回椭圆圆心。
    CanvasVector GetCenter() const;

    // 返回从圆心指向长轴端点的向量。
    CanvasVector GetMajorAxis() const;

    // 返回短半轴与长半轴的长度比例。
    qreal GetMinorMajorRatio() const;

    // 返回当前几何是否为椭圆弧。
    bool IsArc() const;

    // 返回椭圆弧起始局部参数角，单位为弧度。
    qreal GetStartParameter() const;

    // 返回椭圆弧终止局部参数角，单位为弧度。
    qreal GetEndParameter() const;

    // 设置完整椭圆或椭圆弧；完整椭圆的角度参数会归零。
    bool SetGeometry(const CanvasVector &center,
                     const CanvasVector &majorAxis,
                     qreal minorMajorRatio, bool isArc,
                     qreal startParameter = 0.0,
                     qreal endParameter = 0.0);

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    CanvasVector m_Center;
    CanvasVector m_MajorAxis;
    qreal m_MinorMajorRatio;
    bool m_IsArc;
    qreal m_StartParameter;
    qreal m_EndParameter;
};

// 由首尾相接的直线段和圆弧段组成的复合图元。
// 父对象统一控制子段线型样式；子段的 visible 独立控制该段的绘制、命中、框选和捕捉。
class MINIGRAPH_EXPORT CanvasPolyline final : public CanvasEntity
{
public:
    CanvasPolyline();

    // 返回多段线包含的线段和圆弧段数量。
    int GetSegmentCount() const;

    // 返回指定段的只读借用指针；索引无效时返回空指针。
    const CanvasEntity *GetSegment(int index) const;

    // 深拷贝并追加一条线段；不连续或无效几何会被拒绝。
    bool AddSegment(const CanvasLine &line);

    // 深拷贝并追加一段圆弧；不连续或无效几何会被拒绝。
    bool AddSegment(const CanvasArc &arc);

    // 深拷贝并替换指定线段；非法索引或破坏连接时会被拒绝。
    bool SetSegment(int index, const CanvasLine &line);

    // 深拷贝并替换指定圆弧；非法索引或破坏连接时会被拒绝。
    bool SetSegment(int index, const CanvasArc &arc);

    // 删除指定段；删除后无效或不连续时会被拒绝。
    bool RemoveSegment(int index);

    // 返回多段线是否闭合。
    bool IsClosed() const;

    // 设置闭合状态；闭合拓扑无效时会被拒绝。
    bool SetClosed(bool closed);

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    bool AddSegmentClone(const CanvasEntity &entity);
    bool SetSegmentClone(int index, const CanvasEntity &entity);

    QVector<QSharedPointer<CanvasEntity>> m_Segments;
    bool m_Closed;
};

// 以控制点、节点向量和可选权重描述的非均匀样条图元。
class MINIGRAPH_EXPORT CanvasSpline final : public CanvasEntity
{
public:
    CanvasSpline();

    // 返回样条阶数。
    int GetDegree() const;

    // 返回样条控制点。
    const QVector<CanvasVector> &GetControlPoints() const;

    // 返回样条节点向量。
    const QVector<qreal> &GetKnots() const;

    // 返回可选权重；为空时表示所有权重均为 1。
    const QVector<qreal> &GetWeights() const;

    // 返回调用方声明的闭合标志；该值不从控制点几何关系推导。
    bool IsClosed() const;

    // 返回调用方声明的周期标志；该值不从控制点几何关系推导。
    bool IsPeriodic() const;

    // 一次设置样条数据；拓扑标志独立保存但不做曲线拓扑验证，失败时保留原数据。
    bool SetData(int degree, const QVector<CanvasVector> &controlPoints,
                 const QVector<qreal> &knots, const QVector<qreal> &weights,
                 bool closed, bool periodic);

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    int m_Degree;
    QVector<CanvasVector> m_ControlPoints;
    QVector<qreal> m_Knots;
    QVector<qreal> m_Weights;
    bool m_Closed;
    bool m_Periodic;
};

// 可嵌套并深拷贝子图元的有序组容器。
class MINIGRAPH_EXPORT CanvasGroupContainer final : public CanvasEntity
{
public:
    CanvasGroupContainer();

    // 返回组内子图元数量。
    int GetChildCount() const;

    // 返回指定子图元的只读借用指针；索引无效时返回空指针。
    const CanvasEntity *GetChild(int index) const;

    // 深拷贝并追加有效子图元；不允许直接加入自身。
    bool AddChild(const CanvasEntity &entity);

    // 深拷贝并替换指定子图元；非法索引或无效对象会被拒绝。
    bool ReplaceChild(int index, const CanvasEntity &entity);

    // 删除指定子图元；非法索引会被拒绝。
    bool RemoveChild(int index);

    // 返回组是否允许打散。
    bool CanExplode() const;

    // 设置组是否允许打散。
    void SetAllowExplode(bool allow);

    CanvasEntityType GetType() const override;
    bool IsValid() const override;
    QSharedPointer<CanvasEntity> Clone() const override;

private:
    QVector<QSharedPointer<CanvasEntity>> m_Children;
    bool m_AllowExplode;
};

} // namespace mini_graph
