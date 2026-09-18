#pragma once

#include "CanvasTypes.h"
#include "CanvasTransform.h"
#include "MiniGraph_global.h"

#include <QPoint>
#include <QPointer>
#include <QRectF>
#include <QOpenGLWidget>
#include <QVector>

#include <memory>

class QPainter;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace mini_graph {

class CanvasDocument;
class CanvasViewPrivate;

// 基于 QOpenGLWidget 的二维高性能画布视图。
// CanvasView 负责画布背景、网格、坐标轴和图元的显示，并处理缩放、
// 平移以及基础鼠标键盘交互。图元数据由 CanvasDocument 提供，坐标换算
// 由 CanvasTransform 管理。
class MINIGRAPH_EXPORT CanvasView : public QOpenGLWidget
{
    Q_OBJECT

    // 是否显示背景网格。
    Q_PROPERTY(bool gridVisible READ IsGridVisible WRITE SetGridVisible NOTIFY GridVisibleChanged)

    // 网格的基础世界坐标间距。
    Q_PROPERTY(qreal gridSpacing READ GetGridSpacing WRITE SetGridSpacing NOTIFY GridSpacingChanged)

    // 当前画布缩放比例，只读。
    Q_PROPERTY(qreal zoom READ GetZoom NOTIFY ZoomChanged)

    // 是否显示 X/Y 坐标轴和原点标记。
    Q_PROPERTY(bool axisVisible READ IsAxisVisible WRITE SetAxisVisible NOTIFY AxisVisibleChanged)

    // 是否显示坐标标尺文字。
    Q_PROPERTY(bool rulerVisible READ IsRulerVisible WRITE SetRulerVisible NOTIFY RulerVisibleChanged)

    // 当前是否处于 Shift 正交交互状态，只读。
    Q_PROPERTY(bool orthogonalMode READ IsOrthogonalMode NOTIFY OrthogonalModeChanged)

public:
    // 创建画布视图，parent 为 Qt 对象父级，默认为 nullptr。
    explicit CanvasView(QWidget *parent = nullptr);

    // 释放画布视图及其 OpenGL 资源。
    ~CanvasView() override;

    // 返回当前关联的画布文档，未设置文档时返回 nullptr。
    CanvasDocument *GetDocument() const;

    // 设置画布文档，传入 nullptr 可解除当前关联。
    void SetDocument(CanvasDocument *document);

    // 返回网格是否可见。
    bool IsGridVisible() const;

    // 设置网格可见状态。
    void SetGridVisible(bool visible);

    // 返回网格基础世界坐标间距。
    qreal GetGridSpacing() const;

    // 设置网格基础世界坐标间距，spacing 必须大于 0。
    void SetGridSpacing(qreal spacing);

    // 返回当前画布缩放比例。
    qreal GetZoom() const;

    // 返回当前画布坐标变换对象的值拷贝。
    CanvasTransform GetTransform() const;

    // 将视口像素坐标转换为画布世界坐标。
    QPointF MapToWorld(const QPointF &viewportPosition) const;

    // 将画布世界坐标转换为视口像素坐标。
    QPointF MapFromWorld(const QPointF &worldPosition) const;

    // 返回坐标轴是否可见。
    bool IsAxisVisible() const;

    // 设置坐标轴的可见状态。
    void SetAxisVisible(bool visible);

    // 返回标尺文字是否可见。
    bool IsRulerVisible() const;

    // 设置标尺文字的可见状态。
    void SetRulerVisible(bool visible);

    // 返回当前是否处于正交交互状态。
    bool IsOrthogonalMode() const;

    // 将缩放比例和视图中心恢复为默认值。
    void ResetView();

signals:
    // 网格可见状态发生变化时发出。
    void GridVisibleChanged(bool visible);

    // 网格基础间距发生变化时发出。
    void GridSpacingChanged(qreal spacing);

    // 缩放比例发生变化时发出。
    void ZoomChanged(qreal zoom);

    // 坐标轴可见状态发生变化时发出。
    void AxisVisibleChanged(bool visible);

    // 标尺可见状态发生变化时发出。
    void RulerVisibleChanged(bool visible);

    // 正交交互状态发生变化时发出。
    void OrthogonalModeChanged(bool enabled);

    // 鼠标位置对应的世界坐标发生变化时发出。
    void MouseWorldPositionChanged(const QPointF &worldPosition);

    // 用户释放右键请求上下文操作时发出。
    // worldPosition 和 viewportPosition 分别为右键位置的世界坐标和视口坐标。
    void ContextMenuRequested(const QPointF &worldPosition,
                              const QPoint &viewportPosition);

