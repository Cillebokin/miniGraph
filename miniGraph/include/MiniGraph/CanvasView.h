#pragma once

#include "CanvasTypes.h"
#include "CanvasSnap.h"
#include "CanvasTransform.h"
#include "CanvasAction.h"
#include "MiniGraph_global.h"

#include <QColor>
#include <QPoint>
#include <QPointer>
#include <QRectF>
#include <QSharedPointer>
#include <QWidget>
#include <QVector>

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
#include <QHash>
#endif

#include <memory>

class QPainter;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace mini_graph {

class CanvasDocument;
class CanvasEntity;
class CanvasRenderer;
class CanvasViewPrivate;

// 基于 QWidget 和 QPainter 的二维画布视图。
// CanvasView 负责画布背景、网格、坐标轴和图元的显示，并处理缩放、
// 平移以及基础鼠标键盘交互。图元数据由 CanvasDocument 提供，坐标换算
// 由 CanvasTransform 管理。本类遵循 QWidget 的 GUI 线程约束，
// 不提供跨线程调用封装。
class MINIGRAPH_EXPORT CanvasView : public QWidget
{
    Q_OBJECT

    // 是否显示背景网格。
    Q_PROPERTY(bool gridVisible READ IsGridVisible WRITE SetGridVisible NOTIFY GridVisibleChanged)

    // 是否显示毫米单位图元的实际线宽；默认关闭，与 CADLab 一致。
    Q_PROPERTY(bool lineWidthVisible READ IsLineWidthVisible WRITE SetLineWidthVisible NOTIFY LineWidthVisibleChanged)

    // 画布背景颜色，默认为黑色。
    Q_PROPERTY(QColor backgroundColor READ GetBackgroundColor WRITE SetBackgroundColor NOTIFY BackgroundColorChanged)

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
    enum class InteractionMode {
        Edit,
        PreviewNavigation
    };

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
    // 仅在 performancebench 构建中累积命中检测的分阶段诊断数据。
    struct CanvasHitTestProfile
    {
        enum {
            EntityTypeCount =
                    static_cast<int>(CanvasEntityType::GroupContainer) + 1
        };

        CanvasHitTestProfile()
        {
            Reset();
        }

        void Reset()
        {
            hitTestCallCount = 0;
            itemIdVisitCount = 0;
            boundsCheckCount = 0;
            boundsRejectCount = 0;
            geometryBoundsCacheLookupCount = 0;
            geometryBoundsCacheMissCount = 0;
            maximumDisplaySizeCacheLookupCount = 0;
            maximumDisplaySizeCacheMissCount = 0;
            maximumDisplaySizeCalculationCallCount = 0;
            maximumDisplaySizeCacheEntryCount = 0;
            maximumDisplaySizeCacheCapacity = 0;
            pathCollectionCallCount = 0;
            distancePathCount = 0;
            distanceSegmentCount = 0;
            itemIdEnumerationNanoseconds = 0;
            entityLookupNanoseconds = 0;
            boundsCheckNanoseconds = 0;
            geometryBoundsAccessNanoseconds = 0;
            maximumDisplaySizeCacheLookupNanoseconds = 0;
            maximumDisplaySizeCalculationNanoseconds = 0;
            hitBoundsExpansionNanoseconds = 0;
            pathCollectionNanoseconds = 0;
            pathDistanceNanoseconds = 0;
            totalNanoseconds = 0;
            for (int index = 0; index < EntityTypeCount; ++index) {
                entityCountByType[index] = 0;
                selectionPathCountByType[index] = 0;
                selectionPathPointCountByType[index] = 0;
                pathCollectionNanosecondsByType[index] = 0;
                pathDistanceNanosecondsByType[index] = 0;
            }
        }

