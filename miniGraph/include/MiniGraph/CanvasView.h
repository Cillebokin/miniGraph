#pragma once

#include "CanvasTypes.h"
#include "CanvasSnap.h"
#include "CanvasTransform.h"
#include "MiniGraph_global.h"

#include <QPoint>
#include <QPointer>
#include <QRectF>
#include <QWidget>
#include <QVector>

#include <memory>

class QPainter;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace mini_graph {

class CanvasDocument;
class CanvasRenderer;
class CanvasViewPrivate;

// 基于 QWidget 和 QPainter 的二维画布视图。
// CanvasView 负责画布背景、网格、坐标轴和图元的显示，并处理缩放、
// 平移以及基础鼠标键盘交互。图元数据由 CanvasDocument 提供，坐标换算
// 由 CanvasTransform 管理。
class MINIGRAPH_EXPORT CanvasView : public QWidget
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

    // 是否启用当前鼠标位置的捕捉预览。
    Q_PROPERTY(bool snapEnabled READ IsSnapEnabled WRITE SetSnapEnabled NOTIFY SnapEnabledChanged)

    // 是否启用网格捕捉。
    Q_PROPERTY(bool gridSnapEnabled READ IsGridSnapEnabled WRITE SetGridSnapEnabled NOTIFY GridSnapEnabledChanged)

    // 是否启用图元端点和圆心捕捉。
    Q_PROPERTY(bool geometrySnapEnabled READ IsGeometrySnapEnabled WRITE SetGeometrySnapEnabled NOTIFY GeometrySnapEnabledChanged)

public:
    // 创建画布视图，parent 为 Qt 对象父级，默认为 nullptr。
    explicit CanvasView(QWidget *parent = nullptr);

    // 释放画布视图。
    ~CanvasView() override;

    // 返回当前关联的画布文档，未设置文档时返回 nullptr。
    CanvasDocument *GetDocument() const;

    // 设置画布文档，传入 nullptr 可解除当前关联。
    void SetDocument(CanvasDocument *document);

    // 返回当前附加的自定义渲染器，未设置时返回 nullptr。
    CanvasRenderer *GetCustomRenderer() const;

    // 设置附加的自定义渲染器，传入 nullptr 可恢复为仅使用默认渲染器。
    // CanvasView 不接管 renderer 的所有权，renderer 必须在使用期间保持有效。
    void SetCustomRenderer(CanvasRenderer *renderer);

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

    // 返回是否启用当前鼠标位置的捕捉预览。
    bool IsSnapEnabled() const;

    // 设置当前鼠标位置的捕捉预览是否启用。
    void SetSnapEnabled(bool enabled);

    // 返回是否启用网格捕捉。
    bool IsGridSnapEnabled() const;

    // 设置网格捕捉是否启用。
    void SetGridSnapEnabled(bool enabled);

    // 返回是否启用图元端点和圆心捕捉。
    bool IsGeometrySnapEnabled() const;

    // 设置图元端点和圆心捕捉是否启用。
    void SetGeometrySnapEnabled(bool enabled);

    // 返回当前鼠标位置的捕捉结果，未捕捉时 type 为 None。
    CanvasSnapResult GetCurrentSnapResult() const;

    // 将缩放比例和视图中心恢复为默认值。
    void ResetView();

    // 将全部图元适配到当前视口，成功时返回 true。
    bool FitAll(qreal marginPixels = 40.0);

    // 将当前选中的图元适配到当前视口，成功时返回 true。
    bool FitSelection(qreal marginPixels = 40.0);

    // 将视图中心移动到指定世界坐标。
    void CenterOnWorld(const QPointF &worldPosition);

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

    // 捕捉预览开关发生变化时发出。
    void SnapEnabledChanged(bool enabled);

    // 网格捕捉开关发生变化时发出。
    void GridSnapEnabledChanged(bool enabled);

    // 几何捕捉开关发生变化时发出。
    void GeometrySnapEnabledChanged(bool enabled);

    // 当前鼠标位置的捕捉结果发生变化时发出。
    void SnapChanged(const CanvasSnapResult &result);

    // 鼠标位置对应的世界坐标发生变化时发出。
    void MouseWorldPositionChanged(const QPointF &worldPosition);

    // 用户释放右键请求上下文操作时发出。
    // worldPosition 和 viewportPosition 分别为右键位置的世界坐标和视口坐标。
    void ContextMenuRequested(const QPointF &worldPosition,
                              const QPoint &viewportPosition);

    // 用户按下 Esc 或其他取消操作时发出。
    void InteractionCancelled();

protected:
    // 响应视口尺寸变化。
    void resizeEvent(QResizeEvent *event) override;

    // 按背景、图元和 overlay 层顺序绘制画布。
    void paintEvent(QPaintEvent *event) override;

    // 处理 Shift、Esc、Delete 和 Backspace 等键盘交互。
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
    // 文档内容变化后的刷新槽函数。
    void OnDocumentChanged();

    // 文档选择状态变化后的刷新槽函数。
    void OnSelectionChanged();

private:
    // 中键平移的内部状态。
    enum class PanState {
        Idle,
        Pressed,
        Dragging
    };

    // 设置正交状态并在状态变化时发出信号。
    void SetOrthogonalModeInternal(bool enabled);

