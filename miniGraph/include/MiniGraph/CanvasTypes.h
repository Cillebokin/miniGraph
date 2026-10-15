#pragma once

#include "MiniGraph_global.h"
#include "CanvasVector.h"

#include <cmath>

#include <QColor>
#include <QMetaType>
#include <QVector>
#include <QtGlobal>

namespace mini_graph {

// 画布图元的唯一标识。
using ItemId = quint64;

// 画布图元的几何类型。
enum class CanvasEntityType : quint8
{
    Invalid,
    Point,
    Line,
    Arc,
    Circle,
    Polyline,
    Ellipse,
    Spline,
    GroupContainer
};

// 画布图元线型，数值与 CADLab 的 RS2::LineType 对齐。
enum class CanvasLineType : quint8
{
    NoPen = 0,
    SolidLine = 1,
    DotLine = 2,
    DotLineTiny = 3,
    DotLine2 = 4,
    DotLineX2 = 5,
    DashLine = 6,
    DashLineTiny = 7,
    DashLine2 = 8,
    DashLineX2 = 9,
    DashDotLine = 10,
    DashDotLineTiny = 11,
    DashDotLine2 = 12,
    DashDotLineX2 = 13,
    DivideLine = 14,
    DivideLineTiny = 15,
    DivideLine2 = 16,
    DivideLineX2 = 17,
    CenterLine = 18,
    CenterLineTiny = 19,
    CenterLine2 = 20,
    CenterLineX2 = 21,
    BorderLine = 22,
    BorderLineTiny = 23,
    BorderLine2 = 24,
    BorderLineX2 = 25
};

// 画布图元线宽的单位。
enum class CanvasLineWidthUnit : quint8
{
    ViewportPixels,
    Millimeters
};

// 点图元的屏幕标记形状。
enum class CanvasPointShape : quint8
{
    FilledCircle,
    Cross
};

// 画布中的点几何数据。
struct StruPointGeometry
{
    // 创建位于世界坐标原点的点几何数据。
    StruPointGeometry()
        : position(0.0, 0.0)
    {
    }

    // 点的世界坐标。
    CanvasVector position;
};

// 画布中的线段几何数据。
struct StruLineGeometry
{
    // 创建起点和终点均位于世界坐标原点的线段几何数据。
    StruLineGeometry()
        : startPoint(0.0, 0.0)
        , endPoint(0.0, 0.0)
    {
    }

    // 线段起点的世界坐标。
    CanvasVector startPoint;

    // 线段终点的世界坐标。
    CanvasVector endPoint;
};

// 画布中的圆弧几何数据。
struct StruArcGeometry
{
    // 创建位于原点、半径为 0、角度为 0 的圆弧几何数据。
    StruArcGeometry()
        : center(0.0, 0.0)
        , radius(0.0)
        , startAngle(0.0)
        , endAngle(0.0)
        , reversed(false)
    {
    }

    // 圆弧圆心的世界坐标。
    CanvasVector center;

    // 圆弧半径，必须大于 0 才能加入文档。
    qreal radius;

    // 圆弧起始角，单位为弧度。
    qreal startAngle;

    // 圆弧终止角，单位为弧度。
    qreal endAngle;

    // 是否沿顺时针方向扫掠，false 表示逆时针方向。
    bool reversed;
};

// 画布中的圆几何数据。
struct StruCircleGeometry
{
    // 创建位于原点、半径为 0 的圆几何数据。
    StruCircleGeometry()
        : center(0.0, 0.0)
        , radius(0.0)
    {
    }

    // 圆心的世界坐标。
    CanvasVector center;

    // 圆半径，必须大于 0 才能加入文档。
    qreal radius;
};

// 画布中的多段线几何数据。
struct StruPolylineGeometry
{
    // 创建一个开放且不含顶点的多段线几何数据。
    StruPolylineGeometry()
        : closed(false)
    {
    }

    // 按绘制顺序排列的世界坐标顶点。
    QVector<CanvasVector> vertices;

    // 是否闭合；闭合时自动连接末点与首点。
    bool closed;
};

// 画布图元的统一显示样式。
struct StruCanvasStyle
{
    // 创建默认的画布图元显示样式。
    StruCanvasStyle()
        : strokeColor(Qt::white)
        , selectedStrokeColor(165, 71, 71)
        , strokeWidth(1.0)
        , lineType(CanvasLineType::SolidLine)
        , strokeWidthUnit(CanvasLineWidthUnit::ViewportPixels)
        , pointShape(CanvasPointShape::FilledCircle)
        , pointSize(6.0)
        , visible(true)
    {
    }

    // 图元未选中时使用的实际颜色；图层继承等解析由调用方完成。
    QColor strokeColor;

    // 图元选中时使用的实际颜色；图层继承等解析由调用方完成。
    QColor selectedStrokeColor;