    // 用户按下 Esc 或其他取消操作时发出。
    void InteractionCancelled();

protected:
    // 创建 OpenGL 着色器、缓冲区等绘制资源。
    void initializeGL() override;

    // 响应视口尺寸变化。
    void resizeGL(int width, int height) override;

    // 按背景、图元和 overlay 层顺序绘制画布。
    void paintGL() override;

    // 处理 Shift、Esc 等键盘交互。
    void keyPressEvent(QKeyEvent *event) override;

    // 处理 Shift 释放等键盘交互。
    void keyReleaseEvent(QKeyEvent *event) override;

    // 处理中键平移和右键上下文操作的开始。
    void mousePressEvent(QMouseEvent *event) override;

    // 更新鼠标世界坐标并执行中键拖拽。
    void mouseMoveEvent(QMouseEvent *event) override;

    // 处理中键平移和右键上下文操作的结束。
    void mouseReleaseEvent(QMouseEvent *event) override;

    // 处理滚轮缩放、滚动和触控板平移。
    void wheelEvent(QWheelEvent *event) override;

private slots:
    // 文档内容变化后的 OpenGL 缓冲区更新槽函数。
    void OnDocumentChanged();

    // 文档选择状态变化后的 OpenGL 缓冲区更新槽函数。
    void OnSelectionChanged();

    // 释放当前 OpenGL 上下文关联的资源。
    void CleanupGL();

private:
    // 中键平移的内部状态。
    enum class PanState {
        Idle,
        Pressed,
        Dragging
    };

    // 设置正交状态并在状态变化时发出信号。
    void SetOrthogonalModeInternal(bool enabled);

protected:
    // 绘制背景层。
    void DrawBackgroundLayer(QPainter &painter) const;

    // 绘制 CADLab 风格的自适应 meta 网格。
    void DrawGrid(QPainter &painter) const;

    // 绘制坐标轴和标尺等前景 overlay。
    void DrawOverlayLayer(QPainter &painter) const;

    // 绘制 X/Y 坐标轴、箭头和原点标记。
    void DrawAxes(QPainter &painter) const;

    // 绘制当前视口范围内的坐标标尺文字。
    void DrawRulerLabels(QPainter &painter) const;

    // 绘制当前正在进行的 CADLab 风格框选预览。
    void DrawSelectionBox(QPainter &painter) const;

private:
    // 框选手势的内部状态。
    enum class SelectionState {
        Idle,
        Pressed,
        Dragging
    };

    // 将文档线段数据上传到 OpenGL 顶点缓冲区。
    void UploadLineBuffer();

    // 使用当前变换绘制文档中的线段图元。
    void DrawLines();

    // 在指定视口位置查找距离最近的线段图元。
    ItemId HitTestItem(const QPointF &viewportPosition) const;

    // 将拖拽框对应的图元标识收集出来。
    QVector<ItemId> CollectWindowSelection(const QRectF &worldRect,
                                            bool crossing) const;

    // 应用单击选择结果。
    void ApplySingleSelection(ItemId id, bool append);

    // 应用框选结果。
    void ApplyWindowSelection(const QRectF &viewportRect, bool append);

    // 当前关联的画布文档，使用 QPointer 观察对象生命周期。
    QPointer<CanvasDocument> m_Document;

    // 文档 Changed 信号与当前视图之间的连接句柄。
    QMetaObject::Connection m_DocumentConnection;

    // 文档 SelectionChanged 信号与当前视图之间的连接句柄。
    QMetaObject::Connection m_DocumentSelectionConnection;

    // 当前画布的世界坐标变换状态。
    CanvasTransform m_Transform;

    // 网格可见状态。
    bool m_GridVisible;

    // 网格基础世界坐标间距。
    qreal m_GridSpacing;

    // 坐标轴可见状态。
    bool m_AxisVisible;

    // 标尺可见状态。
    bool m_RulerVisible;

    // Shift 正交交互状态。
    bool m_OrthogonalMode;

    // 当前中键平移状态。
    PanState m_PanState;

    // 当前一次中键平移的起始视口位置。
    QPointF m_PanStartPosition;

    // 上一次处理的鼠标视口位置。
    QPointF m_LastMousePosition;

    // 当前左键选择手势的状态。
    SelectionState m_SelectionState;

    // 当前左键选择手势的起始视口位置。
    QPointF m_SelectionStartPosition;

    // 当前左键选择手势的最新视口位置。
    QPointF m_SelectionCurrentPosition;

    // 当前左键选择手势开始时的键盘修饰键。
    Qt::KeyboardModifiers m_SelectionModifiers;

    // OpenGL 程序、缓冲区和函数指针等私有资源。
    std::unique_ptr<CanvasViewPrivate> m_PrivateData;
};

} // namespace mini_graph
