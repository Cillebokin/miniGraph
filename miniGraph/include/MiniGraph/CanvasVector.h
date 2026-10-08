#pragma once

#include "MiniGraph_global.h"

#include <QMetaType>
#include <QtGlobal>

namespace mini_graph {

// 表示画布世界坐标或二维位移向量的值类型。
class MINIGRAPH_EXPORT CanvasVector
{
public:
    // 创建一个坐标为零但无效的向量。
    CanvasVector();

    // 创建一个由有限 x、y 分量构成的有效向量。
    CanvasVector(qreal x, qreal y);

    // 返回 x 分量；无效向量返回其存储值 0。
    qreal GetX() const;

    // 返回 y 分量；无效向量返回其存储值 0。
    qreal GetY() const;

    // 返回向量当前是否有效。
    bool IsValid() const;

    // 使用有限分量更新向量；输入无效时保留原值并返回 false。
    bool Set(qreal x, qreal y);

    // 将向量置为无效状态并把分量清零。
    void Invalidate();

    // 按分量相加；任一操作数无效或结果非有限时返回无效向量。
    CanvasVector operator+(const CanvasVector &other) const;

    // 按分量相减；任一操作数无效或结果非有限时返回无效向量。
    CanvasVector operator-(const CanvasVector &other) const;

    // 返回方向相反的向量。
    CanvasVector operator-() const;

    // 按有限标量缩放向量。
    CanvasVector operator*(qreal scale) const;

    // 按非零有限标量缩放向量。
    CanvasVector operator/(qreal divisor) const;

    // 将另一个向量加到当前向量；运算失败时当前向量变为无效。
    CanvasVector &operator+=(const CanvasVector &other);

    // 从当前向量减去另一个向量；运算失败时当前向量变为无效。
    CanvasVector &operator-=(const CanvasVector &other);

    // 按有限标量缩放当前向量；运算失败时当前向量变为无效。
    CanvasVector &operator*=(qreal scale);

    // 按非零有限标量缩放当前向量；运算失败时当前向量变为无效。
    CanvasVector &operator/=(qreal divisor);

    // 比较有效状态及有效向量的精确分量。
    bool operator==(const CanvasVector &other) const;

    // 比较两个向量是否不相等。
    bool operator!=(const CanvasVector &other) const;

    // 返回向量长度；无效输入或非有限结果返回 qQNaN()。
    qreal Length() const;

    // 返回向量长度的平方；无效输入或非有限结果返回 qQNaN()。
    qreal SquaredLength() const;

    // 返回到目标坐标的距离；无效输入或非有限结果返回 qQNaN()。
    qreal DistanceTo(const CanvasVector &other) const;

    // 返回到目标坐标的距离平方；无效输入或非有限结果返回 qQNaN()。
    qreal SquaredDistanceTo(const CanvasVector &other) const;

    // 返回两个向量的点积；无效输入或非有限结果返回 qQNaN()。
    qreal Dot(const CanvasVector &other) const;

    // 返回二维叉积的标量 z 分量；无效输入或非有限结果返回 qQNaN()。
    qreal Cross(const CanvasVector &other) const;

    // 返回从正 x 轴到当前向量的角度，范围为 [0, 2π)。
    qreal Angle() const;

    // 返回从当前坐标指向目标坐标的角度，范围为 [0, 2π)。
    qreal AngleTo(const CanvasVector &other) const;

    // 返回绕原点旋转指定弧度后的新向量。
    CanvasVector Rotated(qreal radians) const;

    // 返回单位向量；无效向量或零向量返回无效向量。
    CanvasVector Normalized() const;

    // 使用非负有限容差判断两向量的距离是否在容差内。
    bool IsNearTo(const CanvasVector &other, qreal tolerance) const;

    // 按半径和弧度创建向量。
    static CanvasVector FromPolar(qreal radius, qreal radians);

    // 返回两个有效坐标的中点。
    static CanvasVector Midpoint(const CanvasVector &first,
                                 const CanvasVector &second);

private:
    qreal m_X;
    qreal m_Y;
    bool m_Valid;
};

} // namespace mini_graph

Q_DECLARE_METATYPE(mini_graph::CanvasVector)