    // 将给定世界坐标矩形适配到当前视口。
    bool FitWorldRect(const QRectF &worldRect, qreal marginPixels);

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

    // 绘制当前单选图元的几何编辑控制柄。
    void DrawEditHandles(QPainter &painter) const;

    // 绘制当前鼠标位置的捕捉标记。
    void DrawSnapMarker(QPainter &painter) const;

private:
    // 左键选择和图元移动手势的内部状态。
    enum class SelectionState {
        Idle,
        Pressed,
        Dragging,
        Moving,
        Editing
    };

    // 当前正在交互的几何编辑控制柄类型。
    enum class EditHandleType {
        None,
        PointCenter,
        LineStart,
        LineEnd,
        ArcCenter,
        ArcStart,
        ArcEnd,
        CircleCenter,
        CircleRadius,
        PolylineVertex
    };

    // 一个控制柄及其世界坐标位置。
    struct EditHandlePosition {
        EditHandleType type = EditHandleType::None;
        QPointF worldPosition;
        int vertexIndex = -1;
    };

    // 在指定视口位置查找距离最近的图元。
    ItemId HitTestItem(const QPointF &viewportPosition) const;

    // 将拖拽框对应的图元标识收集出来。
    QVector<ItemId> CollectWindowSelection(const QRectF &worldRect,
                                            bool crossing) const;

    // 应用单击选择结果。
    void ApplySingleSelection(ItemId id, bool append);

    // 应用框选结果。
    void ApplyWindowSelection(const QRectF &viewportRect, bool append);

    // 查找当前单选图元上距离最近的控制柄。
    EditHandleType HitTestEditHandle(const QPointF &viewportPosition,
                                     int *vertexIndex = nullptr) const;

    // 返回指定图元的控制柄世界坐标。
    QVector<EditHandlePosition> GetEditHandlePositions(ItemId id) const;

    // 开始一次控制柄编辑并保存原始几何数据。
    bool BeginEdit(ItemId id,
                   EditHandleType handleType,
                   int vertexIndex = -1);

    // 根据鼠标世界坐标更新当前控制柄编辑。
    bool UpdateEdit(const QPointF &worldPosition);

    // 清除控制柄编辑状态。
    void ResetEditState();

    // 取消当前控制柄编辑并恢复原始几何数据。
    void CancelEdit();

    // 根据当前鼠标世界坐标更新捕捉结果。
    void UpdateSnap(const QPointF &worldPosition);

    // 清除当前移动手势的临时状态。
    void ResetMoveState();

    // 回滚当前尚未结束的移动手势。
    void CancelMove();

    // 当前关联的画布文档，使用 QPointer 观察对象生命周期。
    QPointer<CanvasDocument> m_Document;

    // 文档 Changed 信号与当前视图之间的连接句柄。
    QMetaObject::Connection m_DocumentConnection;

    // 文档 SelectionChanged 信号与当前视图之间的连接句柄。
    QMetaObject::Connection m_DocumentSelectionConnection;

    // 当前附加的自定义渲染器，使用 QPointer 观察对象生命周期。
    QPointer<CanvasRenderer> m_CustomRenderer;

    // 自定义渲染器销毁信号与当前视图之间的连接句柄。
    QMetaObject::Connection m_CustomRendererDestroyedConnection;

    // 自定义渲染器刷新信号与当前视图之间的连接句柄。
    QMetaObject::Connection m_CustomRendererUpdateConnection;

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

    // 当前左键按下位置命中的图元标识。
    ItemId m_PressedItemId;

    // 当前移动手势涉及的图元标识。
    QVector<ItemId> m_MoveItemIds;

    // 当前移动手势上一次处理位置的世界坐标。
    QPointF m_MoveLastWorldPosition;

    // 当前移动手势累计产生的世界坐标增量。
    QPointF m_MoveTotalWorldDelta;

    // 是否启用当前鼠标位置的捕捉预览。
    bool m_SnapEnabled;

    // 是否启用网格捕捉。
    bool m_GridSnapEnabled;

    // 是否启用图元端点和圆心捕捉。
    bool m_GeometrySnapEnabled;

    // 当前鼠标视口位置。
    QPointF m_CurrentMousePosition;

    // 当前鼠标位置的捕捉结果。
    CanvasSnapResult m_CurrentSnapResult;

    // 当前控制柄编辑状态。
    EditHandleType m_EditHandleType;

    // 当前正在编辑的折线顶点下标。
    int m_EditVertexIndex;

    // 当前控制柄编辑图元标识。
    ItemId m_EditItemId;

    // 当前控制柄编辑图元类型。
    CanvasEntityType m_EditEntityType;

    // 当前编辑是否已经写入过新的有效几何数据。
    bool m_EditGeometryChanged;

    // 控制柄编辑开始前的原始几何数据。
    StruPointGeometry m_EditOriginalPointGeometry;
    StruLineGeometry m_EditOriginalLineGeometry;
    StruArcGeometry m_EditOriginalArcGeometry;
    StruCircleGeometry m_EditOriginalCircleGeometry;
    StruPolylineGeometry m_EditOriginalPolylineGeometry;

    // 内置 QPainter 渲染器等私有资源。
    std::unique_ptr<CanvasViewPrivate> m_PrivateData;
};

} // namespace mini_graph