    // 图元线宽；单位由 strokeWidthUnit 指定，毫米宽度按视图逻辑 DPI 映射。
    qreal strokeWidth;

    // 图元线型。
    CanvasLineType lineType;

    // 图元线宽单位；默认视口像素，Millimeters 按屏幕逻辑 DPI 映射。
    CanvasLineWidthUnit strokeWidthUnit;

    // 点图元的屏幕标记形状；默认保持填充圆点。
    CanvasPointShape pointShape;

    // 点图元的显示尺寸，单位为视口像素。
    qreal pointSize;

    // 是否显示图元。
    bool visible;

    // 检查颜色和尺寸是否满足画布文档的样式约束。
    bool IsValid() const
    {
        return strokeColor.isValid()
                && selectedStrokeColor.isValid()
                && std::isfinite(static_cast<double>(strokeWidth))
                && strokeWidth >= 0.0
                && static_cast<quint8>(lineType)
                   <= static_cast<quint8>(CanvasLineType::BorderLineX2)
                && (strokeWidthUnit == CanvasLineWidthUnit::ViewportPixels
                    || strokeWidthUnit == CanvasLineWidthUnit::Millimeters)
                && (pointShape == CanvasPointShape::FilledCircle
                    || pointShape == CanvasPointShape::Cross)
                && std::isfinite(static_cast<double>(pointSize))
                && pointSize > 0.0;
    }

    // 比较两个显示样式是否完全相同。
    bool operator==(const StruCanvasStyle &other) const
    {
        return strokeColor == other.strokeColor
                && selectedStrokeColor == other.selectedStrokeColor
                && strokeWidth == other.strokeWidth
                && lineType == other.lineType
                && strokeWidthUnit == other.strokeWidthUnit
                && pointShape == other.pointShape
                && pointSize == other.pointSize
                && visible == other.visible;
    }

    // 比较两个显示样式是否存在差异。
    bool operator!=(const StruCanvasStyle &other) const
    {
        return !(*this == other);
    }
};

// 画布中的点图元存储数据。
struct StruPointPrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 点的几何数据。
    StruPointGeometry geometry;

    // 点图元的显示样式。
    StruCanvasStyle style;
};

// 画布中的线段图元存储数据。
// 坐标使用画布世界坐标系表示；样式以 style 为准。
struct StruLinePrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 线段几何数据。
    StruLineGeometry geometry;

    // 线段的显示样式。
    StruCanvasStyle style;

    // 兼容旧接口的线段颜色镜像值。
    // 新代码应通过 style.strokeColor 访问和修改显示样式。
    QColor color = QColor(Qt::white);

    // 兼容旧接口的线段宽度数值镜像；单位跟随 style.strokeWidthUnit。
    // 新代码应通过 style.strokeWidth 和 style.strokeWidthUnit 访问样式。
    qreal width = 1.0;
};

// 画布中的圆弧图元存储数据。
struct StruArcPrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 圆弧的几何数据。
    StruArcGeometry geometry;

    // 圆弧图元的显示样式。
    StruCanvasStyle style;
};

// 画布中的圆图元存储数据。
struct StruCirclePrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 圆的几何数据。
    StruCircleGeometry geometry;

    // 圆图元的显示样式。
    StruCanvasStyle style;
};

// 画布中的多段线图元存储数据。
struct StruPolylinePrimitive
{
    // 图元唯一标识，由 CanvasDocument 分配。
    ItemId id = 0;

    // 多段线几何数据。
    StruPolylineGeometry geometry;

    // 多段线图元的显示样式。
    StruCanvasStyle style;
};

} // namespace mini_graph

Q_DECLARE_METATYPE(mini_graph::CanvasEntityType)
Q_DECLARE_METATYPE(mini_graph::CanvasLineType)
Q_DECLARE_METATYPE(mini_graph::CanvasLineWidthUnit)
Q_DECLARE_METATYPE(mini_graph::StruPointGeometry)
Q_DECLARE_METATYPE(mini_graph::StruLineGeometry)
Q_DECLARE_METATYPE(mini_graph::StruArcGeometry)
Q_DECLARE_METATYPE(mini_graph::StruCircleGeometry)
Q_DECLARE_METATYPE(mini_graph::StruPolylineGeometry)
Q_DECLARE_METATYPE(mini_graph::StruCanvasStyle)
Q_DECLARE_METATYPE(mini_graph::StruPointPrimitive)
Q_DECLARE_METATYPE(mini_graph::StruLinePrimitive)
Q_DECLARE_METATYPE(mini_graph::StruArcPrimitive)
Q_DECLARE_METATYPE(mini_graph::StruCirclePrimitive)
Q_DECLARE_METATYPE(mini_graph::StruPolylinePrimitive)