        quint64 hitTestCallCount;
        quint64 itemIdVisitCount;
        quint64 boundsCheckCount;
        quint64 boundsRejectCount;
        quint64 geometryBoundsCacheLookupCount;
        quint64 geometryBoundsCacheMissCount;
        quint64 maximumDisplaySizeCacheLookupCount;
        quint64 maximumDisplaySizeCacheMissCount;
        quint64 maximumDisplaySizeCalculationCallCount;
        quint64 maximumDisplaySizeCacheEntryCount;
        quint64 maximumDisplaySizeCacheCapacity;
        quint64 pathCollectionCallCount;
        quint64 entityCountByType[EntityTypeCount];
        quint64 selectionPathCountByType[EntityTypeCount];
        quint64 selectionPathPointCountByType[EntityTypeCount];
        quint64 distancePathCount;
        quint64 distanceSegmentCount;
        qint64 itemIdEnumerationNanoseconds;
        qint64 entityLookupNanoseconds;
        qint64 boundsCheckNanoseconds;
        qint64 geometryBoundsAccessNanoseconds;
        qint64 maximumDisplaySizeCacheLookupNanoseconds;
        qint64 maximumDisplaySizeCalculationNanoseconds;
        qint64 hitBoundsExpansionNanoseconds;
        qint64 pathCollectionNanoseconds;
        qint64 pathCollectionNanosecondsByType[EntityTypeCount];
        qint64 pathDistanceNanoseconds;
        qint64 pathDistanceNanosecondsByType[EntityTypeCount];
        qint64 totalNanoseconds;
    };
#endif

    // 创建画布视图，parent 为 Qt 对象父级，默认为 nullptr。
    explicit CanvasView(QWidget *parent = nullptr);

    // 释放画布视图。
    ~CanvasView() override;

    // 设置交互模式；预览导航模式只允许平移和缩放。
    void SetInteractionMode(InteractionMode mode);

    // 返回当前交互模式，默认是编辑模式。
    InteractionMode GetInteractionMode() const;

    // 返回当前关联的画布文档，未设置文档时返回 nullptr。
    CanvasDocument *GetDocument() const;

    // 设置画布文档，传入 nullptr 可解除当前关联。
    void SetDocument(CanvasDocument *document);

    // 返回注册顺序最前的自定义渲染器；没有渲染器时返回 nullptr。
    CanvasRenderer *GetCustomRenderer() const;

    // 将自定义渲染器集合替换为传入的单个渲染器；nullptr 表示清空集合。
    // CanvasView 不接管渲染器的所有权，渲染器必须在使用期间保持有效。
    // 每帧开始时固定渲染器集合；在渲染回调内更改集合从下一帧影响绘制。
    void SetCustomRenderer(CanvasRenderer *renderer);

    // 按注册顺序返回当前所有自定义渲染器。
    QVector<CanvasRenderer *> GetCustomRenderers() const;

    // 在集合末尾添加渲染器；空指针或重复注册时返回 false。
    // CanvasView 不接管渲染器的所有权。
    bool AddCustomRenderer(CanvasRenderer *renderer);

    // 移除指定渲染器；该渲染器不在集合中时返回 false。
    bool RemoveCustomRenderer(CanvasRenderer *renderer);

    // 清空所有自定义渲染器。
    void ClearCustomRenderers();

    // 返回当前临时 Action；没有临时 Action 时返回 nullptr。
    CanvasAction *GetCurrentAction() const;

    // 设置临时 Action 并接管其所有权；nullptr 等价于 KillAllActions()。
    // Action 必须未被其他 QObject 持有，或已是当前 CanvasView 的子对象。
    // 临时 Action 活跃时独占画布编辑输入，结束后恢复默认交互。
    bool SetCurrentAction(CanvasAction *action);

    // 返回调用层设置的默认 Action；nullptr 表示使用 CanvasView 内置交互。
    CanvasAction *GetDefaultAction() const;

    // 设置调用层默认 Action 并接管其所有权；nullptr 等价于 ResetDefaultAction()。
    // 临时 Action 优先级高于默认 Action。
    bool SetDefaultAction(CanvasAction *action);

    // 移除调用层默认 Action，恢复 CanvasView 内置交互。
    void ResetDefaultAction();

    // 取消临时 Action 并恢复默认交互。
    void KillAllActions();

    // 返回网格是否可见。
    bool IsGridVisible() const;

    // 设置网格可见状态。
    void SetGridVisible(bool visible);

    // 返回是否按毫米线宽显示图元笔画。
    bool IsLineWidthVisible() const;

    // 设置毫米线宽显示状态；视口像素单位的既有线宽不受此开关影响。
    void SetLineWidthVisible(bool visible);

    // 返回画布背景颜色。
    QColor GetBackgroundColor() const;

    // 设置画布背景颜色；无效颜色会被忽略。
    void SetBackgroundColor(const QColor &color);

    // 返回网格基础世界坐标间距。
    qreal GetGridSpacing() const;

    // 设置网格基础世界坐标间距，spacing 必须为有限正数；无效输入不改变当前值。
    void SetGridSpacing(qreal spacing);

    // 返回当前画布缩放比例。
    qreal GetZoom() const;

    // 返回当前画布坐标变换对象的值拷贝。
    CanvasTransform GetTransform() const;

    // 将视口像素坐标转换为画布世界坐标。
    CanvasVector MapToWorld(const QPointF &viewportPosition) const;

    // 将画布世界坐标转换为视口像素坐标。
    QPointF MapFromWorld(const CanvasVector &worldPosition) const;

    // 返回坐标轴是否可见。
    bool IsAxisVisible() const;

    // 设置坐标轴的可见状态。
    void SetAxisVisible(bool visible);

    // 返回当前画布的 X 轴翻转状态。
    bool GetFlipX() const;

    // 设置当前画布的 X 轴翻转状态，接口和行为与 CADLab 的 SetFlipX 一致。
    void SetFlipX(bool flipX);

    // 返回当前画布的 Y 轴翻转状态。
    bool GetFlipY() const;

    // 设置当前画布的 Y 轴翻转状态，接口和行为与 CADLab 的 SetFlipY 一致。
    void SetFlipY(bool flipY);

    // 同时设置 X/Y 轴翻转状态。
    void SetAxisFlip(bool flipX, bool flipY);

    // 设置坐标轴指示箭头方向，不改变画布坐标变换；保留旧接口语义。
    void SetAxisFlips(bool flipX, bool flipY);

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
    void CenterOnWorld(const CanvasVector &worldPosition);

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
    // 返回自上次重置以来累积的命中检测诊断数据。
    CanvasHitTestProfile GetHitTestProfile() const;

    // 清零命中检测诊断数据。
    void ResetHitTestProfile();

    // 在性能剖析构建中旁路生产最大显示尺寸缓存，作为实时计算对照。
    void SetHitTestMaximumDisplaySizeCacheBypassedForProfiling(bool bypassed);

    // 仅供性能剖析构建启用/关闭实验性包围盒粗筛，不影响普通构建。
    void SetHitTestBoundsFilterEnabledForProfiling(bool enabled);

    // 设置性能剖析专用的几何包围盒缓存；缓存不包含样式扩展和命中容差。
    void SetHitTestGeometryBoundsCacheForProfiling(
            const QHash<ItemId, QRectF> &boundsCache);

    // 清除性能剖析专用的几何包围盒缓存。
    void ClearHitTestGeometryBoundsCacheForProfiling();

    // 按当前文档构建每个图元的最大显示尺寸缓存，并返回遍历节点总数。
    bool BuildHitTestMaximumDisplaySizeCacheForProfiling(
            QHash<ItemId, qreal> *displaySizeCache,
            quint64 *entityVisitCount = nullptr) const;

    // 设置性能剖析专用的最大显示尺寸缓存。
    void SetHitTestMaximumDisplaySizeCacheForProfiling(
            const QHash<ItemId, qreal> &displaySizeCache);

    // 清除性能剖析专用的最大显示尺寸缓存。
    void ClearHitTestMaximumDisplaySizeCacheForProfiling();
#endif

signals:
    // 网格可见状态发生变化时发出。
    void GridVisibleChanged(bool visible);

    // 毫米线宽显示状态发生变化时发出。
    void LineWidthVisibleChanged(bool visible);

    // 画布背景颜色发生变化时发出。
    void BackgroundColorChanged(const QColor &color);

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
    void MouseWorldPositionChanged(const CanvasVector &worldPosition);

    // 用户释放右键请求上下文操作时发出。
    // worldPosition 和 viewportPosition 分别为右键位置的世界坐标和视口坐标。
    void ContextMenuRequested(const CanvasVector &worldPosition,
                              const QPoint &viewportPosition);

    // 用户按下 Esc 或其他取消操作时发出。
    void InteractionCancelled();

protected:
    // 响应视口尺寸变化。
    void resizeEvent(QResizeEvent *event) override;

    // 按背景、图元和叠加层的顺序绘制画布。
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

    // 预览导航模式下双击适配全部图元。
    void mouseDoubleClickEvent(QMouseEvent *event) override;

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

    // 绘制 CADLab 风格的自适应元网格。
    void DrawGrid(QPainter &painter) const;

    // 绘制坐标轴和标尺等前景叠加层。
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
        EllipseCenter,
        EllipseMajorAxis,
        EllipseMinorAxis,
        EllipseStart,
        EllipseEnd,
        PolylineVertex,
        SplineControlPoint,
        GroupReferencePoint
    };

    // 一个控制柄及其世界坐标位置。
    struct EditHandlePosition {
        EditHandleType type = EditHandleType::None;
        CanvasVector worldPosition;
        int pointIndex = -1;
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
                                     int *pointIndex = nullptr) const;

    // 返回指定图元的控制柄世界坐标。
    QVector<EditHandlePosition> GetEditHandlePositions(ItemId id) const;

    // 开始一次控制柄编辑并保存原始几何数据。
    bool BeginEdit(ItemId id,
                   EditHandleType handleType,
                   int pointIndex = -1);

    // 根据鼠标世界坐标更新当前控制柄编辑。
    bool UpdateEdit(const CanvasVector &worldPosition);

    // 清除控制柄编辑状态。
    void ResetEditState();

    // 取消当前控制柄编辑并恢复原始几何数据。
    void CancelEdit();

    // 根据当前鼠标世界坐标更新捕捉结果。
    void UpdateSnap(const CanvasVector &worldPosition);

    // 清除当前移动手势的临时状态。
    void ResetMoveState();

    // 回滚当前尚未结束的移动手势。
    void CancelMove();

    // 返回当前生效的调用层 Action；为空时由 CanvasView 执行内置交互。
    CanvasAction *ActiveAction() const;

    // 检查 Action 可否由当前视图接管且未注册到另一个 Action 槽。
    bool CanInstallAction(CanvasAction *action) const;

    // 连接 Action 的刷新、结束及销毁通知。
    void ConnectActionSignals(CanvasAction *action);

    // 取消并延迟销毁当前临时 Action。
    void CancelCurrentAction();

    // 正常结束并延迟销毁当前临时 Action。
    void FinishCurrentAction();

    // 清理鼠标、移动和编辑的临时状态，以开始或结束 Action。
    void ResetInteractionForAction();

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

    // 当前临时 Action，由 CanvasView 接管生命周期。
    QPointer<CanvasAction> m_CurrentAction;

    // 调用层自定义默认 Action；为空时使用 CanvasView 内置交互。
    QPointer<CanvasAction> m_DefaultAction;

    // 当前画布的世界坐标变换状态。
    CanvasTransform m_Transform;

    // 当前交互模式。
    InteractionMode m_InteractionMode;

    // 网格可见状态。
    bool m_GridVisible;

    // 是否显示毫米单位的实际图元线宽。
    bool m_LineWidthVisible;

    // 画布背景颜色。
    QColor m_BackgroundColor;

    // 网格基础世界坐标间距。
    qreal m_GridSpacing;

    // 坐标轴可见状态。
    bool m_AxisVisible;

    // 坐标轴指示的翻转状态。
    bool m_AxisFlipX;
    bool m_AxisFlipY;

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
    CanvasVector m_MoveLastWorldPosition;

    // 当前移动手势累计产生的世界坐标增量。
    CanvasVector m_MoveTotalWorldDelta;

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

    // 当前正在编辑的多段线顶点或样条控制点下标。
    int m_EditPointIndex;

    // 当前控制柄编辑图元标识。
    ItemId m_EditItemId;

    // 当前控制柄编辑图元类型。
    CanvasEntityType m_EditEntityType;

    // 当前编辑是否已经写入过新的有效几何数据。
    bool m_EditGeometryChanged;

    // 控制柄编辑开始前的实体副本，用于几何更新和取消恢复。
    QSharedPointer<CanvasEntity> m_EditOriginalEntity;

    // 内置 QPainter 渲染器等私有资源。
    std::unique_ptr<CanvasViewPrivate> m_PrivateData;
};

} // namespace mini_graph
