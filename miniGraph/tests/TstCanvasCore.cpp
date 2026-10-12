#include <QtTest>
#include <QCheckBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLineF>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPicture>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include <limits>
#include <cmath>
#include <functional>
#include <memory>
#include <type_traits>

#include "MiniGraph/MiniGraph.h"
#include "MiniGraph/CanvasVector.h"
#include "MiniGraph/CanvasGeometry.h"
#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasSnap.h"
#include "MiniGraph/CanvasRenderer.h"
#include "MiniGraph/CanvasData.h"
#include "MiniGraph/CanvasGlobalData.h"
#include "MiniGraph/CanvasTransform.h"
#include "MiniGraph/CanvasView.h"
#include "MiniGraph/CanvasAction.h"
#include "BasicCustomRenderer.h"
#include "BasicCircleCreationAction.h"
#include "BasicLineCreationAction.h"
#include "BasicPointPlacementAction.h"
#include "GeometryCreateDialog.h"
#include "GeometryEditDialog.h"

class CanvasViewHarness : public mini_graph::CanvasView
{
public:
    using CanvasView::keyPressEvent;
    using CanvasView::keyReleaseEvent;
    using CanvasView::mouseMoveEvent;
    using CanvasView::mousePressEvent;
    using CanvasView::mouseReleaseEvent;
    using CanvasView::mouseDoubleClickEvent;
    using CanvasView::DrawAxes;
    using CanvasView::DrawEditHandles;
    using CanvasView::DrawGrid;
    using CanvasView::DrawRulerLabels;
    using CanvasView::DrawSnapMarker;
    using CanvasView::DrawSelectionBox;
    using CanvasView::wheelEvent;

    int paintEventCount = 0;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        ++paintEventCount;
        CanvasView::paintEvent(event);
    }
};

static bool HasVisiblePixelNear(const QImage &image,
                                const QPointF &position,
                                int radius = 2)
{
    const QPoint center = position.toPoint();
    const int left = qMax(0, center.x() - radius);
    const int right = qMin(image.width() - 1, center.x() + radius);
    const int top = qMax(0, center.y() - radius);
    const int bottom = qMin(image.height() - 1, center.y() + radius);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (image.pixelColor(x, y).alpha() > 0) {
                return true;
            }
        }
    }
    return false;
}

static int CountYellowPixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > 200 && color.green() > 200 && color.blue() < 128) {
                ++count;
            }
        }
    }
    return count;
}

class TestCanvasAction final : public mini_graph::CanvasAction
{
public:
    explicit TestCanvasAction(int *destructionCount = nullptr)
        : m_DestructionCount(destructionCount)
    {
    }

    ~TestCanvasAction() override
    {
        if (m_DestructionCount) {
            ++(*m_DestructionCount);
        }
    }

    void MousePressEvent(QMouseEvent *event) override
    {
        ++mousePressCount;
        lastButton = event->button();
    }

    void MouseMoveEvent(QMouseEvent *event) override
    {
        ++mouseMoveCount;
        lastMousePosition = event->localPos();
    }

    void MouseReleaseEvent(QMouseEvent *) override
    {
        ++mouseReleaseCount;
    }

    void MouseDoubleClickEvent(QMouseEvent *) override
    {
        ++mouseDoubleClickCount;
    }

    void KeyPressEvent(QKeyEvent *event) override
    {
        ++keyPressCount;
        lastKey = event->key();
    }

    void KeyReleaseEvent(QKeyEvent *) override
    {
        ++keyReleaseCount;
    }

    void RenderOverlay(QPainter &painter,
                       const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(context)
        ++overlayRenderCount;
        painter.fillRect(QRectF(30.0, 30.0, 8.0, 8.0), QColor(255, 0, 255));
    }

    void Cancel() override
    {
        ++cancelCount;
    }

    int mousePressCount = 0;
    int mouseMoveCount = 0;
    int mouseReleaseCount = 0;
    int mouseDoubleClickCount = 0;
    int keyPressCount = 0;
    int keyReleaseCount = 0;
    int overlayRenderCount = 0;
    int cancelCount = 0;
    Qt::MouseButton lastButton = Qt::NoButton;
    QPointF lastMousePosition;
    int lastKey = 0;

private:
    int *m_DestructionCount;
};

class CanvasCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void CanvasVectorDefaultAndFiniteConstruction();
    void CanvasVectorRejectsNonFiniteCoordinatesWithoutMutation();
    void CanvasVectorArithmeticUsesValidityRules();
    void CanvasVectorRejectsUndefinedArithmetic();
    void CanvasVectorCalculatesMetricProducts();
    void CanvasVectorCalculatesNormalizedAngles();
    void CanvasVectorRotatesAndNormalizes();
    void CanvasVectorBuildsPolarAndMidpointValues();
    void CanvasVectorSeparatesExactAndToleranceEquality();
    void CanvasEntityCommonPropertiesAndStableTypes();
    void CanvasEntityDataClonesAndRebindsWithEntity();
    void CanvasEntityUnbindsCanvasDataWhenReplacedOrDestroyed();
    void CanvasEntityPreventsCanvasDataBindingToMultipleEntities();
    void DocumentRejectsCanvasDataBindingConflictAtomically();
    void CanvasEntityRejectsFailedCanvasDataClone();
    void CanvasEntitiesRequireCloneInsteadOfCopy();
    void CanvasPointAndLineValidateAndClone();
    void CanvasArcAndCircleValidateAndClone();
    void CanvasSweepsSupportLargeFiniteAngles();
    void CanvasEllipseValidatesGeometryAndClone();
    void CanvasPolylineValidatesConnectionsAndDeepCopies();
    void CanvasSplineValidatesNurbsDataAndClone();
    void CanvasGroupDeepCopiesChildrenAndNestedGroups();
    void CanvasGroupRejectsExcessiveNestingWithoutMutation();
    void WorldCoordinateApisUseCanvasVector();
    void CanvasViewQueuesCanvasVectorWorldSignal();
    void DefaultTransformIsIdentityLike();
    void MinimumZoomSupportsMicroScale();
    void ZoomAtKeepsTheWorldPointUnderTheCursor();
    void PanByMovesWorldPointWithViewportDelta();
    void CanvasTransformRejectsNonFiniteInputsWithoutMutation();
    void CanvasTransformRejectsInvalidViewportDimensions();
    void FitRectCentersAndFitsViewport();
    void FitRectRejectsInvalidViewport();
    void FitRectRejectsNonFiniteInputsWithoutMutation();
    void DocumentTracksLinesAndBounds();
    void DocumentUpdatesGeometryByType();
    void DocumentCalculatesSelectedBounds();
    void DocumentChangesEmitSignals();
    void DocumentDisplaySizeMutationsEmitChanged();
    void DocumentOwnsCanvasDataAndGlobalData();
    void DocumentDetachesCanvasDataWhenEntitiesAreRemoved();
    void DocumentNotifiesCanvasDataMutations();
    void DocumentStoresMixedGeometryPools();
    void DocumentMixedGeometryBoundsAndOrder();
    void DocumentOwnsCanvasEntityClonesAndBoundsAllTypes();
    void DocumentReplacesCanvasEntityWithoutChangingIdentity();
    void DocumentRejectsInvalidCanvasEntitiesAtomically();
    void DocumentTranslatesSplineAndGroupEntities();
    void DocumentRejectsInvalidCircularGeometry();
    void DocumentStoresPolylineAndPreservesOrder();
    void DocumentRejectsInvalidPolylineGeometry();
    void DocumentBoundsAndTranslatesPolyline();
    void DocumentPreservesCanvasDataWhenTranslatingPolyline();
    void DocumentPreservesCanvasDataWhenUpdatingPolylineGeometry();
    void DocumentPreservesPolylineSegmentPropertiesWhenUpdatingGeometry();
    void DocumentSupportsParameterizedCreation();
    void CanvasStyleHasStableDefaultsAndValueSemantics();
    void CanvasStyleValidationIsConsistentAcrossEntityAndDocument();
    void DocumentStoresAndUpdatesItemStyle();
    void DocumentNormalizesLineStyleCompatibilityFields();
    void DocumentRejectsNonFiniteLineWidths();
    void GeometryArcSweepFollowsDirection();
    void GeometryAndCanvasArcUseConsistentSweepTolerance();
    void GeometryRejectsOverflowingArcAngleDifference();
    void GeometryBoundsCoverAllPrimitiveTypes();
    void GeometryValidatesPolylineAndBounds();
    void EntityGeometryBoundsCoverAllEntityTypes();
    void EntityGeometryBoundsAreConservativeForCurvesAndGroups();
    void EntityGeometryBoundsRejectInvalidAndUnrepresentableResults();
    void GeometryTessellatesArcAndCircle();
    void CanvasRenderContextProvidesWorldHelpers();
    void CanvasRendererSupportsBackgroundLayer();
    void CanvasRendererSupportsPainterGeometryLayer();
    void UmbrellaHeaderExposesCanvasApi();
    void CanvasRendererRequestUpdateEmitsSignal();
    void BasicCustomRendererDrawsPainterLayers();
    void CanvasSnapperPrefersVisibleGeometry();
    void CanvasSnapperRejectsNonFiniteDistances();
    void CanvasSnapperSnapsToGrid();
    void CanvasSnapperSnapsToPolylineVertices();
    void CanvasSnapperSnapsToEllipseAndSplineEntities();
    void CanvasSnapperTraversesGroupsAndArcPolylines();
    void CanvasSnapperSkipsInvisiblePolylineSegments();
    void CanvasViewReportsCurrentSnap();
    void CanvasViewDrawsSnapMarker();
    void CanvasViewRendersDocumentWithPainter();
    void CanvasViewRendersCanvasDataInLayerOrder();
    void CanvasViewRendersCanvasDataBeforeEntityGeometry();
    void CanvasViewRendersGlobalDataAroundEntityGeometry();
    void CanvasViewIsolatesCustomRendererPainterState();
    void CanvasViewAntialiasesGlobalCanvasData();
    void CanvasDataInheritsOwningEntityPenWhenUnspecified();
    void CanvasViewDrawsAllCadLabCanvasDataCommands();
    void CanvasDataBrushHatchFillsClosedShapes();
    void CanvasViewRendersAllCanvasEntityTypes();
    void CanvasViewRendersCompleteSplineAtTessellationLimit();
    void CanvasViewDoesNotCloseEachDiscontinuousSplinePath();
    void CanvasViewDoesNotCloseNonmatchingPeriodicSplineEndpoints();
    void CanvasViewRendersNurbsWithLargeFiniteWeights();
    void CanvasViewRendersNurbsWithExtremeWeightRange();
    void CanvasRendererDrawsOpenAndClosedPolylines();
    void CanvasRendererSkipsInvisiblePolylineSegments();
    void CanvasViewWheelUsesCadLabScale();
    void CanvasViewMiddlePanUsesDeadband();
    void CanvasViewPreviewNavigationPansWithLeftAndMiddleButtons();
    void CanvasViewPreviewNavigationIgnoresSelectionAndDelete();
    void CanvasViewPreviewNavigationMatchesCadLabWheelGestures();
    void CanvasViewPreviewNavigationDoubleClickFitsAll();
    void CanvasViewKeyboardAndContextSignals();
    void CanvasViewDisplayPropertiesAndMapping();
    void CanvasViewFitAllUsesDocumentBounds();
    void CanvasViewFitSelectionUsesSelectedBounds();
    void CanvasViewFitCommandsRejectEmptyDocument();
    void CanvasViewCenterOnWorldMovesViewportCenter();
    void CanvasViewZoomInShowsFinerGridUnits();
    void CanvasViewGridAndRulerHandleLargeFiniteCoordinates();
    void CanvasViewGridAndRulerSkipUnsafeTickIndices();
    void CanvasViewGridCapsVisibleTickCount();
    void CanvasViewGridSpacingRejectsNonFiniteValues();
    void CanvasViewRendersCadLabOverlays();
    void CanvasViewAxesStayAttachedToWorldAfterPan();
    void CanvasViewAxisDirectionsFollowFlipState();
    void DocumentSelectionTogglesAndSignals();
    void CanvasViewSingleClickSelectionFollowsCadLabRules();
    void CanvasViewSingleClickSelectsAllGeometryTypes();
    void CanvasViewSelectsPolylineByStroke();
    void CanvasViewHitTestIgnoresInvisibleGeometry();
    void CanvasViewHitTestUsesPointDisplaySize();
    void CanvasViewHitTestRejectsNonFiniteDistances();
    void CanvasViewHitTestUsesLineDisplayWidth();
    void CanvasViewMaximumDisplaySizeCacheInvalidatesOnDocumentChanges();
    void CanvasViewMaximumDisplaySizeCacheInvalidatesOnPolylineReplacement();
#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
    void CanvasViewMaximumDisplaySizeCacheProfilingBypassPreservesHitTesting();
    void CanvasViewMaximumDisplaySizeCacheProfileReportsMixedCapacity();
#endif
    void CanvasViewWindowSelectionUsesDragDirection();
    void CanvasViewWindowSelectionSelectsAllGeometryTypes();
    void CanvasViewSingleClickSelectsAllCanvasEntityTypes();
    void CanvasViewWindowSelectionSelectsAllCanvasEntityTypes();
    void CanvasViewCrossingSelectionUsesCanvasEntityPaths();
    void CanvasViewSelectionSkipsInvisiblePolylineSegments();
    void CanvasViewEntityHitTestRespectsGroupVisibility();
    void CanvasViewWindowSelectionSelectsPolylines();
    void CanvasViewWindowSelectionIgnoresInvisibleGeometry();
    void CanvasViewSelectionBoxUsesCadLabStyle();
    void CanvasViewDoesNotDrawEditHandlesForHiddenPolylineVertices();
    void CanvasViewDeleteKeyRemovesSelectedGeometry();
    void CanvasViewEscapeClearsSelection();
    void CanvasViewDragMovesSelectedGeometry();
    void CanvasViewEscapeCancelsGeometryMove();
    void CanvasViewDragMovesLineEndpointHandle();
    void CanvasViewMovesPolylineVerticesTogether();
    void CanvasViewDragsPolylineVertexHandle();
    void CanvasViewEscapeRestoresPolylineVertexEdit();
    void CanvasViewDragsEllipseMajorAxisHandle();
    void CanvasViewDragsEllipseMinorAxisHandle();
    void CanvasViewDragsEllipseArcEndpointHandle();
    void CanvasViewEscapeRestoresEllipseHandleEdit();
    void CanvasViewDragsSplineControlPointHandle();
    void CanvasViewMixedPolylineOnlyExposesSafeVertexHandles();
    void CanvasViewDragChangesCircleRadiusHandle();
    void CanvasViewDragChangesArcEndpointHandle();
    void CanvasViewEscapeCancelsGeometryHandleEdit();
    void CanvasViewDrawsEditHandlesForSingleSelection();
    void CanvasViewDrawsCadLabGroupReferencePointAndMovesWholeGroup();
    void CanvasViewMovesWholeGroupFromReferencePoint();
    void CanvasViewDrawsPolylineVertexHandles();
    void CanvasViewSupportsCustomRendererAssignment();
    void CanvasViewRendersCustomRenderersInRegistrationOrder();
    void CanvasViewSetCustomRendererReplacesRendererCollection();
    void CanvasViewCanRemoveAndClearCustomRenderers();
    void CanvasViewDefersRendererCollectionChangesUntilNextFrame();
    void CanvasViewRemovesDestroyedCustomRenderers();
    void CanvasViewCanBeDestroyedWithParentedCustomRenderers();
    void CanvasViewRefreshesForEachCustomRenderer();
    void CanvasViewIsolatesPainterStateBetweenCustomRenderers();
    void CanvasViewDispatchesCurrentActionAndRefreshesPreview();
    void CanvasViewEscapeCancelsCurrentActionWithoutClearingSelection();
    void CanvasViewDefaultActionCanBeOverriddenAndReset();
    void CanvasViewFinishedActionReturnsToBuiltinInteraction();
    void CanvasViewKeepsNavigationAvailableDuringAction();
    void BasicPointPlacementActionAddsPointAndFinishes();
    void BasicLineCreationActionAddsLineAfterTwoPointClicks();
    void BasicLineCreationActionCompletesOnDoubleClickEvent();
    void BasicLineCreationActionUsesGeometrySnap();
    void BasicLineCreationActionEscapeCancelsWithoutInsertion();
    void BasicCircleCreationActionAddsCircleAndPreviews();
    void BasicCircleCreationActionCompletesOnDoubleClickEvent();
    void BasicCircleCreationActionUsesGeometrySnap();
    void BasicCircleCreationActionRejectsZeroRadiusAndCanRetry();
    void BasicCircleCreationActionEscapeCancelsWithoutInsertion();
    void CanvasViewLifecycleCanBeCreatedAndDestroyed();
    void BasicExampleGeometryDialogCreatesCircle();
    void BasicExampleGeometryDialogCreatesPolyline();
    void BasicExampleGeometryEditDialogUpdatesPolyline();
    void BasicExampleGeometryEditDialogUpdatesArcAndStyle();
    void BasicExampleGeometryEditDialogCancelKeepsGeometry();
};

class TestCanvasRenderer : public mini_graph::CanvasRenderer
{
public:
    explicit TestCanvasRenderer(QObject *parent = nullptr)
        : mini_graph::CanvasRenderer(parent)
    {
    }

    void Render(QPainter &painter,
                const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(painter)
        Q_UNUSED(context)
        ++renderCount;
    }

    void RenderOverlay(QPainter &painter,
                       const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(painter)
        Q_UNUSED(context)
        ++overlayCount;
    }

    void RenderBackground(QPainter &painter,
                          const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(painter)
        Q_UNUSED(context)
        ++backgroundCount;
    }

    int renderCount = 0;
    int overlayCount = 0;
    int backgroundCount = 0;
};

class TestCanvasData : public mini_graph::CanvasData
{
public:
    explicit TestCanvasData(QStringList *events = nullptr,
                            const QStringList &commands = QStringList())
        : m_Events(events)
        , m_Commands(commands)
        , m_BoundEntity(nullptr)
        , m_BoundId(0)
        , m_UpdateCount(0)
    {
    }

    std::shared_ptr<mini_graph::CanvasData> Clone() override
    {
        return std::make_shared<TestCanvasData>(*this);
    }

    void Update(mini_graph::CanvasEntity *entity) override
    {
        m_BoundEntity = entity;
        m_BoundId = entity ? entity->GetId() : 0;
        ++m_UpdateCount;
    }

    QStringList CalculateToQStringList() override
    {
        if (m_Events) {
            m_Events->append(QStringLiteral("entity-data"));
        }
        return m_Commands;
    }

    void SetCommands(const QStringList &commands)
    {
        m_Commands = commands;
    }

    mini_graph::CanvasEntity *BoundEntity() const
    {
        return m_BoundEntity;
    }

    int UpdateCount() const
    {
        return m_UpdateCount;
    }

    mini_graph::ItemId BoundId() const
    {
        return m_BoundId;
    }

private:
    QStringList *m_Events;
    QStringList m_Commands;
    mini_graph::CanvasEntity *m_BoundEntity;
    mini_graph::ItemId m_BoundId;
    int m_UpdateCount;
};

class NullCloneCanvasData : public mini_graph::CanvasData
{
public:
    std::shared_ptr<mini_graph::CanvasData> Clone() override
    {
        return std::shared_ptr<mini_graph::CanvasData>();
    }

    QStringList CalculateToQStringList() override
    {
        return QStringList();
    }

    void Update(mini_graph::CanvasEntity *entity) override
    {
        m_BoundEntity = entity;
    }

    mini_graph::CanvasEntity *BoundEntity() const
    {
        return m_BoundEntity;
    }

private:
    mini_graph::CanvasEntity *m_BoundEntity = nullptr;
};

class TestCanvasGlobalData : public mini_graph::CanvasGlobalData
{
public:
    explicit TestCanvasGlobalData(QStringList *events = nullptr,
                                  const QStringList &preCommands = QStringList(),
                                  const QStringList &postCommands = QStringList())
        : m_Events(events)
        , m_PreCommands(preCommands)
        , m_PostCommands(postCommands)
    {
    }

    std::shared_ptr<mini_graph::CanvasGlobalData> Clone() override
    {
        return std::make_shared<TestCanvasGlobalData>(*this);
    }

    QStringList CalToQStringListPre() override
    {
        if (m_Events) {
            m_Events->append(QStringLiteral("global-before"));
        }
        return m_PreCommands;
    }

    QStringList CalToQStringListPost() override
    {
        if (m_Events) {
            m_Events->append(QStringLiteral("global-after"));
        }
        return m_PostCommands;
    }

    void SetPreCommands(const QStringList &commands)
    {
        m_PreCommands = commands;
    }

    void SetPostCommands(const QStringList &commands)
    {
        m_PostCommands = commands;
    }

private:
    QStringList *m_Events;
    QStringList m_PreCommands;
    QStringList m_PostCommands;
};

class OrderedCanvasRenderer : public mini_graph::CanvasRenderer
{
public:
    explicit OrderedCanvasRenderer(QStringList *events)
        : m_Events(events)
    {
    }

    void Render(QPainter &painter,
                const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(painter)
        Q_UNUSED(context)
        m_Events->append(QStringLiteral("custom-renderer"));
    }

private:
    QStringList *m_Events;
};

class PainterStateMutatingRenderer : public mini_graph::CanvasRenderer
{
public:
    void RenderBackground(
            QPainter &painter,
            const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(context)
        painter.setClipRect(QRect(0, 0, 1, 1));
        painter.translate(40.0, 0.0);
    }

    void Render(QPainter &painter,
                const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(context)
        painter.setClipRect(QRect(0, 0, 1, 1));
        painter.translate(40.0, 0.0);
    }
};

class RecordingCanvasRenderer : public mini_graph::CanvasRenderer
{
public:
    RecordingCanvasRenderer(const QString &name, QStringList *events)
        : m_Name(name)
        , m_Events(events)
    {
    }

    void RenderBackground(
            QPainter &painter,
            const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(painter)
        Q_UNUSED(context)
        m_Events->append(m_Name + QStringLiteral(".background"));
    }

    void Render(QPainter &painter,
                const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(painter)
        Q_UNUSED(context)
        m_Events->append(m_Name + QStringLiteral(".geometry"));
        if (geometryCallback) {
            geometryCallback();
        }
    }

    void RenderOverlay(QPainter &painter,
                       const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(painter)
        Q_UNUSED(context)
        m_Events->append(m_Name + QStringLiteral(".overlay"));
    }

    std::function<void()> geometryCallback;

private:
    QString m_Name;
    QStringList *m_Events;
};

class SolidGeometryCanvasRenderer : public mini_graph::CanvasRenderer
{
public:
    void Render(QPainter &painter,
                const mini_graph::CanvasRenderContext &context) override
    {
        Q_UNUSED(context)
        painter.fillRect(QRect(20, 20, 12, 12), QColor(Qt::cyan));
    }
};

void CanvasCoreTest::CanvasVectorDefaultAndFiniteConstruction()
{
    const mini_graph::CanvasVector invalid;
    QVERIFY(!invalid.IsValid());
    QCOMPARE(invalid.GetX(), 0.0);
    QCOMPARE(invalid.GetY(), 0.0);

    const mini_graph::CanvasVector origin(0.0, 0.0);
    QVERIFY(origin.IsValid());
    QCOMPARE(origin.GetX(), 0.0);
    QCOMPARE(origin.GetY(), 0.0);

    const mini_graph::CanvasVector point(-12.5, 30.25);
    QVERIFY(point.IsValid());
    QCOMPARE(point.GetX(), -12.5);
    QCOMPARE(point.GetY(), 30.25);
}

void CanvasCoreTest::CanvasVectorRejectsNonFiniteCoordinatesWithoutMutation()
{
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    QVERIFY(!mini_graph::CanvasVector(nan, 1.0).IsValid());
    QVERIFY(!mini_graph::CanvasVector(1.0, infinity).IsValid());

    mini_graph::CanvasVector value(2.0, 3.0);
    QVERIFY(!value.Set(nan, 8.0));
    QVERIFY(value.IsValid());
    QVERIFY(value == mini_graph::CanvasVector(2.0, 3.0));

    value.Invalidate();
    QVERIFY(!value.IsValid());
    QCOMPARE(value.GetX(), 0.0);
    QCOMPARE(value.GetY(), 0.0);
}

void CanvasCoreTest::CanvasVectorArithmeticUsesValidityRules()
{
    const mini_graph::CanvasVector first(2.0, -3.0);
    const mini_graph::CanvasVector second(4.0, 5.0);
    QVERIFY(first + second == mini_graph::CanvasVector(6.0, 2.0));
    QVERIFY(first - second == mini_graph::CanvasVector(-2.0, -8.0));
    QVERIFY(-first == mini_graph::CanvasVector(-2.0, 3.0));
    QVERIFY(first * 2.0 == mini_graph::CanvasVector(4.0, -6.0));
    QVERIFY(first / 2.0 == mini_graph::CanvasVector(1.0, -1.5));

    mini_graph::CanvasVector accumulated = first;
    accumulated += second;
    QVERIFY(accumulated == mini_graph::CanvasVector(6.0, 2.0));
    accumulated -= second;
    QVERIFY(accumulated == first);
    accumulated *= 2.0;
    QVERIFY(accumulated == mini_graph::CanvasVector(4.0, -6.0));
    accumulated /= 2.0;
    QVERIFY(accumulated == first);

    const mini_graph::CanvasVector invalid;
    QVERIFY(!(invalid + first).IsValid());
    QVERIFY(!(first - invalid).IsValid());
    QVERIFY(!(invalid * 2.0).IsValid());
}

void CanvasCoreTest::CanvasVectorRejectsUndefinedArithmetic()
{
    const qreal maximum = std::numeric_limits<qreal>::max();
    const mini_graph::CanvasVector large(maximum, 1.0);
    QVERIFY(!(large * 2.0).IsValid());
    QVERIFY(qIsNaN(large.Dot(mini_graph::CanvasVector(2.0, 0.0))));

    QVERIFY(!(mini_graph::CanvasVector(1.0, 2.0) / 0.0).IsValid());
    QVERIFY(!(mini_graph::CanvasVector(1.0, 2.0) / qQNaN()).IsValid());

    mini_graph::CanvasVector accumulated(1.0, 2.0);
    accumulated /= 0.0;
    QVERIFY(!accumulated.IsValid());
    accumulated = mini_graph::CanvasVector(1.0, 2.0);
    accumulated += mini_graph::CanvasVector();
    QVERIFY(!accumulated.IsValid());
}

void CanvasCoreTest::CanvasVectorCalculatesMetricProducts()
{
    const mini_graph::CanvasVector first(3.0, 4.0);
    const mini_graph::CanvasVector second(-1.0, 2.0);
    QCOMPARE(first.Length(), 5.0);
    QCOMPARE(first.SquaredLength(), 25.0);
    QVERIFY(qAbs(first.DistanceTo(second) - qSqrt(20.0)) < 1e-12);
    QCOMPARE(first.SquaredDistanceTo(second), 20.0);
    QCOMPARE(first.Dot(second), 5.0);
    QCOMPARE(first.Cross(second), 10.0);

    const mini_graph::CanvasVector invalid;
    QVERIFY(qIsNaN(invalid.Length()));
    QVERIFY(qIsNaN(first.DistanceTo(invalid)));
    QVERIFY(qIsNaN(first.Dot(invalid)));
}

void CanvasCoreTest::CanvasVectorCalculatesNormalizedAngles()
{
    const qreal pi = 3.14159265358979323846;
    const mini_graph::CanvasVector east(1.0, 0.0);
    const mini_graph::CanvasVector north(1.0, 2.0);
    const mini_graph::CanvasVector west(0.0, 1.0);
    QVERIFY(qAbs(east.Angle() - 0.0) < 1e-12);
    QVERIFY(qAbs(mini_graph::CanvasVector(0.0, -1.0).Angle()
                 - (1.5 * pi)) < 1e-12);
    QVERIFY(qAbs(north.AngleTo(west) - (5.0 * pi / 4.0)) < 1e-12);
    QVERIFY(qAbs(mini_graph::CanvasVector(0.0, 1.0).AngleTo(
                     mini_graph::CanvasVector(1.0, 0.0))
                 - (7.0 * pi / 4.0)) < 1e-12);
    QVERIFY(qIsNaN(east.AngleTo(east)));
    QVERIFY(qIsNaN(mini_graph::CanvasVector().Angle()));
}

void CanvasCoreTest::CanvasVectorRotatesAndNormalizes()
{
    const qreal halfPi = 1.57079632679489661923;
    const mini_graph::CanvasVector rotated =
            mini_graph::CanvasVector(1.0, 0.0).Rotated(halfPi);
    QVERIFY(rotated.IsNearTo(mini_graph::CanvasVector(0.0, 1.0), 1e-12));

    const mini_graph::CanvasVector normalized =
            mini_graph::CanvasVector(3.0, 4.0).Normalized();
    QVERIFY(normalized.IsNearTo(mini_graph::CanvasVector(0.6, 0.8), 1e-12));
    QVERIFY(!mini_graph::CanvasVector(0.0, 0.0).Normalized().IsValid());
}

void CanvasCoreTest::CanvasVectorBuildsPolarAndMidpointValues()
{
    const qreal halfPi = 1.57079632679489661923;
    const mini_graph::CanvasVector polar =
            mini_graph::CanvasVector::FromPolar(2.0, halfPi);
    QVERIFY(polar.IsNearTo(mini_graph::CanvasVector(0.0, 2.0), 1e-12));

    const mini_graph::CanvasVector midpoint = mini_graph::CanvasVector::Midpoint(
                mini_graph::CanvasVector(-2.0, 4.0),
                mini_graph::CanvasVector(6.0, -2.0));
    QVERIFY(midpoint == mini_graph::CanvasVector(2.0, 1.0));
    QVERIFY(!mini_graph::CanvasVector::Midpoint(
                mini_graph::CanvasVector(), mini_graph::CanvasVector(1.0, 1.0)).IsValid());
}

void CanvasCoreTest::CanvasVectorSeparatesExactAndToleranceEquality()
{
    const mini_graph::CanvasVector first(1.0, 2.0);
    const mini_graph::CanvasVector exact(1.0, 2.0);
    const mini_graph::CanvasVector nearby(1.0 + 1e-6, 2.0);
    QVERIFY(first == exact);
    QVERIFY(first != nearby);
    QVERIFY(first.IsNearTo(nearby, 1.1e-6));
    QVERIFY(!first.IsNearTo(nearby, 0.9e-6));
    QVERIFY(!first.IsNearTo(exact, -1.0));
}

void CanvasCoreTest::CanvasEntityCommonPropertiesAndStableTypes()
{
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Invalid), 0);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Point), 1);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Line), 2);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Arc), 3);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Circle), 4);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Polyline), 5);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Ellipse), 6);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::Spline), 7);
    QCOMPARE(static_cast<int>(mini_graph::CanvasEntityType::GroupContainer), 8);

    mini_graph::CanvasPoint point;
    QCOMPARE(point.GetId(), mini_graph::ItemId(0));
    QVERIFY(point.GetLayerName().isEmpty());
    QVERIFY(point.GetTag().isEmpty());
    QCOMPARE(point.GetBusinessId(), qint64(0));
    QVERIFY(!point.IsReversed());

    const mini_graph::StruCanvasStyle defaultStyle;
    QVERIFY(point.GetStyle() == defaultStyle);
    mini_graph::StruCanvasStyle invalidStyle = defaultStyle;
    invalidStyle.strokeWidth = std::numeric_limits<qreal>::infinity();
    QVERIFY(!point.SetStyle(invalidStyle));
    QVERIFY(point.GetStyle() == defaultStyle);
    invalidStyle = defaultStyle;
    invalidStyle.pointSize = -1.0;
    QVERIFY(!point.SetStyle(invalidStyle));
    QVERIFY(point.GetStyle() == defaultStyle);

    mini_graph::StruCanvasStyle style = defaultStyle;
    style.strokeWidth = 2.5;
    style.pointSize = 9.0;
    style.visible = false;
    QVERIFY(point.SetStyle(style));
    point.SetLayerName(QStringLiteral("construction"));
    point.SetTag(QStringLiteral("origin-marker"));
    point.SetBusinessId(42);
    point.SetReversed(true);

    const QSharedPointer<mini_graph::CanvasEntity> clone = point.Clone();
    QVERIFY(clone);
    QCOMPARE(clone->GetId(), mini_graph::ItemId(0));
    QVERIFY(clone->GetStyle() == style);
    QCOMPARE(clone->GetLayerName(), QStringLiteral("construction"));
    QCOMPARE(clone->GetTag(), QStringLiteral("origin-marker"));
    QCOMPARE(clone->GetBusinessId(), qint64(42));
    QVERIFY(clone->IsReversed());
}

void CanvasCoreTest::CanvasEntityDataClonesAndRebindsWithEntity()
{
    mini_graph::CanvasLine source(mini_graph::CanvasVector(0.0, 0.0),
                                  mini_graph::CanvasVector(10.0, 0.0));
    const std::shared_ptr<TestCanvasData> data = std::make_shared<TestCanvasData>();
    source.SetCanvasData(data);

    QCOMPARE(data->BoundEntity(), static_cast<mini_graph::CanvasEntity *>(&source));
    QCOMPARE(source.GetCanvasData().get(),
             static_cast<mini_graph::CanvasData *>(data.get()));

    const QSharedPointer<mini_graph::CanvasEntity> clone = source.Clone();
    QVERIFY(clone);
    QVERIFY(clone->GetCanvasData());
    QVERIFY(clone->GetCanvasData().get() != source.GetCanvasData().get());

    const TestCanvasData *clonedData =
            dynamic_cast<const TestCanvasData *>(clone->GetCanvasData().get());
    QVERIFY(clonedData);
    QCOMPARE(clonedData->BoundEntity(), clone.data());
    QCOMPARE(data->BoundEntity(), static_cast<mini_graph::CanvasEntity *>(&source));
}

void CanvasCoreTest::CanvasEntityUnbindsCanvasDataWhenReplacedOrDestroyed()
{
    const std::shared_ptr<TestCanvasData> firstData =
            std::make_shared<TestCanvasData>();
    const std::shared_ptr<TestCanvasData> replacementData =
            std::make_shared<TestCanvasData>();
    const std::shared_ptr<TestCanvasData> destroyedEntityData =
            std::make_shared<TestCanvasData>();

    {
        mini_graph::CanvasLine entity(mini_graph::CanvasVector(0.0, 0.0),
                                      mini_graph::CanvasVector(1.0, 0.0));
        entity.SetCanvasData(firstData);
        QCOMPARE(firstData->BoundEntity(),
                 static_cast<mini_graph::CanvasEntity *>(&entity));

        entity.SetCanvasData(replacementData);
        QVERIFY(!firstData->BoundEntity());
        QCOMPARE(replacementData->BoundEntity(),
                 static_cast<mini_graph::CanvasEntity *>(&entity));

        entity.SetCanvasData(nullptr);
        QVERIFY(!replacementData->BoundEntity());

        entity.SetCanvasData(destroyedEntityData);
        QCOMPARE(destroyedEntityData->BoundEntity(),
                 static_cast<mini_graph::CanvasEntity *>(&entity));
    }

    QVERIFY(!destroyedEntityData->BoundEntity());
}

void CanvasCoreTest::CanvasEntityPreventsCanvasDataBindingToMultipleEntities()
{
    mini_graph::CanvasLine firstEntity(mini_graph::CanvasVector(0.0, 0.0),
                                       mini_graph::CanvasVector(1.0, 0.0));
    mini_graph::CanvasLine secondEntity(mini_graph::CanvasVector(0.0, 1.0),
                                        mini_graph::CanvasVector(1.0, 1.0));
    const std::shared_ptr<TestCanvasData> data =
            std::make_shared<TestCanvasData>();

    QVERIFY(firstEntity.TrySetCanvasData(data));
    QVERIFY(!secondEntity.TrySetCanvasData(data));
    QCOMPARE(firstEntity.GetCanvasData().get(),
             static_cast<mini_graph::CanvasData *>(data.get()));
    QVERIFY(!secondEntity.GetCanvasData());
    QCOMPARE(data->BoundEntity(),
             static_cast<mini_graph::CanvasEntity *>(&firstEntity));

    QVERIFY(firstEntity.TrySetCanvasData(nullptr));
    QVERIFY(!data->BoundEntity());
    QVERIFY(secondEntity.TrySetCanvasData(data));
    QCOMPARE(data->BoundEntity(),
             static_cast<mini_graph::CanvasEntity *>(&secondEntity));
}

void CanvasCoreTest::CanvasEntityRejectsFailedCanvasDataClone()
{
    mini_graph::CanvasLine source(mini_graph::CanvasVector(0.0, 0.0),
                                  mini_graph::CanvasVector(1.0, 0.0));
    const std::shared_ptr<NullCloneCanvasData> data =
            std::make_shared<NullCloneCanvasData>();
    source.SetCanvasData(data);

    QVERIFY(source.Clone().isNull());
    QCOMPARE(data->BoundEntity(), static_cast<mini_graph::CanvasEntity *>(&source));

    mini_graph::CanvasDocument document;
    QCOMPARE(document.AddEntity(source), mini_graph::ItemId(0));
    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(data->BoundEntity(), static_cast<mini_graph::CanvasEntity *>(&source));
}

void CanvasCoreTest::CanvasEntitiesRequireCloneInsteadOfCopy()
{
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasPoint>::value);
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasLine>::value);
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasArc>::value);
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasCircle>::value);
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasEllipse>::value);
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasPolyline>::value);
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasSpline>::value);
    QVERIFY(!std::is_copy_constructible<mini_graph::CanvasGroupContainer>::value);

    QVERIFY(!std::is_copy_assignable<mini_graph::CanvasPoint>::value);
    QVERIFY(!std::is_copy_assignable<mini_graph::CanvasPolyline>::value);
    QVERIFY(!std::is_copy_assignable<mini_graph::CanvasGroupContainer>::value);
}

void CanvasCoreTest::CanvasPointAndLineValidateAndClone()
{
    mini_graph::CanvasPoint point;
    QCOMPARE(point.GetType(), mini_graph::CanvasEntityType::Point);
    QVERIFY(!point.IsValid());
    QVERIFY(point.SetPosition(mini_graph::CanvasVector(3.0, -4.0)));
    QVERIFY(point.IsValid());
    QVERIFY(point.GetPosition() == mini_graph::CanvasVector(3.0, -4.0));
    QVERIFY(!point.SetPosition(mini_graph::CanvasVector()));
    QVERIFY(point.GetPosition() == mini_graph::CanvasVector(3.0, -4.0));

    const QSharedPointer<mini_graph::CanvasEntity> pointClone = point.Clone();
    const mini_graph::CanvasPoint *clonedPoint =
            dynamic_cast<const mini_graph::CanvasPoint *>(pointClone.data());
    QVERIFY(clonedPoint);
    QVERIFY(clonedPoint->GetPosition() == mini_graph::CanvasVector(3.0, -4.0));
    QVERIFY(point.SetPosition(mini_graph::CanvasVector(10.0, 11.0)));
    QVERIFY(clonedPoint->GetPosition() == mini_graph::CanvasVector(3.0, -4.0));

    mini_graph::CanvasLine line;
    QCOMPARE(line.GetType(), mini_graph::CanvasEntityType::Line);
    QVERIFY(!line.IsValid());
    QVERIFY(line.SetGeometry(mini_graph::CanvasVector(1.0, 2.0),
                             mini_graph::CanvasVector(1.0, 2.0)));
    QVERIFY(line.IsValid());
    QVERIFY(line.GetStartPoint() == mini_graph::CanvasVector(1.0, 2.0));
    QVERIFY(line.GetEndPoint() == mini_graph::CanvasVector(1.0, 2.0));
    QVERIFY(!line.SetGeometry(mini_graph::CanvasVector(5.0, 6.0),
                              mini_graph::CanvasVector()));
    QVERIFY(line.GetStartPoint() == mini_graph::CanvasVector(1.0, 2.0));
    QVERIFY(line.GetEndPoint() == mini_graph::CanvasVector(1.0, 2.0));

    const QSharedPointer<mini_graph::CanvasEntity> lineClone = line.Clone();
    const mini_graph::CanvasLine *clonedLine =
            dynamic_cast<const mini_graph::CanvasLine *>(lineClone.data());
    QVERIFY(clonedLine);
    QVERIFY(clonedLine->IsValid());
    QVERIFY(clonedLine->GetStartPoint() == line.GetStartPoint());
    QVERIFY(clonedLine->GetEndPoint() == line.GetEndPoint());
}

void CanvasCoreTest::CanvasArcAndCircleValidateAndClone()
{
    const qreal pi = std::acos(-1.0);
    mini_graph::CanvasArc arc;
    QCOMPARE(arc.GetType(), mini_graph::CanvasEntityType::Arc);
    QVERIFY(!arc.IsValid());
    QVERIFY(arc.SetGeometry(mini_graph::CanvasVector(1.0, 2.0),
                            5.0, 0.25, pi));
    arc.SetReversed(true);
    QVERIFY(arc.IsValid());
    QVERIFY(arc.GetCenter() == mini_graph::CanvasVector(1.0, 2.0));
    QCOMPARE(arc.GetRadius(), 5.0);
    QCOMPARE(arc.GetStartAngle(), 0.25);
    QCOMPARE(arc.GetEndAngle(), pi);
    QVERIFY(arc.IsReversed());

    QVERIFY(!arc.SetGeometry(mini_graph::CanvasVector(9.0, 9.0),
                             2.0, 0.5, 0.5));
    QVERIFY(!arc.SetGeometry(mini_graph::CanvasVector(9.0, 9.0),
                             2.0, 0.1, 0.1 + 2.0 * pi));
    QVERIFY(arc.GetCenter() == mini_graph::CanvasVector(1.0, 2.0));
    QCOMPARE(arc.GetRadius(), 5.0);
    QCOMPARE(arc.GetStartAngle(), 0.25);
    QCOMPARE(arc.GetEndAngle(), pi);

    const QSharedPointer<mini_graph::CanvasEntity> arcClone = arc.Clone();
    const mini_graph::CanvasArc *clonedArc =
            dynamic_cast<const mini_graph::CanvasArc *>(arcClone.data());
    QVERIFY(clonedArc);
    QVERIFY(clonedArc->IsValid());
    QVERIFY(clonedArc->IsReversed());
    QVERIFY(clonedArc->GetCenter() == arc.GetCenter());
    QCOMPARE(clonedArc->GetRadius(), arc.GetRadius());
    QCOMPARE(clonedArc->GetStartAngle(), arc.GetStartAngle());
    QCOMPARE(clonedArc->GetEndAngle(), arc.GetEndAngle());

    mini_graph::CanvasCircle circle;
    QCOMPARE(circle.GetType(), mini_graph::CanvasEntityType::Circle);
    QVERIFY(!circle.IsValid());
    QVERIFY(!circle.SetGeometry(mini_graph::CanvasVector(0.0, 0.0), 0.0));
    QVERIFY(circle.SetGeometry(mini_graph::CanvasVector(-3.0, 7.0), 2.5));
    QVERIFY(circle.IsValid());
    QVERIFY(circle.GetCenter() == mini_graph::CanvasVector(-3.0, 7.0));
    QCOMPARE(circle.GetRadius(), 2.5);
    QVERIFY(!circle.SetGeometry(mini_graph::CanvasVector(), 4.0));
    QVERIFY(circle.GetCenter() == mini_graph::CanvasVector(-3.0, 7.0));
    QCOMPARE(circle.GetRadius(), 2.5);

    const QSharedPointer<mini_graph::CanvasEntity> circleClone = circle.Clone();
    const mini_graph::CanvasCircle *clonedCircle =
            dynamic_cast<const mini_graph::CanvasCircle *>(circleClone.data());
    QVERIFY(clonedCircle);
    QVERIFY(clonedCircle->IsValid());
    QVERIFY(clonedCircle->GetCenter() == circle.GetCenter());
    QCOMPARE(clonedCircle->GetRadius(), circle.GetRadius());
}

void CanvasCoreTest::CanvasSweepsSupportLargeFiniteAngles()
{
    const qreal startAngle = 1.0e16;
    const qreal endAngle = startAngle + 4.0;

    mini_graph::CanvasArc arc;
    QVERIFY(arc.SetGeometry(mini_graph::CanvasVector(0.0, 0.0), 2.0,
                            startAngle, endAngle));
    QVERIFY(arc.IsValid());

    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                mini_graph::CanvasVector(3.0, 0.0),
                                0.5, true, startAngle, endAngle));
    QVERIFY(ellipse.IsValid());
}

void CanvasCoreTest::CanvasEllipseValidatesGeometryAndClone()
{
    const qreal pi = std::acos(-1.0);
    mini_graph::CanvasEllipse ellipse;
    QCOMPARE(ellipse.GetType(), mini_graph::CanvasEntityType::Ellipse);
    QVERIFY(!ellipse.IsValid());
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(2.0, -1.0),
                                mini_graph::CanvasVector(4.0, 0.0),
                                0.5, false, 0.25, 1.0));
    QVERIFY(ellipse.IsValid());
    QVERIFY(!ellipse.IsArc());
    QVERIFY(ellipse.GetCenter() == mini_graph::CanvasVector(2.0, -1.0));
    QVERIFY(ellipse.GetMajorAxis() == mini_graph::CanvasVector(4.0, 0.0));
    QCOMPARE(ellipse.GetMinorMajorRatio(), 0.5);
    QCOMPARE(ellipse.GetStartParameter(), 0.0);
    QCOMPARE(ellipse.GetEndParameter(), 0.0);

    QVERIFY(!ellipse.SetGeometry(mini_graph::CanvasVector(9.0, 9.0),
                                 mini_graph::CanvasVector(3.0, 0.0),
                                 0.0, false));
    QVERIFY(!ellipse.SetGeometry(mini_graph::CanvasVector(9.0, 9.0),
                                 mini_graph::CanvasVector(0.0, 0.0),
                                 0.5, false));
    QVERIFY(ellipse.GetCenter() == mini_graph::CanvasVector(2.0, -1.0));

    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(-2.0, 5.0),
                                mini_graph::CanvasVector(0.0, 6.0),
                                0.75, true, -pi / 2.0, pi / 2.0));
    QVERIFY(ellipse.IsArc());
    QCOMPARE(ellipse.GetStartParameter(), -pi / 2.0);
    QCOMPARE(ellipse.GetEndParameter(), pi / 2.0);
    QVERIFY(!ellipse.SetGeometry(mini_graph::CanvasVector(9.0, 9.0),
                                 mini_graph::CanvasVector(3.0, 0.0),
                                 0.5, true, 0.1, 0.1 + 2.0 * pi));
    QVERIFY(ellipse.GetCenter() == mini_graph::CanvasVector(-2.0, 5.0));
    QCOMPARE(ellipse.GetStartParameter(), -pi / 2.0);
    QCOMPARE(ellipse.GetEndParameter(), pi / 2.0);

    ellipse.SetReversed(true);
    const QSharedPointer<mini_graph::CanvasEntity> clone = ellipse.Clone();
    const mini_graph::CanvasEllipse *clonedEllipse =
            dynamic_cast<const mini_graph::CanvasEllipse *>(clone.data());
    QVERIFY(clonedEllipse);
    QVERIFY(clonedEllipse->IsValid());
    QVERIFY(clonedEllipse->IsReversed());
    QVERIFY(clonedEllipse->IsArc());
    QVERIFY(clonedEllipse->GetCenter() == ellipse.GetCenter());
    QVERIFY(clonedEllipse->GetMajorAxis() == ellipse.GetMajorAxis());
    QCOMPARE(clonedEllipse->GetMinorMajorRatio(), ellipse.GetMinorMajorRatio());
    QCOMPARE(clonedEllipse->GetStartParameter(), ellipse.GetStartParameter());
    QCOMPARE(clonedEllipse->GetEndParameter(), ellipse.GetEndParameter());
}

void CanvasCoreTest::CanvasPolylineValidatesConnectionsAndDeepCopies()
{
    const qreal pi = qAcos(-1.0);
    mini_graph::CanvasPolyline polyline;
    QCOMPARE(polyline.GetType(), mini_graph::CanvasEntityType::Polyline);
    QVERIFY(!polyline.IsValid());
    QCOMPARE(polyline.GetSegmentCount(), 0);
    QVERIFY(!polyline.GetSegment(0));
    QVERIFY(!polyline.SetClosed(true));

    mini_graph::CanvasLine first(mini_graph::CanvasVector(0.0, 0.0),
                                 mini_graph::CanvasVector(1.0, 0.0));
    first.SetTag(QStringLiteral("first-input"));
    QVERIFY(polyline.AddSegment(first));
    QVERIFY(polyline.IsValid());
    QVERIFY(!polyline.AddSegment(mini_graph::CanvasLine(
                                     mini_graph::CanvasVector(10.0, 10.0),
                                     mini_graph::CanvasVector(11.0, 11.0))));
    QCOMPARE(polyline.GetSegmentCount(), 1);
    const mini_graph::CanvasLine *storedFirst =
            dynamic_cast<const mini_graph::CanvasLine *>(polyline.GetSegment(0));
    QVERIFY(storedFirst);
    QVERIFY(storedFirst->GetStartPoint() == mini_graph::CanvasVector(0.0, 0.0));

    QVERIFY(first.SetGeometry(mini_graph::CanvasVector(50.0, 50.0),
                              mini_graph::CanvasVector(60.0, 60.0)));
    QVERIFY(storedFirst->GetStartPoint() == mini_graph::CanvasVector(0.0, 0.0));
    QCOMPARE(storedFirst->GetTag(), QStringLiteral("first-input"));

    mini_graph::CanvasArc second(mini_graph::CanvasVector(1.0, 1.0),
                                 1.0, -pi / 2.0, 0.0);
    QVERIFY(polyline.AddSegment(second));
    QCOMPARE(polyline.GetSegmentCount(), 2);
    QVERIFY(dynamic_cast<const mini_graph::CanvasArc *>(polyline.GetSegment(1)));
    QVERIFY(!polyline.GetSegment(-1));
    QVERIFY(!polyline.GetSegment(2));
    QVERIFY(!polyline.SetSegment(2, first));
    QVERIFY(!polyline.RemoveSegment(-1));

    const QSharedPointer<mini_graph::CanvasEntity> clone = polyline.Clone();
    const mini_graph::CanvasPolyline *clonedPolyline =
            dynamic_cast<const mini_graph::CanvasPolyline *>(clone.data());
    QVERIFY(clonedPolyline);
    QVERIFY(clonedPolyline->IsValid());
    QCOMPARE(clonedPolyline->GetSegmentCount(), 2);
    QVERIFY(clonedPolyline->GetSegment(0) != polyline.GetSegment(0));
    QVERIFY(clonedPolyline->GetSegment(0)->GetId() == mini_graph::ItemId(0));

    QVERIFY(!polyline.SetSegment(0, mini_graph::CanvasLine(
                                     mini_graph::CanvasVector(8.0, 8.0),
                                     mini_graph::CanvasVector(9.0, 9.0))));
    QCOMPARE(polyline.GetSegmentCount(), 2);
    const mini_graph::CanvasLine *stillFirst =
            dynamic_cast<const mini_graph::CanvasLine *>(polyline.GetSegment(0));
    QVERIFY(stillFirst);
    QVERIFY(stillFirst->GetStartPoint() == mini_graph::CanvasVector(0.0, 0.0));

    QVERIFY(polyline.RemoveSegment(0));
    QCOMPARE(polyline.GetSegmentCount(), 1);
    QVERIFY(polyline.IsValid());
    QVERIFY(!polyline.RemoveSegment(0));
    QCOMPARE(polyline.GetSegmentCount(), 1);

    mini_graph::CanvasPolyline closed;
    const mini_graph::CanvasLine edge1(mini_graph::CanvasVector(0.0, 0.0),
                                       mini_graph::CanvasVector(1.0, 0.0));
    const mini_graph::CanvasLine edge2(mini_graph::CanvasVector(1.0, 0.0),
                                       mini_graph::CanvasVector(0.0, 1.0));
    const mini_graph::CanvasLine edge3(mini_graph::CanvasVector(0.0, 1.0),
                                       mini_graph::CanvasVector(0.0, 0.0));
    QVERIFY(closed.AddSegment(edge1));
    QVERIFY(closed.AddSegment(edge2));
    QVERIFY(closed.AddSegment(edge3));
    QVERIFY(closed.SetClosed(true));
    QVERIFY(closed.IsValid());
    QVERIFY(closed.IsClosed());
    QVERIFY(!closed.SetSegment(2, mini_graph::CanvasLine(
                                   mini_graph::CanvasVector(0.0, 1.0),
                                   mini_graph::CanvasVector(0.0, 2.0))));
    QVERIFY(!closed.RemoveSegment(1));
    QCOMPARE(closed.GetSegmentCount(), 3);
    QVERIFY(closed.SetClosed(false));
    QVERIFY(closed.IsValid());
}

void CanvasCoreTest::CanvasSplineValidatesNurbsDataAndClone()
{
    mini_graph::CanvasSpline spline;
    QCOMPARE(spline.GetType(), mini_graph::CanvasEntityType::Spline);
    QVERIFY(!spline.IsValid());
    QCOMPARE(spline.GetDegree(), 0);
    QVERIFY(spline.GetControlPoints().isEmpty());

    const QVector<mini_graph::CanvasVector> points =
            QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(0.0, 0.0)
            << mini_graph::CanvasVector(1.0, 2.0)
            << mini_graph::CanvasVector(3.0, 0.0);
    const QVector<qreal> knots = QVector<qreal>()
            << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0;
    QVERIFY(spline.SetData(2, points, knots, QVector<qreal>(), true, false));
    QVERIFY(spline.IsValid());
    QCOMPARE(spline.GetDegree(), 2);
    QVERIFY(spline.IsClosed());
    QVERIFY(!spline.IsPeriodic());
    QVERIFY(spline.GetControlPoints() == points);
    QVERIFY(spline.GetKnots() == knots);
    QVERIFY(spline.GetWeights().isEmpty());

    QVERIFY(!spline.SetData(0, points, knots, QVector<qreal>(), false, false));
    QVERIFY(!spline.SetData(2, points, QVector<qreal>() << 0.0 << 0.0,
                            QVector<qreal>(), false, false));
    QVERIFY(!spline.SetData(2, points,
                            QVector<qreal>() << 0.0 << 0.0 << 0.0
                                            << 1.0 << 0.5 << 1.0,
                            QVector<qreal>(), false, false));
    QVERIFY(!spline.SetData(2, points,
                            QVector<qreal>() << 0.0 << 0.0 << 0.0
                                            << 0.0 << 0.0 << 0.0,
                            QVector<qreal>(), false, false));
    QVERIFY(!spline.SetData(2, points, knots,
                            QVector<qreal>() << 1.0 << 0.0 << 1.0,
                            false, false));
    QVERIFY(!spline.SetData(2, points, knots,
                            QVector<qreal>() << 1.0 << 1.0,
                            false, false));
    QVERIFY(!spline.SetData(2,
                            QVector<mini_graph::CanvasVector>()
                            << mini_graph::CanvasVector(0.0, 0.0)
                            << mini_graph::CanvasVector()
                            << mini_graph::CanvasVector(3.0, 0.0),
                            knots, QVector<qreal>(), false, false));

    mini_graph::CanvasSpline excessiveKnotMultiplicity;
    QVERIFY(!excessiveKnotMultiplicity.SetData(
                2,
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(0.0, 0.0)
                << mini_graph::CanvasVector(1.0, 1.0)
                << mini_graph::CanvasVector(2.0, 0.0)
                << mini_graph::CanvasVector(3.0, 1.0)
                << mini_graph::CanvasVector(4.0, 0.0)
                << mini_graph::CanvasVector(5.0, 1.0)
                << mini_graph::CanvasVector(6.0, 0.0),
                QVector<qreal>() << 0.0 << 0.0 << 0.0
                << 0.5 << 0.5 << 0.5 << 0.5
                << 1.0 << 1.0 << 1.0,
                QVector<qreal>(), false, false));

    QVERIFY(spline.IsValid());
    QVERIFY(spline.IsClosed());

    const QVector<qreal> weights = QVector<qreal>() << 1.0 << 2.0 << 1.0;
    QVERIFY(spline.SetData(2, points, knots, weights, false, true));
    QVERIFY(!spline.IsClosed());
    QVERIFY(spline.IsPeriodic());
    QCOMPARE(spline.GetWeights(), weights);

    const QSharedPointer<mini_graph::CanvasEntity> clone = spline.Clone();
    const mini_graph::CanvasSpline *clonedSpline =
            dynamic_cast<const mini_graph::CanvasSpline *>(clone.data());
    QVERIFY(clonedSpline);
    QVERIFY(clonedSpline->IsValid());
    QCOMPARE(clonedSpline->GetDegree(), spline.GetDegree());
    QVERIFY(clonedSpline->GetControlPoints() == spline.GetControlPoints());
    QVERIFY(clonedSpline->GetKnots() == spline.GetKnots());
    QCOMPARE(clonedSpline->GetWeights(), spline.GetWeights());
    QVERIFY(clonedSpline->IsPeriodic());
    QVERIFY(!clonedSpline->IsClosed());
}

void CanvasCoreTest::CanvasGroupDeepCopiesChildrenAndNestedGroups()
{
    mini_graph::CanvasGroupContainer group;
    QCOMPARE(group.GetType(), mini_graph::CanvasEntityType::GroupContainer);
    QVERIFY(!group.IsValid());
    QVERIFY(group.CanExplode());
    QCOMPARE(group.GetChildCount(), 0);
    QVERIFY(!group.GetChild(0));
    QVERIFY(!group.AddChild(mini_graph::CanvasPoint()));
    QVERIFY(!group.ReplaceChild(0, mini_graph::CanvasPoint(
                                    mini_graph::CanvasVector(1.0, 2.0))));
    QVERIFY(!group.RemoveChild(0));

    mini_graph::CanvasPoint sourcePoint(mini_graph::CanvasVector(1.0, 2.0));
    sourcePoint.SetTag(QStringLiteral("source"));
    QVERIFY(group.AddChild(sourcePoint));
    QVERIFY(group.IsValid());
    QCOMPARE(group.GetChildCount(), 1);
    QVERIFY(group.GetChild(0) != &sourcePoint);
    const mini_graph::CanvasPoint *storedPoint =
            dynamic_cast<const mini_graph::CanvasPoint *>(group.GetChild(0));
    QVERIFY(storedPoint);
    QVERIFY(storedPoint->GetPosition() == mini_graph::CanvasVector(1.0, 2.0));
    QVERIFY(sourcePoint.SetPosition(mini_graph::CanvasVector(9.0, 9.0)));
    QVERIFY(storedPoint->GetPosition() == mini_graph::CanvasVector(1.0, 2.0));

    mini_graph::CanvasGroupContainer nested;
    mini_graph::CanvasCircle sourceCircle(mini_graph::CanvasVector(3.0, 4.0), 2.0);
    QVERIFY(nested.AddChild(sourceCircle));
    QVERIFY(group.AddChild(nested));
    QVERIFY(group.IsValid());
    const mini_graph::CanvasGroupContainer *storedNested =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(group.GetChild(1));
    QVERIFY(storedNested);
    QVERIFY(storedNested != &nested);
    QVERIFY(storedNested->IsValid());
    QVERIFY(storedNested->GetChild(0) != &sourceCircle);
    QVERIFY(!group.AddChild(group));
    QCOMPARE(group.GetChildCount(), 2);
    QVERIFY(!group.ReplaceChild(-1, sourcePoint));
    QVERIFY(!group.ReplaceChild(2, sourcePoint));
    QVERIFY(!group.RemoveChild(2));

    group.SetAllowExplode(false);
    QVERIFY(!group.CanExplode());
    group.SetLayerName(QStringLiteral("parts"));
    group.SetBusinessId(99);
    const QSharedPointer<mini_graph::CanvasEntity> clone = group.Clone();
    const mini_graph::CanvasGroupContainer *clonedGroup =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(clone.data());
    QVERIFY(clonedGroup);
    QVERIFY(clonedGroup->IsValid());
    QVERIFY(!clonedGroup->CanExplode());
    QCOMPARE(clonedGroup->GetLayerName(), QStringLiteral("parts"));
    QCOMPARE(clonedGroup->GetBusinessId(), qint64(99));
    QCOMPARE(clonedGroup->GetChildCount(), 2);
    QVERIFY(clonedGroup->GetChild(0) != group.GetChild(0));
    QVERIFY(clonedGroup->GetChild(0)->GetId() == mini_graph::ItemId(0));

    const mini_graph::CanvasGroupContainer *clonedNested =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                clonedGroup->GetChild(1));
    QVERIFY(clonedNested);
    QVERIFY(clonedNested != storedNested);
    QVERIFY(clonedNested->IsValid());
    QVERIFY(clonedNested->GetChild(0) != storedNested->GetChild(0));
    QVERIFY(clonedNested->GetChild(0)->GetId() == mini_graph::ItemId(0));

    mini_graph::CanvasPoint replacement(mini_graph::CanvasVector(-5.0, 7.0));
    QVERIFY(group.ReplaceChild(0, replacement));
    const mini_graph::CanvasPoint *replacedPoint =
            dynamic_cast<const mini_graph::CanvasPoint *>(group.GetChild(0));
    QVERIFY(replacedPoint);
    QVERIFY(replacedPoint->GetPosition() == mini_graph::CanvasVector(-5.0, 7.0));
    QVERIFY(clonedGroup->GetChild(0)->GetType()
            == mini_graph::CanvasEntityType::Point);
    const mini_graph::CanvasPoint *clonedOriginalPoint =
            dynamic_cast<const mini_graph::CanvasPoint *>(clonedGroup->GetChild(0));
    QVERIFY(clonedOriginalPoint);
    QVERIFY(clonedOriginalPoint->GetPosition()
            == mini_graph::CanvasVector(1.0, 2.0));

    QVERIFY(group.RemoveChild(0));
    QCOMPARE(group.GetChildCount(), 1);
    QVERIFY(group.IsValid());
    QVERIFY(group.RemoveChild(0));
    QVERIFY(!group.IsValid());
}

void CanvasCoreTest::CanvasGroupRejectsExcessiveNestingWithoutMutation()
{
    const int maximumDepth =
            mini_graph::CanvasGroupContainer::MaximumNestingDepth;
    QCOMPARE(maximumDepth, 64);
    QSharedPointer<mini_graph::CanvasEntity> nested(
            new mini_graph::CanvasPoint(mini_graph::CanvasVector(1.0, 2.0)));

    for (int depth = 1; depth <= maximumDepth; ++depth) {
        QSharedPointer<mini_graph::CanvasGroupContainer> parent(
                new mini_graph::CanvasGroupContainer);
        QVERIFY(parent->AddChild(*nested));
        nested = parent;
    }

    QVERIFY(nested->IsValid());
    const QSharedPointer<mini_graph::CanvasEntity> clone = nested->Clone();
    QVERIFY(clone);
    QVERIFY(clone->IsValid());

    const mini_graph::CanvasEntity *current = clone.data();
    for (int depth = 0; depth < maximumDepth; ++depth) {
        const mini_graph::CanvasGroupContainer *group =
                dynamic_cast<const mini_graph::CanvasGroupContainer *>(current);
        QVERIFY(group);
        QCOMPARE(group->GetChildCount(), 1);
        current = group->GetChild(0);
        QVERIFY(current);
    }
    QVERIFY(dynamic_cast<const mini_graph::CanvasPoint *>(current));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId id = document.AddEntity(*nested);
    QVERIFY(id != 0);
    const mini_graph::CanvasEntity *storedEntity = document.GetEntity(id);
    QVERIFY(storedEntity);
    QVERIFY(storedEntity != nested.data());
    QVERIFY(storedEntity->IsValid());
    QRectF nestedBounds;
    QVERIFY(mini_graph::TryGetGeometryBounds(*storedEntity, &nestedBounds));
    QCOMPARE(nestedBounds, QRectF(1.0, 2.0, 0.0, 0.0));

    mini_graph::CanvasGroupContainer tooDeep;
    QVERIFY(!tooDeep.AddChild(*nested));
    QCOMPARE(tooDeep.GetChildCount(), 0);

    mini_graph::CanvasGroupContainer replacementHost;
    const mini_graph::CanvasPoint marker(mini_graph::CanvasVector(8.0, 9.0));
    QVERIFY(replacementHost.AddChild(marker));
    const mini_graph::CanvasEntity *originalChild = replacementHost.GetChild(0);
    QVERIFY(!replacementHost.ReplaceChild(0, *nested));
    QCOMPARE(replacementHost.GetChildCount(), 1);
    QCOMPARE(replacementHost.GetChild(0), originalChild);

    mini_graph::CanvasGroupContainer wideGroup;
    for (int index = 0; index < 256; ++index) {
        QVERIFY(wideGroup.AddChild(mini_graph::CanvasPoint(
                                      mini_graph::CanvasVector(index, index))));
    }
    QVERIFY(wideGroup.IsValid());
    QCOMPARE(wideGroup.GetChildCount(), 256);
}

void CanvasCoreTest::WorldCoordinateApisUseCanvasVector()
{
    const mini_graph::CanvasVector origin(0.0, 0.0);
    mini_graph::StruPointGeometry pointGeometry;
    QVERIFY(pointGeometry.position.IsValid());
    QVERIFY(pointGeometry.position == origin);

    mini_graph::CanvasTransform transform;
    transform.SetCenter(origin);
    QVERIFY(transform.GetCenter() == origin);
    const QSizeF viewportSize(400.0, 300.0);
    const mini_graph::CanvasVector worldCenter = transform.MapToWorld(
                QPointF(200.0, 150.0), viewportSize);
    QVERIFY(worldCenter == origin);
    QCOMPARE(transform.MapFromWorld(origin, viewportSize), QPointF(200.0, 150.0));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(origin);
    QVERIFY(pointId != 0);
    QVERIFY(document.TranslateItems(QVector<mini_graph::ItemId>() << pointId,
                                    mini_graph::CanvasVector(3.0, -2.0)));
    QVERIFY(document.GetPointGeometry(pointId, &pointGeometry));
    const mini_graph::CanvasVector translated(3.0, -2.0);
    QVERIFY(pointGeometry.position == translated);

    const mini_graph::CanvasSnapResult snap = mini_graph::CanvasSnapper::FindSnap(
                &document, translated, 0.5, 1.0, true, false);
    QVERIFY(snap.IsValid());
    QVERIFY(snap.position == translated);

    mini_graph::CanvasRenderContext context(&document, transform, QSize(400, 300));
    QVERIFY(context.MapToWorld(QPointF(200.0, 150.0)) == origin);
    QCOMPARE(context.MapFromWorld(origin), QPointF(200.0, 150.0));

    mini_graph::CanvasView view;
    view.resize(200, 100);
    view.CenterOnWorld(origin);
    QVERIFY(view.MapToWorld(QPointF(100.0, 50.0)) == origin);
    QCOMPARE(view.MapFromWorld(origin), QPointF(100.0, 50.0));
}

void CanvasCoreTest::CanvasViewQueuesCanvasVectorWorldSignal()
{
    mini_graph::CanvasView view;
    const mini_graph::CanvasVector expected(12.5, -4.0);
    mini_graph::CanvasVector received;
    bool delivered = false;
    const QMetaObject::Connection connection = QObject::connect(
                &view, &mini_graph::CanvasView::MouseWorldPositionChanged,
                &view,
                [&received, &delivered](const mini_graph::CanvasVector &position) {
        received = position;
        delivered = true;
    }, Qt::QueuedConnection);
    QVERIFY(connection);

    emit view.MouseWorldPositionChanged(expected);
    QTRY_VERIFY(delivered);
    QVERIFY(received == expected);
}

void CanvasCoreTest::DefaultTransformIsIdentityLike()
{
    mini_graph::CanvasTransform transform;
    const QSizeF viewport(800.0, 600.0);

    QCOMPARE(transform.GetZoom(), 1.0);
    QVERIFY(transform.GetCenter() == mini_graph::CanvasVector(0.0, 0.0));
    QVERIFY(transform.MapToWorld(QPointF(400.0, 300.0), viewport)
            == mini_graph::CanvasVector(0.0, 0.0));
    QCOMPARE(transform.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0), viewport),
             QPointF(400.0, 300.0));
}

void CanvasCoreTest::MinimumZoomSupportsMicroScale()
{
    mini_graph::CanvasTransform transform;

    QVERIFY(qAbs(mini_graph::CanvasTransform::GetMinimumZoom() - 0.000001) < 0.000000000001);

    transform.SetZoom(0.0000001);
    QVERIFY(qAbs(transform.GetZoom() - 0.000001) < 0.000000000001);
}

void CanvasCoreTest::ZoomAtKeepsTheWorldPointUnderTheCursor()
{
    mini_graph::CanvasTransform transform;
    const QSizeF viewport(800.0, 600.0);
    const QPointF cursor(625.0, 225.0);

    const mini_graph::CanvasVector worldBefore = transform.MapToWorld(cursor, viewport);
    transform.ZoomAt(cursor, viewport, 2.0);
    const mini_graph::CanvasVector worldAfter = transform.MapToWorld(cursor, viewport);

    QVERIFY(qAbs(worldBefore.GetX() - worldAfter.GetX()) < 0.000001);
    QVERIFY(qAbs(worldBefore.GetY() - worldAfter.GetY()) < 0.000001);
    QCOMPARE(transform.GetZoom(), 2.0);
}

void CanvasCoreTest::PanByMovesWorldPointWithViewportDelta()
{
    mini_graph::CanvasTransform transform;
    const QSizeF viewport(800.0, 600.0);
    const mini_graph::CanvasVector worldPoint(35.0, -20.0);
    const QPointF viewportDelta(45.0, -18.0);

    const QPointF viewportBefore = transform.MapFromWorld(worldPoint, viewport);
    transform.PanBy(viewportDelta);
    const QPointF viewportAfter = transform.MapFromWorld(worldPoint, viewport);

    QVERIFY(qAbs((viewportAfter.x() - viewportBefore.x()) - viewportDelta.x()) < 0.000001);
    QVERIFY(qAbs((viewportAfter.y() - viewportBefore.y()) - viewportDelta.y()) < 0.000001);
}

void CanvasCoreTest::CanvasTransformRejectsNonFiniteInputsWithoutMutation()
{
    mini_graph::CanvasTransform transform;
    const mini_graph::CanvasVector originalCenter(12.0, -8.0);
    transform.SetCenter(originalCenter);
    transform.SetZoom(2.0);

    const qreal invalidValues[] = {
        std::numeric_limits<qreal>::quiet_NaN(),
        std::numeric_limits<qreal>::infinity(),
        -std::numeric_limits<qreal>::infinity(),
        0.0,
        -1.0
    };
    for (qreal value : invalidValues) {
        transform.SetZoom(value);
        QCOMPARE(transform.GetZoom(), 2.0);
    }

    const QPointF cursor(600.0, 200.0);
    const QSizeF viewport(800.0, 600.0);
    for (qreal value : invalidValues) {
        transform.ZoomAt(cursor, viewport, value);
        QCOMPARE(transform.GetZoom(), 2.0);
        QVERIFY(transform.GetCenter() == originalCenter);
    }
    transform.ZoomAt(cursor,
                     viewport,
                     std::numeric_limits<qreal>::max());
    QCOMPARE(transform.GetZoom(), 2.0);
    QVERIFY(transform.GetCenter() == originalCenter);

    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    transform.ZoomAt(QPointF(nan, 0.0), viewport, 2.0);
    transform.ZoomAt(cursor, QSizeF(infinity, 600.0), 2.0);
    QCOMPARE(transform.GetZoom(), 2.0);
    QVERIFY(transform.GetCenter() == originalCenter);

    transform.PanBy(QPointF(nan, 0.0));
    transform.PanBy(QPointF(infinity, 0.0));
    QCOMPARE(transform.GetZoom(), 2.0);
    QVERIFY(transform.GetCenter() == originalCenter);

    transform.SetZoom(mini_graph::CanvasTransform::GetMinimumZoom());
    transform.PanBy(QPointF(std::numeric_limits<qreal>::max(), 0.0));
    QVERIFY(transform.GetCenter() == originalCenter);

    mini_graph::CanvasTransform largeCoordinateTransform;
    const mini_graph::CanvasVector largeCenter(
                std::numeric_limits<qreal>::max() * 0.75, 0.0);
    largeCoordinateTransform.SetCenter(largeCenter);
    largeCoordinateTransform.SetZoom(2.0);
    largeCoordinateTransform.ZoomAt(cursor, viewport, 2.0);
    QCOMPARE(largeCoordinateTransform.GetZoom(), 4.0);
    QCOMPARE(largeCoordinateTransform.GetCenter().GetX(), largeCenter.GetX());
    QVERIFY(qAbs(largeCoordinateTransform.GetCenter().GetY() - 25.0) < 1.0e-12);
}

void CanvasCoreTest::CanvasTransformRejectsInvalidViewportDimensions()
{
    mini_graph::CanvasTransform transform;
    const mini_graph::CanvasVector originalCenter(12.0, -8.0);
    transform.SetCenter(originalCenter);
    transform.SetZoom(2.0);

    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    QVERIFY(!transform.MapToWorld(QPointF(nan, 0.0), QSizeF(800.0, 600.0)).IsValid());
    QCOMPARE(transform.MapFromWorld(mini_graph::CanvasVector(nan, 0.0),
                                    QSizeF(800.0, 600.0)),
             QPointF());
    const QSizeF invalidViewports[] = {
        QSizeF(0.0, 600.0),
        QSizeF(-1.0, 600.0),
        QSizeF(800.0, 0.0),
        QSizeF(800.0, -1.0),
        QSizeF(infinity, 600.0),
        QSizeF(800.0, nan)
    };

    for (const QSizeF &viewport : invalidViewports) {
        QVERIFY(!transform.MapToWorld(QPointF(100.0, 100.0), viewport).IsValid());
        QCOMPARE(transform.MapFromWorld(mini_graph::CanvasVector(3.0, 4.0),
                                        viewport),
                 QPointF());
        transform.ZoomAt(QPointF(600.0, 200.0), viewport, 2.0);
        QCOMPARE(transform.GetZoom(), 2.0);
        QVERIFY(transform.GetCenter() == originalCenter);
    }

    mini_graph::CanvasTransform overflowTransform;
    overflowTransform.SetCenter(mini_graph::CanvasVector(
                                    -std::numeric_limits<qreal>::max(), 0.0));
    QCOMPARE(overflowTransform.MapFromWorld(
                 mini_graph::CanvasVector(std::numeric_limits<qreal>::max(), 0.0),
                 QSizeF(800.0, 600.0)),
             QPointF());

    mini_graph::CanvasTransform mappedPointOverflowTransform;
    mappedPointOverflowTransform.SetZoom(std::numeric_limits<qreal>::max());
    QCOMPARE(mappedPointOverflowTransform.MapFromWorld(
                 mini_graph::CanvasVector(std::numeric_limits<qreal>::max(), 0.0),
                 QSizeF(800.0, 600.0)),
             QPointF());
}

void CanvasCoreTest::FitRectCentersAndFitsViewport()
{
    mini_graph::CanvasTransform transform;
    const QSizeF viewport(800.0, 600.0);

    QVERIFY(transform.FitRect(QRectF(-100.0, -50.0, 200.0, 100.0),
                              viewport,
                              40.0));
    QVERIFY(transform.GetCenter() == mini_graph::CanvasVector(0.0, 0.0));
    QVERIFY(qAbs(transform.GetZoom() - 3.6) < 0.000001);

    const QPointF topLeft = transform.MapFromWorld(
                mini_graph::CanvasVector(-100.0, 50.0), viewport);
    const QPointF bottomRight = transform.MapFromWorld(
                mini_graph::CanvasVector(100.0, -50.0), viewport);
    QVERIFY(qAbs(topLeft.x() - 40.0) < 0.000001);
    QVERIFY(qAbs(topLeft.y() - 120.0) < 0.000001);
    QVERIFY(qAbs(bottomRight.x() - 760.0) < 0.000001);
    QVERIFY(qAbs(bottomRight.y() - 480.0) < 0.000001);
}

void CanvasCoreTest::FitRectRejectsInvalidViewport()
{
    mini_graph::CanvasTransform transform;
    transform.SetCenter(mini_graph::CanvasVector(12.0, -8.0));
    transform.SetZoom(2.0);

    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    const QSizeF invalidViewports[] = {
        QSizeF(0.0, 600.0),
        QSizeF(-1.0, 600.0),
        QSizeF(800.0, 0.0),
        QSizeF(800.0, -1.0),
        QSizeF(infinity, 600.0),
        QSizeF(800.0, nan)
    };
    for (const QSizeF &viewport : invalidViewports) {
        QVERIFY(!transform.FitRect(QRectF(0.0, 0.0, 10.0, 10.0),
                                   viewport,
                                   40.0));
        QVERIFY(transform.GetCenter() == mini_graph::CanvasVector(12.0, -8.0));
        QCOMPARE(transform.GetZoom(), 2.0);
    }
}

void CanvasCoreTest::FitRectRejectsNonFiniteInputsWithoutMutation()
{
    mini_graph::CanvasTransform transform;
    const mini_graph::CanvasVector originalCenter(12.0, -8.0);
    transform.SetCenter(originalCenter);
    transform.SetZoom(2.0);

    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    const qreal invalidMargins[] = {nan, infinity, -infinity};
    for (qreal margin : invalidMargins) {
        QVERIFY(!transform.FitRect(QRectF(-10.0, -5.0, 20.0, 10.0),
                                   QSizeF(800.0, 600.0),
                                   margin));
        QCOMPARE(transform.GetZoom(), 2.0);
        QVERIFY(transform.GetCenter() == originalCenter);
    }

    const qreal maximum = std::numeric_limits<qreal>::max();
    QVERIFY(!transform.FitRect(QRectF(-maximum, -5.0, maximum * 2.0, 10.0),
                               QSizeF(800.0, 600.0),
                               40.0));
    QCOMPARE(transform.GetZoom(), 2.0);
    QVERIFY(transform.GetCenter() == originalCenter);
}

void CanvasCoreTest::DocumentTracksLinesAndBounds()
{
    mini_graph::CanvasDocument document;

    const mini_graph::ItemId firstId = document.AddLine(
                mini_graph::CanvasVector(-5.0, 2.0),
                mini_graph::CanvasVector(10.0, 8.0));
    const mini_graph::ItemId secondId = document.AddLine(
                mini_graph::CanvasVector(-2.0, -4.0),
                mini_graph::CanvasVector(3.0, 1.0));

    QVERIFY(firstId != secondId);
    QCOMPARE(document.GetItemCount(), 2);
    QCOMPARE(document.GetBounds(), QRectF(-5.0, -4.0, 15.0, 12.0));
    QCOMPARE(document.GetLinePrimitives().size(), 2);
}

void CanvasCoreTest::DocumentUpdatesGeometryByType()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(mini_graph::CanvasVector(1.0, 2.0));
    const mini_graph::ItemId lineId = document.AddLine(mini_graph::CanvasVector(0.0, 0.0),
                                                       mini_graph::CanvasVector(1.0, 1.0));
    const mini_graph::ItemId arcId = document.AddArc(mini_graph::CanvasVector(0.0, 0.0),
                                                     5.0,
                                                     0.0,
                                                     1.0);
    const mini_graph::ItemId circleId = document.AddCircle(
                mini_graph::CanvasVector(0.0, 0.0), 3.0);
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    mini_graph::StruPointGeometry pointGeometry;
    pointGeometry.position = mini_graph::CanvasVector(10.0, 20.0);
    QVERIFY(document.SetPointGeometry(pointId, pointGeometry));

    mini_graph::StruLineGeometry lineGeometry;
    lineGeometry.startPoint = mini_graph::CanvasVector(-4.0, -5.0);
    lineGeometry.endPoint = mini_graph::CanvasVector(6.0, 7.0);
    QVERIFY(document.SetLineGeometry(lineId, lineGeometry));

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = mini_graph::CanvasVector(8.0, 9.0);
    arcGeometry.radius = 12.0;
    arcGeometry.startAngle = 0.5;
    arcGeometry.endAngle = 2.0;
    arcGeometry.reversed = true;
    QVERIFY(document.SetArcGeometry(arcId, arcGeometry));

    mini_graph::StruCircleGeometry circleGeometry;
    circleGeometry.center = mini_graph::CanvasVector(-8.0, -9.0);
    circleGeometry.radius = 14.0;
    QVERIFY(document.SetCircleGeometry(circleId, circleGeometry));

    mini_graph::StruPointGeometry storedPoint;
    mini_graph::StruLineGeometry storedLine;
    mini_graph::StruArcGeometry storedArc;
    mini_graph::StruCircleGeometry storedCircle;
    QVERIFY(document.GetPointGeometry(pointId, &storedPoint));
    QVERIFY(document.GetLineGeometry(lineId, &storedLine));
    QVERIFY(document.GetArcGeometry(arcId, &storedArc));
    QVERIFY(document.GetCircleGeometry(circleId, &storedCircle));
    QCOMPARE(storedPoint.position, pointGeometry.position);
    QCOMPARE(storedLine.startPoint, lineGeometry.startPoint);
    QCOMPARE(storedLine.endPoint, lineGeometry.endPoint);
    QCOMPARE(storedArc.center, arcGeometry.center);
    QCOMPARE(storedArc.radius, arcGeometry.radius);
    QCOMPARE(storedArc.reversed, arcGeometry.reversed);
    QCOMPARE(storedCircle.center, circleGeometry.center);
    QCOMPARE(storedCircle.radius, circleGeometry.radius);
    QCOMPARE(changedSpy.count(), 4);

    QVERIFY(!document.SetArcGeometry(arcId, mini_graph::StruArcGeometry()));
    QVERIFY(!document.GetPointGeometry(999, &storedPoint));
    QVERIFY(!document.SetCircleGeometry(999, circleGeometry));
}

void CanvasCoreTest::DocumentCalculatesSelectedBounds()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId firstId = document.AddLine(
                mini_graph::CanvasVector(-10.0, -2.0),
                mini_graph::CanvasVector(4.0, 6.0));
    const mini_graph::ItemId secondId = document.AddCircle(
                mini_graph::CanvasVector(100.0, 100.0), 5.0);
    const mini_graph::ItemId thirdId = document.AddPoint(
                mini_graph::CanvasVector(-80.0, -80.0));

    QCOMPARE(document.GetBounds(QVector<mini_graph::ItemId>() << firstId),
             QRectF(-10.0, -2.0, 14.0, 8.0));

    document.SelectItem(firstId);
    document.SelectItem(secondId);
    QCOMPARE(document.GetBounds(document.GetSelectedItemIds()),
             QRectF(-10.0, -2.0, 115.0, 107.0));
    QVERIFY(document.GetBounds(QVector<mini_graph::ItemId>() << thirdId)
            == QRectF(-80.0, -80.0, 0.0, 0.0));
}

void CanvasCoreTest::DocumentChangesEmitSignals()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    const mini_graph::ItemId id = document.AddLine(mini_graph::CanvasVector(0.0, 0.0),
                                                  mini_graph::CanvasVector(1.0, 1.0));
    document.AddLine(mini_graph::CanvasVector(2.0, 2.0),
                     mini_graph::CanvasVector(3.0, 3.0));
    QVERIFY(document.RemoveItem(id));
    document.Clear();

    QCOMPARE(changedSpy.count(), 4);
    QCOMPARE(document.GetItemCount(), 0);
}

void CanvasCoreTest::DocumentDisplaySizeMutationsEmitChanged()
{
    mini_graph::CanvasDocument document;

    mini_graph::CanvasPoint point(mini_graph::CanvasVector(0.0, 0.0));
    mini_graph::StruCanvasStyle pointStyle = point.GetStyle();
    pointStyle.pointSize = 4.0;
    QVERIFY(point.SetStyle(pointStyle));
    const mini_graph::ItemId pointId = document.AddEntity(point);
    QVERIFY(pointId != 0);

    mini_graph::CanvasLine polylineSegment(
                mini_graph::CanvasVector(0.0, 10.0),
                mini_graph::CanvasVector(10.0, 10.0));
    mini_graph::StruCanvasStyle segmentStyle = polylineSegment.GetStyle();
    segmentStyle.strokeWidth = 2.0;
    QVERIFY(polylineSegment.SetStyle(segmentStyle));
    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(polylineSegment));
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);
    QVERIFY(polylineId != 0);

    mini_graph::CanvasPoint nestedPoint(mini_graph::CanvasVector(20.0, 0.0));
    mini_graph::StruCanvasStyle nestedPointStyle = nestedPoint.GetStyle();
    nestedPointStyle.pointSize = 3.0;
    QVERIFY(nestedPoint.SetStyle(nestedPointStyle));
    mini_graph::CanvasGroupContainer innerGroup;
    QVERIFY(innerGroup.AddChild(nestedPoint));
    mini_graph::CanvasGroupContainer outerGroup;
    QVERIFY(outerGroup.AddChild(innerGroup));
    const mini_graph::ItemId groupId = document.AddEntity(outerGroup);
    QVERIFY(groupId != 0);

    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);
    QVERIFY(changedSpy.isValid());

    bool pointStyleWasUpdatedWhenChangedWasEmitted = false;
    const QMetaObject::Connection pointStyleObserver = QObject::connect(
                &document, &mini_graph::CanvasDocument::Changed, &document,
                [&document, pointId,
                 &pointStyleWasUpdatedWhenChangedWasEmitted]() {
        mini_graph::StruCanvasStyle observedStyle;
        pointStyleWasUpdatedWhenChangedWasEmitted =
                document.GetItemStyle(pointId, &observedStyle)
                && observedStyle.pointSize == 24.0;
    });
    QVERIFY(pointStyleObserver);
    QVERIFY(document.GetItemStyle(pointId, &pointStyle));
    pointStyle.pointSize = 24.0;
    QVERIFY(document.SetItemStyle(pointId, pointStyle));
    QObject::disconnect(pointStyleObserver);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(pointStyleWasUpdatedWhenChangedWasEmitted);
    mini_graph::StruCanvasStyle storedPointStyle;
    QVERIFY(document.GetItemStyle(pointId, &storedPointStyle));
    QCOMPARE(storedPointStyle.pointSize, 24.0);
    QVERIFY(!document.SetItemStyle(pointId, pointStyle));
    QCOMPARE(changedSpy.count(), 1);

    QSharedPointer<mini_graph::CanvasEntity> polylineClone =
            document.GetEntity(polylineId)->Clone();
    mini_graph::CanvasPolyline *updatedPolyline =
            dynamic_cast<mini_graph::CanvasPolyline *>(polylineClone.data());
    QVERIFY(updatedPolyline);
    mini_graph::CanvasLine widerSegment(
                mini_graph::CanvasVector(0.0, 10.0),
                mini_graph::CanvasVector(10.0, 10.0));
    segmentStyle = widerSegment.GetStyle();
    segmentStyle.strokeWidth = 32.0;
    QVERIFY(widerSegment.SetStyle(segmentStyle));
    QVERIFY(updatedPolyline->SetSegment(0, widerSegment));
    QCOMPARE(changedSpy.count(), 1);
    bool polylineStyleWasUpdatedWhenChangedWasEmitted = false;
    const QMetaObject::Connection polylineStyleObserver = QObject::connect(
                &document, &mini_graph::CanvasDocument::Changed, &document,
                [&document, polylineId,
                 &polylineStyleWasUpdatedWhenChangedWasEmitted]() {
        const mini_graph::CanvasPolyline *observedPolyline =
                dynamic_cast<const mini_graph::CanvasPolyline *>(
                    document.GetEntity(polylineId));
        const mini_graph::CanvasLine *observedSegment = observedPolyline
                ? dynamic_cast<const mini_graph::CanvasLine *>(
                    observedPolyline->GetSegment(0))
                : nullptr;
        polylineStyleWasUpdatedWhenChangedWasEmitted = observedSegment
                && observedSegment->GetStyle().strokeWidth == 32.0;
    });
    QVERIFY(polylineStyleObserver);
    QVERIFY(document.ReplaceEntity(polylineId, *updatedPolyline));
    QObject::disconnect(polylineStyleObserver);
    QCOMPARE(changedSpy.count(), 2);
    QVERIFY(polylineStyleWasUpdatedWhenChangedWasEmitted);

    const mini_graph::CanvasPolyline *storedPolyline =
            dynamic_cast<const mini_graph::CanvasPolyline *>(
                document.GetEntity(polylineId));
    QVERIFY(storedPolyline);
    const mini_graph::CanvasLine *storedSegment =
            dynamic_cast<const mini_graph::CanvasLine *>(
                storedPolyline->GetSegment(0));
    QVERIFY(storedSegment);
    QCOMPARE(storedSegment->GetStyle().strokeWidth, 32.0);

    QSharedPointer<mini_graph::CanvasEntity> groupClone =
            document.GetEntity(groupId)->Clone();
    mini_graph::CanvasGroupContainer *updatedOuterGroup =
            dynamic_cast<mini_graph::CanvasGroupContainer *>(groupClone.data());
    QVERIFY(updatedOuterGroup);
    QSharedPointer<mini_graph::CanvasEntity> innerClone =
            updatedOuterGroup->GetChild(0)->Clone();
    mini_graph::CanvasGroupContainer *updatedInnerGroup =
            dynamic_cast<mini_graph::CanvasGroupContainer *>(innerClone.data());
    QVERIFY(updatedInnerGroup);

    mini_graph::CanvasPoint widerNestedPoint(
                mini_graph::CanvasVector(20.0, 0.0));
    nestedPointStyle = widerNestedPoint.GetStyle();
    nestedPointStyle.pointSize = 40.0;
    QVERIFY(widerNestedPoint.SetStyle(nestedPointStyle));
    QVERIFY(updatedInnerGroup->ReplaceChild(0, widerNestedPoint));
    QVERIFY(updatedOuterGroup->ReplaceChild(0, *updatedInnerGroup));
    QCOMPARE(changedSpy.count(), 2);
    bool nestedGroupStyleWasUpdatedWhenChangedWasEmitted = false;
    const QMetaObject::Connection nestedGroupStyleObserver = QObject::connect(
                &document, &mini_graph::CanvasDocument::Changed, &document,
                [&document, groupId,
                 &nestedGroupStyleWasUpdatedWhenChangedWasEmitted]() {
        const mini_graph::CanvasGroupContainer *observedOuterGroup =
                dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                    document.GetEntity(groupId));
        const mini_graph::CanvasGroupContainer *observedInnerGroup =
                observedOuterGroup
                ? dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                    observedOuterGroup->GetChild(0))
                : nullptr;
        const mini_graph::CanvasPoint *observedPoint = observedInnerGroup
                ? dynamic_cast<const mini_graph::CanvasPoint *>(
                    observedInnerGroup->GetChild(0))
                : nullptr;
        nestedGroupStyleWasUpdatedWhenChangedWasEmitted = observedPoint
                && observedPoint->GetStyle().pointSize == 40.0;
    });
    QVERIFY(nestedGroupStyleObserver);
    QVERIFY(document.ReplaceEntity(groupId, *updatedOuterGroup));
    QObject::disconnect(nestedGroupStyleObserver);
    QCOMPARE(changedSpy.count(), 3);
    QVERIFY(nestedGroupStyleWasUpdatedWhenChangedWasEmitted);

    const mini_graph::CanvasGroupContainer *storedOuterGroup =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                document.GetEntity(groupId));
    QVERIFY(storedOuterGroup);
    const mini_graph::CanvasGroupContainer *storedInnerGroup =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                storedOuterGroup->GetChild(0));
    QVERIFY(storedInnerGroup);
    const mini_graph::CanvasPoint *storedNestedPoint =
            dynamic_cast<const mini_graph::CanvasPoint *>(
                storedInnerGroup->GetChild(0));
    QVERIFY(storedNestedPoint);
    QCOMPARE(storedNestedPoint->GetStyle().pointSize, 40.0);

    QSharedPointer<mini_graph::CanvasEntity> groupWithAddedChildClone =
            document.GetEntity(groupId)->Clone();
    updatedOuterGroup = dynamic_cast<mini_graph::CanvasGroupContainer *>(
                groupWithAddedChildClone.data());
    QVERIFY(updatedOuterGroup);
    innerClone = updatedOuterGroup->GetChild(0)->Clone();
    updatedInnerGroup = dynamic_cast<mini_graph::CanvasGroupContainer *>(
                innerClone.data());
    QVERIFY(updatedInnerGroup);
    mini_graph::CanvasLine addedWideChild(
                mini_graph::CanvasVector(30.0, 0.0),
                mini_graph::CanvasVector(40.0, 0.0));
    mini_graph::StruCanvasStyle addedChildStyle = addedWideChild.GetStyle();
    addedChildStyle.strokeWidth = 48.0;
    QVERIFY(addedWideChild.SetStyle(addedChildStyle));
    QVERIFY(updatedInnerGroup->AddChild(addedWideChild));
    QVERIFY(updatedOuterGroup->ReplaceChild(0, *updatedInnerGroup));
    QVERIFY(document.ReplaceEntity(groupId, *updatedOuterGroup));
    QCOMPARE(changedSpy.count(), 4);
    storedOuterGroup = dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                document.GetEntity(groupId));
    QVERIFY(storedOuterGroup);
    storedInnerGroup = dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                storedOuterGroup->GetChild(0));
    QVERIFY(storedInnerGroup);
    QCOMPARE(storedInnerGroup->GetChildCount(), 2);
    const mini_graph::CanvasLine *storedAddedWideChild =
            dynamic_cast<const mini_graph::CanvasLine *>(
                storedInnerGroup->GetChild(1));
    QVERIFY(storedAddedWideChild);
    QCOMPARE(storedAddedWideChild->GetStyle().strokeWidth, 48.0);

    QSharedPointer<mini_graph::CanvasEntity> groupWithRemovedChildClone =
            document.GetEntity(groupId)->Clone();
    updatedOuterGroup = dynamic_cast<mini_graph::CanvasGroupContainer *>(
                groupWithRemovedChildClone.data());
    QVERIFY(updatedOuterGroup);
    innerClone = updatedOuterGroup->GetChild(0)->Clone();
    updatedInnerGroup = dynamic_cast<mini_graph::CanvasGroupContainer *>(
                innerClone.data());
    QVERIFY(updatedInnerGroup);
    QVERIFY(updatedInnerGroup->RemoveChild(1));
    QVERIFY(updatedOuterGroup->ReplaceChild(0, *updatedInnerGroup));
    QVERIFY(document.ReplaceEntity(groupId, *updatedOuterGroup));
    QCOMPARE(changedSpy.count(), 5);
    storedOuterGroup = dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                document.GetEntity(groupId));
    QVERIFY(storedOuterGroup);
    storedInnerGroup = dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                storedOuterGroup->GetChild(0));
    QVERIFY(storedInnerGroup);
    QCOMPARE(storedInnerGroup->GetChildCount(), 1);
}

void CanvasCoreTest::DocumentStoresMixedGeometryPools()
{
    mini_graph::CanvasDocument document;

    mini_graph::StruPointGeometry pointGeometry;
    pointGeometry.position = mini_graph::CanvasVector(1.0, 2.0);
    const mini_graph::ItemId pointId = document.AddPoint(pointGeometry);

    mini_graph::StruLineGeometry lineGeometry;
    lineGeometry.startPoint = mini_graph::CanvasVector(-2.0, 0.0);
    lineGeometry.endPoint = mini_graph::CanvasVector(3.0, 4.0);
    const mini_graph::ItemId lineId = document.AddLine(lineGeometry);

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = mini_graph::CanvasVector(10.0, 10.0);
    arcGeometry.radius = 5.0;
    arcGeometry.startAngle = 0.0;
    arcGeometry.endAngle = 1.5707963267948966;
    arcGeometry.reversed = false;
    const mini_graph::ItemId arcId = document.AddArc(arcGeometry);

    mini_graph::StruCircleGeometry circleGeometry;
    circleGeometry.center = mini_graph::CanvasVector(-10.0, -5.0);
    circleGeometry.radius = 2.0;
    const mini_graph::ItemId circleId = document.AddCircle(circleGeometry);

    QVERIFY(pointId != 0);
    QVERIFY(lineId != 0);
    QVERIFY(arcId != 0);
    QVERIFY(circleId != 0);
    QVERIFY(pointId != lineId);
    QVERIFY(lineId != arcId);
    QVERIFY(arcId != circleId);
    QCOMPARE(document.GetItemCount(), 4);
    QCOMPARE(document.GetPointPrimitives().size(), 1);
    QCOMPARE(document.GetLinePrimitives().size(), 1);
    QCOMPARE(document.GetArcPrimitives().size(), 1);
    QCOMPARE(document.GetCirclePrimitives().size(), 1);
    QCOMPARE(document.GetPointPrimitives().at(0).geometry.position,
             pointGeometry.position);
    QCOMPARE(document.GetLinePrimitives().at(0).geometry.startPoint,
             lineGeometry.startPoint);
    QCOMPARE(document.GetLinePrimitives().at(0).geometry.endPoint,
             lineGeometry.endPoint);
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.center,
             arcGeometry.center);
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.radius,
             arcGeometry.radius);
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.startAngle,
             arcGeometry.startAngle);
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.endAngle,
             arcGeometry.endAngle);
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.reversed,
             arcGeometry.reversed);
    QCOMPARE(document.GetCirclePrimitives().at(0).geometry.center,
             circleGeometry.center);
    QCOMPARE(document.GetCirclePrimitives().at(0).geometry.radius,
             circleGeometry.radius);
    QCOMPARE(static_cast<int>(document.GetItemType(pointId)),
             static_cast<int>(mini_graph::CanvasEntityType::Point));
    QCOMPARE(static_cast<int>(document.GetItemType(lineId)),
             static_cast<int>(mini_graph::CanvasEntityType::Line));
    QCOMPARE(static_cast<int>(document.GetItemType(arcId)),
             static_cast<int>(mini_graph::CanvasEntityType::Arc));
    QCOMPARE(static_cast<int>(document.GetItemType(circleId)),
             static_cast<int>(mini_graph::CanvasEntityType::Circle));
    QCOMPARE(document.GetItemIds(),
             QVector<mini_graph::ItemId>() << pointId << lineId << arcId << circleId);
}

void CanvasCoreTest::DocumentMixedGeometryBoundsAndOrder()
{
    mini_graph::CanvasDocument document;

    mini_graph::StruPointGeometry pointGeometry;
    pointGeometry.position = mini_graph::CanvasVector(30.0, 40.0);
    const mini_graph::ItemId pointId = document.AddPoint(pointGeometry);

    mini_graph::StruLineGeometry lineGeometry;
    lineGeometry.startPoint = mini_graph::CanvasVector(-5.0, -2.0);
    lineGeometry.endPoint = mini_graph::CanvasVector(2.0, 3.0);
    const mini_graph::ItemId lineId = document.AddLine(lineGeometry);

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = mini_graph::CanvasVector(10.0, 10.0);
    arcGeometry.radius = 5.0;
    arcGeometry.startAngle = 0.0;
    arcGeometry.endAngle = 1.5707963267948966;
    arcGeometry.reversed = false;
    const mini_graph::ItemId arcId = document.AddArc(arcGeometry);

    mini_graph::StruCircleGeometry circleGeometry;
    circleGeometry.center = mini_graph::CanvasVector(-10.0, -5.0);
    circleGeometry.radius = 2.0;
    const mini_graph::ItemId circleId = document.AddCircle(circleGeometry);

    QCOMPARE(document.GetBounds(), QRectF(-12.0, -7.0, 42.0, 47.0));

    document.SelectItems(QVector<mini_graph::ItemId>()
                             << pointId << lineId << arcId << circleId,
                         false,
                         false);
    QCOMPARE(document.GetSelectedItemCount(), 4);
    QVERIFY(document.RemoveItem(arcId));
    QCOMPARE(document.GetItemCount(), 3);
    QCOMPARE(static_cast<int>(document.GetItemType(arcId)),
             static_cast<int>(mini_graph::CanvasEntityType::Invalid));
    QCOMPARE(document.GetSelectedItemCount(), 3);
    QCOMPARE(document.GetItemIds(),
             QVector<mini_graph::ItemId>() << pointId << lineId << circleId);
    QCOMPARE(document.GetSelectedItemIds(),
             QVector<mini_graph::ItemId>() << pointId << lineId << circleId);
}

void CanvasCoreTest::DocumentOwnsCanvasDataAndGlobalData()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasLine source(mini_graph::CanvasVector(0.0, 0.0),
                                  mini_graph::CanvasVector(10.0, 0.0));
    const std::shared_ptr<TestCanvasData> sourceData =
            std::make_shared<TestCanvasData>();
    source.SetCanvasData(sourceData);
    const mini_graph::ItemId id = document.AddEntity(source);
    QVERIFY(id != 0);

    const mini_graph::CanvasEntity *storedEntity = document.GetEntity(id);
    QVERIFY(storedEntity);
    QVERIFY(storedEntity->GetCanvasData());
    QVERIFY(storedEntity->GetCanvasData().get() != sourceData.get());
    const TestCanvasData *storedData =
            dynamic_cast<const TestCanvasData *>(storedEntity->GetCanvasData().get());
    QVERIFY(storedData);
    QCOMPARE(storedData->BoundEntity(),
             const_cast<mini_graph::CanvasEntity *>(storedEntity));
    QCOMPARE(storedData->BoundId(), id);

    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);
    const std::shared_ptr<TestCanvasData> replacementData =
            std::make_shared<TestCanvasData>();
    QVERIFY(document.SetItemCanvasData(id, replacementData));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(replacementData->BoundEntity(),
             const_cast<mini_graph::CanvasEntity *>(storedEntity));
    QCOMPARE(replacementData->BoundId(), id);
    QCOMPARE(document.GetItemCanvasData(id).get(),
             static_cast<mini_graph::CanvasData *>(replacementData.get()));

    const std::shared_ptr<TestCanvasGlobalData> globalData =
            std::make_shared<TestCanvasGlobalData>();
    document.SetCanvasGlobalData(globalData);
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(document.GetCanvasGlobalData().get(),
             static_cast<mini_graph::CanvasGlobalData *>(globalData.get()));
    document.SetCanvasGlobalData(globalData);
    QCOMPARE(changedSpy.count(), 3);
}

void CanvasCoreTest::DocumentRejectsCanvasDataBindingConflictAtomically()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId firstId = document.AddPoint(
                mini_graph::CanvasVector(0.0, 0.0));
    const mini_graph::ItemId secondId = document.AddPoint(
                mini_graph::CanvasVector(10.0, 0.0));
    const std::shared_ptr<TestCanvasData> data =
            std::make_shared<TestCanvasData>();

    QVERIFY(firstId != 0);
    QVERIFY(secondId != 0);
    QVERIFY(document.SetItemCanvasData(firstId, data));
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    QVERIFY(!document.SetItemCanvasData(secondId, data));
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(document.GetItemCanvasData(firstId).get(),
             static_cast<mini_graph::CanvasData *>(data.get()));
    QVERIFY(!document.GetItemCanvasData(secondId));
    QCOMPARE(data->BoundEntity(),
             const_cast<mini_graph::CanvasEntity *>(document.GetEntity(firstId)));
}

void CanvasCoreTest::DocumentDetachesCanvasDataWhenEntitiesAreRemoved()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId replacedId = document.AddLine(
                mini_graph::CanvasVector(0.0, 0.0),
                mini_graph::CanvasVector(1.0, 0.0));
    const std::shared_ptr<TestCanvasData> replacedData =
            std::make_shared<TestCanvasData>();
    QVERIFY(document.SetItemCanvasData(replacedId, replacedData));

    const mini_graph::CanvasLine replacement(
                mini_graph::CanvasVector(0.0, 1.0),
                mini_graph::CanvasVector(1.0, 1.0));
    QVERIFY(document.ReplaceEntity(replacedId, replacement));
    QVERIFY(!replacedData->BoundEntity());

    const mini_graph::ItemId removedId = document.AddLine(
                mini_graph::CanvasVector(0.0, 2.0),
                mini_graph::CanvasVector(1.0, 2.0));
    const std::shared_ptr<TestCanvasData> removedData =
            std::make_shared<TestCanvasData>();
    QVERIFY(document.SetItemCanvasData(removedId, removedData));
    QVERIFY(document.RemoveItem(removedId));
    QVERIFY(!removedData->BoundEntity());

    const mini_graph::ItemId clearedId = document.AddLine(
                mini_graph::CanvasVector(0.0, 3.0),
                mini_graph::CanvasVector(1.0, 3.0));
    const std::shared_ptr<TestCanvasData> clearedData =
            std::make_shared<TestCanvasData>();
    QVERIFY(document.SetItemCanvasData(clearedId, clearedData));
    document.Clear();
    QVERIFY(!clearedData->BoundEntity());
}

void CanvasCoreTest::DocumentNotifiesCanvasDataMutations()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId id = document.AddLine(
                mini_graph::CanvasVector(0.0, 0.0),
                mini_graph::CanvasVector(10.0, 0.0));
    QVERIFY(id != 0);

    const std::shared_ptr<TestCanvasData> entityData =
            std::make_shared<TestCanvasData>();
    QVERIFY(document.SetItemCanvasData(id, entityData));

    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);
    entityData->SetCommands(QStringList()
                            << QStringLiteral("Point,1,2"));
    QVERIFY(document.NotifyItemCanvasDataChanged(id));
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!document.NotifyItemCanvasDataChanged(id + 100));
    QCOMPARE(changedSpy.count(), 1);

    const std::shared_ptr<TestCanvasGlobalData> globalData =
            std::make_shared<TestCanvasGlobalData>();
    document.SetCanvasGlobalData(globalData);
    QCOMPARE(changedSpy.count(), 2);
    globalData->SetPostCommands(QStringList()
                                << QStringLiteral("Point,3,4"));
    QVERIFY(document.NotifyCanvasGlobalDataChanged());
    QCOMPARE(changedSpy.count(), 3);

    document.SetCanvasGlobalData(std::shared_ptr<mini_graph::CanvasGlobalData>());
    QCOMPARE(changedSpy.count(), 4);
    QVERIFY(!document.NotifyCanvasGlobalDataChanged());
    QCOMPARE(changedSpy.count(), 4);
}

void CanvasCoreTest::DocumentOwnsCanvasEntityClonesAndBoundsAllTypes()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    mini_graph::CanvasPoint point(mini_graph::CanvasVector(1.0, 2.0));
    mini_graph::CanvasLine line(mini_graph::CanvasVector(0.0, 0.0),
                                mini_graph::CanvasVector(4.0, 4.0));
    mini_graph::CanvasArc arc(mini_graph::CanvasVector(4.0, 0.0),
                              2.0, 0.0, 1.5707963267948966);
    mini_graph::CanvasCircle circle(mini_graph::CanvasVector(10.0, 0.0), 1.0);
    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(20.0, 0.0),
                                mini_graph::CanvasVector(3.0, 0.0),
                                0.5, false));

    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(30.0, 0.0),
                                    mini_graph::CanvasVector(31.0, 0.0))));
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(31.0, 0.0),
                                    mini_graph::CanvasVector(32.0, 0.0))));

    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(1,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(40.0, 0.0)
                           << mini_graph::CanvasVector(42.0, 0.0),
                           QVector<qreal>() << 0.0 << 0.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, false));

    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(mini_graph::CanvasPoint(
                               mini_graph::CanvasVector(-10.0, -10.0))));

    QVector<mini_graph::ItemId> ids;
    ids.append(document.AddEntity(point));
    ids.append(document.AddEntity(line));
    ids.append(document.AddEntity(arc));
    ids.append(document.AddEntity(circle));
    ids.append(document.AddEntity(ellipse));
    ids.append(document.AddEntity(polyline));
    ids.append(document.AddEntity(spline));
    ids.append(document.AddEntity(group));
    const QVector<mini_graph::CanvasEntityType> expectedTypes =
            QVector<mini_graph::CanvasEntityType>()
            << mini_graph::CanvasEntityType::Point
            << mini_graph::CanvasEntityType::Line
            << mini_graph::CanvasEntityType::Arc
            << mini_graph::CanvasEntityType::Circle
            << mini_graph::CanvasEntityType::Ellipse
            << mini_graph::CanvasEntityType::Polyline
            << mini_graph::CanvasEntityType::Spline
            << mini_graph::CanvasEntityType::GroupContainer;

    QCOMPARE(ids.size(), 8);
    for (int index = 0; index < ids.size(); ++index) {
        QVERIFY(ids.at(index) != 0);
        const mini_graph::CanvasEntity *stored = document.GetEntity(ids.at(index));
        QVERIFY(stored);
        QCOMPARE(stored->GetId(), ids.at(index));
        QCOMPARE(stored->GetType(), expectedTypes.at(index));
    }
    QCOMPARE(document.GetItemIds(), ids);
    QCOMPARE(document.GetBounds(), QRectF(-10.0, -10.0, 52.0, 14.0));
    QCOMPARE(changedSpy.count(), 8);

    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(200.0, 200.0),
                                mini_graph::CanvasVector(3.0, 0.0),
                                0.5, false));
    QVERIFY(group.ReplaceChild(0, mini_graph::CanvasPoint(
                                   mini_graph::CanvasVector(500.0, 500.0))));
    const mini_graph::CanvasEllipse *storedEllipse =
            dynamic_cast<const mini_graph::CanvasEllipse *>(
                document.GetEntity(ids.at(4)));
    const mini_graph::CanvasGroupContainer *storedGroup =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                document.GetEntity(ids.at(7)));
    QVERIFY(storedEllipse);
    QVERIFY(storedGroup);
    QCOMPARE(storedEllipse->GetCenter(), mini_graph::CanvasVector(20.0, 0.0));
    const mini_graph::CanvasPoint *storedChild =
            dynamic_cast<const mini_graph::CanvasPoint *>(storedGroup->GetChild(0));
    QVERIFY(storedChild);
    QCOMPARE(storedChild->GetPosition(), mini_graph::CanvasVector(-10.0, -10.0));
    QCOMPARE(changedSpy.count(), 8);
}

void CanvasCoreTest::DocumentReplacesCanvasEntityWithoutChangingIdentity()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId firstId = document.AddEntity(
                mini_graph::CanvasPoint(mini_graph::CanvasVector(1.0, 2.0)));
    const mini_graph::ItemId secondId = document.AddEntity(
                mini_graph::CanvasLine(mini_graph::CanvasVector(0.0, 0.0),
                                       mini_graph::CanvasVector(2.0, 2.0)));
    QVERIFY(firstId != 0);
    QVERIFY(secondId != 0);
    document.SelectItem(firstId);
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);
    QSignalSpy selectionSpy(&document, &mini_graph::CanvasDocument::SelectionChanged);

    mini_graph::CanvasCircle replacement(mini_graph::CanvasVector(8.0, 9.0), 3.0);
    replacement.SetTag(QStringLiteral("replacement"));
    QVERIFY(document.ReplaceEntity(firstId, replacement));

    QCOMPARE(document.GetItemIds(),
             QVector<mini_graph::ItemId>() << firstId << secondId);
    QCOMPARE(document.GetItemType(firstId), mini_graph::CanvasEntityType::Circle);
    QCOMPARE(document.GetEntity(firstId)->GetId(), firstId);
    QCOMPARE(document.GetEntity(firstId)->GetTag(), QStringLiteral("replacement"));
    QVERIFY(document.IsItemSelected(firstId));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(selectionSpy.count(), 0);
    QCOMPARE(document.GetBounds(QVector<mini_graph::ItemId>() << firstId),
             QRectF(5.0, 6.0, 6.0, 6.0));
}

void CanvasCoreTest::DocumentRejectsInvalidCanvasEntitiesAtomically()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    mini_graph::CanvasCircle invalidCircle;
    QCOMPARE(document.AddEntity(invalidCircle), mini_graph::ItemId(0));
    mini_graph::CanvasCircle unrepresentableCircle(
                mini_graph::CanvasVector(1.0e308, 0.0), 1.0e308);
    QCOMPARE(document.AddEntity(unrepresentableCircle), mini_graph::ItemId(0));
    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(changedSpy.count(), 0);

    const mini_graph::ItemId id = document.AddEntity(
                mini_graph::CanvasPoint(mini_graph::CanvasVector(3.0, 4.0)));
    QVERIFY(id != 0);
    changedSpy.clear();
    QVERIFY(!document.ReplaceEntity(id, invalidCircle));
    QVERIFY(!document.ReplaceEntity(999, mini_graph::CanvasCircle(
                                       mini_graph::CanvasVector(0.0, 0.0), 1.0)));
    QCOMPARE(document.GetItemType(id), mini_graph::CanvasEntityType::Point);
    QCOMPARE(document.GetEntity(id)->GetId(), id);
    QCOMPARE(document.GetBounds(), QRectF(3.0, 4.0, 0.0, 0.0));
    QCOMPARE(changedSpy.count(), 0);
}

void CanvasCoreTest::DocumentTranslatesSplineAndGroupEntities()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(1,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(0.0, 0.0)
                           << mini_graph::CanvasVector(2.0, 2.0),
                           QVector<qreal>() << 0.0 << 0.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, false));
    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(mini_graph::CanvasPoint(
                               mini_graph::CanvasVector(5.0, 5.0))));
    const mini_graph::ItemId splineId = document.AddEntity(spline);
    const mini_graph::ItemId groupId = document.AddEntity(group);

    QVERIFY(document.TranslateItems(QVector<mini_graph::ItemId>()
                                    << splineId << groupId,
                                    mini_graph::CanvasVector(10.0, -3.0)));

    const mini_graph::CanvasSpline *translatedSpline =
            dynamic_cast<const mini_graph::CanvasSpline *>(document.GetEntity(splineId));
    const mini_graph::CanvasGroupContainer *translatedGroup =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(document.GetEntity(groupId));
    QVERIFY(translatedSpline);
    QVERIFY(translatedGroup);
    QCOMPARE(translatedSpline->GetControlPoints(),
             QVector<mini_graph::CanvasVector>()
             << mini_graph::CanvasVector(10.0, -3.0)
             << mini_graph::CanvasVector(12.0, -1.0));
    const mini_graph::CanvasPoint *translatedChild =
            dynamic_cast<const mini_graph::CanvasPoint *>(translatedGroup->GetChild(0));
    QVERIFY(translatedChild);
    QCOMPARE(translatedChild->GetPosition(), mini_graph::CanvasVector(15.0, 2.0));
}

void CanvasCoreTest::DocumentRejectsInvalidCircularGeometry()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    mini_graph::StruCircleGeometry zeroRadiusCircle;
    zeroRadiusCircle.center = mini_graph::CanvasVector(0.0, 0.0);
    zeroRadiusCircle.radius = 0.0;
    QCOMPARE(document.AddCircle(zeroRadiusCircle), mini_graph::ItemId(0));

    mini_graph::StruCircleGeometry negativeRadiusCircle;
    negativeRadiusCircle.center = mini_graph::CanvasVector(0.0, 0.0);
    negativeRadiusCircle.radius = -1.0;
    QCOMPARE(document.AddCircle(negativeRadiusCircle), mini_graph::ItemId(0));

    mini_graph::StruArcGeometry zeroSweepArc;
    zeroSweepArc.center = mini_graph::CanvasVector(0.0, 0.0);
    zeroSweepArc.radius = 1.0;
    zeroSweepArc.startAngle = 0.5;
    zeroSweepArc.endAngle = 0.5;
    zeroSweepArc.reversed = false;
    QCOMPARE(document.AddArc(zeroSweepArc), mini_graph::ItemId(0));

    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(changedSpy.count(), 0);
}

void CanvasCoreTest::DocumentStoresPolylineAndPreservesOrder()
{
    mini_graph::CanvasDocument document;

    const mini_graph::ItemId pointId = document.AddPoint(mini_graph::CanvasVector(0.0, 0.0));
    const QVector<mini_graph::CanvasVector> openVertices =
            QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(1.0, 2.0)
            << mini_graph::CanvasVector(4.0, 2.0);
    const mini_graph::ItemId openPolylineId = document.AddPolyline(openVertices);
    const mini_graph::ItemId lineId = document.AddLine(mini_graph::CanvasVector(5.0, 5.0),
                                                       mini_graph::CanvasVector(6.0, 6.0));

    mini_graph::StruPolylineGeometry closedGeometry;
    closedGeometry.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-1.0, -2.0)
            << mini_graph::CanvasVector(2.0, -2.0)
            << mini_graph::CanvasVector(2.0, 1.0);
    closedGeometry.closed = true;
    const mini_graph::ItemId closedPolylineId = document.AddPolyline(closedGeometry);

    QVERIFY(pointId != 0);
    QVERIFY(openPolylineId != 0);
    QVERIFY(lineId != 0);
    QVERIFY(closedPolylineId != 0);
    QCOMPARE(document.GetItemType(openPolylineId),
             mini_graph::CanvasEntityType::Polyline);
    QCOMPARE(document.GetItemType(closedPolylineId),
             mini_graph::CanvasEntityType::Polyline);
    QCOMPARE(document.GetPolylinePrimitives().size(), 2);
    QCOMPARE(document.GetItemIds(),
             QVector<mini_graph::ItemId>()
             << pointId << openPolylineId << lineId << closedPolylineId);

    mini_graph::StruPolylineGeometry storedGeometry;
    QVERIFY(document.GetPolylineGeometry(openPolylineId, &storedGeometry));
    QCOMPARE(storedGeometry.vertices, openVertices);
    QVERIFY(!storedGeometry.closed);
    QVERIFY(document.GetPolylineGeometry(closedPolylineId, &storedGeometry));
    QCOMPARE(storedGeometry.vertices, closedGeometry.vertices);
    QVERIFY(storedGeometry.closed);

    mini_graph::StruCanvasStyle style;
    style.strokeColor = QColor(20, 40, 60);
    style.selectedStrokeColor = QColor(80, 100, 120);
    style.strokeWidth = 3.0;
    QVERIFY(document.SetItemStyle(openPolylineId, style));
    mini_graph::StruCanvasStyle storedStyle;
    QVERIFY(document.GetItemStyle(openPolylineId, &storedStyle));
    QCOMPARE(storedStyle, style);

    document.SelectItem(closedPolylineId);
    QVERIFY(document.RemoveItem(closedPolylineId));
    QCOMPARE(document.GetItemType(closedPolylineId),
             mini_graph::CanvasEntityType::Invalid);
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QCOMPARE(document.GetPolylinePrimitives().size(), 1);
    document.Clear();
    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(document.GetPolylinePrimitives().size(), 0);
}

void CanvasCoreTest::DocumentRejectsInvalidPolylineGeometry()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    QCOMPARE(document.AddPolyline(QVector<mini_graph::CanvasVector>()
                                  << mini_graph::CanvasVector(0.0, 0.0)),
             mini_graph::ItemId(0));

    mini_graph::StruPolylineGeometry closedWithTwoVertices;
    closedWithTwoVertices.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(0.0, 0.0)
            << mini_graph::CanvasVector(1.0, 1.0);
    closedWithTwoVertices.closed = true;
    QCOMPARE(document.AddPolyline(closedWithTwoVertices), mini_graph::ItemId(0));

    mini_graph::StruPolylineGeometry validGeometry;
    validGeometry.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(0.0, 0.0)
            << mini_graph::CanvasVector(1.0, 1.0);
    const mini_graph::ItemId id = document.AddPolyline(validGeometry);
    QVERIFY(id != 0);
    changedSpy.clear();

    mini_graph::StruPolylineGeometry invalidUpdate = validGeometry;
    invalidUpdate.vertices.removeLast();
    QVERIFY(!document.SetPolylineGeometry(id, invalidUpdate));

    mini_graph::StruPolylineGeometry storedGeometry;
    QVERIFY(document.GetPolylineGeometry(id, &storedGeometry));
    QCOMPARE(storedGeometry.vertices, validGeometry.vertices);
    QCOMPARE(changedSpy.count(), 0);

    mini_graph::StruPolylineGeometry nonFiniteGeometry = validGeometry;
    nonFiniteGeometry.vertices[1].Invalidate();
    QCOMPARE(document.AddPolyline(nonFiniteGeometry), mini_graph::ItemId(0));
    QCOMPARE(document.GetItemCount(), 1);
    QCOMPARE(changedSpy.count(), 0);
}

void CanvasCoreTest::DocumentBoundsAndTranslatesPolyline()
{
    mini_graph::CanvasDocument document;
    mini_graph::StruPolylineGeometry geometry;
    geometry.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-2.0, 1.0)
            << mini_graph::CanvasVector(4.0, -5.0)
            << mini_graph::CanvasVector(10.0, 3.0);
    geometry.closed = true;
    const mini_graph::ItemId id = document.AddPolyline(geometry);

    QVERIFY(id != 0);
    QCOMPARE(document.GetBounds(), QRectF(-2.0, -5.0, 12.0, 8.0));
    QVERIFY(document.TranslateItems(QVector<mini_graph::ItemId>() << id,
                                    mini_graph::CanvasVector(3.0, -2.0)));

    mini_graph::StruPolylineGeometry translatedGeometry;
    QVERIFY(document.GetPolylineGeometry(id, &translatedGeometry));
    QCOMPARE(translatedGeometry.vertices,
             QVector<mini_graph::CanvasVector>()
             << mini_graph::CanvasVector(1.0, -1.0)
             << mini_graph::CanvasVector(7.0, -7.0)
             << mini_graph::CanvasVector(13.0, 1.0));
    QVERIFY(translatedGeometry.closed);
    QCOMPARE(document.GetBounds(), QRectF(1.0, -7.0, 12.0, 8.0));
}

void CanvasCoreTest::DocumentPreservesCanvasDataWhenTranslatingPolyline()
{
    mini_graph::CanvasPolyline polyline;
    mini_graph::CanvasLine firstSegment(mini_graph::CanvasVector(0.0, 0.0),
                                        mini_graph::CanvasVector(10.0, 0.0));
    mini_graph::CanvasLine secondSegment(mini_graph::CanvasVector(10.0, 0.0),
                                         mini_graph::CanvasVector(10.0, 10.0));
    firstSegment.SetCanvasData(std::make_shared<TestCanvasData>(
                                   nullptr,
                                   QStringList() << QStringLiteral("Point,1,1")));
    secondSegment.SetCanvasData(std::make_shared<TestCanvasData>(
                                    nullptr,
                                    QStringList() << QStringLiteral("Point,2,2")));
    QVERIFY(polyline.AddSegment(firstSegment));
    QVERIFY(polyline.AddSegment(secondSegment));
    polyline.SetCanvasData(std::make_shared<TestCanvasData>(
                               nullptr,
                               QStringList() << QStringLiteral("Point,3,3")));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId id = document.AddEntity(polyline);
    QVERIFY(id != 0);
    QVERIFY(document.TranslateItems(QVector<mini_graph::ItemId>() << id,
                                    mini_graph::CanvasVector(5.0, 7.0)));

    const mini_graph::CanvasPolyline *translated =
            dynamic_cast<const mini_graph::CanvasPolyline *>(document.GetEntity(id));
    QVERIFY(translated);
    QVERIFY(translated->GetCanvasData());
    QCOMPARE(translated->GetCanvasData()->CalculateToQStringList(),
             QStringList() << QStringLiteral("Point,3,3"));
    const TestCanvasData *parentData =
            dynamic_cast<const TestCanvasData *>(translated->GetCanvasData().get());
    QVERIFY(parentData);
    QCOMPARE(parentData->BoundEntity(),
             const_cast<mini_graph::CanvasPolyline *>(translated));

    for (int index = 0; index < translated->GetSegmentCount(); ++index) {
        const mini_graph::CanvasEntity *segment = translated->GetSegment(index);
        QVERIFY(segment);
        QVERIFY(segment->GetCanvasData());
        const TestCanvasData *segmentData =
                dynamic_cast<const TestCanvasData *>(segment->GetCanvasData().get());
        QVERIFY(segmentData);
        QCOMPARE(segmentData->BoundEntity(),
                 const_cast<mini_graph::CanvasEntity *>(segment));
    }
}

void CanvasCoreTest::DocumentPreservesCanvasDataWhenUpdatingPolylineGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId id = document.AddPolyline(
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(0.0, 0.0)
                << mini_graph::CanvasVector(10.0, 0.0)
                << mini_graph::CanvasVector(10.0, 10.0));
    QVERIFY(id != 0);

    QVERIFY(document.SetItemCanvasData(
                id, std::make_shared<TestCanvasData>(
                    nullptr, QStringList() << QStringLiteral("Point,4,5"))));

    mini_graph::StruPolylineGeometry geometry;
    geometry.vertices << mini_graph::CanvasVector(2.0, 3.0)
                      << mini_graph::CanvasVector(12.0, 3.0)
                      << mini_graph::CanvasVector(12.0, 13.0);
    QVERIFY(document.SetPolylineGeometry(id, geometry));

    const mini_graph::CanvasEntity *updated = document.GetEntity(id);
    QVERIFY(updated);
    QVERIFY(updated->GetCanvasData());
    QCOMPARE(updated->GetCanvasData()->CalculateToQStringList(),
             QStringList() << QStringLiteral("Point,4,5"));
    const TestCanvasData *data =
            dynamic_cast<const TestCanvasData *>(updated->GetCanvasData().get());
    QVERIFY(data);
    QCOMPARE(data->BoundEntity(), const_cast<mini_graph::CanvasEntity *>(updated));
}

void CanvasCoreTest::DocumentPreservesPolylineSegmentPropertiesWhenUpdatingGeometry()
{
    mini_graph::CanvasPolyline polyline;
    mini_graph::CanvasLine firstSegment(mini_graph::CanvasVector(0.0, 0.0),
                                        mini_graph::CanvasVector(10.0, 0.0));
    mini_graph::CanvasLine secondSegment(mini_graph::CanvasVector(10.0, 0.0),
                                         mini_graph::CanvasVector(10.0, 10.0));
    mini_graph::StruCanvasStyle style = firstSegment.GetStyle();
    style.strokeColor = QColor(12, 34, 56);
    QVERIFY(firstSegment.SetStyle(style));
    firstSegment.SetTag(QStringLiteral("first-edge"));
    firstSegment.SetCanvasData(std::make_shared<TestCanvasData>(
                                   nullptr,
                                   QStringList() << QStringLiteral("Point,6,7")));
    QVERIFY(polyline.AddSegment(firstSegment));
    QVERIFY(polyline.AddSegment(secondSegment));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId id = document.AddEntity(polyline);
    QVERIFY(id != 0);
    mini_graph::StruPolylineGeometry geometry;
    geometry.vertices << mini_graph::CanvasVector(1.0, 2.0)
                      << mini_graph::CanvasVector(11.0, 2.0)
                      << mini_graph::CanvasVector(11.0, 12.0);
    QVERIFY(document.SetPolylineGeometry(id, geometry));

    const mini_graph::CanvasPolyline *updated =
            dynamic_cast<const mini_graph::CanvasPolyline *>(document.GetEntity(id));
    QVERIFY(updated);
    const mini_graph::CanvasEntity *updatedFirstSegment = updated->GetSegment(0);
    QVERIFY(updatedFirstSegment);
    QCOMPARE(updatedFirstSegment->GetStyle().strokeColor, QColor(12, 34, 56));
    QCOMPARE(updatedFirstSegment->GetTag(), QStringLiteral("first-edge"));
    QVERIFY(updatedFirstSegment->GetCanvasData());
    QCOMPARE(updatedFirstSegment->GetCanvasData()->CalculateToQStringList(),
             QStringList() << QStringLiteral("Point,6,7"));
    const TestCanvasData *data = dynamic_cast<const TestCanvasData *>(
                updatedFirstSegment->GetCanvasData().get());
    QVERIFY(data);
    QCOMPARE(data->BoundEntity(),
             const_cast<mini_graph::CanvasEntity *>(updatedFirstSegment));
}

void CanvasCoreTest::CanvasViewIsolatesCustomRendererPainterState()
{
    mini_graph::CanvasDocument document;
    document.SetCanvasGlobalData(std::make_shared<TestCanvasGlobalData>(
                                     nullptr,
                                     QStringList(),
                                     QStringList()
                                     << QStringLiteral("Pen,0,255,0,255,10,1")
                                     << QStringLiteral("Point,0,0")));

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);
    PainterStateMutatingRenderer renderer;
    view.SetCustomRenderer(&renderer);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QColor gridPixel = image.pixelColor(100, 50);
    QVERIFY(gridPixel.red() > 30);

    const QColor globalPostPixel = image.pixelColor(100, 100);
    QVERIFY(globalPostPixel.green() > 180);
    QVERIFY(globalPostPixel.red() < 80);
}

void CanvasCoreTest::CanvasViewAntialiasesGlobalCanvasData()
{
    const QStringList preCommands = QStringList()
            << QStringLiteral("Pen,255,0,0,255,1,1")
            << QStringLiteral("Line,-45.5,-19.5,45.5,19.5");
    const QStringList postCommands = QStringList()
            << QStringLiteral("Pen,0,255,0,255,1,1")
            << QStringLiteral("Line,-45.5,19.5,45.5,-19.5");
    mini_graph::CanvasDocument document;
    document.SetCanvasGlobalData(std::make_shared<TestCanvasGlobalData>(
                                     nullptr, preCommands, postCommands));

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    bool foundRed = false;
    bool foundGreen = false;
    bool foundPartialRed = false;
    bool foundPartialGreen = false;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            foundRed = foundRed
                    || (color.red() > 0 && color.green() < 10 && color.blue() < 10);
            foundGreen = foundGreen
                    || (color.green() > 0 && color.red() < 10 && color.blue() < 10);
            foundPartialRed = foundPartialRed
                    || (color.red() > 0 && color.red() < 255
                        && color.green() < 10 && color.blue() < 10);
            foundPartialGreen = foundPartialGreen
                    || (color.green() > 0 && color.green() < 255
                        && color.red() < 10 && color.blue() < 10);
        }
    }
    QVERIFY(foundRed);
    QVERIFY(foundGreen);
    QVERIFY(foundPartialRed);
    QVERIFY(foundPartialGreen);
}

void CanvasCoreTest::DocumentSupportsParameterizedCreation()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, SIGNAL(Changed()));

    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(1.0, 2.0));
    const mini_graph::ItemId arcId = document.AddArc(
                                                     mini_graph::CanvasVector(3.0, 4.0),
                                                     5.0,
                                                     0.0,
                                                     1.5707963267948966,
                                                     true);
    const mini_graph::ItemId circleId = document.AddCircle(
                                                           mini_graph::CanvasVector(-2.0, -1.0),
                                                           6.0);

    QVERIFY(pointId != 0);
    QVERIFY(arcId != 0);
    QVERIFY(circleId != 0);
    QCOMPARE(document.GetItemCount(), 3);
    QVERIFY(document.GetPointPrimitives().at(0).geometry.position
            == mini_graph::CanvasVector(1.0, 2.0));
    QVERIFY(document.GetArcPrimitives().at(0).geometry.center
            == mini_graph::CanvasVector(3.0, 4.0));
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.radius, 5.0);
    QVERIFY(document.GetArcPrimitives().at(0).geometry.reversed);
    QVERIFY(document.GetCirclePrimitives().at(0).geometry.center
            == mini_graph::CanvasVector(-2.0, -1.0));
    QCOMPARE(document.GetCirclePrimitives().at(0).geometry.radius, 6.0);
    QCOMPARE(changedSpy.count(), 3);

    QCOMPARE(document.AddCircle(mini_graph::CanvasVector(0.0, 0.0), 0.0),
             mini_graph::ItemId(0));
    QCOMPARE(changedSpy.count(), 3);
}

void CanvasCoreTest::CanvasStyleHasStableDefaultsAndValueSemantics()
{
    const mini_graph::StruCanvasStyle style;

    QCOMPARE(style.strokeColor, QColor(Qt::white));
    QCOMPARE(style.selectedStrokeColor, QColor(165, 71, 71));
    QCOMPARE(style.strokeWidth, 1.0);
    QCOMPARE(style.pointSize, 6.0);
    QVERIFY(style.visible);
    QVERIFY(style == mini_graph::StruCanvasStyle());

    mini_graph::StruCanvasStyle changedStyle = style;
    changedStyle.strokeWidth = 2.0;
    QVERIFY(changedStyle != style);
}

void CanvasCoreTest::CanvasStyleValidationIsConsistentAcrossEntityAndDocument()
{
    mini_graph::StruCanvasStyle zeroPointSizeStyle;
    zeroPointSizeStyle.pointSize = 0.0;

    mini_graph::StruCanvasStyle invalidStrokeColorStyle;
    invalidStrokeColorStyle.strokeColor = QColor();

    mini_graph::StruCanvasStyle invalidSelectedColorStyle;
    invalidSelectedColorStyle.selectedStrokeColor = QColor();

    const mini_graph::StruCanvasStyle invalidStyles[] = {
        zeroPointSizeStyle,
        invalidStrokeColorStyle,
        invalidSelectedColorStyle
    };

    for (int index = 0;
         index < static_cast<int>(sizeof(invalidStyles)
                                  / sizeof(invalidStyles[0]));
         ++index) {
        mini_graph::CanvasPoint point(mini_graph::CanvasVector(0.0, 0.0));
        const mini_graph::StruCanvasStyle originalStyle = point.GetStyle();
        QVERIFY2(!point.SetStyle(invalidStyles[index]),
                 "CanvasEntity accepted a style rejected by CanvasDocument");
        QVERIFY(point.GetStyle() == originalStyle);

        mini_graph::CanvasDocument document;
        const mini_graph::ItemId pointId = document.AddEntity(point);
        QVERIFY(pointId != 0);
        QSignalSpy changedSpy(&document,
                              &mini_graph::CanvasDocument::Changed);
        QVERIFY(changedSpy.isValid());

        QVERIFY(!document.SetItemStyle(pointId, invalidStyles[index]));
        QCOMPARE(changedSpy.count(), 0);
        mini_graph::StruCanvasStyle storedStyle;
        QVERIFY(document.GetItemStyle(pointId, &storedStyle));
        QVERIFY(storedStyle == originalStyle);
    }

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(0.0, 0.0));
    QVERIFY(pointId != 0);
    mini_graph::StruCanvasStyle zeroWidthStyle;
    zeroWidthStyle.strokeWidth = 0.0;
    QVERIFY(document.SetItemStyle(pointId, zeroWidthStyle));
    mini_graph::StruCanvasStyle storedStyle;
    QVERIFY(document.GetItemStyle(pointId, &storedStyle));
    QCOMPARE(storedStyle.strokeWidth, 0.0);
}

void CanvasCoreTest::DocumentStoresAndUpdatesItemStyle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(1.0, 2.0));
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    mini_graph::StruCanvasStyle style;
    style.strokeColor = QColor(10, 20, 30);
    style.selectedStrokeColor = QColor(40, 50, 60);
    style.strokeWidth = 3.0;
    style.pointSize = 9.0;
    style.visible = false;

    QVERIFY(document.SetItemStyle(pointId, style));
    QCOMPARE(changedSpy.count(), 1);

    mini_graph::StruCanvasStyle storedStyle;
    QVERIFY(document.GetItemStyle(pointId, &storedStyle));
    QCOMPARE(storedStyle, style);
    QVERIFY(!document.SetItemStyle(pointId, style));
    QVERIFY(!document.GetItemStyle(0, &storedStyle));
    QVERIFY(!document.SetItemStyle(0, style));
}

void CanvasCoreTest::DocumentNormalizesLineStyleCompatibilityFields()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                                                       mini_graph::CanvasVector(0.0, 0.0),
                                                       mini_graph::CanvasVector(1.0, 1.0),
                                                       QColor(),
                                                       -2.0);
    QVERIFY(lineId != 0);

    const mini_graph::StruLinePrimitive &line = document.GetLinePrimitives().at(0);
    QCOMPARE(line.style.strokeColor, QColor(Qt::white));
    QCOMPARE(line.color, line.style.strokeColor);
    QCOMPARE(line.style.strokeWidth, 0.0);
    QCOMPARE(line.width, line.style.strokeWidth);
}

void CanvasCoreTest::DocumentRejectsNonFiniteLineWidths()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);
    const mini_graph::CanvasVector start(0.0, 0.0);
    const mini_graph::CanvasVector end(1.0, 1.0);

    const qreal invalidWidths[] = {
        std::numeric_limits<qreal>::quiet_NaN(),
        std::numeric_limits<qreal>::infinity()
    };

    for (qreal width : invalidWidths) {
        QCOMPARE(document.AddLine(start, end, QColor(Qt::white), width),
                 mini_graph::ItemId(0));
        QCOMPARE(document.GetItemCount(), 0);
        QCOMPARE(changedSpy.count(), 0);
    }
}

void CanvasCoreTest::GeometryArcSweepFollowsDirection()
{
    const qreal halfPi = 1.5707963267948966;

    mini_graph::StruArcGeometry counterClockwiseArc;
    counterClockwiseArc.center = mini_graph::CanvasVector(0.0, 0.0);
    counterClockwiseArc.radius = 10.0;
    counterClockwiseArc.startAngle = 0.0;
    counterClockwiseArc.endAngle = halfPi;
    counterClockwiseArc.reversed = false;

    QVERIFY(qAbs(mini_graph::GetArcSweep(counterClockwiseArc) - halfPi)
            < 0.000000000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, 0.0).GetX() - 10.0)
            < 0.000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, 0.0).GetY())
            < 0.000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, halfPi).GetX())
            < 0.000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, halfPi).GetY() - 10.0)
            < 0.000001);

    mini_graph::StruArcGeometry clockwiseArc;
    clockwiseArc.center = mini_graph::CanvasVector(0.0, 0.0);
    clockwiseArc.radius = 10.0;
    clockwiseArc.startAngle = halfPi;
    clockwiseArc.endAngle = 0.0;
    clockwiseArc.reversed = true;

    QVERIFY(qAbs(mini_graph::GetArcSweep(clockwiseArc) - halfPi)
            < 0.000000000001);
    QVERIFY(mini_graph::IsValidGeometry(clockwiseArc));
}

void CanvasCoreTest::GeometryAndCanvasArcUseConsistentSweepTolerance()
{
    const qreal pi = std::acos(-1.0);
    const qreal fullTurn = 2.0 * pi;
    const qreal tolerance = 1.0e-12;
    const qreal startAngle = 0.25;
    const qreal invalidSweeps[] = {
        0.0,
        fullTurn,
        -fullTurn,
        2.0 * fullTurn,
        -2.0 * fullTurn,
        fullTurn + 0.5 * tolerance,
        fullTurn - 0.5 * tolerance,
        -fullTurn + 0.5 * tolerance,
        -fullTurn - 0.5 * tolerance,
        0.5 * tolerance,
        -0.5 * tolerance
    };
    const bool directions[] = { false, true };

    for (qreal sweep : invalidSweeps) {
        mini_graph::CanvasArc entity;
        QVERIFY(!entity.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                    1.0, startAngle, startAngle + sweep));
        QVERIFY(!entity.IsValid());

        for (bool reversed : directions) {
            mini_graph::StruArcGeometry geometry;
            geometry.center = mini_graph::CanvasVector(0.0, 0.0);
            geometry.radius = 1.0;
            geometry.startAngle = startAngle;
            geometry.endAngle = startAngle + sweep;
            geometry.reversed = reversed;

            QVERIFY(!mini_graph::IsValidGeometry(geometry));
            QCOMPARE(mini_graph::GetArcSweep(geometry), 0.0);
            QVERIFY(mini_graph::TessellateArc(geometry, 0.01).isEmpty());
        }
    }

    const qreal validSweeps[] = {
        0.25,
        2.0 * tolerance,
        -2.0 * tolerance,
        fullTurn + 4.0 * tolerance,
        fullTurn - 4.0 * tolerance,
        -fullTurn + 4.0 * tolerance,
        -fullTurn - 4.0 * tolerance
    };

    for (qreal sweep : validSweeps) {
        mini_graph::CanvasArc entity;
        QVERIFY(entity.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                   1.0, startAngle, startAngle + sweep));
        QVERIFY(entity.IsValid());

        for (bool reversed : directions) {
            mini_graph::StruArcGeometry geometry;
            geometry.center = mini_graph::CanvasVector(0.0, 0.0);
            geometry.radius = 1.0;
            geometry.startAngle = startAngle;
            geometry.endAngle = startAngle + sweep;
            geometry.reversed = reversed;

            QVERIFY(mini_graph::IsValidGeometry(geometry));
            const qreal directedSweep = mini_graph::GetArcSweep(geometry);
            QVERIFY(directedSweep > tolerance);
            QVERIFY(directedSweep < fullTurn - tolerance);
            QVERIFY(!mini_graph::TessellateArc(geometry, 0.01).isEmpty());
        }
    }
}

void CanvasCoreTest::GeometryRejectsOverflowingArcAngleDifference()
{
    mini_graph::StruArcGeometry arc;
    arc.center = mini_graph::CanvasVector(0.0, 0.0);
    arc.radius = 1.0;
    arc.startAngle = -std::numeric_limits<qreal>::max();
    arc.endAngle = std::numeric_limits<qreal>::max();

    QVERIFY(!mini_graph::IsValidGeometry(arc));
    QCOMPARE(mini_graph::GetArcSweep(arc), 0.0);
    QVERIFY(mini_graph::TessellateArc(arc, 0.1).isEmpty());

    mini_graph::CanvasArc entity;
    QVERIFY(!entity.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                1.0, arc.startAngle, arc.endAngle));
    QVERIFY(!entity.IsValid());
}

void CanvasCoreTest::GeometryBoundsCoverAllPrimitiveTypes()
{
    mini_graph::StruPointGeometry point;
    point.position = mini_graph::CanvasVector(2.0, -3.0);
    QCOMPARE(mini_graph::GetGeometryBounds(point), QRectF(2.0, -3.0, 0.0, 0.0));

    mini_graph::StruLineGeometry line;
    line.startPoint = mini_graph::CanvasVector(-2.0, 1.0);
    line.endPoint = mini_graph::CanvasVector(4.0, -5.0);
    QCOMPARE(mini_graph::GetGeometryBounds(line), QRectF(-2.0, -5.0, 6.0, 6.0));

    mini_graph::StruArcGeometry arc;
    arc.center = mini_graph::CanvasVector(10.0, 10.0);
    arc.radius = 5.0;
    arc.startAngle = 0.0;
    arc.endAngle = 1.5707963267948966;
    arc.reversed = false;
    QCOMPARE(mini_graph::GetGeometryBounds(arc), QRectF(10.0, 10.0, 5.0, 5.0));

    mini_graph::StruCircleGeometry circle;
    circle.center = mini_graph::CanvasVector(-3.0, -4.0);
    circle.radius = 2.0;
    QCOMPARE(mini_graph::GetGeometryBounds(circle), QRectF(-5.0, -6.0, 4.0, 4.0));
}

void CanvasCoreTest::GeometryValidatesPolylineAndBounds()
{
    mini_graph::StruPolylineGeometry openGeometry;
    openGeometry.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(1.0, 2.0)
            << mini_graph::CanvasVector(5.0, 2.0)
            << mini_graph::CanvasVector(5.0, 8.0);
    QVERIFY(mini_graph::IsValidGeometry(openGeometry));
    QCOMPARE(mini_graph::GetGeometryBounds(openGeometry),
             QRectF(1.0, 2.0, 4.0, 6.0));

    mini_graph::StruPolylineGeometry closedGeometry = openGeometry;
    closedGeometry.closed = true;
    QVERIFY(mini_graph::IsValidGeometry(closedGeometry));
    QCOMPARE(mini_graph::GetGeometryBounds(closedGeometry),
             QRectF(1.0, 2.0, 4.0, 6.0));

    mini_graph::StruPolylineGeometry emptyGeometry;
    QVERIFY(!mini_graph::IsValidGeometry(emptyGeometry));

    mini_graph::StruPolylineGeometry oneVertexGeometry;
    oneVertexGeometry.vertices << mini_graph::CanvasVector(0.0, 0.0);
    QVERIFY(!mini_graph::IsValidGeometry(oneVertexGeometry));

    mini_graph::StruPolylineGeometry closedWithTwoVertices;
    closedWithTwoVertices.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(0.0, 0.0)
            << mini_graph::CanvasVector(1.0, 1.0);
    closedWithTwoVertices.closed = true;
    QVERIFY(!mini_graph::IsValidGeometry(closedWithTwoVertices));

    mini_graph::StruPolylineGeometry nonFiniteGeometry = openGeometry;
    nonFiniteGeometry.vertices[0].Invalidate();
    QVERIFY(!mini_graph::IsValidGeometry(nonFiniteGeometry));
}

void CanvasCoreTest::EntityGeometryBoundsCoverAllEntityTypes()
{
    QRectF bounds;

    const mini_graph::CanvasPoint point(mini_graph::CanvasVector(2.0, -3.0));
    QVERIFY(mini_graph::TryGetGeometryBounds(point, &bounds));
    QCOMPARE(bounds, QRectF(2.0, -3.0, 0.0, 0.0));

    const mini_graph::CanvasLine line(mini_graph::CanvasVector(-2.0, 1.0),
                                      mini_graph::CanvasVector(4.0, -5.0));
    QVERIFY(mini_graph::TryGetGeometryBounds(line, &bounds));
    QCOMPARE(bounds, QRectF(-2.0, -5.0, 6.0, 6.0));

    const qreal pi = std::acos(-1.0);
    const mini_graph::CanvasArc counterClockwiseArc(
                mini_graph::CanvasVector(10.0, 10.0), 5.0, 0.0, pi / 2.0);
    QVERIFY(mini_graph::TryGetGeometryBounds(counterClockwiseArc, &bounds));
    QCOMPARE(bounds, QRectF(10.0, 10.0, 5.0, 5.0));

    const mini_graph::CanvasArc unitQuarterArc(
                mini_graph::CanvasVector(0.0, 0.0), 1.0, 0.0, pi / 2.0);
    QVERIFY(mini_graph::TryGetGeometryBounds(unitQuarterArc, &bounds));
    QCOMPARE(bounds, QRectF(0.0, 0.0, 1.0, 1.0));

    const mini_graph::CanvasArc clockwiseArc(
                mini_graph::CanvasVector(10.0, 10.0), 5.0,
                0.0, 3.0 * pi / 2.0, true);
    QVERIFY(mini_graph::TryGetGeometryBounds(clockwiseArc, &bounds));
    QCOMPARE(bounds, QRectF(10.0, 5.0, 5.0, 5.0));

    const mini_graph::CanvasCircle circle(mini_graph::CanvasVector(-3.0, -4.0),
                                          2.0);
    QVERIFY(mini_graph::TryGetGeometryBounds(circle, &bounds));
    QCOMPARE(bounds, QRectF(-5.0, -6.0, 4.0, 4.0));

    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(1.0, -2.0),
                                mini_graph::CanvasVector(4.0, 0.0),
                                0.5, false));
    QVERIFY(mini_graph::TryGetGeometryBounds(ellipse, &bounds));
    QCOMPARE(bounds, QRectF(-3.0, -4.0, 8.0, 4.0));

    mini_graph::CanvasEllipse ellipseArc;
    QVERIFY(ellipseArc.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                   mini_graph::CanvasVector(4.0, 0.0),
                                   0.5, true, 0.0, pi / 2.0));
    QVERIFY(mini_graph::TryGetGeometryBounds(ellipseArc, &bounds));
    QCOMPARE(bounds, QRectF(0.0, 0.0, 4.0, 2.0));

    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(0.0, 0.0),
                                    mini_graph::CanvasVector(5.0, 0.0))));
    QVERIFY(polyline.AddSegment(mini_graph::CanvasArc(
                                    mini_graph::CanvasVector(0.0, 0.0),
                                    5.0, 0.0, pi / 2.0)));
    QVERIFY(mini_graph::TryGetGeometryBounds(polyline, &bounds));
    QCOMPARE(bounds, QRectF(0.0, 0.0, 5.0, 5.0));

    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(
                2,
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(0.0, 0.0)
                << mini_graph::CanvasVector(5.0, 10.0)
                << mini_graph::CanvasVector(10.0, 0.0),
                QVector<qreal>() << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0,
                QVector<qreal>() << 1.0 << 10.0 << 1.0,
                false, false));
    QVERIFY(mini_graph::TryGetGeometryBounds(spline, &bounds));
    QCOMPARE(bounds, QRectF(0.0, 0.0, 10.0, 10.0));
}

void CanvasCoreTest::EntityGeometryBoundsAreConservativeForCurvesAndGroups()
{
    QRectF bounds;
    const qreal pi = std::acos(-1.0);

    mini_graph::CanvasEllipse rotatedEllipse;
    QVERIFY(rotatedEllipse.SetGeometry(mini_graph::CanvasVector(1.0, -2.0),
                                       mini_graph::CanvasVector(4.0, 3.0),
                                       0.5, false));
    QVERIFY(mini_graph::TryGetGeometryBounds(rotatedEllipse, &bounds));
    const qreal expectedXRadius = std::sqrt(18.25);
    const qreal expectedYRadius = std::sqrt(13.0);
    QVERIFY(qAbs(bounds.left() - (1.0 - expectedXRadius)) < 1.0e-12);
    QVERIFY(qAbs(bounds.right() - (1.0 + expectedXRadius)) < 1.0e-12);
    QVERIFY(qAbs(bounds.top() - (-2.0 - expectedYRadius)) < 1.0e-12);
    QVERIFY(qAbs(bounds.bottom() - (-2.0 + expectedYRadius)) < 1.0e-12);

    mini_graph::CanvasEllipse clockwiseEllipseArc;
    QVERIFY(clockwiseEllipseArc.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                            mini_graph::CanvasVector(4.0, 0.0),
                                            0.5, true, 0.0, 3.0 * pi / 2.0));
    clockwiseEllipseArc.SetReversed(true);
    QVERIFY(mini_graph::TryGetGeometryBounds(clockwiseEllipseArc, &bounds));
    QCOMPARE(bounds, QRectF(0.0, -2.0, 4.0, 2.0));

    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(
                2,
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(-2.0, 1.0)
                << mini_graph::CanvasVector(5.0, 12.0)
                << mini_graph::CanvasVector(9.0, -4.0),
                QVector<qreal>() << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0,
                QVector<qreal>() << 1.0 << 8.0 << 1.0,
                false, false));

    mini_graph::CanvasGroupContainer nestedGroup;
    QVERIFY(nestedGroup.AddChild(spline));
    QVERIFY(nestedGroup.AddChild(mini_graph::CanvasCircle(
                                     mini_graph::CanvasVector(20.0, 0.0), 3.0)));

    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(mini_graph::CanvasPoint(
                               mini_graph::CanvasVector(-10.0, 2.0))));
    QVERIFY(group.AddChild(nestedGroup));
    QVERIFY(mini_graph::TryGetGeometryBounds(group, &bounds));
    QCOMPARE(bounds, QRectF(-10.0, -4.0, 33.0, 16.0));
}

void CanvasCoreTest::EntityGeometryBoundsRejectInvalidAndUnrepresentableResults()
{
    QRectF bounds(7.0, 8.0, 9.0, 10.0);
    const QRectF originalBounds = bounds;

    const mini_graph::CanvasPoint invalidPoint;
    QVERIFY(!mini_graph::TryGetGeometryBounds(invalidPoint, &bounds));
    QCOMPARE(bounds, originalBounds);
    QVERIFY(!mini_graph::TryGetGeometryBounds(invalidPoint, nullptr));

    const mini_graph::CanvasGroupContainer emptyGroup;
    QVERIFY(!mini_graph::TryGetGeometryBounds(emptyGroup, &bounds));
    QCOMPARE(bounds, originalBounds);

    const qreal maximum = std::numeric_limits<qreal>::max();
    const mini_graph::CanvasLine overflowingLine(
                mini_graph::CanvasVector(-maximum, 0.0),
                mini_graph::CanvasVector(maximum, 0.0));
    QVERIFY(overflowingLine.IsValid());
    QVERIFY(!mini_graph::TryGetGeometryBounds(overflowingLine, &bounds));
    QCOMPARE(bounds, originalBounds);

    const mini_graph::CanvasCircle overflowingCircle(
                mini_graph::CanvasVector(maximum, 0.0), maximum / 2.0);
    QVERIFY(overflowingCircle.IsValid());
    QVERIFY(!mini_graph::TryGetGeometryBounds(overflowingCircle, &bounds));
    QCOMPARE(bounds, originalBounds);
}

void CanvasCoreTest::GeometryTessellatesArcAndCircle()
{
    mini_graph::StruArcGeometry arc;
    arc.center = mini_graph::CanvasVector(1.0, -2.0);
    arc.radius = 10.0;
    arc.startAngle = 0.0;
    arc.endAngle = 1.5707963267948966;
    arc.reversed = false;

    const QVector<mini_graph::CanvasVector> arcPoints =
            mini_graph::TessellateArc(arc, 0.01);
    QVERIFY(arcPoints.size() >= 2);
    QVERIFY(arcPoints.first().DistanceTo(
                mini_graph::CalculateArcPoint(arc, arc.startAngle)) < 0.000001);
    QVERIFY(arcPoints.last().DistanceTo(
                mini_graph::CalculateArcPoint(arc, arc.endAngle)) < 0.000001);
    for (const mini_graph::CanvasVector &point : arcPoints) {
        QVERIFY(qAbs(arc.center.DistanceTo(point) - arc.radius) < 0.01);
    }

    mini_graph::StruCircleGeometry circle;
    circle.center = mini_graph::CanvasVector(4.0, 5.0);
    circle.radius = 12.0;
    const QVector<mini_graph::CanvasVector> circlePoints =
            mini_graph::TessellateCircle(circle, 0.01);
    QVERIFY(circlePoints.size() >= 17);
    QCOMPARE(circlePoints.first(), circlePoints.last());
    for (const mini_graph::CanvasVector &point : circlePoints) {
        QVERIFY(qAbs(circle.center.DistanceTo(point) - circle.radius) < 0.01);
    }
}

void CanvasCoreTest::CanvasRenderContextProvidesWorldHelpers()
{
    mini_graph::CanvasTransform transform;
    transform.SetZoom(2.0);
    transform.SetCenter(mini_graph::CanvasVector(10.0, -5.0));

    const mini_graph::CanvasRenderContext context(nullptr,
                                                  transform,
                                                  QSize(800, 600));
    QVERIFY(context.MapToWorld(QPointF(400.0, 300.0))
            == mini_graph::CanvasVector(10.0, -5.0));
    QCOMPARE(context.MapFromWorld(mini_graph::CanvasVector(10.0, -5.0)),
             QPointF(400.0, 300.0));
    QCOMPARE(context.GetVisibleWorldRect(), QRectF(-190.0, -155.0, 400.0, 300.0));
}

void CanvasCoreTest::CanvasRendererSupportsBackgroundLayer()
{
    TestCanvasRenderer renderer;
    QImage image(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    const mini_graph::CanvasRenderContext context;
    renderer.RenderBackground(painter, context);
    painter.end();

    QCOMPARE(renderer.backgroundCount, 1);
}

void CanvasCoreTest::CanvasRendererSupportsPainterGeometryLayer()
{
    TestCanvasRenderer renderer;
    QImage image(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    const mini_graph::CanvasRenderContext context;
    renderer.Render(painter, context);
    painter.end();

    QCOMPARE(renderer.renderCount, 1);
}

void CanvasCoreTest::UmbrellaHeaderExposesCanvasApi()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasTransform transform;
    mini_graph::CanvasRenderContext context(&document,
                                            transform,
                                            QSize(320, 240));

    QVERIFY(context.document == &document);
    QCOMPARE(context.viewportSize, QSize(320, 240));
}

void CanvasCoreTest::CanvasRendererRequestUpdateEmitsSignal()
{
    TestCanvasRenderer renderer;
    QSignalSpy updateSpy(&renderer,
                         &mini_graph::CanvasRenderer::UpdateRequested);

    renderer.RequestUpdate();

    QCOMPARE(updateSpy.count(), 1);
}

void CanvasCoreTest::BasicCustomRendererDrawsPainterLayers()
{
    BasicCustomRenderer renderer;
    mini_graph::CanvasTransform transform;
    const mini_graph::CanvasRenderContext context(nullptr,
                                                  transform,
                                                  QSize(320, 240));
    QImage image(QSize(320, 240), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);

    QPainter painter(&image);
    renderer.RenderBackground(painter, context);
    const QPointF anchor = context.MapFromWorld(mini_graph::CanvasVector(0.0, 25.0));
    QVERIFY(image.pixelColor(qRound(anchor.x()), qRound(anchor.y())) != Qt::black);

    renderer.RenderOverlay(painter, context);
    painter.end();

    QVERIFY(image.pixelColor(16, 22) != Qt::black);
}

void CanvasCoreTest::CanvasSnapperPrefersVisibleGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(3.0, 3.0),
                mini_graph::CanvasVector(20.0, 3.0));

    const mini_graph::CanvasSnapResult result = mini_graph::CanvasSnapper::FindSnap(
                &document,
                mini_graph::CanvasVector(3.4, 3.2),
                1.0,
                10.0,
                true,
                true);
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::LineEndpoint));
    QCOMPARE(result.itemId, lineId);
    QVERIFY(result.position == mini_graph::CanvasVector(3.0, 3.0));

    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(lineId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(lineId, style));

    const mini_graph::CanvasSnapResult hiddenResult = mini_graph::CanvasSnapper::FindSnap(
                &document,
                mini_graph::CanvasVector(3.4, 3.2),
                1.0,
                10.0,
                true,
                false);
    QCOMPARE(static_cast<int>(hiddenResult.type),
             static_cast<int>(mini_graph::CanvasSnapType::None));
}

void CanvasCoreTest::CanvasSnapperRejectsNonFiniteDistances()
{
    mini_graph::CanvasDocument document;
    const qreal maximum = std::numeric_limits<qreal>::max();
    QVERIFY(document.AddPoint(mini_graph::CanvasVector(maximum, 0.0)) != 0);

    const mini_graph::CanvasSnapResult result = mini_graph::CanvasSnapper::FindSnap(
                &document,
                mini_graph::CanvasVector(-maximum, 0.0),
                1.0,
                10.0,
                true,
                false);
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::None));
}

void CanvasCoreTest::CanvasSnapperSnapsToGrid()
{
    const mini_graph::CanvasSnapResult result = mini_graph::CanvasSnapper::FindSnap(
                nullptr,
                mini_graph::CanvasVector(4.6, 5.2),
                1.0,
                10.0,
                false,
                true);
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::Grid));
    QCOMPARE(result.itemId, static_cast<mini_graph::ItemId>(0));
    QVERIFY(result.position == mini_graph::CanvasVector(0.0, 10.0));
}

void CanvasCoreTest::CanvasSnapperSnapsToPolylineVertices()
{
    mini_graph::CanvasDocument document;
    const QVector<mini_graph::CanvasVector> vertices =
            QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(1.0, 2.0)
            << mini_graph::CanvasVector(5.0, 6.0)
            << mini_graph::CanvasVector(9.0, 2.0);
    const mini_graph::ItemId polylineId = document.AddPolyline(vertices);

    QVERIFY(polylineId != 0);
    const mini_graph::CanvasSnapResult result = mini_graph::CanvasSnapper::FindSnap(
                &document,
                mini_graph::CanvasVector(5.1, 6.05),
                0.25,
                1.0,
                true,
                false);
    QVERIFY(result.IsValid());
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::PolylineVertex));
    QCOMPARE(result.itemId, polylineId);
    QVERIFY(result.position == vertices.at(1));

    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(polylineId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(polylineId, style));
    const mini_graph::CanvasSnapResult hiddenResult = mini_graph::CanvasSnapper::FindSnap(
                &document,
                mini_graph::CanvasVector(5.1, 6.05),
                0.25,
                1.0,
                true,
                false);
    QVERIFY(!hiddenResult.IsValid());
}

void CanvasCoreTest::CanvasSnapperSnapsToEllipseAndSplineEntities()
{
    mini_graph::CanvasDocument ellipseDocument;
    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(10.0, 20.0),
                                mini_graph::CanvasVector(8.0, 0.0),
                                0.5, false));
    const mini_graph::ItemId ellipseId = ellipseDocument.AddEntity(ellipse);
    QVERIFY(ellipseId != 0);

    const mini_graph::CanvasSnapResult ellipseResult =
            mini_graph::CanvasSnapper::FindSnap(
                &ellipseDocument,
                mini_graph::CanvasVector(18.05, 20.0),
                0.1,
                1.0,
                true,
                false);
    QVERIFY(ellipseResult.IsValid());
    QCOMPARE(ellipseResult.type, mini_graph::CanvasSnapType::EllipseAxisPoint);
    QCOMPARE(ellipseResult.itemId, ellipseId);
    QVERIFY(ellipseResult.position == mini_graph::CanvasVector(18.0, 20.0));

    mini_graph::CanvasDocument ellipseArcDocument;
    mini_graph::CanvasEllipse ellipseArc;
    QVERIFY(ellipseArc.SetGeometry(mini_graph::CanvasVector(20.0, 30.0),
                                   mini_graph::CanvasVector(8.0, 0.0),
                                   0.5, true, 0.0,
                                   1.5707963267948966));
    const mini_graph::ItemId ellipseArcId =
            ellipseArcDocument.AddEntity(ellipseArc);
    QVERIFY(ellipseArcId != 0);
    const mini_graph::CanvasVector ellipseArcEnd(20.0, 34.0);
    const mini_graph::CanvasSnapResult ellipseArcResult =
            mini_graph::CanvasSnapper::FindSnap(
                &ellipseArcDocument,
                ellipseArcEnd + mini_graph::CanvasVector(0.05, 0.0),
                0.1,
                1.0,
                true,
                false);
    QVERIFY(ellipseArcResult.IsValid());
    QCOMPARE(ellipseArcResult.type,
             mini_graph::CanvasSnapType::EllipseEndpoint);
    QCOMPARE(ellipseArcResult.itemId, ellipseArcId);
    QVERIFY(ellipseArcResult.position == ellipseArcEnd);

    mini_graph::CanvasDocument splineDocument;
    mini_graph::CanvasSpline spline;
    const QVector<mini_graph::CanvasVector> controlPoints =
            QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(0.0, 0.0)
            << mini_graph::CanvasVector(10.0, 0.0)
            << mini_graph::CanvasVector(20.0, 0.0)
            << mini_graph::CanvasVector(30.0, 0.0);
    QVERIFY(spline.SetData(2, controlPoints,
                           QVector<qreal>() << 0.0 << 1.0 << 2.0 << 3.0
                                            << 4.0 << 5.0 << 6.0,
                           QVector<qreal>(), false, false));
    const mini_graph::ItemId splineId = splineDocument.AddEntity(spline);
    QVERIFY(splineId != 0);

    const mini_graph::CanvasSnapResult splineResult =
            mini_graph::CanvasSnapper::FindSnap(
                &splineDocument,
                mini_graph::CanvasVector(5.05, 0.0),
                0.1,
                1.0,
                true,
                false);
    QVERIFY(splineResult.IsValid());
    QCOMPARE(splineResult.type, mini_graph::CanvasSnapType::SplineEndpoint);
    QCOMPARE(splineResult.itemId, splineId);
    QVERIFY(splineResult.position == mini_graph::CanvasVector(5.0, 0.0));
}

void CanvasCoreTest::CanvasSnapperTraversesGroupsAndArcPolylines()
{
    mini_graph::CanvasDocument groupDocument;
    mini_graph::CanvasLine childLine(mini_graph::CanvasVector(40.0, 50.0),
                                     mini_graph::CanvasVector(60.0, 50.0));
    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(childLine));
    const mini_graph::ItemId groupId = groupDocument.AddEntity(group);
    QVERIFY(groupId != 0);

    const mini_graph::CanvasSnapResult groupResult =
            mini_graph::CanvasSnapper::FindSnap(
                &groupDocument,
                mini_graph::CanvasVector(40.05, 50.0),
                0.1,
                1.0,
                true,
                false);
    QVERIFY(groupResult.IsValid());
    QCOMPARE(groupResult.type, mini_graph::CanvasSnapType::LineEndpoint);
    QCOMPARE(groupResult.itemId, groupId);
    QVERIFY(groupResult.position == mini_graph::CanvasVector(40.0, 50.0));

    mini_graph::StruCanvasStyle hiddenStyle = group.GetStyle();
    hiddenStyle.visible = false;
    QVERIFY(group.SetStyle(hiddenStyle));
    mini_graph::CanvasDocument hiddenGroupDocument;
    const mini_graph::ItemId hiddenGroupId =
            hiddenGroupDocument.AddEntity(group);
    QVERIFY(hiddenGroupId != 0);
    const mini_graph::CanvasSnapResult hiddenGroupResult =
            mini_graph::CanvasSnapper::FindSnap(
                &hiddenGroupDocument,
                mini_graph::CanvasVector(40.05, 50.0),
                0.1,
                1.0,
                true,
                false);
    QVERIFY(!hiddenGroupResult.IsValid());

    mini_graph::CanvasLine hiddenChildLine(
                mini_graph::CanvasVector(40.0, 50.0),
                mini_graph::CanvasVector(60.0, 50.0));
    mini_graph::StruCanvasStyle hiddenChildStyle = hiddenChildLine.GetStyle();
    hiddenChildStyle.visible = false;
    QVERIFY(hiddenChildLine.SetStyle(hiddenChildStyle));
    mini_graph::CanvasGroupContainer visibleGroup;
    QVERIFY(visibleGroup.AddChild(hiddenChildLine));
    mini_graph::CanvasDocument hiddenChildDocument;
    QVERIFY(hiddenChildDocument.AddEntity(visibleGroup) != 0);
    const mini_graph::CanvasSnapResult hiddenChildResult =
            mini_graph::CanvasSnapper::FindSnap(
                &hiddenChildDocument,
                mini_graph::CanvasVector(40.05, 50.0),
                0.1,
                1.0,
                true,
                false);
    QVERIFY(!hiddenChildResult.IsValid());

    mini_graph::CanvasDocument polylineDocument;
    mini_graph::CanvasPolyline polyline;
    const mini_graph::CanvasArc arc(mini_graph::CanvasVector(100.0, 0.0),
                                    10.0,
                                    0.0,
                                    1.5707963267948966);
    QVERIFY(polyline.AddSegment(arc));
    const mini_graph::ItemId polylineId = polylineDocument.AddEntity(polyline);
    QVERIFY(polylineId != 0);
    const mini_graph::CanvasSnapResult polylineResult =
            mini_graph::CanvasSnapper::FindSnap(
                &polylineDocument,
                arc.GetEndPoint() + mini_graph::CanvasVector(0.05, 0.0),
                0.1,
                1.0,
                true,
                false);
    QVERIFY(polylineResult.IsValid());
    QCOMPARE(polylineResult.type, mini_graph::CanvasSnapType::PolylineVertex);
    QCOMPARE(polylineResult.itemId, polylineId);
    QVERIFY(polylineResult.position == arc.GetEndPoint());
}

void CanvasCoreTest::CanvasSnapperSkipsInvisiblePolylineSegments()
{
    const qreal pi = std::acos(-1.0);
    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(-80.0, 0.0),
                                    mini_graph::CanvasVector(-40.0, 0.0))));

    mini_graph::CanvasLine hiddenLine(mini_graph::CanvasVector(-40.0, 0.0),
                                      mini_graph::CanvasVector(-20.0, 0.0));
    mini_graph::StruCanvasStyle hiddenLineStyle = hiddenLine.GetStyle();
    hiddenLineStyle.visible = false;
    QVERIFY(hiddenLine.SetStyle(hiddenLineStyle));
    QVERIFY(polyline.AddSegment(hiddenLine));

    mini_graph::CanvasArc hiddenArc(mini_graph::CanvasVector(-10.0, 0.0),
                                    10.0, pi, 2.0 * pi);
    mini_graph::StruCanvasStyle hiddenArcStyle = hiddenArc.GetStyle();
    hiddenArcStyle.visible = false;
    QVERIFY(hiddenArc.SetStyle(hiddenArcStyle));
    QVERIFY(polyline.AddSegment(hiddenArc));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);
    QVERIFY(polylineId != 0);

    const mini_graph::CanvasSnapResult hiddenArcCenter =
            mini_graph::CanvasSnapper::FindSnap(
                &document, mini_graph::CanvasVector(-10.0, 0.0),
                0.01, 1.0, true, false);
    QVERIFY(!hiddenArcCenter.IsValid());

    const mini_graph::CanvasSnapResult hiddenEndpoint =
            mini_graph::CanvasSnapper::FindSnap(
                &document, mini_graph::CanvasVector(0.0, 0.0),
                0.01, 1.0, true, false);
    QVERIFY(!hiddenEndpoint.IsValid());

    const mini_graph::CanvasSnapResult visibleEndpoint =
            mini_graph::CanvasSnapper::FindSnap(
                &document, mini_graph::CanvasVector(-80.0, 0.0),
                0.01, 1.0, true, false);
    QVERIFY(visibleEndpoint.IsValid());
    QCOMPARE(visibleEndpoint.itemId, polylineId);
}

void CanvasCoreTest::CanvasViewReportsCurrentSnap()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(3.0, 3.0),
                mini_graph::CanvasVector(20.0, 3.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    view.SetSnapEnabled(true);

    const QPointF mousePosition = view.MapFromWorld(
                mini_graph::CanvasVector(3.4, 3.2));
    QMouseEvent move(QEvent::MouseMove,
                     mousePosition,
                     mousePosition,
                     mousePosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasSnapResult result = view.GetCurrentSnapResult();
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::LineEndpoint));
    QCOMPARE(result.itemId, lineId);
    QVERIFY(result.position == mini_graph::CanvasVector(3.0, 3.0));

    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);
    QVERIFY(view.IsSnapEnabled());
    QCOMPARE(view.GetCurrentSnapResult().type, mini_graph::CanvasSnapType::None);
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::Edit);
    QVERIFY(view.IsSnapEnabled());
    QCOMPARE(view.GetCurrentSnapResult().type, mini_graph::CanvasSnapType::None);

    view.mouseMoveEvent(&move);
    QCOMPARE(view.GetCurrentSnapResult().type,
             mini_graph::CanvasSnapType::LineEndpoint);

    view.SetSnapEnabled(false);
    QCOMPARE(static_cast<int>(view.GetCurrentSnapResult().type),
             static_cast<int>(mini_graph::CanvasSnapType::None));
}

void CanvasCoreTest::CanvasViewDrawsSnapMarker()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetSnapEnabled(true);
    view.SetGeometrySnapEnabled(false);

    const QPointF mousePosition = view.MapFromWorld(
                mini_graph::CanvasVector(4.6, 5.2));
    QMouseEvent move(QEvent::MouseMove,
                     mousePosition,
                     mousePosition,
                     mousePosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawSnapMarker(painter);
    painter.end();

    const QPointF snapPosition = view.MapFromWorld(
                mini_graph::CanvasVector(0.0, 10.0));
    QVERIFY(image.pixelColor(qRound(snapPosition.x()),
                             qRound(snapPosition.y())).yellow() > 150);
}

void CanvasCoreTest::CanvasViewRendersDocumentWithPainter()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-30.0, 0.0),
                mini_graph::CanvasVector(30.0, 0.0),
                QColor(255, 0, 0),
                2.0);
    QVERIFY(lineId != 0);

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    bool foundRedPixel = false;
    for (int y = 97; y <= 103 && !foundRedPixel; ++y) {
        for (int x = 90; x <= 110; ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > 180 && color.green() < 100 && color.blue() < 100) {
                foundRedPixel = true;
                break;
            }
        }
    }
    QVERIFY(foundRedPixel);
}

void CanvasCoreTest::CanvasViewRendersCanvasDataInLayerOrder()
{
    QStringList events;
    mini_graph::CanvasDocument document;
    mini_graph::CanvasLine line(mini_graph::CanvasVector(-20.0, 0.0),
                                mini_graph::CanvasVector(20.0, 0.0));
    const std::shared_ptr<TestCanvasData> entityData =
            std::make_shared<TestCanvasData>(&events);
    line.SetCanvasData(entityData);
    QVERIFY(document.AddEntity(line) != 0);
    document.SetCanvasGlobalData(
                std::make_shared<TestCanvasGlobalData>(&events));

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);
    OrderedCanvasRenderer renderer(&events);
    view.SetCustomRenderer(&renderer);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    QCOMPARE(events, QStringList() << QStringLiteral("global-before")
                                   << QStringLiteral("entity-data")
                                   << QStringLiteral("custom-renderer")
                                   << QStringLiteral("global-after"));
}

void CanvasCoreTest::CanvasViewRendersCanvasDataBeforeEntityGeometry()
{
    const QStringList commands = QStringList()
            << QStringLiteral("Pen,255,0,0,255,2,1")
            << QStringLiteral("Line,-30,0,30,0");
    mini_graph::CanvasDocument document;
    mini_graph::CanvasPoint point(mini_graph::CanvasVector(0.0, 0.0));
    mini_graph::StruCanvasStyle pointStyle = point.GetStyle();
    pointStyle.strokeColor = QColor(0, 80, 255);
    pointStyle.selectedStrokeColor = pointStyle.strokeColor;
    pointStyle.pointSize = 16.0;
    QVERIFY(point.SetStyle(pointStyle));
    point.SetCanvasData(std::make_shared<TestCanvasData>(nullptr, commands));
    QVERIFY(document.AddEntity(point) != 0);

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QColor center = image.pixelColor(100, 100);
    QVERIFY(center.blue() > 180);
    QVERIFY(center.red() < 100);

    const QColor outsidePoint = image.pixelColor(88, 100);
    QVERIFY(outsidePoint.red() > 180);
    QVERIFY(outsidePoint.green() < 100);
}

void CanvasCoreTest::CanvasViewRendersGlobalDataAroundEntityGeometry()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasLine line(mini_graph::CanvasVector(-30.0, 0.0),
                                mini_graph::CanvasVector(30.0, 0.0));
    mini_graph::StruCanvasStyle lineStyle = line.GetStyle();
    lineStyle.strokeColor = QColor(0, 80, 255);
    lineStyle.selectedStrokeColor = lineStyle.strokeColor;
    lineStyle.strokeWidth = 2.0;
    QVERIFY(line.SetStyle(lineStyle));
    QVERIFY(document.AddEntity(line) != 0);

    const QStringList preCommands = QStringList()
            << QStringLiteral("Pen,255,0,0,255,8,1")
            << QStringLiteral("Line,-30,0,30,0");
    const QStringList postCommands = QStringList()
            << QStringLiteral("Pen,0,255,0,255,1,1")
            << QStringLiteral("Point,0,0");
    document.SetCanvasGlobalData(std::make_shared<TestCanvasGlobalData>(
                                     nullptr, preCommands, postCommands));

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QColor preLayer = image.pixelColor(90, 97);
    QVERIFY(preLayer.red() > 150);
    QVERIFY(preLayer.green() < 100);

    const QColor postLayer = image.pixelColor(100, 100);
    QVERIFY(postLayer.green() > 150);
    QVERIFY(postLayer.red() < 100);
}

void CanvasCoreTest::CanvasDataInheritsOwningEntityPenWhenUnspecified()
{
    const QStringList commands = QStringList()
            << QStringLiteral("Line,-30,20,30,20");
    mini_graph::CanvasDocument document;
    mini_graph::CanvasLine line(mini_graph::CanvasVector(-10.0, 0.0),
                                mini_graph::CanvasVector(10.0, 0.0));
    mini_graph::StruCanvasStyle lineStyle = line.GetStyle();
    lineStyle.strokeColor = QColor(0, 80, 255);
    lineStyle.selectedStrokeColor = lineStyle.strokeColor;
    lineStyle.strokeWidth = 3.0;
    QVERIFY(line.SetStyle(lineStyle));
    line.SetCanvasData(std::make_shared<TestCanvasData>(nullptr, commands));
    QVERIFY(document.AddEntity(line) != 0);

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QColor inheritedPenPixel = image.pixelColor(90, 80);
    QVERIFY(inheritedPenPixel.blue() > 180);
    QVERIFY(inheritedPenPixel.red() < 100);
}

void CanvasCoreTest::CanvasViewDrawsAllCadLabCanvasDataCommands()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    QImage stamp(4, 4, QImage::Format_ARGB32_Premultiplied);
    stamp.fill(QColor(255, 0, 255));
    const QString imagePath = temporaryDirectory.filePath(QStringLiteral("stamp.png"));
    QVERIFY(stamp.save(imagePath));

    const QStringList commands = QStringList()
            << QStringLiteral("Pen,255,0,0,255,2,1")
            << QStringLiteral("Brush,0,0,0,0,0")
            << QStringLiteral("Point,-160,160")
            << QStringLiteral("Line,-140,150,-120,150")
            << QStringLiteral("Arc,-95,150,12,0,1.5707963267948966,1")
            << QStringLiteral("Ellipse,-60,150,10,6")
            << QStringLiteral("Polyline,-25,140,-15,160,-5,140")
            << QStringLiteral("Polygon,10,140,30,140,20,160")
            << QStringLiteral("Rect,50,150,20,20")
            << QStringLiteral("Cubic,0,80,140,90,160,100,160,110,140")
            << QStringLiteral("Quad,0,120,140,130,160,140,140")
            << QStringLiteral("Font,Arial,12,1,0")
            << QStringLiteral("Text,miniGraph,150,140,5,0")
            << QStringLiteral("Image,%1,150,100,0,1,1,0").arg(imagePath);

    mini_graph::CanvasDocument document;
    mini_graph::CanvasPoint point(mini_graph::CanvasVector(0.0, 0.0));
    point.SetCanvasData(std::make_shared<TestCanvasData>(nullptr, commands));
    QVERIFY(document.AddEntity(point) != 0);

    CanvasViewHarness view;
    view.resize(400, 400);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const auto hasRedPixel = [&image](int left, int top, int width, int height) {
        for (int y = top; y < top + height; ++y) {
            for (int x = left; x < left + width; ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.red() > 180 && color.green() < 100 && color.blue() < 100) {
                    return true;
                }
            }
        }
        return false;
    };

    QVERIFY(hasRedPixel(35, 35, 12, 12));
    QVERIFY(hasRedPixel(55, 45, 32, 12));
    QVERIFY(hasRedPixel(78, 33, 35, 35));
    QVERIFY(hasRedPixel(128, 37, 25, 25));
    QVERIFY(hasRedPixel(168, 33, 36, 36));
    QVERIFY(hasRedPixel(205, 33, 30, 35));
    QVERIFY(hasRedPixel(245, 25, 32, 32));
    QVERIFY(hasRedPixel(275, 30, 42, 42));
    QVERIFY(hasRedPixel(315, 30, 42, 42));
    QVERIFY(hasRedPixel(332, 45, 38, 24));

    bool foundMagentaPixel = false;
    for (int y = 95; y < 110 && !foundMagentaPixel; ++y) {
        for (int x = 345; x < 365; ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > 180 && color.blue() > 180 && color.green() < 100) {
                foundMagentaPixel = true;
                break;
            }
        }
    }
    QVERIFY(foundMagentaPixel);
}

void CanvasCoreTest::CanvasDataBrushHatchFillsClosedShapes()
{
    const QStringList commands = QStringList()
            << QStringLiteral("Brush,0,255,0,255,11")
            << QStringLiteral("Pen,0,0,0,0,0,0")
            << QStringLiteral("Polygon,-20,-20,20,-20,20,20,-20,20");

    mini_graph::CanvasDocument document;
    mini_graph::CanvasPoint point(mini_graph::CanvasVector(0.0, 0.0));
    point.SetCanvasData(std::make_shared<TestCanvasData>(nullptr, commands));
    QVERIFY(document.AddEntity(point) != 0);

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    int greenPixels = 0;
    for (int y = 85; y < 115; ++y) {
        for (int x = 85; x < 115; ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.green() > 180 && color.red() < 100 && color.blue() < 100) {
                ++greenPixels;
            }
        }
    }
    QVERIFY(greenPixels > 0);
    QVERIFY(greenPixels < 450);
}

void CanvasCoreTest::CanvasViewRendersAllCanvasEntityTypes()
{
    mini_graph::CanvasDocument document;
    const qreal halfPi = 1.5707963267948966;
    const qreal quarterPi = 0.7853981633974483;
    const auto styleFor = [](const QColor &color) {
        mini_graph::StruCanvasStyle style;
        style.strokeColor = color;
        style.selectedStrokeColor = color;
        style.strokeWidth = 4.0;
        style.pointSize = 10.0;
        return style;
    };

    mini_graph::CanvasPoint point(mini_graph::CanvasVector(-110.0, 100.0));
    QVERIFY(point.SetStyle(styleFor(QColor(240, 50, 50))));
    QVERIFY(document.AddEntity(point) != 0);

    mini_graph::CanvasLine line(mini_graph::CanvasVector(-80.0, 100.0),
                                mini_graph::CanvasVector(-40.0, 100.0));
    QVERIFY(line.SetStyle(styleFor(QColor(50, 230, 70))));
    QVERIFY(document.AddEntity(line) != 0);

    mini_graph::CanvasArc arc(mini_graph::CanvasVector(0.0, 100.0),
                              20.0, 0.0, halfPi);
    QVERIFY(arc.SetStyle(styleFor(QColor(40, 100, 250))));
    QVERIFY(document.AddEntity(arc) != 0);

    mini_graph::CanvasCircle circle(mini_graph::CanvasVector(60.0, 100.0),
                                    15.0);
    QVERIFY(circle.SetStyle(styleFor(QColor(230, 230, 40))));
    QVERIFY(document.AddEntity(circle) != 0);

    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(-105.0, 0.0),
                                mini_graph::CanvasVector(20.0, 0.0),
                                0.5, false));
    QVERIFY(ellipse.SetStyle(styleFor(QColor(230, 40, 220))));
    QVERIFY(document.AddEntity(ellipse) != 0);

    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(45.0, 30.0),
                                    mini_graph::CanvasVector(65.0, 30.0))));
    QVERIFY(polyline.AddSegment(mini_graph::CanvasArc(
                                    mini_graph::CanvasVector(65.0, 40.0),
                                    10.0, -halfPi, 0.0)));
    QVERIFY(polyline.SetStyle(styleFor(QColor(30, 210, 210))));
    QVERIFY(document.AddEntity(polyline) != 0);

    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-20.0, -60.0)
                           << mini_graph::CanvasVector(0.0, 0.0)
                           << mini_graph::CanvasVector(20.0, -60.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, false));
    QVERIFY(spline.SetStyle(styleFor(QColor(255, 130, 20))));
    QVERIFY(document.AddEntity(spline) != 0);

    mini_graph::CanvasPoint groupPoint(mini_graph::CanvasVector(100.0, -100.0));
    QVERIFY(groupPoint.SetStyle(styleFor(QColor(145, 80, 245))));
    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(groupPoint));
    QVERIFY(document.AddEntity(group) != 0);

    CanvasViewHarness view;
    view.resize(320, 280);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const auto hasColorNear = [&image, &view](const mini_graph::CanvasVector &world,
                                               const QColor &expectedColor) {
        const QPointF position = view.MapFromWorld(world);
        const int centerX = qRound(position.x());
        const int centerY = qRound(position.y());
        for (int y = centerY - 3; y <= centerY + 3; ++y) {
            for (int x = centerX - 3; x <= centerX + 3; ++x) {
                if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                    continue;
                }
                const QColor actualColor = image.pixelColor(x, y);
                if (qAbs(actualColor.red() - expectedColor.red()) < 45
                        && qAbs(actualColor.green() - expectedColor.green()) < 45
                        && qAbs(actualColor.blue() - expectedColor.blue()) < 45) {
                    return true;
                }
            }
        }
        return false;
    };

    QVERIFY(hasColorNear(mini_graph::CanvasVector(-110.0, 100.0),
                         QColor(240, 50, 50)));
    QVERIFY(hasColorNear(mini_graph::CanvasVector(-60.0, 100.0),
                         QColor(50, 230, 70)));
    QVERIFY(hasColorNear(mini_graph::CanvasVector(20.0 * std::cos(quarterPi),
                                                   100.0 + 20.0 * std::sin(quarterPi)),
                         QColor(40, 100, 250)));
    QVERIFY(hasColorNear(mini_graph::CanvasVector(75.0, 100.0),
                         QColor(230, 230, 40)));
    QVERIFY(hasColorNear(mini_graph::CanvasVector(-105.0, 10.0),
                         QColor(230, 40, 220)));
    QVERIFY(hasColorNear(mini_graph::CanvasVector(55.0, 30.0),
                         QColor(30, 210, 210)));
    QVERIFY(hasColorNear(mini_graph::CanvasVector(0.0, -30.0),
                         QColor(255, 130, 20)));
    QVERIFY(hasColorNear(mini_graph::CanvasVector(100.0, -100.0),
                         QColor(145, 80, 245)));
}

void CanvasCoreTest::CanvasViewRendersCompleteSplineAtTessellationLimit()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(3,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(0.0, 0.0)
                           << mini_graph::CanvasVector(100.0, 1000.0)
                           << mini_graph::CanvasVector(200.0, -1000.0)
                           << mini_graph::CanvasVector(300.0, 1000.0)
                           << mini_graph::CanvasVector(400.0, -1000.0)
                           << mini_graph::CanvasVector(500.0, 1000.0)
                           << mini_graph::CanvasVector(600.0, -1000.0)
                           << mini_graph::CanvasVector(700.0, 1000.0)
                           << mini_graph::CanvasVector(800.0, -1000.0)
                           << mini_graph::CanvasVector(900.0, 1000.0)
                           << mini_graph::CanvasVector(1000.0, 0.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0 << 0.0
                           << (1.0 / 8.0) << (2.0 / 8.0) << (3.0 / 8.0)
                           << (4.0 / 8.0) << (5.0 / 8.0) << (6.0 / 8.0)
                           << (7.0 / 8.0)
                           << 1.0 << 1.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, false));
    mini_graph::StruCanvasStyle style;
    style.strokeColor = QColor(240, 50, 50);
    style.selectedStrokeColor = style.strokeColor;
    style.strokeWidth = 4.0;
    QVERIFY(spline.SetStyle(style));
    QVERIFY(document.AddEntity(spline) != 0);

    CanvasViewHarness view;
    view.resize(320, 280);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);
    view.CenterOnWorld(mini_graph::CanvasVector(1000.0, 0.0));

    for (int index = 0; index < 100; ++index) {
        QWheelEvent wheelEvent(QPointF(160.0, 140.0),
                               QPointF(160.0, 140.0),
                               QPoint(0, 0),
                               QPoint(0, 120),
                               Qt::NoButton,
                               Qt::NoModifier,
                               Qt::ScrollUpdate,
                               false);
        view.wheelEvent(&wheelEvent);
    }
    QCOMPARE(view.GetZoom(), mini_graph::CanvasTransform::GetMaximumZoom());

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QPointF endpoint = view.MapFromWorld(
                mini_graph::CanvasVector(1000.0, 0.0));
    bool foundEndpoint = false;
    for (int y = qRound(endpoint.y()) - 3;
         y <= qRound(endpoint.y()) + 3 && !foundEndpoint; ++y) {
        for (int x = qRound(endpoint.x()) - 3;
             x <= qRound(endpoint.x()) + 3; ++x) {
            if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                continue;
            }
            const QColor actualColor = image.pixelColor(x, y);
            if (actualColor.red() > 180 && actualColor.green() < 100
                    && actualColor.blue() < 100) {
                foundEndpoint = true;
                break;
            }
        }
    }
    QVERIFY(foundEndpoint);
}

void CanvasCoreTest::CanvasViewDoesNotCloseEachDiscontinuousSplinePath()
{
    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-80.0, -60.0)
                           << mini_graph::CanvasVector(-50.0, -30.0)
                           << mini_graph::CanvasVector(-20.0, -60.0)
                           << mini_graph::CanvasVector(20.0, -60.0)
                           << mini_graph::CanvasVector(50.0, -30.0)
                           << mini_graph::CanvasVector(80.0, -20.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 0.5 << 0.5 << 0.5
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>(), true, false));

    mini_graph::StruCanvasStyle style;
    style.strokeColor = QColor(Qt::red);
    style.selectedStrokeColor = style.strokeColor;
    style.strokeWidth = 4.0;
    QVERIFY(spline.SetStyle(style));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId splineId = document.AddEntity(spline);
    QVERIFY(splineId != 0);
    const mini_graph::CanvasSpline *storedSpline =
            dynamic_cast<const mini_graph::CanvasSpline *>(
                document.GetEntity(splineId));
    QVERIFY(storedSpline);
    QVERIFY(storedSpline->IsClosed());
    QVERIFY(!storedSpline->IsPeriodic());

    CanvasViewHarness view;
    view.resize(320, 280);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QPointF gapPoint = view.MapFromWorld(
                mini_graph::CanvasVector(-50.0, -60.0));
    bool foundUnwantedClosingSegment = false;
    for (int y = qRound(gapPoint.y()) - 1;
         y <= qRound(gapPoint.y()) + 1 && !foundUnwantedClosingSegment; ++y) {
        for (int x = qRound(gapPoint.x()) - 1;
             x <= qRound(gapPoint.x()) + 1; ++x) {
            if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                continue;
            }
            const QColor actualColor = image.pixelColor(x, y);
            if (actualColor.red() > 150 && actualColor.green() < 100
                    && actualColor.blue() < 100) {
                foundUnwantedClosingSegment = true;
                break;
            }
        }
    }
    QVERIFY(!foundUnwantedClosingSegment);

    const QPointF expectedClosurePoint = view.MapFromWorld(
                mini_graph::CanvasVector(0.0, -40.0));
    bool foundExpectedClosure = false;
    for (int y = qRound(expectedClosurePoint.y()) - 1;
         y <= qRound(expectedClosurePoint.y()) + 1 && !foundExpectedClosure;
         ++y) {
        for (int x = qRound(expectedClosurePoint.x()) - 1;
             x <= qRound(expectedClosurePoint.x()) + 1; ++x) {
            if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                continue;
            }
            const QColor actualColor = image.pixelColor(x, y);
            if (actualColor.red() > 150 && actualColor.green() < 100
                    && actualColor.blue() < 100) {
                foundExpectedClosure = true;
                break;
            }
        }
    }
    QVERIFY(foundExpectedClosure);
}

void CanvasCoreTest::CanvasViewDoesNotCloseNonmatchingPeriodicSplineEndpoints()
{
    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-80.0, 0.0)
                           << mini_graph::CanvasVector(0.0, 60.0)
                           << mini_graph::CanvasVector(80.0, 0.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, true));
    QVERIFY(spline.IsPeriodic());
    QVERIFY(!spline.IsClosed());

    mini_graph::StruCanvasStyle style;
    style.strokeColor = QColor(Qt::red);
    style.selectedStrokeColor = style.strokeColor;
    style.strokeWidth = 4.0;
    QVERIFY(spline.SetStyle(style));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId splineId = document.AddEntity(spline);
    QVERIFY(splineId != 0);
    const mini_graph::CanvasSpline *storedSpline =
            dynamic_cast<const mini_graph::CanvasSpline *>(
                document.GetEntity(splineId));
    QVERIFY(storedSpline);
    QVERIFY(storedSpline->IsPeriodic());
    QVERIFY(!storedSpline->IsClosed());

    CanvasViewHarness view;
    view.resize(320, 280);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QPointF gapPoint = view.MapFromWorld(
                mini_graph::CanvasVector(0.0, 0.0));
    bool foundArtificialClosure = false;
    for (int y = qRound(gapPoint.y()) - 1;
         y <= qRound(gapPoint.y()) + 1 && !foundArtificialClosure; ++y) {
        for (int x = qRound(gapPoint.x()) - 1;
             x <= qRound(gapPoint.x()) + 1; ++x) {
            if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                continue;
            }
            const QColor actualColor = image.pixelColor(x, y);
            if (actualColor.red() > 150 && actualColor.green() < 100
                    && actualColor.blue() < 100) {
                foundArtificialClosure = true;
                break;
            }
        }
    }
    QVERIFY(!foundArtificialClosure);

    const QPointF curvePoint = view.MapFromWorld(
                mini_graph::CanvasVector(0.0, 30.0));
    bool foundCurve = false;
    for (int y = qRound(curvePoint.y()) - 2;
         y <= qRound(curvePoint.y()) + 2 && !foundCurve; ++y) {
        for (int x = qRound(curvePoint.x()) - 2;
             x <= qRound(curvePoint.x()) + 2; ++x) {
            if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                continue;
            }
            const QColor actualColor = image.pixelColor(x, y);
            if (actualColor.red() > 150 && actualColor.green() < 100
                    && actualColor.blue() < 100) {
                foundCurve = true;
                break;
            }
        }
    }
    QVERIFY(foundCurve);
}

void CanvasCoreTest::CanvasViewRendersNurbsWithLargeFiniteWeights()
{
    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-50.0, 0.0)
                           << mini_graph::CanvasVector(0.0, 60.0)
                           << mini_graph::CanvasVector(50.0, 0.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>() << 1.0e308 << 1.0e308 << 1.0e308,
                           false, false));

    mini_graph::StruCanvasStyle style;
    style.strokeColor = QColor(Qt::red);
    style.selectedStrokeColor = style.strokeColor;
    QVERIFY(spline.SetStyle(style));

    mini_graph::CanvasDocument document;
    QVERIFY(document.AddEntity(spline) != 0);

    CanvasViewHarness view;
    view.resize(320, 280);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QPointF curvePoint = view.MapFromWorld(
                mini_graph::CanvasVector(0.0, 30.0));
    bool foundCurve = false;
    for (int y = qRound(curvePoint.y()) - 2;
         y <= qRound(curvePoint.y()) + 2 && !foundCurve; ++y) {
        for (int x = qRound(curvePoint.x()) - 2;
             x <= qRound(curvePoint.x()) + 2; ++x) {
            if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                continue;
            }
            const QColor actualColor = image.pixelColor(x, y);
            if (actualColor.red() > 150 && actualColor.green() < 100
                    && actualColor.blue() < 100) {
                foundCurve = true;
                break;
            }
        }
    }
    QVERIFY(foundCurve);
}

void CanvasCoreTest::CanvasViewRendersNurbsWithExtremeWeightRange()
{
    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-50.0, 0.0)
                           << mini_graph::CanvasVector(0.0, 60.0)
                           << mini_graph::CanvasVector(50.0, 0.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>() << 1.0e308 << 1.0e-308 << 1.0e-308,
                           false, false));

    mini_graph::StruCanvasStyle style;
    style.strokeColor = QColor(Qt::red);
    style.selectedStrokeColor = style.strokeColor;
    style.strokeWidth = 4.0;
    QVERIFY(spline.SetStyle(style));

    mini_graph::CanvasDocument document;
    QVERIFY(document.AddEntity(spline) != 0);

    CanvasViewHarness view;
    view.resize(320, 280);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const QPointF endpoint = view.MapFromWorld(
                mini_graph::CanvasVector(50.0, 0.0));
    bool foundEndpoint = false;
    for (int y = qRound(endpoint.y()) - 2;
         y <= qRound(endpoint.y()) + 2 && !foundEndpoint; ++y) {
        for (int x = qRound(endpoint.x()) - 2;
             x <= qRound(endpoint.x()) + 2; ++x) {
            if (x < 0 || x >= image.width() || y < 0 || y >= image.height()) {
                continue;
            }
            const QColor actualColor = image.pixelColor(x, y);
            if (actualColor.red() > 150 && actualColor.green() < 100
                    && actualColor.blue() < 100) {
                foundEndpoint = true;
                break;
            }
        }
    }
    QVERIFY(foundEndpoint);
}

void CanvasCoreTest::CanvasRendererDrawsOpenAndClosedPolylines()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId openId = document.AddPolyline(
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(-30.0, 20.0)
                << mini_graph::CanvasVector(0.0, 50.0)
                << mini_graph::CanvasVector(30.0, 20.0));
    const mini_graph::ItemId closedId = document.AddPolyline(
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(-30.0, -10.0)
                << mini_graph::CanvasVector(30.0, -10.0)
                << mini_graph::CanvasVector(0.0, -50.0),
                true);
    QVERIFY(openId != 0);
    QVERIFY(closedId != 0);

    mini_graph::StruCanvasStyle openStyle;
    openStyle.strokeColor = QColor(Qt::red);
    openStyle.selectedStrokeColor = QColor(Qt::blue);
    openStyle.strokeWidth = 5.0;
    QVERIFY(document.SetItemStyle(openId, openStyle));
    document.SelectItem(openId);

    mini_graph::StruCanvasStyle closedStyle;
    closedStyle.strokeColor = QColor(Qt::green);
    closedStyle.strokeWidth = 5.0;
    QVERIFY(document.SetItemStyle(closedId, closedStyle));

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const auto hasColorNear = [&image](const QPointF &position,
                                       const QColor &expectedColor) {
        const int centerX = qRound(position.x());
        const int centerY = qRound(position.y());
        for (int y = centerY - 2; y <= centerY + 2; ++y) {
            for (int x = centerX - 2; x <= centerX + 2; ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.red() > expectedColor.red() - 40
                        && color.red() < expectedColor.red() + 40
                        && color.green() > expectedColor.green() - 40
                        && color.green() < expectedColor.green() + 40
                        && color.blue() > expectedColor.blue() - 40
                        && color.blue() < expectedColor.blue() + 40) {
                    return true;
                }
            }
        }
        return false;
    };

    QVERIFY(hasColorNear(view.MapFromWorld(mini_graph::CanvasVector(-15.0, 35.0)),
                         QColor(Qt::blue)));
    QVERIFY(hasColorNear(view.MapFromWorld(mini_graph::CanvasVector(-15.0, -30.0)),
                         QColor(Qt::green)));
}

void CanvasCoreTest::CanvasRendererSkipsInvisiblePolylineSegments()
{
    const qreal pi = std::acos(-1.0);
    QStringList hiddenSegmentEvents;
    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(-80.0, 0.0),
                                    mini_graph::CanvasVector(-40.0, 0.0))));

    mini_graph::CanvasLine hiddenLine(mini_graph::CanvasVector(-40.0, 0.0),
                                      mini_graph::CanvasVector(-20.0, 0.0));
    mini_graph::StruCanvasStyle hiddenLineStyle = hiddenLine.GetStyle();
    hiddenLineStyle.visible = false;
    QVERIFY(hiddenLine.SetStyle(hiddenLineStyle));
    hiddenLine.SetCanvasData(std::make_shared<TestCanvasData>(
                                 &hiddenSegmentEvents));
    QVERIFY(polyline.AddSegment(hiddenLine));

    mini_graph::CanvasArc hiddenArc(mini_graph::CanvasVector(-10.0, 0.0),
                                    10.0, pi, 2.0 * pi);
    mini_graph::StruCanvasStyle hiddenArcStyle = hiddenArc.GetStyle();
    hiddenArcStyle.visible = false;
    QVERIFY(hiddenArc.SetStyle(hiddenArcStyle));
    QVERIFY(polyline.AddSegment(hiddenArc));

    mini_graph::StruCanvasStyle polylineStyle = polyline.GetStyle();
    polylineStyle.strokeColor = QColor(Qt::red);
    polylineStyle.strokeWidth = 5.0;
    QVERIFY(polyline.SetStyle(polylineStyle));

    mini_graph::CanvasDocument document;
    QVERIFY(document.AddEntity(polyline) != 0);

    CanvasViewHarness view;
    view.resize(200, 200);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    const auto hasRedPixelNear = [&image](const QPointF &position) {
        const int centerX = qRound(position.x());
        const int centerY = qRound(position.y());
        for (int y = centerY - 3; y <= centerY + 3; ++y) {
            for (int x = centerX - 3; x <= centerX + 3; ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.red() > 180 && color.green() < 100
                        && color.blue() < 100) {
                    return true;
                }
            }
        }
        return false;
    };

    QVERIFY(hasRedPixelNear(view.MapFromWorld(
                                mini_graph::CanvasVector(-60.0, 0.0))));
    QVERIFY(!hasRedPixelNear(view.MapFromWorld(
                                 mini_graph::CanvasVector(-30.0, 0.0))));
    QVERIFY(!hasRedPixelNear(view.MapFromWorld(
                                 mini_graph::CanvasVector(-10.0, -10.0))));
    QVERIFY(hiddenSegmentEvents.isEmpty());
}

void CanvasCoreTest::CanvasViewWheelUsesCadLabScale()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);

    QWheelEvent wheelEvent(QPointF(400.0, 300.0),
                           QPointF(400.0, 300.0),
                           QPoint(0, 0),
                           QPoint(0, 120),
                           Qt::NoButton,
                           Qt::NoModifier,
                           Qt::ScrollUpdate,
                           false);
    view.wheelEvent(&wheelEvent);

    QVERIFY(qAbs(view.GetZoom() - 1.137) < 0.000001);
}

void CanvasCoreTest::CanvasViewMiddlePanUsesDeadband()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    const mini_graph::CanvasVector initialCenter = view.GetTransform().GetCenter();

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(100.0, 100.0),
                      QPointF(100.0, 100.0),
                      QPointF(100.0, 100.0),
                      Qt::MiddleButton,
                      Qt::MiddleButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QCOMPARE(view.cursor().shape(), Qt::OpenHandCursor);

    QMouseEvent insideDeadband(QEvent::MouseMove,
                               QPointF(103.0, 100.0),
                               QPointF(103.0, 100.0),
                               QPointF(103.0, 100.0),
                               Qt::NoButton,
                               Qt::MiddleButton,
                               Qt::NoModifier);
    view.mouseMoveEvent(&insideDeadband);
        QVERIFY(view.GetTransform().GetCenter() == initialCenter);

    QMouseEvent outsideDeadband(QEvent::MouseMove,
                                QPointF(112.0, 100.0),
                                QPointF(112.0, 100.0),
                                QPointF(112.0, 100.0),
                                Qt::NoButton,
                                Qt::MiddleButton,
                                Qt::NoModifier);
    view.mouseMoveEvent(&outsideDeadband);
    QVERIFY(view.GetTransform().GetCenter() != initialCenter);
    QCOMPARE(view.cursor().shape(), Qt::ClosedHandCursor);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(112.0, 100.0),
                        QPointF(112.0, 100.0),
                        QPointF(112.0, 100.0),
                        Qt::MiddleButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
    QCOMPARE(view.cursor().shape(), Qt::ArrowCursor);
}

void CanvasCoreTest::CanvasViewPreviewNavigationPansWithLeftAndMiddleButtons()
{
    CanvasViewHarness view;
    mini_graph::CanvasDocument document;
    document.AddLine(mini_graph::CanvasVector(0.0, 0.0),
                     mini_graph::CanvasVector(20.0, 0.0));
    view.resize(640, 480);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    QCOMPARE(view.GetInteractionMode(),
             mini_graph::CanvasView::InteractionMode::Edit);
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);

    const Qt::MouseButton panButtons[] = { Qt::LeftButton, Qt::MiddleButton };
    for (Qt::MouseButton button : panButtons) {
        const QPointF before = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(100.0, 100.0),
                          QPointF(100.0, 100.0), QPointF(100.0, 100.0),
                          button, button, Qt::NoModifier);
        view.mousePressEvent(&press);
        QCOMPARE(view.cursor().shape(), Qt::OpenHandCursor);

        QMouseEvent inside(QEvent::MouseMove, QPointF(103.0, 100.0),
                           QPointF(103.0, 100.0), QPointF(103.0, 100.0),
                           Qt::NoButton, button, Qt::NoModifier);
        view.mouseMoveEvent(&inside);
        QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)), before);

        QMouseEvent move(QEvent::MouseMove, QPointF(140.0, 120.0),
                         QPointF(140.0, 120.0), QPointF(140.0, 120.0),
                         Qt::NoButton, button, Qt::NoModifier);
        view.mouseMoveEvent(&move);
        QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)),
                 before + QPointF(40.0, 20.0));
        QCOMPARE(view.cursor().shape(), Qt::ClosedHandCursor);

        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(140.0, 120.0),
                            QPointF(140.0, 120.0), QPointF(140.0, 120.0),
                            button, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
        QCOMPARE(view.cursor().shape(), Qt::ArrowCursor);
        QCOMPARE(document.GetSelectedItemCount(), 0);
    }

    QSignalSpy contextSpy(&view, &mini_graph::CanvasView::ContextMenuRequested);
    const QPointF beforeRight = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    QMouseEvent rightPress(QEvent::MouseButtonPress, QPointF(100.0, 100.0),
                           QPointF(100.0, 100.0), QPointF(100.0, 100.0),
                           Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    view.mousePressEvent(&rightPress);
    QMouseEvent rightMove(QEvent::MouseMove, QPointF(140.0, 120.0),
                          QPointF(140.0, 120.0), QPointF(140.0, 120.0),
                          Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    view.mouseMoveEvent(&rightMove);
    QMouseEvent rightRelease(QEvent::MouseButtonRelease, QPointF(140.0, 120.0),
                             QPointF(140.0, 120.0), QPointF(140.0, 120.0),
                             Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&rightRelease);
    QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)), beforeRight);
    QCOMPARE(contextSpy.count(), 0);
}

void CanvasCoreTest::CanvasViewPreviewNavigationIgnoresSelectionAndDelete()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-20.0, 0.0),
                mini_graph::CanvasVector(20.0, 0.0));
    document.SelectItem(lineId);
    CanvasViewHarness view;
    view.resize(640, 480);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    view.SetSnapEnabled(true);
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QVERIFY(view.IsSnapEnabled());

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    const QPointF end = start + QPointF(30.0, 20.0);
    QMouseEvent clickPress(QEvent::MouseButtonPress, start, start, start,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&clickPress);
    QMouseEvent clickRelease(QEvent::MouseButtonRelease, start, start, start,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&clickRelease);
    QCOMPARE(document.GetSelectedItemCount(), 0);

    QMouseEvent press(QEvent::MouseButtonPress, start, start, start,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove, end, end, end,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    view.mouseMoveEvent(&move);
    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&release);
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QVERIFY(document.GetLinePrimitives().first().geometry.startPoint
            == mini_graph::CanvasVector(-20.0, 0.0));
    QVERIFY(document.GetLinePrimitives().first().geometry.endPoint
            == mini_graph::CanvasVector(20.0, 0.0));

    QKeyEvent deleteEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    view.keyPressEvent(&deleteEvent);
    QKeyEvent backspaceEvent(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    view.keyPressEvent(&backspaceEvent);
    QCOMPARE(document.GetItemCount(), 1);

    document.SelectItem(lineId);
    QMouseEvent panPress(QEvent::MouseButtonPress, QPointF(100.0, 100.0),
                         QPointF(100.0, 100.0), QPointF(100.0, 100.0),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&panPress);
    QMouseEvent panMove(QEvent::MouseMove, QPointF(120.0, 100.0),
                        QPointF(120.0, 100.0), QPointF(120.0, 100.0),
                        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    view.mouseMoveEvent(&panMove);
    const QPointF originAfterPan = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    QKeyEvent escapeEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);
    QCOMPARE(view.cursor().shape(), Qt::ArrowCursor);
    QMouseEvent moveAfterEscape(QEvent::MouseMove, QPointF(150.0, 100.0),
                                QPointF(150.0, 100.0), QPointF(150.0, 100.0),
                                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    view.mouseMoveEvent(&moveAfterEscape);
    QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)), originAfterPan);
    QCOMPARE(document.GetSelectedItemCount(), 1);
    QCOMPARE(document.GetItemCount(), 1);
    QVERIFY(document.GetLinePrimitives().first().geometry.startPoint
            == mini_graph::CanvasVector(-20.0, 0.0));
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::Edit);
    QVERIFY(view.IsSnapEnabled());
    QCOMPARE(view.GetInteractionMode(), mini_graph::CanvasView::InteractionMode::Edit);

    const QPointF editStart = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    const QPointF editEnd = editStart + QPointF(25.0, 0.0);
    QMouseEvent editPress(QEvent::MouseButtonPress, editStart, editStart, editStart,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&editPress);
    QMouseEvent editMove(QEvent::MouseMove, editEnd, editEnd, editEnd,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    view.mouseMoveEvent(&editMove);
    QVERIFY(document.GetLinePrimitives().first().geometry.startPoint
            != mini_graph::CanvasVector(-20.0, 0.0));
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);
    QVERIFY(document.GetLinePrimitives().first().geometry.startPoint
            == mini_graph::CanvasVector(-20.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);

    document.SelectItem(lineId);
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::Edit);
    const QPointF handleStart = view.MapFromWorld(
                mini_graph::CanvasVector(-20.0, 0.0));
    const QPointF handleEnd = handleStart + QPointF(-15.0, -10.0);
    QMouseEvent handlePress(QEvent::MouseButtonPress,
                            handleStart, handleStart, handleStart,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&handlePress);
    QMouseEvent handleMove(QEvent::MouseMove, handleEnd, handleEnd, handleEnd,
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    view.mouseMoveEvent(&handleMove);
    QVERIFY(document.GetLinePrimitives().first().geometry.startPoint
            != mini_graph::CanvasVector(-20.0, 0.0));
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);
    QVERIFY(document.GetLinePrimitives().first().geometry.startPoint
            == mini_graph::CanvasVector(-20.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
}

void CanvasCoreTest::CanvasViewPreviewNavigationMatchesCadLabWheelGestures()
{
    struct WheelCase {
        QPoint pixel;
        QPoint angle;
        Qt::KeyboardModifiers modifiers;
        qreal expectedZoom;
        QPointF expectedPan;
    };
    const WheelCase cases[] = {
        { QPoint(30, -20), QPoint(), Qt::NoModifier, 1.0, QPointF(30.0, -20.0) },
        { QPoint(30, -20), QPoint(), Qt::ShiftModifier, 1.0, QPointF(30.0, -20.0) },
        { QPoint(0, 20), QPoint(), Qt::ControlModifier, 1.0 / 1.2, QPointF() },
        { QPoint(0, -20), QPoint(), Qt::ControlModifier, 1.2, QPointF() },
        { QPoint(30, -20), QPoint(), Qt::ControlModifier | Qt::ShiftModifier,
          1.0, QPointF(30.0, -20.0) },
        { QPoint(0, 120), QPoint(), Qt::ShiftModifier, 1.0, QPointF(0.0, 120.0) },
        { QPoint(), QPoint(0, 120), Qt::NoModifier, 1.137, QPointF() },
        { QPoint(), QPoint(120, 0), Qt::NoModifier, 1.137, QPointF() },
        { QPoint(), QPoint(240, 120), Qt::NoModifier, 1.292769, QPointF() },
        { QPoint(), QPoint(0, 120), Qt::ControlModifier, 1.0, QPointF(0.0, 30.0) },
        { QPoint(), QPoint(120, 0), Qt::ControlModifier, 1.0, QPointF(-30.0, 0.0) },
        { QPoint(), QPoint(0, 120), Qt::ShiftModifier, 1.0, QPointF(-30.0, 0.0) },
        { QPoint(), QPoint(120, 0), Qt::ShiftModifier, 1.0, QPointF(0.0, 30.0) },
        { QPoint(), QPoint(0, 120), Qt::ControlModifier | Qt::ShiftModifier,
          1.0, QPointF() },
        { QPoint(), QPoint(120, 120), Qt::ControlModifier,
          1.0, QPointF(0.0, 30.0) }
    };

    for (const WheelCase &testCase : cases) {
        CanvasViewHarness view;
        view.resize(640, 480);
        view.setUpdatesEnabled(false);
        view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);
        const QPointF originBefore = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
        const QPointF anchor(200.0, 150.0);
        const mini_graph::CanvasVector worldAtAnchor = view.MapToWorld(anchor);
        QWheelEvent wheel(anchor, anchor, testCase.pixel, testCase.angle,
                          Qt::NoButton, testCase.modifiers,
                          Qt::ScrollUpdate, false);
        view.wheelEvent(&wheel);
        QVERIFY(qAbs(view.GetZoom() - testCase.expectedZoom) < 0.000001);
        if (testCase.expectedZoom != 1.0) {
            const mini_graph::CanvasVector anchored = view.MapToWorld(anchor);
            QVERIFY(anchored.DistanceTo(worldAtAnchor) < 0.000001);
        } else {
            QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)),
                     originBefore + testCase.expectedPan);
        }
    }
}

void CanvasCoreTest::CanvasViewPreviewNavigationDoubleClickFitsAll()
{
    mini_graph::CanvasDocument document;
    document.AddLine(mini_graph::CanvasVector(-100.0, -50.0),
                     mini_graph::CanvasVector(100.0, 50.0));
    CanvasViewHarness view;
    view.resize(640, 480);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);
    view.CenterOnWorld(mini_graph::CanvasVector(1000.0, 1000.0));

    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,
                            QPointF(220.0, 180.0), QPointF(220.0, 180.0),
                            QPointF(220.0, 180.0),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mouseDoubleClickEvent(&doubleClick);

    const QPointF first = view.MapFromWorld(mini_graph::CanvasVector(-100.0, -50.0));
    const QPointF second = view.MapFromWorld(mini_graph::CanvasVector(100.0, 50.0));
    QVERIFY(first.x() >= 9.9 && first.y() <= 470.1);
    QVERIFY(second.x() <= 630.1 && second.y() >= 9.9);
    QVERIFY(QLineF(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)),
                   QPointF(320.0, 240.0)).length() < 0.000001);
    QVERIFY(view.GetZoom() > 1.0);
}

void CanvasCoreTest::CanvasViewKeyboardAndContextSignals()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    QSignalSpy contextSpy(&view,
                          &mini_graph::CanvasView::ContextMenuRequested);
    QSignalSpy cancelledSpy(&view, SIGNAL(InteractionCancelled()));

    QVERIFY(contextSpy.isValid());
    QVERIFY(cancelledSpy.isValid());

    QKeyEvent shiftPress(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    view.keyPressEvent(&shiftPress);
    QVERIFY(view.property("orthogonalMode").toBool());

    QKeyEvent shiftRelease(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
    view.keyReleaseEvent(&shiftRelease);
    QVERIFY(!view.property("orthogonalMode").toBool());

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    view.keyPressEvent(&escape);
    QCOMPARE(cancelledSpy.count(), 1);

    QMouseEvent rightPress(QEvent::MouseButtonPress,
                           QPointF(250.0, 200.0),
                           QPointF(250.0, 200.0),
                           QPointF(250.0, 200.0),
                           Qt::RightButton,
                           Qt::RightButton,
                           Qt::NoModifier);
    view.mousePressEvent(&rightPress);

    QMouseEvent rightRelease(QEvent::MouseButtonRelease,
                             QPointF(250.0, 200.0),
                             QPointF(250.0, 200.0),
                             QPointF(250.0, 200.0),
                             Qt::RightButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    view.mouseReleaseEvent(&rightRelease);

    QCOMPARE(contextSpy.count(), 1);
    const QList<QVariant> contextArguments = contextSpy.takeFirst();
    QCOMPARE(contextArguments.at(1).toPoint(), QPoint(250, 200));
}

void CanvasCoreTest::CanvasViewDisplayPropertiesAndMapping()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);

    QVERIFY(view.IsGridVisible());
    QVERIFY(view.IsAxisVisible());
    QVERIFY(!view.IsRulerVisible());

    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(true);

    QVERIFY(!view.IsGridVisible());
    QVERIFY(!view.IsAxisVisible());
    QVERIFY(view.IsRulerVisible());
    QVERIFY(view.MapToWorld(QPointF(400.0, 300.0))
            == mini_graph::CanvasVector(0.0, 0.0));
    QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)),
             QPointF(400.0, 300.0));
}

void CanvasCoreTest::CanvasViewFitAllUsesDocumentBounds()
{
    mini_graph::CanvasDocument document;
    document.AddLine(mini_graph::CanvasVector(-100.0, -50.0),
                     mini_graph::CanvasVector(100.0, 50.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QVERIFY(view.FitAll(40.0));
    QVERIFY(view.GetTransform().GetCenter() == mini_graph::CanvasVector(0.0, 0.0));
    QVERIFY(qAbs(view.GetZoom() - 3.6) < 0.000001);
    QVERIFY(view.MapFromWorld(mini_graph::CanvasVector(-100.0, 50.0)).x() >= 40.0);
    QVERIFY(view.MapFromWorld(mini_graph::CanvasVector(100.0, -50.0)).x() <= 760.0);
}

void CanvasCoreTest::CanvasViewFitSelectionUsesSelectedBounds()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId selectedId = document.AddCircle(
                                                             mini_graph::CanvasVector(100.0, 50.0),
                                                             10.0);
    document.AddCircle(mini_graph::CanvasVector(-500.0, -500.0), 20.0);
    document.SelectItem(selectedId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QVERIFY(view.FitSelection(40.0));
    QVERIFY(view.GetTransform().GetCenter() == mini_graph::CanvasVector(100.0, 50.0));
    QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(100.0, 50.0)),
             QPointF(400.0, 300.0));
}

void CanvasCoreTest::CanvasViewFitCommandsRejectEmptyDocument()
{
    mini_graph::CanvasDocument document;
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const mini_graph::CanvasTransform initialTransform = view.GetTransform();
    QVERIFY(!view.FitAll());
    QVERIFY(!view.FitSelection());
    QVERIFY(view.GetTransform().GetCenter() == initialTransform.GetCenter());
    QCOMPARE(view.GetTransform().GetZoom(), initialTransform.GetZoom());
}

void CanvasCoreTest::CanvasViewCenterOnWorldMovesViewportCenter()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);

    view.CenterOnWorld(mini_graph::CanvasVector(125.0, -75.0));

    QVERIFY(view.GetTransform().GetCenter() == mini_graph::CanvasVector(125.0, -75.0));
    QCOMPARE(view.MapFromWorld(mini_graph::CanvasVector(125.0, -75.0)),
             QPointF(400.0, 300.0));
}

void CanvasCoreTest::CanvasViewZoomInShowsFinerGridUnits()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);

    QWheelEvent zoomIn(QPointF(400.0, 300.0),
                       QPointF(400.0, 300.0),
                       QPoint(0, -900),
                       QPoint(0, 0),
                       Qt::NoButton,
                       Qt::ControlModifier,
                       Qt::ScrollUpdate,
                       false);
    view.wheelEvent(&zoomIn);
    QVERIFY(qAbs(view.GetZoom() - 10.0) < 0.000001);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawGrid(painter);
    painter.end();

    int fineGridPixelCount = 0;
    for (int x = 499; x <= 501; ++x) {
        for (int y = 220; y <= 280; ++y) {
            if (image.pixelColor(x, y) == QColor(64, 64, 64)) {
                ++fineGridPixelCount;
            }
        }
    }

    QVERIFY2(fineGridPixelCount > 5,
             "Zooming in must expose the finer 10-world-unit meta-grid line.");
}

void CanvasCoreTest::CanvasViewGridAndRulerHandleLargeFiniteCoordinates()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.CenterOnWorld(mini_graph::CanvasVector(1.0e15, 1.0e15));

    QImage gridImage(view.size(), QImage::Format_ARGB32_Premultiplied);
    gridImage.fill(Qt::black);
    QPainter gridPainter(&gridImage);
    gridPainter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawGrid(gridPainter);
    gridPainter.end();

    int verticalGridPixelCount = 0;
    for (int y = 0; y < gridImage.height(); ++y) {
        if (gridImage.pixelColor(400, y) == QColor(64, 64, 64)) {
            ++verticalGridPixelCount;
        }
    }
    QVERIFY2(verticalGridPixelCount > 20,
             "The grid must draw visible ticks when world indices exceed int range.");

    QPicture rulerPicture;
    QPainter rulerPainter(&rulerPicture);
    view.DrawRulerLabels(rulerPainter);
    rulerPainter.end();
    QVERIFY2(rulerPicture.size() > 0,
             "The ruler must record labels for world coordinates beyond int range.");
}

void CanvasCoreTest::CanvasViewGridAndRulerSkipUnsafeTickIndices()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.CenterOnWorld(mini_graph::CanvasVector(1.0e18, 1.0e18));

    QImage gridImage(view.size(), QImage::Format_ARGB32_Premultiplied);
    gridImage.fill(Qt::black);
    QPainter gridPainter(&gridImage);
    gridPainter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawGrid(gridPainter);
    gridPainter.end();

    int gridPixelCount = 0;
    for (int y = 0; y < gridImage.height(); ++y) {
        for (int x = 0; x < gridImage.width(); ++x) {
            if (gridImage.pixelColor(x, y) != Qt::black) {
                ++gridPixelCount;
            }
        }
    }
    QCOMPARE(gridPixelCount, 0);

    QImage rulerImage(view.size(), QImage::Format_ARGB32_Premultiplied);
    rulerImage.fill(Qt::black);
    QPainter rulerPainter(&rulerImage);
    view.DrawRulerLabels(rulerPainter);
    rulerPainter.end();
    QCOMPARE(CountYellowPixels(rulerImage), 0);
}

void CanvasCoreTest::CanvasViewGridCapsVisibleTickCount()
{
    const int wideViewportWidth = 1200000;
    CanvasViewHarness view;
    view.resize(wideViewportWidth, 600);
    view.setUpdatesEnabled(false);
    QCOMPARE(view.width(), wideViewportWidth);

    QPicture gridPicture;
    QPainter gridPainter(&gridPicture);
    gridPainter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawGrid(gridPainter);
    gridPainter.end();

    QImage gridSample(100, 60, QImage::Format_ARGB32_Premultiplied);
    gridSample.fill(Qt::black);
    QPainter samplePainter(&gridSample);
    samplePainter.setRenderHint(QPainter::Antialiasing, false);
    samplePainter.drawPicture(QPointF(-599950.0, -220.0), gridPicture);
    samplePainter.end();

    int verticalLinePixelCount = 0;
    for (int y = 0; y < gridSample.height(); ++y) {
        if (gridSample.pixelColor(50, y) == QColor(64, 64, 64)) {
            ++verticalLinePixelCount;
        }
    }
    QCOMPARE(verticalLinePixelCount, 0);

    QPicture rulerPicture;
    QPainter rulerPainter(&rulerPicture);
    view.DrawRulerLabels(rulerPainter);
    rulerPainter.end();

    QImage rulerSample(100, 60, QImage::Format_ARGB32_Premultiplied);
    rulerSample.fill(Qt::black);
    QPainter rulerSamplePainter(&rulerSample);
    rulerSamplePainter.drawPicture(QPointF(-599950.0, -550.0), rulerPicture);
    rulerSamplePainter.end();
    QCOMPARE(CountYellowPixels(rulerSample), 0);
}

void CanvasCoreTest::CanvasViewGridSpacingRejectsNonFiniteValues()
{
    CanvasViewHarness view;
    QSignalSpy spacingSpy(&view, &mini_graph::CanvasView::GridSpacingChanged);

    view.SetGridSpacing(25.0);
    QCOMPARE(view.GetGridSpacing(), 25.0);
    QCOMPARE(spacingSpy.count(), 1);
    spacingSpy.clear();

    const qreal invalidSpacings[] = {
        std::numeric_limits<qreal>::quiet_NaN(),
        std::numeric_limits<qreal>::infinity(),
        -std::numeric_limits<qreal>::infinity()
    };
    for (qreal spacing : invalidSpacings) {
        view.SetGridSpacing(spacing);
        QCOMPARE(view.GetGridSpacing(), 25.0);
        QCOMPARE(spacingSpy.count(), 0);
    }
}

void CanvasCoreTest::CanvasViewRendersCadLabOverlays()
{
    CanvasViewHarness view;
    view.resize(800, 600);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    view.DrawAxes(painter);
    view.DrawRulerLabels(painter);
    painter.end();

    const QColor xAxisPixel = image.pixelColor(350, 300);
    QVERIFY(xAxisPixel.red() > xAxisPixel.green() + 10
            && xAxisPixel.red() > xAxisPixel.blue() + 10);

    const QColor yAxisPixel = image.pixelColor(400, 250);
    QVERIFY2(yAxisPixel.green() > yAxisPixel.red() + 10
                 && yAxisPixel.green() > yAxisPixel.blue() + 10,
             qPrintable(QStringLiteral("The Y axis must be green, got %1.").arg(yAxisPixel.name())));

    const QColor originMarkerPixel = image.pixelColor(395, 292);
    QVERIFY2(originMarkerPixel.red() >= 100
                 && originMarkerPixel.green() == originMarkerPixel.red()
                 && originMarkerPixel.blue() == originMarkerPixel.red(),
             qPrintable(QStringLiteral("The CADLab-style origin marker must render a gray square border, got %1.")
                            .arg(originMarkerPixel.name())));

}

void CanvasCoreTest::CanvasViewAxesStayAttachedToWorldAfterPan()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(400.0, 300.0),
                      QPointF(400.0, 300.0),
                      QPointF(400.0, 300.0),
                      Qt::MiddleButton,
                      Qt::MiddleButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);

    QMouseEvent move(QEvent::MouseMove,
                     QPointF(799.0, 300.0),
                     QPointF(799.0, 300.0),
                     QPointF(799.0, 300.0),
                     Qt::NoButton,
                     Qt::MiddleButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const QPointF origin = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    QVERIFY(qAbs(origin.x() - 799.0) < 0.000001);
    QVERIFY(qAbs(origin.y() - 300.0) < 0.000001);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    view.DrawAxes(painter);
    painter.end();

    int screenAnchoredMarkerPixels = 0;
    for (int x = 20; x <= 105; ++x) {
        for (int y = 555; y <= 585; ++y) {
            if (image.pixelColor(x, y) != QColor(Qt::black)) {
                ++screenAnchoredMarkerPixels;
            }
        }
    }

    QVERIFY2(screenAnchoredMarkerPixels == 0,
             "The coordinate origin marker must follow the world origin instead of staying at the viewport corner.");
}

void CanvasCoreTest::CanvasViewAxisDirectionsFollowFlipState()
{
    CanvasViewHarness view;
    view.resize(400, 300);

    const auto renderAxes = [&view](bool flipX, bool flipY) {
        view.SetAxisFlips(flipX, flipY);
        QImage image(view.size(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        view.DrawAxes(painter);
        painter.end();
        return image;
    };
    const auto isAxisArrowPixel = [](const QColor& color) {
        return color.red() > 50 && color.red() == color.green()
            && color.green() == color.blue();
    };

    const QImage normal = renderAxes(false, false);
    const QImage flipX = renderAxes(true, false);
    const QImage flipY = renderAxes(false, true);
    const QImage flipBoth = renderAxes(true, true);

    const QColor normalXAxisTip = normal.pixelColor(290, 150);
    QVERIFY2(isAxisArrowPixel(normalXAxisTip),
             qPrintable(QStringLiteral("Expected +X arrow near (290, 150), got %1.")
                            .arg(normalXAxisTip.name())));
    QVERIFY(isAxisArrowPixel(normal.pixelColor(200, 60)));
    QVERIFY(isAxisArrowPixel(flipX.pixelColor(110, 150)));
    QVERIFY(isAxisArrowPixel(flipX.pixelColor(200, 60)));
    QVERIFY(isAxisArrowPixel(flipY.pixelColor(290, 150)));
    QVERIFY(isAxisArrowPixel(flipY.pixelColor(200, 240)));
    QVERIFY(isAxisArrowPixel(flipBoth.pixelColor(110, 150)));
    QVERIFY(isAxisArrowPixel(flipBoth.pixelColor(200, 240)));
}

void CanvasCoreTest::DocumentSelectionTogglesAndSignals()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId firstId = document.AddLine(
                mini_graph::CanvasVector(-1.0, 0.0), mini_graph::CanvasVector(1.0, 0.0));
    const mini_graph::ItemId secondId = document.AddLine(
                mini_graph::CanvasVector(-1.0, 1.0), mini_graph::CanvasVector(1.0, 1.0));
    QSignalSpy selectionSpy(&document, &mini_graph::CanvasDocument::SelectionChanged);

    document.SelectItem(firstId);
    QVERIFY(document.IsItemSelected(firstId));
    QCOMPARE(document.GetSelectedItemCount(), 1);

    document.ToggleItemSelection(firstId);
    QVERIFY(!document.IsItemSelected(firstId));
    QCOMPARE(document.GetSelectedItemCount(), 0);

    document.SelectItems(QVector<mini_graph::ItemId>() << firstId << secondId, false, false);
    QCOMPARE(document.GetSelectedItemCount(), 2);
    document.ToggleItems(QVector<mini_graph::ItemId>() << firstId);
    QVERIFY(!document.IsItemSelected(firstId));
    QVERIFY(document.IsItemSelected(secondId));

    document.ClearSelection();
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QCOMPARE(selectionSpy.count(), 5);
}

void CanvasCoreTest::CanvasViewSingleClickSelectionFollowsCadLabRules()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId firstId = document.AddLine(
                mini_graph::CanvasVector(-100.0, 50.0),
                mini_graph::CanvasVector(100.0, 50.0));
    const mini_graph::ItemId secondId = document.AddLine(
                mini_graph::CanvasVector(-100.0, -50.0),
                mini_graph::CanvasVector(100.0, -50.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QMouseEvent firstPress(QEvent::MouseButtonPress,
                           QPointF(400.0, 250.0),
                           QPointF(400.0, 250.0),
                           QPointF(400.0, 250.0),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    view.mousePressEvent(&firstPress);
    QMouseEvent firstRelease(QEvent::MouseButtonRelease,
                             QPointF(400.0, 250.0),
                             QPointF(400.0, 250.0),
                             QPointF(400.0, 250.0),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    view.mouseReleaseEvent(&firstRelease);
    QVERIFY(document.IsItemSelected(firstId));
    QVERIFY(!document.IsItemSelected(secondId));

    QMouseEvent secondPress(QEvent::MouseButtonPress,
                            QPointF(400.0, 350.0),
                            QPointF(400.0, 350.0),
                            QPointF(400.0, 350.0),
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&secondPress);
    QMouseEvent secondRelease(QEvent::MouseButtonRelease,
                              QPointF(400.0, 350.0),
                              QPointF(400.0, 350.0),
                              QPointF(400.0, 350.0),
                              Qt::LeftButton,
                              Qt::NoButton,
                              Qt::NoModifier);
    view.mouseReleaseEvent(&secondRelease);
    QVERIFY(document.IsItemSelected(firstId));
    QVERIFY(document.IsItemSelected(secondId));

    view.mousePressEvent(&firstPress);
    view.mouseReleaseEvent(&firstRelease);
    QVERIFY(!document.IsItemSelected(firstId));
    QVERIFY(document.IsItemSelected(secondId));

    QMouseEvent shiftPress(QEvent::MouseButtonPress,
                           QPointF(400.0, 250.0),
                           QPointF(400.0, 250.0),
                           QPointF(400.0, 250.0),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::ShiftModifier);
    QMouseEvent shiftRelease(QEvent::MouseButtonRelease,
                             QPointF(400.0, 250.0),
                             QPointF(400.0, 250.0),
                             QPointF(400.0, 250.0),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::ShiftModifier);
    view.mousePressEvent(&shiftPress);
    view.mouseReleaseEvent(&shiftRelease);
    QVERIFY(document.IsItemSelected(firstId));
    QVERIFY(document.IsItemSelected(secondId));

    view.mousePressEvent(&shiftPress);
    view.mouseReleaseEvent(&shiftRelease);
    QVERIFY(!document.IsItemSelected(firstId));
    QVERIFY(document.IsItemSelected(secondId));

    QMouseEvent emptyPress(QEvent::MouseButtonPress,
                           QPointF(700.0, 500.0),
                           QPointF(700.0, 500.0),
                           QPointF(700.0, 500.0),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QMouseEvent emptyRelease(QEvent::MouseButtonRelease,
                             QPointF(700.0, 500.0),
                             QPointF(700.0, 500.0),
                             QPointF(700.0, 500.0),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    view.mousePressEvent(&emptyPress);
    view.mouseReleaseEvent(&emptyRelease);
    QVERIFY(document.IsItemSelected(secondId));
}

void CanvasCoreTest::CanvasViewSingleClickSelectsAllGeometryTypes()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(-150.0, 100.0));
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-80.0, -30.0),
                mini_graph::CanvasVector(80.0, -30.0));
    const mini_graph::ItemId arcId = document.AddArc(
                                                     mini_graph::CanvasVector(0.0, 100.0),
                                                     40.0,
                                                     0.0,
                                                     1.5707963267948966);
    const mini_graph::ItemId circleId = document.AddCircle(
                                                            mini_graph::CanvasVector(150.0, 100.0),
                                                            35.0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    auto clickAt = [&view](const QPointF &position) {
        QMouseEvent press(QEvent::MouseButtonPress,
                          position,
                          position,
                          position,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            position,
                            position,
                            position,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    clickAt(view.MapFromWorld(mini_graph::CanvasVector(-150.0, 100.0)));
    QVERIFY(document.IsItemSelected(pointId));
    document.ClearSelection();

    clickAt(view.MapFromWorld(mini_graph::CanvasVector(0.0, -30.0)));
    QVERIFY(document.IsItemSelected(lineId));
    document.ClearSelection();

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = mini_graph::CanvasVector(0.0, 100.0);
    arcGeometry.radius = 40.0;
    arcGeometry.startAngle = 0.0;
    arcGeometry.endAngle = 1.5707963267948966;
    const mini_graph::CanvasVector arcPoint =
            mini_graph::CalculateArcPoint(arcGeometry, 0.7853981633974483);
    clickAt(view.MapFromWorld(arcPoint));
    QVERIFY(document.IsItemSelected(arcId));
    document.ClearSelection();

    clickAt(view.MapFromWorld(mini_graph::CanvasVector(185.0, 100.0)));
    QVERIFY(document.IsItemSelected(circleId));
}

void CanvasCoreTest::CanvasViewSingleClickSelectsAllCanvasEntityTypes()
{
    mini_graph::CanvasDocument document;
    const qreal pi = std::acos(-1.0);

    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(-150.0, -100.0),
                                mini_graph::CanvasVector(30.0, 0.0),
                                0.5, false));
    const mini_graph::ItemId ellipseId = document.AddEntity(ellipse);

    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasArc(
                                    mini_graph::CanvasVector(50.0, -100.0),
                                    30.0, 0.0, pi / 2.0)));
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);

    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-70.0, -100.0)
                           << mini_graph::CanvasVector(-50.0, -60.0)
                           << mini_graph::CanvasVector(-30.0, -100.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, false));
    const mini_graph::ItemId splineId = document.AddEntity(spline);

    mini_graph::CanvasGroupContainer nestedGroup;
    QVERIFY(nestedGroup.AddChild(mini_graph::CanvasLine(
                                     mini_graph::CanvasVector(120.0, -100.0),
                                     mini_graph::CanvasVector(180.0, -100.0))));
    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(nestedGroup));
    const mini_graph::ItemId groupId = document.AddEntity(group);

    QVERIFY(ellipseId != 0);
    QVERIFY(polylineId != 0);
    QVERIFY(splineId != 0);
    QVERIFY(groupId != 0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const auto clickAt = [&view](const mini_graph::CanvasVector &worldPosition) {
        const QPointF position = view.MapFromWorld(worldPosition);
        QMouseEvent press(QEvent::MouseButtonPress,
                          position, position, position,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            position, position, position,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    clickAt(mini_graph::CanvasVector(-150.0, -85.0));
    QVERIFY(document.IsItemSelected(ellipseId));
    document.ClearSelection();
    clickAt(mini_graph::CanvasVector(-150.0, -100.0));
    QVERIFY(!document.IsItemSelected(ellipseId));

    clickAt(mini_graph::CanvasVector(50.0 + 30.0 / std::sqrt(2.0),
                                    -100.0 + 30.0 / std::sqrt(2.0)));
    QVERIFY(document.IsItemSelected(polylineId));
    document.ClearSelection();

    clickAt(mini_graph::CanvasVector(-50.0, -80.0));
    QVERIFY(document.IsItemSelected(splineId));
    document.ClearSelection();
    clickAt(mini_graph::CanvasVector(-50.0, -60.0));
    QVERIFY(!document.IsItemSelected(splineId));

    clickAt(mini_graph::CanvasVector(150.0, -100.0));
    QVERIFY(document.IsItemSelected(groupId));
    QCOMPARE(document.GetSelectedItemCount(), 1);
}

void CanvasCoreTest::CanvasViewWindowSelectionSelectsAllCanvasEntityTypes()
{
    mini_graph::CanvasDocument document;
    const qreal pi = std::acos(-1.0);
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(-150.0, 100.0));
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-100.0, -50.0),
                mini_graph::CanvasVector(100.0, -50.0));
    const mini_graph::ItemId arcId = document.AddArc(
                mini_graph::CanvasVector(0.0, 100.0), 40.0, 0.0, pi / 2.0);
    const mini_graph::ItemId circleId = document.AddCircle(
                mini_graph::CanvasVector(150.0, 100.0), 30.0);

    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(-100.0, 0.0),
                                mini_graph::CanvasVector(20.0, 0.0),
                                0.5, false));
    const mini_graph::ItemId ellipseId = document.AddEntity(ellipse);

    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasArc(
                                    mini_graph::CanvasVector(120.0, -10.0),
                                    15.0, 0.0, pi / 2.0)));
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);

    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-30.0, 30.0)
                           << mini_graph::CanvasVector(0.0, 60.0)
                           << mini_graph::CanvasVector(30.0, 30.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, false));
    const mini_graph::ItemId splineId = document.AddEntity(spline);

    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(mini_graph::CanvasLine(
                               mini_graph::CanvasVector(150.0, -60.0),
                               mini_graph::CanvasVector(180.0, -60.0))));
    const mini_graph::ItemId groupId = document.AddEntity(group);
    const mini_graph::ItemId outsideId = document.AddPoint(
                mini_graph::CanvasVector(300.0, 300.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const auto dragSelect = [&view](const mini_graph::CanvasVector &worldStart,
                                    const mini_graph::CanvasVector &worldEnd) {
        const QPointF start = view.MapFromWorld(worldStart);
        const QPointF end = view.MapFromWorld(worldEnd);
        QMouseEvent press(QEvent::MouseButtonPress,
                          start, start, start,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent move(QEvent::MouseMove,
                         end, end, end,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        view.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            end, end, end,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    const auto verifySelected = [&document, pointId, lineId, arcId, circleId,
                                 ellipseId, polylineId, splineId, groupId,
                                 outsideId]() {
        QVERIFY(document.IsItemSelected(pointId));
        QVERIFY(document.IsItemSelected(lineId));
        QVERIFY(document.IsItemSelected(arcId));
        QVERIFY(document.IsItemSelected(circleId));
        QVERIFY(document.IsItemSelected(ellipseId));
        QVERIFY(document.IsItemSelected(polylineId));
        QVERIFY(document.IsItemSelected(splineId));
        QVERIFY(document.IsItemSelected(groupId));
        QVERIFY(!document.IsItemSelected(outsideId));
    };

    dragSelect(mini_graph::CanvasVector(-200.0, -100.0),
               mini_graph::CanvasVector(220.0, 180.0));
    verifySelected();

    document.ClearSelection();
    dragSelect(mini_graph::CanvasVector(220.0, 180.0),
               mini_graph::CanvasVector(-200.0, -100.0));
    verifySelected();
}

void CanvasCoreTest::CanvasViewCrossingSelectionUsesCanvasEntityPaths()
{
    mini_graph::CanvasDocument document;
    const qreal pi = std::acos(-1.0);

    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(-200.0, 0.0),
                                mini_graph::CanvasVector(50.0, 0.0),
                                0.5, false));
    const mini_graph::ItemId ellipseId = document.AddEntity(ellipse);

    mini_graph::CanvasSpline spline;
    QVERIFY(spline.SetData(2,
                           QVector<mini_graph::CanvasVector>()
                           << mini_graph::CanvasVector(-100.0, 0.0)
                           << mini_graph::CanvasVector(-60.0, 80.0)
                           << mini_graph::CanvasVector(-20.0, 0.0),
                           QVector<qreal>() << 0.0 << 0.0 << 0.0
                           << 1.0 << 1.0 << 1.0,
                           QVector<qreal>(), false, false));
    const mini_graph::ItemId splineId = document.AddEntity(spline);

    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasArc(
                                    mini_graph::CanvasVector(100.0, 0.0),
                                    50.0, 0.0, pi / 2.0)));
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);

    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(mini_graph::CanvasLine(
                               mini_graph::CanvasVector(200.0, 150.0),
                               mini_graph::CanvasVector(300.0, 150.0))));
    const mini_graph::ItemId groupId = document.AddEntity(group);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const auto crossingSelect = [&view](const QPointF &worldTopLeft,
                                        const QPointF &worldBottomRight) {
        const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(
                    worldBottomRight.x(), worldTopLeft.y()));
        const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(
                    worldTopLeft.x(), worldBottomRight.y()));
        QMouseEvent press(QEvent::MouseButtonPress,
                          start, start, start,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent move(QEvent::MouseMove,
                         end, end, end,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        view.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            end, end, end,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    crossingSelect(QPointF(-210.0, -10.0), QPointF(-190.0, 10.0));
    QVERIFY(!document.IsItemSelected(ellipseId));
    crossingSelect(QPointF(-210.0, 20.0), QPointF(-190.0, 30.0));
    QVERIFY(document.IsItemSelected(ellipseId));

    document.ClearSelection();
    crossingSelect(QPointF(-70.0, 60.0), QPointF(-50.0, 70.0));
    QVERIFY(!document.IsItemSelected(splineId));
    crossingSelect(QPointF(-70.0, 35.0), QPointF(-50.0, 45.0));
    QVERIFY(document.IsItemSelected(splineId));

    document.ClearSelection();
    crossingSelect(QPointF(130.0, 30.0), QPointF(140.0, 40.0));
    QVERIFY(document.IsItemSelected(polylineId));

    document.ClearSelection();
    crossingSelect(QPointF(240.0, 145.0), QPointF(260.0, 155.0));
    QVERIFY(document.IsItemSelected(groupId));
    QCOMPARE(document.GetSelectedItemCount(), 1);
}

void CanvasCoreTest::CanvasViewEntityHitTestRespectsGroupVisibility()
{
    mini_graph::CanvasDocument document;

    mini_graph::CanvasLine hiddenChild(mini_graph::CanvasVector(-100.0, 0.0),
                                       mini_graph::CanvasVector(-50.0, 0.0));
    mini_graph::StruCanvasStyle hiddenChildStyle = hiddenChild.GetStyle();
    hiddenChildStyle.visible = false;
    QVERIFY(hiddenChild.SetStyle(hiddenChildStyle));
    mini_graph::CanvasGroupContainer groupWithHiddenChild;
    QVERIFY(groupWithHiddenChild.AddChild(hiddenChild));
    const mini_graph::ItemId hiddenChildGroupId =
            document.AddEntity(groupWithHiddenChild);

    mini_graph::CanvasGroupContainer hiddenGroup;
    QVERIFY(hiddenGroup.AddChild(mini_graph::CanvasLine(
                                     mini_graph::CanvasVector(50.0, 0.0),
                                     mini_graph::CanvasVector(100.0, 0.0))));
    mini_graph::StruCanvasStyle hiddenGroupStyle = hiddenGroup.GetStyle();
    hiddenGroupStyle.visible = false;
    QVERIFY(hiddenGroup.SetStyle(hiddenGroupStyle));
    const mini_graph::ItemId hiddenGroupId = document.AddEntity(hiddenGroup);

    mini_graph::CanvasGroupContainer visibleGroup;
    QVERIFY(visibleGroup.AddChild(mini_graph::CanvasLine(
                                      mini_graph::CanvasVector(150.0, 0.0),
                                      mini_graph::CanvasVector(200.0, 0.0))));
    const mini_graph::ItemId visibleGroupId = document.AddEntity(visibleGroup);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const auto clickAt = [&view](const mini_graph::CanvasVector &worldPosition) {
        const QPointF position = view.MapFromWorld(worldPosition);
        QMouseEvent press(QEvent::MouseButtonPress,
                          position, position, position,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            position, position, position,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    QVERIFY(hiddenChildGroupId != 0);
    QVERIFY(hiddenGroupId != 0);
    QVERIFY(visibleGroupId != 0);
    clickAt(mini_graph::CanvasVector(175.0, 0.0));
    QVERIFY(document.IsItemSelected(visibleGroupId));
    document.ClearSelection();
    clickAt(mini_graph::CanvasVector(-75.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
    clickAt(mini_graph::CanvasVector(75.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
}

void CanvasCoreTest::CanvasViewSelectsPolylineByStroke()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId openId = document.AddPolyline(
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(-100.0, 0.0)
                << mini_graph::CanvasVector(100.0, 0.0));
    const mini_graph::ItemId closedId = document.AddPolyline(
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(-30.0, -10.0)
                << mini_graph::CanvasVector(30.0, -10.0)
                << mini_graph::CanvasVector(0.0, -50.0),
                true);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const auto clickAt = [&view](const QPointF &position) {
        QMouseEvent press(QEvent::MouseButtonPress,
                          position,
                          position,
                          position,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            position,
                            position,
                            position,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    QVERIFY(openId != 0);
    QVERIFY(closedId != 0);
    clickAt(view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0)));
    QVERIFY(document.IsItemSelected(openId));

    document.ClearSelection();
    clickAt(view.MapFromWorld(mini_graph::CanvasVector(-15.0, -30.0)));
    QVERIFY(document.IsItemSelected(closedId));
}

void CanvasCoreTest::CanvasViewSelectionSkipsInvisiblePolylineSegments()
{
    const qreal pi = std::acos(-1.0);
    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(-80.0, 0.0),
                                    mini_graph::CanvasVector(-40.0, 0.0))));

    mini_graph::CanvasLine hiddenLine(mini_graph::CanvasVector(-40.0, 0.0),
                                      mini_graph::CanvasVector(-20.0, 0.0));
    mini_graph::StruCanvasStyle hiddenLineStyle = hiddenLine.GetStyle();
    hiddenLineStyle.visible = false;
    QVERIFY(hiddenLine.SetStyle(hiddenLineStyle));
    QVERIFY(polyline.AddSegment(hiddenLine));

    mini_graph::CanvasArc hiddenArc(mini_graph::CanvasVector(-10.0, 0.0),
                                    10.0, pi, 2.0 * pi);
    mini_graph::StruCanvasStyle hiddenArcStyle = hiddenArc.GetStyle();
    hiddenArcStyle.visible = false;
    QVERIFY(hiddenArc.SetStyle(hiddenArcStyle));
    QVERIFY(polyline.AddSegment(hiddenArc));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);
    QVERIFY(polylineId != 0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const auto clickAt = [&view](const mini_graph::CanvasVector &worldPosition) {
        const QPointF position = view.MapFromWorld(worldPosition);
        QMouseEvent press(QEvent::MouseButtonPress,
                          position, position, position,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            position, position, position,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };
    const auto dragSelect = [&view](const mini_graph::CanvasVector &startWorld,
                                    const mini_graph::CanvasVector &endWorld) {
        const QPointF start = view.MapFromWorld(startWorld);
        const QPointF end = view.MapFromWorld(endWorld);
        QMouseEvent press(QEvent::MouseButtonPress,
                          start, start, start,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent move(QEvent::MouseMove,
                         end, end, end,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        view.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            end, end, end,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    clickAt(mini_graph::CanvasVector(-30.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
    clickAt(mini_graph::CanvasVector(-10.0, -10.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);

    clickAt(mini_graph::CanvasVector(-60.0, 0.0));
    QVERIFY(document.IsItemSelected(polylineId));

    document.ClearSelection();
    dragSelect(mini_graph::CanvasVector(-90.0, 5.0),
               mini_graph::CanvasVector(-30.0, -5.0));
    QVERIFY(document.IsItemSelected(polylineId));

    document.ClearSelection();
    dragSelect(mini_graph::CanvasVector(5.0, 5.0),
               mini_graph::CanvasVector(-35.0, -12.0));
    QVERIFY(!document.IsItemSelected(polylineId));
}

void CanvasCoreTest::CanvasViewHitTestIgnoresInvisibleGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-20.0, 0.0),
                mini_graph::CanvasVector(20.0, 0.0));
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(lineId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(lineId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF position = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      position,
                      position,
                      position,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        position,
                        position,
                        position,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);

    QCOMPARE(document.GetSelectedItemCount(), 0);
}

void CanvasCoreTest::CanvasViewHitTestUsesPointDisplaySize()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(0.0, 0.0));
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(pointId, &style));
    style.pointSize = 24.0;
    QVERIFY(document.SetItemStyle(pointId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF pointPosition = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    const QPointF clickPosition = pointPosition + QPointF(10.0, 0.0);
    QMouseEvent press(QEvent::MouseButtonPress,
                      clickPosition,
                      clickPosition,
                      clickPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        clickPosition,
                        clickPosition,
                        clickPosition,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);

    QVERIFY(document.IsItemSelected(pointId));
}

void CanvasCoreTest::CanvasViewHitTestRejectsNonFiniteDistances()
{
    mini_graph::CanvasDocument document;
    const qreal maximum = std::numeric_limits<qreal>::max();
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(maximum, 0.0));
    QVERIFY(pointId != 0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.CenterOnWorld(mini_graph::CanvasVector(-maximum, 0.0));
    view.SetDocument(&document);

    const QPointF clickPosition(400.0, 300.0);
    QMouseEvent press(QEvent::MouseButtonPress,
                      clickPosition,
                      clickPosition,
                      clickPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        clickPosition,
                        clickPosition,
                        clickPosition,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);

    QVERIFY(!document.IsItemSelected(pointId));
}

void CanvasCoreTest::CanvasViewHitTestUsesLineDisplayWidth()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-40.0, 0.0),
                mini_graph::CanvasVector(40.0, 0.0));
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(lineId, &style));
    style.strokeWidth = 20.0;
    QVERIFY(document.SetItemStyle(lineId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF clickPosition = view.MapFromWorld(mini_graph::CanvasVector(0.0, 9.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      clickPosition,
                      clickPosition,
                      clickPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        clickPosition,
                        clickPosition,
                        clickPosition,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);

    QVERIFY(document.IsItemSelected(lineId));
}

void CanvasCoreTest::CanvasViewMaximumDisplaySizeCacheInvalidatesOnDocumentChanges()
{
    const auto addNestedGroup = [](mini_graph::CanvasDocument *document,
                                   qreal strokeWidth) {
        if (!document) {
            return mini_graph::ItemId(0);
        }
        mini_graph::CanvasLine line(mini_graph::CanvasVector(0.0, 0.0),
                                    mini_graph::CanvasVector(40.0, 0.0));
        mini_graph::StruCanvasStyle lineStyle = line.GetStyle();
        lineStyle.strokeWidth = strokeWidth;
        if (!line.SetStyle(lineStyle)) {
            return mini_graph::ItemId(0);
        }

        mini_graph::CanvasGroupContainer innerGroup;
        if (!innerGroup.AddChild(line)) {
            return mini_graph::ItemId(0);
        }
        mini_graph::CanvasGroupContainer outerGroup;
        if (!outerGroup.AddChild(innerGroup)) {
            return mini_graph::ItemId(0);
        }
        return document->AddEntity(outerGroup);
    };
    const auto replaceNestedLineWidth = [](
            mini_graph::CanvasDocument *document,
            mini_graph::ItemId itemId,
            qreal strokeWidth) {
        if (!document) {
            return false;
        }
        const mini_graph::CanvasEntity *entity = document->GetEntity(itemId);
        if (!entity) {
            return false;
        }
        QSharedPointer<mini_graph::CanvasEntity> outerClone = entity->Clone();
        mini_graph::CanvasGroupContainer *outerGroup =
                dynamic_cast<mini_graph::CanvasGroupContainer *>(
                    outerClone.data());
        const mini_graph::CanvasGroupContainer *storedInnerGroup = outerGroup
                ? dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                    outerGroup->GetChild(0)) : nullptr;
        QSharedPointer<mini_graph::CanvasEntity> innerClone =
                storedInnerGroup ? storedInnerGroup->Clone()
                                 : QSharedPointer<mini_graph::CanvasEntity>();
        mini_graph::CanvasGroupContainer *innerGroup =
                dynamic_cast<mini_graph::CanvasGroupContainer *>(
                    innerClone.data());
        const mini_graph::CanvasLine *storedLine = innerGroup
                ? dynamic_cast<const mini_graph::CanvasLine *>(
                    innerGroup->GetChild(0)) : nullptr;
        if (!outerGroup || !innerGroup || !storedLine) {
            return false;
        }

        mini_graph::CanvasLine replacementLine(
                    storedLine->GetStartPoint(),
                    storedLine->GetEndPoint());
        mini_graph::StruCanvasStyle lineStyle = storedLine->GetStyle();
        lineStyle.strokeWidth = strokeWidth;
        if (!replacementLine.SetStyle(lineStyle)
                || !innerGroup->ReplaceChild(0, replacementLine)
                || !outerGroup->ReplaceChild(0, *innerGroup)) {
            return false;
        }
        return document->ReplaceEntity(itemId, *outerGroup);
    };
    const auto click = [](CanvasViewHarness *view, const QPointF &position) {
        QMouseEvent press(QEvent::MouseButtonPress,
                          position,
                          position,
                          position,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        view->mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            position,
                            position,
                            position,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        view->mouseReleaseEvent(&release);
    };

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId itemId = addNestedGroup(&document, 2.0);
    QVERIFY(itemId != 0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    const QPointF clickPosition = view.MapFromWorld(
                mini_graph::CanvasVector(20.0, 12.0));

    click(&view, clickPosition);
    QVERIFY(!document.IsItemSelected(itemId));

    QVERIFY(replaceNestedLineWidth(&document, itemId, 30.0));
    document.ClearSelection();
    click(&view, clickPosition);
    QVERIFY(document.IsItemSelected(itemId));

    QVERIFY(replaceNestedLineWidth(&document, itemId, 2.0));
    document.ClearSelection();
    click(&view, clickPosition);
    QVERIFY(!document.IsItemSelected(itemId));

    mini_graph::CanvasDocument replacementDocument;
    const mini_graph::ItemId replacementItemId =
            addNestedGroup(&replacementDocument, 30.0);
    QCOMPARE(replacementItemId, itemId);
    view.SetDocument(&replacementDocument);
    click(&view, clickPosition);
    QVERIFY(replacementDocument.IsItemSelected(replacementItemId));
}

void CanvasCoreTest::CanvasViewMaximumDisplaySizeCacheInvalidatesOnPolylineReplacement()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasLine originalLine(mini_graph::CanvasVector(0.0, 0.0),
                                        mini_graph::CanvasVector(40.0, 0.0));
    mini_graph::StruCanvasStyle lineStyle = originalLine.GetStyle();
    lineStyle.strokeWidth = 2.0;
    QVERIFY(originalLine.SetStyle(lineStyle));
    mini_graph::CanvasPolyline originalPolyline;
    mini_graph::StruCanvasStyle polylineStyle = originalPolyline.GetStyle();
    polylineStyle.strokeWidth = 2.0;
    QVERIFY(originalPolyline.SetStyle(polylineStyle));
    QVERIFY(originalPolyline.AddSegment(originalLine));
    const mini_graph::ItemId itemId = document.AddEntity(originalPolyline);
    QVERIFY(itemId != 0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    const QPointF clickPosition = view.MapFromWorld(
                mini_graph::CanvasVector(20.0, 12.0));
    const auto click = [&view, &clickPosition]() {
        QMouseEvent press(QEvent::MouseButtonPress,
                          clickPosition,
                          clickPosition,
                          clickPosition,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            clickPosition,
                            clickPosition,
                            clickPosition,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    click();
    QVERIFY(!document.IsItemSelected(itemId));

    QSharedPointer<mini_graph::CanvasEntity> updatedEntity =
            document.GetEntity(itemId)->Clone();
    mini_graph::CanvasPolyline *updatedPolyline =
            dynamic_cast<mini_graph::CanvasPolyline *>(updatedEntity.data());
    QVERIFY(updatedPolyline);
    polylineStyle = updatedPolyline->GetStyle();
    polylineStyle.strokeWidth = 30.0;
    QVERIFY(updatedPolyline->SetStyle(polylineStyle));
    QVERIFY(document.ReplaceEntity(itemId, *updatedPolyline));

    document.ClearSelection();
    click();
    QVERIFY(document.IsItemSelected(itemId));
}

#ifdef MINIGRAPH_ENABLE_PERF_PROFILING
void CanvasCoreTest::CanvasViewMaximumDisplaySizeCacheProfilingBypassPreservesHitTesting()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasLine line(mini_graph::CanvasVector(0.0, 0.0),
                                mini_graph::CanvasVector(40.0, 0.0));
    mini_graph::StruCanvasStyle lineStyle = line.GetStyle();
    lineStyle.strokeWidth = 2.0;
    QVERIFY(line.SetStyle(lineStyle));

    mini_graph::CanvasPolyline polyline;
    mini_graph::StruCanvasStyle polylineStyle = polyline.GetStyle();
    polylineStyle.strokeWidth = 2.0;
    QVERIFY(polyline.SetStyle(polylineStyle));
    QVERIFY(polyline.AddSegment(line));
    const mini_graph::ItemId itemId = document.AddEntity(polyline);
    QVERIFY(itemId != 0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    const QPointF clickPosition = view.MapFromWorld(
                mini_graph::CanvasVector(20.0, 12.0));
    const auto click = [&view, &clickPosition]() {
        QMouseEvent press(QEvent::MouseButtonPress,
                          clickPosition,
                          clickPosition,
                          clickPosition,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            clickPosition,
                            clickPosition,
                            clickPosition,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    click();
    QVERIFY(!document.IsItemSelected(itemId));
    mini_graph::CanvasView::CanvasHitTestProfile profile =
            view.GetHitTestProfile();
    QCOMPARE(profile.maximumDisplaySizeCacheLookupCount, quint64(2));
    QCOMPARE(profile.maximumDisplaySizeCacheMissCount, quint64(0));
    QCOMPARE(profile.maximumDisplaySizeCalculationCallCount, quint64(0));

    QSharedPointer<mini_graph::CanvasEntity> updatedEntity =
            document.GetEntity(itemId)->Clone();
    mini_graph::CanvasPolyline *updatedPolyline =
            dynamic_cast<mini_graph::CanvasPolyline *>(updatedEntity.data());
    QVERIFY(updatedPolyline);
    polylineStyle = updatedPolyline->GetStyle();
    polylineStyle.strokeWidth = 30.0;
    QVERIFY(updatedPolyline->SetStyle(polylineStyle));
    QVERIFY(document.ReplaceEntity(itemId, *updatedPolyline));

    document.ClearSelection();
    view.ResetHitTestProfile();
    view.SetHitTestMaximumDisplaySizeCacheBypassedForProfiling(true);
    click();
    QVERIFY(document.IsItemSelected(itemId));
    profile = view.GetHitTestProfile();
    QCOMPARE(profile.maximumDisplaySizeCacheLookupCount, quint64(0));
    QCOMPARE(profile.maximumDisplaySizeCalculationCallCount, quint64(2));

    document.ClearSelection();
    view.ResetHitTestProfile();
    view.SetHitTestMaximumDisplaySizeCacheBypassedForProfiling(false);
    click();
    QVERIFY(document.IsItemSelected(itemId));
    profile = view.GetHitTestProfile();
    QCOMPARE(profile.maximumDisplaySizeCacheLookupCount, quint64(2));
    QCOMPARE(profile.maximumDisplaySizeCacheMissCount, quint64(0));
    QCOMPARE(profile.maximumDisplaySizeCalculationCallCount, quint64(0));
}

void CanvasCoreTest::CanvasViewMaximumDisplaySizeCacheProfileReportsMixedCapacity()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasLine polylineSegment(
                mini_graph::CanvasVector(-20.0, 0.0),
                mini_graph::CanvasVector(20.0, 0.0));
    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(polylineSegment));
    QVERIFY(document.AddEntity(polyline) != 0);

    mini_graph::CanvasGroupContainer group;
    mini_graph::CanvasLine groupChild(
                mini_graph::CanvasVector(980.0, 1000.0),
                mini_graph::CanvasVector(1020.0, 1000.0));
    QVERIFY(group.AddChild(groupChild));
    QVERIFY(document.AddEntity(group) != 0);
    QVERIFY(document.AddEntity(mini_graph::CanvasLine(
                                  mini_graph::CanvasVector(-20.0, 1000.0),
                                  mini_graph::CanvasVector(20.0, 1000.0))) != 0);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    const QPointF clickPosition = view.MapFromWorld(
                mini_graph::CanvasVector(0.0, 12.0));
    QMouseEvent press(QEvent::MouseButtonPress, clickPosition, clickPosition,
                      clickPosition, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, clickPosition,
                        clickPosition, clickPosition, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&release);

    const mini_graph::CanvasView::CanvasHitTestProfile profile =
            view.GetHitTestProfile();
    QCOMPARE(profile.maximumDisplaySizeCacheEntryCount, quint64(2));
    QVERIFY(profile.maximumDisplaySizeCacheCapacity >= quint64(2));
}
#endif

void CanvasCoreTest::CanvasViewWindowSelectionUsesDragDirection()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId containedId = document.AddLine(
                mini_graph::CanvasVector(-100.0, 50.0),
                mini_graph::CanvasVector(100.0, 50.0));
    const mini_graph::ItemId crossingId = document.AddLine(
                mini_graph::CanvasVector(100.0, 50.0),
                mini_graph::CanvasVector(220.0, 50.0));
    const mini_graph::ItemId outsideId = document.AddLine(
                mini_graph::CanvasVector(220.0, 180.0),
                mini_graph::CanvasVector(300.0, 180.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QMouseEvent windowPress(QEvent::MouseButtonPress,
                            QPointF(250.0, 200.0),
                            QPointF(250.0, 200.0),
                            QPointF(250.0, 200.0),
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&windowPress);
    QMouseEvent windowMove(QEvent::MouseMove,
                           QPointF(550.0, 400.0),
                           QPointF(550.0, 400.0),
                           QPointF(550.0, 400.0),
                           Qt::NoButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    view.mouseMoveEvent(&windowMove);
    QMouseEvent windowRelease(QEvent::MouseButtonRelease,
                              QPointF(550.0, 400.0),
                              QPointF(550.0, 400.0),
                              QPointF(550.0, 400.0),
                              Qt::LeftButton,
                              Qt::NoButton,
                              Qt::NoModifier);
    view.mouseReleaseEvent(&windowRelease);

    QVERIFY(document.IsItemSelected(containedId));
    QVERIFY(!document.IsItemSelected(crossingId));
    QVERIFY(!document.IsItemSelected(outsideId));

    QMouseEvent crossingPress(QEvent::MouseButtonPress,
                              QPointF(550.0, 400.0),
                              QPointF(550.0, 400.0),
                              QPointF(550.0, 400.0),
                              Qt::LeftButton,
                              Qt::LeftButton,
                              Qt::NoModifier);
    view.mousePressEvent(&crossingPress);
    QMouseEvent crossingMove(QEvent::MouseMove,
                             QPointF(250.0, 200.0),
                             QPointF(250.0, 200.0),
                             QPointF(250.0, 200.0),
                             Qt::NoButton,
                             Qt::LeftButton,
                             Qt::NoModifier);
    view.mouseMoveEvent(&crossingMove);
    QMouseEvent crossingRelease(QEvent::MouseButtonRelease,
                                QPointF(250.0, 200.0),
                                QPointF(250.0, 200.0),
                                QPointF(250.0, 200.0),
                                Qt::LeftButton,
                                Qt::NoButton,
                                Qt::NoModifier);
    view.mouseReleaseEvent(&crossingRelease);

    QVERIFY(document.IsItemSelected(containedId));
    QVERIFY(document.IsItemSelected(crossingId));
    QVERIFY(!document.IsItemSelected(outsideId));
}

void CanvasCoreTest::CanvasViewWindowSelectionSelectsAllGeometryTypes()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(-150.0, 100.0));
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-100.0, -50.0),
                mini_graph::CanvasVector(100.0, -50.0));
    const mini_graph::ItemId arcId = document.AddArc(
                                                     mini_graph::CanvasVector(0.0, 100.0),
                                                     40.0,
                                                     0.0,
                                                     1.5707963267948966);
    const mini_graph::ItemId circleId = document.AddCircle(
                                                            mini_graph::CanvasVector(150.0, 100.0),
                                                            30.0);
    const mini_graph::ItemId outsideId = document.AddPoint(
                mini_graph::CanvasVector(300.0, 300.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    auto dragSelect = [&view](const mini_graph::CanvasVector &worldStart,
                              const mini_graph::CanvasVector &worldEnd) {
        const QPointF start = view.MapFromWorld(worldStart);
        const QPointF end = view.MapFromWorld(worldEnd);
        QMouseEvent press(QEvent::MouseButtonPress,
                          start,
                          start,
                          start,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent move(QEvent::MouseMove,
                         end,
                         end,
                         end,
                         Qt::NoButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        view.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            end,
                            end,
                            end,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    dragSelect(mini_graph::CanvasVector(-200.0, -100.0),
               mini_graph::CanvasVector(220.0, 180.0));
    QVERIFY(document.IsItemSelected(pointId));
    QVERIFY(document.IsItemSelected(lineId));
    QVERIFY(document.IsItemSelected(arcId));
    QVERIFY(document.IsItemSelected(circleId));
    QVERIFY(!document.IsItemSelected(outsideId));

    document.ClearSelection();
    dragSelect(mini_graph::CanvasVector(220.0, 180.0),
               mini_graph::CanvasVector(-200.0, -100.0));
    QVERIFY(document.IsItemSelected(pointId));
    QVERIFY(document.IsItemSelected(lineId));
    QVERIFY(document.IsItemSelected(arcId));
    QVERIFY(document.IsItemSelected(circleId));
    QVERIFY(!document.IsItemSelected(outsideId));
}

void CanvasCoreTest::CanvasViewWindowSelectionSelectsPolylines()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId containedOpenId = document.AddPolyline(
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(-30.0, 0.0)
                << mini_graph::CanvasVector(0.0, 30.0)
                << mini_graph::CanvasVector(30.0, 0.0));
    const mini_graph::ItemId crossingClosedId = document.AddPolyline(
                QVector<mini_graph::CanvasVector>()
                << mini_graph::CanvasVector(60.0, -10.0)
                << mini_graph::CanvasVector(0.0, -60.0)
                << mini_graph::CanvasVector(-60.0, -10.0),
                true);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const auto dragSelect = [&view](const QPointF &start,
                                    const QPointF &end) {
        QMouseEvent press(QEvent::MouseButtonPress,
                          start,
                          start,
                          start,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        view.mousePressEvent(&press);
        QMouseEvent move(QEvent::MouseMove,
                         end,
                         end,
                         end,
                         Qt::NoButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        view.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            end,
                            end,
                            end,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };

    QVERIFY(containedOpenId != 0);
    QVERIFY(crossingClosedId != 0);

    dragSelect(view.MapFromWorld(mini_graph::CanvasVector(-50.0, 50.0)),
               view.MapFromWorld(mini_graph::CanvasVector(50.0, -50.0)));
    QVERIFY(document.IsItemSelected(containedOpenId));
    QVERIFY(!document.IsItemSelected(crossingClosedId));

    document.ClearSelection();
    dragSelect(view.MapFromWorld(mini_graph::CanvasVector(50.0, -50.0)),
               view.MapFromWorld(mini_graph::CanvasVector(-50.0, 50.0)));
    QVERIFY(document.IsItemSelected(crossingClosedId));
}

void CanvasCoreTest::CanvasViewWindowSelectionIgnoresInvisibleGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId circleId = document.AddCircle(
                                                            mini_graph::CanvasVector(0.0, 0.0),
                                                            5.0);
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(circleId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(circleId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(-20.0, -20.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(20.0, 20.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);

    QCOMPARE(document.GetSelectedItemCount(), 0);
}

void CanvasCoreTest::CanvasViewSelectionBoxUsesCadLabStyle()
{
    CanvasViewHarness leftToRightView;
    leftToRightView.resize(800, 600);
    leftToRightView.setUpdatesEnabled(false);

    QMouseEvent leftPress(QEvent::MouseButtonPress,
                          QPointF(250.0, 200.0),
                          QPointF(250.0, 200.0),
                          QPointF(250.0, 200.0),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    leftToRightView.mousePressEvent(&leftPress);
    QMouseEvent leftMove(QEvent::MouseMove,
                         QPointF(550.0, 400.0),
                         QPointF(550.0, 400.0),
                         QPointF(550.0, 400.0),
                         Qt::NoButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
    leftToRightView.mouseMoveEvent(&leftMove);

    QImage blueImage(leftToRightView.size(), QImage::Format_ARGB32_Premultiplied);
    blueImage.fill(Qt::black);
    QPainter bluePainter(&blueImage);
    bluePainter.setRenderHint(QPainter::Antialiasing, false);
    leftToRightView.DrawSelectionBox(bluePainter);
    bluePainter.end();
    const QColor blueFill = blueImage.pixelColor(400, 300);
    QVERIFY(blueFill.blue() > blueFill.red() + 20);
    QVERIFY(blueFill.blue() > blueFill.green() + 20);
    QVERIFY(blueImage.pixelColor(400, 200).red() > 200);
    QVERIFY(blueImage.pixelColor(400, 200).green() > 200);
    QVERIFY(blueImage.pixelColor(400, 200).blue() > 200);

    CanvasViewHarness rightToLeftView;
    rightToLeftView.resize(800, 600);
    rightToLeftView.setUpdatesEnabled(false);
    QMouseEvent rightPress(QEvent::MouseButtonPress,
                           QPointF(550.0, 400.0),
                           QPointF(550.0, 400.0),
                           QPointF(550.0, 400.0),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    rightToLeftView.mousePressEvent(&rightPress);
    QMouseEvent rightMove(QEvent::MouseMove,
                          QPointF(250.0, 200.0),
                          QPointF(250.0, 200.0),
                          QPointF(250.0, 200.0),
                          Qt::NoButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    rightToLeftView.mouseMoveEvent(&rightMove);

    QImage greenImage(rightToLeftView.size(), QImage::Format_ARGB32_Premultiplied);
    greenImage.fill(Qt::black);
    QPainter greenPainter(&greenImage);
    greenPainter.setRenderHint(QPainter::Antialiasing, false);
    rightToLeftView.DrawSelectionBox(greenPainter);
    greenPainter.end();
    const QColor greenFill = greenImage.pixelColor(400, 300);
    QVERIFY(greenFill.green() > greenFill.red() + 20);
    QVERIFY(greenFill.green() > greenFill.blue() + 20);
}

void CanvasCoreTest::CanvasViewDeleteKeyRemovesSelectedGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(-20.0, 10.0));
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-10.0, 0.0),
                mini_graph::CanvasVector(10.0, 0.0));
    document.SelectItems(QVector<mini_graph::ItemId>() << pointId << lineId,
                         false,
                         false);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QKeyEvent deleteEvent(QEvent::KeyPress,
                          Qt::Key_Delete,
                          Qt::NoModifier);
    view.keyPressEvent(&deleteEvent);
    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(document.GetSelectedItemCount(), 0);

    const mini_graph::ItemId circleId = document.AddCircle(
                                                            mini_graph::CanvasVector(30.0, 0.0),
                                                            5.0);
    document.SelectItem(circleId);
    QKeyEvent backspaceEvent(QEvent::KeyPress,
                             Qt::Key_Backspace,
                             Qt::NoModifier);
    view.keyPressEvent(&backspaceEvent);
    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(document.GetSelectedItemCount(), 0);
}

void CanvasCoreTest::CanvasViewEscapeClearsSelection()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId firstId = document.AddLine(
                mini_graph::CanvasVector(-20.0, 0.0),
                mini_graph::CanvasVector(20.0, 0.0));
    const mini_graph::ItemId secondId = document.AddCircle(
                                                           mini_graph::CanvasVector(40.0, 0.0),
                                                           5.0);
    document.SelectItems(QVector<mini_graph::ItemId>() << firstId << secondId,
                         false,
                         false);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    QSignalSpy cancelSpy(&view, &mini_graph::CanvasView::InteractionCancelled);

    QKeyEvent escapeEvent(QEvent::KeyPress,
                          Qt::Key_Escape,
                          Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);

    QCOMPARE(document.GetSelectedItemCount(), 0);
    QCOMPARE(cancelSpy.count(), 1);
}

void CanvasCoreTest::CanvasViewDragMovesSelectedGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(
                mini_graph::CanvasVector(-20.0, 10.0));
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-10.0, 0.0),
                mini_graph::CanvasVector(10.0, 0.0));
    const mini_graph::ItemId arcId = document.AddArc(
                                                     mini_graph::CanvasVector(0.0, 30.0),
                                                     5.0,
                                                     0.0,
                                                     1.5707963267948966);
    const mini_graph::ItemId circleId = document.AddCircle(
                                                            mini_graph::CanvasVector(30.0, 0.0),
                                                            4.0);
    document.SelectItems(QVector<mini_graph::ItemId>()
                          << pointId << lineId << arcId << circleId,
                          false,
                          false);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    const QPointF end = start + QPointF(80.0, -40.0);
    const mini_graph::CanvasVector worldDelta = view.MapToWorld(end)
            - view.MapToWorld(start);

    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::StruPointPrimitive &point = document.GetPointPrimitives().at(0);
    const mini_graph::StruLinePrimitive &line = document.GetLinePrimitives().at(0);
    const mini_graph::StruArcPrimitive &arc = document.GetArcPrimitives().at(0);
    const mini_graph::StruCirclePrimitive &circle = document.GetCirclePrimitives().at(0);
    QVERIFY(qAbs(point.geometry.position.GetX() - (-20.0 + worldDelta.GetX())) < 0.000001);
    QVERIFY(qAbs(point.geometry.position.GetY() - (10.0 + worldDelta.GetY())) < 0.000001);
    QVERIFY(qAbs(line.geometry.startPoint.GetX() - (-10.0 + worldDelta.GetX())) < 0.000001);
    QVERIFY(qAbs(line.geometry.startPoint.GetY() - worldDelta.GetY()) < 0.000001);
    QVERIFY(qAbs(line.geometry.endPoint.GetX() - (10.0 + worldDelta.GetX())) < 0.000001);
    QVERIFY(qAbs(line.geometry.endPoint.GetY() - worldDelta.GetY()) < 0.000001);
    QVERIFY(qAbs(arc.geometry.center.GetX() - worldDelta.GetX()) < 0.000001);
    QVERIFY(qAbs(arc.geometry.center.GetY() - (30.0 + worldDelta.GetY())) < 0.000001);
    QVERIFY(qAbs(circle.geometry.center.GetX() - (30.0 + worldDelta.GetX())) < 0.000001);
    QVERIFY(qAbs(circle.geometry.center.GetY() - worldDelta.GetY()) < 0.000001);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
    QCOMPARE(document.GetSelectedItemCount(), 4);
}

void CanvasCoreTest::CanvasViewMovesPolylineVerticesTogether()
{
    mini_graph::CanvasDocument document;
    mini_graph::StruPolylineGeometry geometry;
    geometry.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-20.0, 0.0)
            << mini_graph::CanvasVector(20.0, 0.0)
            << mini_graph::CanvasVector(20.0, 20.0);
    geometry.closed = true;
    const mini_graph::ItemId polylineId = document.AddPolyline(geometry);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    const QPointF end = start + QPointF(40.0, -30.0);
    const mini_graph::CanvasVector worldDelta = view.MapToWorld(end)
            - view.MapToWorld(start);
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    mini_graph::StruPolylineGeometry movedGeometry;
    QVERIFY(document.GetPolylineGeometry(polylineId, &movedGeometry));
    QCOMPARE(movedGeometry.vertices,
             QVector<mini_graph::CanvasVector>()
             << mini_graph::CanvasVector(-20.0, 0.0) + worldDelta
             << mini_graph::CanvasVector(20.0, 0.0) + worldDelta
             << mini_graph::CanvasVector(20.0, 20.0) + worldDelta);
    QVERIFY(movedGeometry.closed);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
}

void CanvasCoreTest::CanvasViewDragsPolylineVertexHandle()
{
    mini_graph::CanvasDocument document;
    mini_graph::StruPolylineGeometry geometry;
    geometry.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-20.0, 0.0)
            << mini_graph::CanvasVector(0.0, 20.0)
            << mini_graph::CanvasVector(20.0, 0.0);
    geometry.closed = true;
    const mini_graph::ItemId polylineId = document.AddPolyline(geometry);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(geometry.vertices.at(1));
    const mini_graph::CanvasVector editedVertex(5.0, 30.0);
    const QPointF end = view.MapFromWorld(editedVertex);
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    mini_graph::StruPolylineGeometry editedGeometry;
    QVERIFY(document.GetPolylineGeometry(polylineId, &editedGeometry));
    QCOMPARE(editedGeometry.vertices.at(0), geometry.vertices.at(0));
    QCOMPARE(editedGeometry.vertices.at(1), editedVertex);
    QCOMPARE(editedGeometry.vertices.at(2), geometry.vertices.at(2));
    QVERIFY(editedGeometry.closed);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
}

void CanvasCoreTest::CanvasViewEscapeRestoresPolylineVertexEdit()
{
    mini_graph::CanvasDocument document;
    mini_graph::StruPolylineGeometry geometry;
    geometry.vertices = QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-20.0, 0.0)
            << mini_graph::CanvasVector(0.0, 20.0)
            << mini_graph::CanvasVector(20.0, 0.0);
    geometry.closed = true;
    const mini_graph::ItemId polylineId = document.AddPolyline(geometry);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(geometry.vertices.at(1));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(5.0, 30.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    mini_graph::StruPolylineGeometry changedGeometry;
    QVERIFY(document.GetPolylineGeometry(polylineId, &changedGeometry));
    QCOMPARE(changedGeometry.vertices.at(0), geometry.vertices.at(0));
    QVERIFY(changedGeometry.vertices.at(1) == mini_graph::CanvasVector(5.0, 30.0));
    QCOMPARE(changedGeometry.vertices.at(2), geometry.vertices.at(2));

    QKeyEvent escapeEvent(QEvent::KeyPress,
                          Qt::Key_Escape,
                          Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);

    mini_graph::StruPolylineGeometry restoredGeometry;
    QVERIFY(document.GetPolylineGeometry(polylineId, &restoredGeometry));
    QCOMPARE(restoredGeometry.vertices, geometry.vertices);
    QVERIFY(restoredGeometry.closed);
}

void CanvasCoreTest::CanvasViewDragsEllipseMajorAxisHandle()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                mini_graph::CanvasVector(20.0, 0.0),
                                0.5, false));
    ellipse.SetTag(QStringLiteral("editable-ellipse"));
    const mini_graph::ItemId ellipseId = document.AddEntity(ellipse);
    QVERIFY(ellipseId != 0);
    document.SelectItem(ellipseId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(20.0, 0.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(30.0, 10.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasEllipse *editedEllipse =
            dynamic_cast<const mini_graph::CanvasEllipse *>(
                document.GetEntity(ellipseId));
    QVERIFY(editedEllipse);
    QVERIFY(editedEllipse->GetCenter() == mini_graph::CanvasVector(0.0, 0.0));
    QVERIFY(editedEllipse->GetMajorAxis() == mini_graph::CanvasVector(30.0, 10.0));
    QCOMPARE(editedEllipse->GetMinorMajorRatio(), 0.5);
    QCOMPARE(editedEllipse->GetTag(), QStringLiteral("editable-ellipse"));

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
}

void CanvasCoreTest::CanvasViewDragsEllipseMinorAxisHandle()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                mini_graph::CanvasVector(20.0, 0.0),
                                0.5, false));
    const mini_graph::ItemId ellipseId = document.AddEntity(ellipse);
    QVERIFY(ellipseId != 0);
    document.SelectItem(ellipseId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(0.0, 10.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(0.0, 15.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasEllipse *editedEllipse =
            dynamic_cast<const mini_graph::CanvasEllipse *>(
                document.GetEntity(ellipseId));
    QVERIFY(editedEllipse);
    QCOMPARE(editedEllipse->GetMinorMajorRatio(), 0.75);
}

void CanvasCoreTest::CanvasViewDragsEllipseArcEndpointHandle()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(0.0, 0.0),
                                mini_graph::CanvasVector(20.0, 0.0),
                                0.5, true, 0.0,
                                1.5707963267948966));
    const mini_graph::ItemId ellipseId = document.AddEntity(ellipse);
    QVERIFY(ellipseId != 0);
    document.SelectItem(ellipseId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(0.0, 10.0));
    const mini_graph::CanvasVector editedEndpoint(14.142135623730951,
                                                   7.0710678118654755);
    const QPointF end = view.MapFromWorld(editedEndpoint);
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasEllipse *editedEllipse =
            dynamic_cast<const mini_graph::CanvasEllipse *>(
                document.GetEntity(ellipseId));
    QVERIFY(editedEllipse);
    QVERIFY2(qAbs(editedEllipse->GetEndParameter()
                  - 0.7853981633974483) < 1.0e-6,
             qPrintable(QString::number(editedEllipse->GetEndParameter(),
                                        'g', 17)));
    QCOMPARE(editedEllipse->GetMinorMajorRatio(), 0.5);
    QCOMPARE(editedEllipse->GetStartParameter(), 0.0);
}

void CanvasCoreTest::CanvasViewEscapeRestoresEllipseHandleEdit()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasEllipse ellipse;
    QVERIFY(ellipse.SetGeometry(mini_graph::CanvasVector(2.0, 3.0),
                                mini_graph::CanvasVector(20.0, 0.0),
                                0.5, false));
    ellipse.SetTag(QStringLiteral("restore-ellipse"));
    const mini_graph::ItemId ellipseId = document.AddEntity(ellipse);
    QVERIFY(ellipseId != 0);
    document.SelectItem(ellipseId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(
                mini_graph::CanvasVector(22.0, 3.0));
    const QPointF end = view.MapFromWorld(
                mini_graph::CanvasVector(35.0, 10.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasEllipse *changedEllipse =
            dynamic_cast<const mini_graph::CanvasEllipse *>(
                document.GetEntity(ellipseId));
    QVERIFY(changedEllipse);
    QVERIFY(changedEllipse->GetMajorAxis()
            != mini_graph::CanvasVector(20.0, 0.0));

    QKeyEvent escapeEvent(QEvent::KeyPress,
                          Qt::Key_Escape,
                          Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);

    const mini_graph::CanvasEllipse *restoredEllipse =
            dynamic_cast<const mini_graph::CanvasEllipse *>(
                document.GetEntity(ellipseId));
    QVERIFY(restoredEllipse);
    QVERIFY(restoredEllipse->GetCenter()
            == mini_graph::CanvasVector(2.0, 3.0));
    QVERIFY(restoredEllipse->GetMajorAxis()
            == mini_graph::CanvasVector(20.0, 0.0));
    QCOMPARE(restoredEllipse->GetMinorMajorRatio(), 0.5);
    QCOMPARE(restoredEllipse->GetTag(), QStringLiteral("restore-ellipse"));
}

void CanvasCoreTest::CanvasViewDragsSplineControlPointHandle()
{
    mini_graph::CanvasDocument document;
    mini_graph::CanvasSpline spline;
    const QVector<mini_graph::CanvasVector> controlPoints =
            QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-10.0, 0.0)
            << mini_graph::CanvasVector(0.0, 20.0)
            << mini_graph::CanvasVector(10.0, 0.0);
    const QVector<qreal> knots = QVector<qreal>()
            << 0.0 << 0.0 << 0.0 << 1.0 << 1.0 << 1.0;
    QVERIFY(spline.SetData(2, controlPoints, knots, QVector<qreal>(),
                           false, false));
    spline.SetTag(QStringLiteral("editable-spline"));
    const mini_graph::ItemId splineId = document.AddEntity(spline);
    QVERIFY(splineId != 0);
    document.SelectItem(splineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(controlPoints.at(1));
    const mini_graph::CanvasVector editedControlPoint(2.0, 30.0);
    const QPointF end = view.MapFromWorld(editedControlPoint);
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasSpline *editedSpline =
            dynamic_cast<const mini_graph::CanvasSpline *>(
                document.GetEntity(splineId));
    QVERIFY(editedSpline);
    QCOMPARE(editedSpline->GetControlPoints().at(0), controlPoints.at(0));
    QVERIFY(editedSpline->GetControlPoints().at(1) == editedControlPoint);
    QCOMPARE(editedSpline->GetControlPoints().at(2), controlPoints.at(2));
    QCOMPARE(editedSpline->GetKnots(), knots);
    QCOMPARE(editedSpline->GetTag(), QStringLiteral("editable-spline"));

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
}

void CanvasCoreTest::CanvasViewEscapeCancelsGeometryMove()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-10.0, 0.0),
                mini_graph::CanvasVector(10.0, 0.0));
    document.SelectItem(lineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    QSignalSpy cancelSpy(&view, &mini_graph::CanvasView::InteractionCancelled);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    const QPointF end = start + QPointF(60.0, -30.0);
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);
    QVERIFY(document.GetLinePrimitives().at(0).geometry.startPoint
            != mini_graph::CanvasVector(-10.0, 0.0));

    QKeyEvent escapeEvent(QEvent::KeyPress,
                          Qt::Key_Escape,
                          Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);

    QVERIFY(document.GetLinePrimitives().at(0).geometry.startPoint
            == mini_graph::CanvasVector(-10.0, 0.0));
    QVERIFY(document.GetLinePrimitives().at(0).geometry.endPoint
            == mini_graph::CanvasVector(10.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QCOMPARE(cancelSpy.count(), 1);
}

void CanvasCoreTest::CanvasViewDragMovesLineEndpointHandle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-10.0, 0.0),
                mini_graph::CanvasVector(10.0, 0.0));
    document.SelectItem(lineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(-10.0, 0.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(-20.0, 15.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::StruLineGeometry geometry =
            document.GetLinePrimitives().at(0).geometry;
    QVERIFY(geometry.startPoint == mini_graph::CanvasVector(-20.0, 15.0));
    QVERIFY(geometry.endPoint == mini_graph::CanvasVector(10.0, 0.0));

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
}

void CanvasCoreTest::CanvasViewDragChangesCircleRadiusHandle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId circleId = document.AddCircle(
                                                           mini_graph::CanvasVector(0.0, 0.0),
                                                           10.0);
    document.SelectItem(circleId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(10.0, 0.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(20.0, 0.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::StruCircleGeometry geometry =
            document.GetCirclePrimitives().at(0).geometry;
    QVERIFY(geometry.center == mini_graph::CanvasVector(0.0, 0.0));
    QCOMPARE(geometry.radius, 20.0);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
}

void CanvasCoreTest::CanvasViewDragChangesArcEndpointHandle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId arcId = document.AddArc(
                                                     mini_graph::CanvasVector(0.0, 0.0),
                                                     10.0,
                                                     0.0,
                                                     1.5707963267948966);
    document.SelectItem(arcId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(10.0, 0.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(20.0, 0.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::StruArcGeometry geometry =
            document.GetArcPrimitives().at(0).geometry;
    QVERIFY(geometry.center == mini_graph::CanvasVector(0.0, 0.0));
    QCOMPARE(geometry.radius, 20.0);
    QCOMPARE(geometry.startAngle, 0.0);
    QCOMPARE(geometry.endAngle, 1.5707963267948966);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        end,
                        end,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
}

void CanvasCoreTest::CanvasViewEscapeCancelsGeometryHandleEdit()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-10.0, 0.0),
                mini_graph::CanvasVector(10.0, 0.0));
    document.SelectItem(lineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    QSignalSpy cancelSpy(&view, &mini_graph::CanvasView::InteractionCancelled);

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(-10.0, 0.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(-20.0, 15.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      start,
                      start,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     end,
                     end,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    QVERIFY(document.GetLinePrimitives().at(0).geometry.startPoint
            == mini_graph::CanvasVector(-20.0, 15.0));
    QVERIFY(document.GetLinePrimitives().at(0).geometry.endPoint
            == mini_graph::CanvasVector(10.0, 0.0));

    QKeyEvent escapeEvent(QEvent::KeyPress,
                          Qt::Key_Escape,
                          Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);

    QVERIFY(document.GetLinePrimitives().at(0).geometry.startPoint
            == mini_graph::CanvasVector(-10.0, 0.0));
    QVERIFY(document.GetLinePrimitives().at(0).geometry.endPoint
            == mini_graph::CanvasVector(10.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QCOMPARE(cancelSpy.count(), 1);
}

void CanvasCoreTest::CanvasViewDrawsEditHandlesForSingleSelection()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-10.0, 0.0),
                mini_graph::CanvasVector(10.0, 0.0));
    document.SelectItem(lineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawEditHandles(painter);
    painter.end();

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(-10.0, 0.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(10.0, 0.0));
    QCOMPARE(image.pixelColor(qRound(start.x()), qRound(start.y())),
             QColor(0, 170, 255));
    QCOMPARE(image.pixelColor(qRound(end.x()), qRound(end.y())),
             QColor(0, 170, 255));
}

void CanvasCoreTest::CanvasViewDoesNotDrawEditHandlesForHiddenPolylineVertices()
{
    mini_graph::CanvasPolyline polyline;
    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(-80.0, 0.0),
                                    mini_graph::CanvasVector(-40.0, 0.0))));

    mini_graph::CanvasLine hiddenFirst(mini_graph::CanvasVector(-40.0, 0.0),
                                       mini_graph::CanvasVector(-20.0, 0.0));
    mini_graph::StruCanvasStyle hiddenFirstStyle = hiddenFirst.GetStyle();
    hiddenFirstStyle.visible = false;
    QVERIFY(hiddenFirst.SetStyle(hiddenFirstStyle));
    QVERIFY(polyline.AddSegment(hiddenFirst));

    mini_graph::CanvasLine hiddenSecond(mini_graph::CanvasVector(-20.0, 0.0),
                                        mini_graph::CanvasVector(0.0, 0.0));
    mini_graph::StruCanvasStyle hiddenSecondStyle = hiddenSecond.GetStyle();
    hiddenSecondStyle.visible = false;
    QVERIFY(hiddenSecond.SetStyle(hiddenSecondStyle));
    QVERIFY(polyline.AddSegment(hiddenSecond));

    QVERIFY(polyline.AddSegment(mini_graph::CanvasLine(
                                    mini_graph::CanvasVector(0.0, 0.0),
                                    mini_graph::CanvasVector(40.0, 0.0))));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);
    QVERIFY(polylineId != 0);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawEditHandles(painter);
    painter.end();

    const QPointF visibleBoundary = view.MapFromWorld(
                mini_graph::CanvasVector(-40.0, 0.0));
    const QPointF hiddenInterior = view.MapFromWorld(
                mini_graph::CanvasVector(-20.0, 0.0));
    QCOMPARE(image.pixelColor(qRound(visibleBoundary.x()),
                              qRound(visibleBoundary.y())),
             QColor(0, 170, 255));
    QCOMPARE(image.pixelColor(qRound(hiddenInterior.x()),
                              qRound(hiddenInterior.y())),
             QColor(Qt::black));
}

void CanvasCoreTest::CanvasViewDrawsCadLabGroupReferencePointAndMovesWholeGroup()
{
    mini_graph::CanvasLine line(mini_graph::CanvasVector(-10.0, 0.0),
                                mini_graph::CanvasVector(10.0, 0.0));
    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(line));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId groupId = document.AddEntity(group);
    QVERIFY(groupId != 0);
    document.SelectItem(groupId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawEditHandles(painter);
    painter.end();

    const QPointF groupReference =
            view.MapFromWorld(mini_graph::CanvasVector(-10.0, 0.0));
    const QPointF childEnd =
            view.MapFromWorld(mini_graph::CanvasVector(10.0, 0.0));
    QCOMPARE(image.pixelColor(qRound(groupReference.x()),
                              qRound(groupReference.y())),
             QColor(0, 170, 255));
    QCOMPARE(image.pixelColor(qRound(childEnd.x()), qRound(childEnd.y())),
             Qt::black);

    const QPointF moveEnd =
            view.MapFromWorld(mini_graph::CanvasVector(-20.0, 10.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      groupReference,
                      groupReference,
                      groupReference,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     moveEnd,
                     moveEnd,
                     moveEnd,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasGroupContainer *movedGroup =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                document.GetEntity(groupId));
    QVERIFY(movedGroup);
    const mini_graph::CanvasLine *movedLine =
            dynamic_cast<const mini_graph::CanvasLine *>(
                movedGroup->GetChild(0));
    QVERIFY(movedLine);
    QVERIFY(movedLine->GetStartPoint()
            == mini_graph::CanvasVector(-20.0, 10.0));
    QVERIFY(movedLine->GetEndPoint()
            == mini_graph::CanvasVector(0.0, 10.0));
}

void CanvasCoreTest::CanvasViewMovesWholeGroupFromReferencePoint()
{
    mini_graph::CanvasCircle circle(mini_graph::CanvasVector(-10.0, 0.0),
                                    50.0);
    mini_graph::CanvasGroupContainer group;
    QVERIFY(group.AddChild(circle));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId groupId = document.AddEntity(group);
    QVERIFY(groupId != 0);
    document.SelectItem(groupId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF referencePoint =
            view.MapFromWorld(mini_graph::CanvasVector(-10.0, 0.0));
    const QPointF targetPoint =
            view.MapFromWorld(mini_graph::CanvasVector(-20.0, 10.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      referencePoint,
                      referencePoint,
                      referencePoint,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove,
                     targetPoint,
                     targetPoint,
                     targetPoint,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasGroupContainer *movedGroup =
            dynamic_cast<const mini_graph::CanvasGroupContainer *>(
                document.GetEntity(groupId));
    QVERIFY(movedGroup);
    const mini_graph::CanvasCircle *movedCircle =
            dynamic_cast<const mini_graph::CanvasCircle *>(
                movedGroup->GetChild(0));
    QVERIFY(movedCircle);
    QVERIFY(movedCircle->GetCenter()
            == mini_graph::CanvasVector(-20.0, 10.0));
    QCOMPARE(movedCircle->GetRadius(), 50.0);
}

void CanvasCoreTest::CanvasViewDrawsPolylineVertexHandles()
{
    mini_graph::CanvasDocument document;
    const QVector<mini_graph::CanvasVector> vertices =
            QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-30.0, -20.0)
            << mini_graph::CanvasVector(0.0, 10.0)
            << mini_graph::CanvasVector(30.0, -20.0);
    const mini_graph::ItemId polylineId = document.AddPolyline(vertices);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawEditHandles(painter);
    painter.end();

    for (const mini_graph::CanvasVector &vertex : vertices) {
        const QPointF position = view.MapFromWorld(vertex);
        QCOMPARE(image.pixelColor(qRound(position.x()), qRound(position.y())),
                 QColor(0, 170, 255));
    }
}

void CanvasCoreTest::CanvasViewMixedPolylineOnlyExposesSafeVertexHandles()
{
    mini_graph::CanvasPolyline polyline;
    const mini_graph::CanvasLine firstLine(
                mini_graph::CanvasVector(-20.0, 0.0),
                mini_graph::CanvasVector(0.0, 0.0));
    const mini_graph::CanvasArc arc(mini_graph::CanvasVector(0.0, 10.0),
                                    10.0,
                                    -1.5707963267948966,
                                    0.0);
    const mini_graph::CanvasLine lastLine(
                mini_graph::CanvasVector(10.0, 10.0),
                mini_graph::CanvasVector(30.0, 10.0));
    QVERIFY(polyline.AddSegment(firstLine));
    QVERIFY(polyline.AddSegment(arc));
    QVERIFY(polyline.AddSegment(lastLine));

    mini_graph::CanvasDocument document;
    const mini_graph::ItemId polylineId = document.AddEntity(polyline);
    QVERIFY(polylineId != 0);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    view.DrawEditHandles(painter);
    painter.end();

    const QPointF start = view.MapFromWorld(mini_graph::CanvasVector(-20.0, 0.0));
    const QPointF firstArcJoin =
            view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    const QPointF secondArcJoin =
            view.MapFromWorld(mini_graph::CanvasVector(10.0, 10.0));
    const QPointF end = view.MapFromWorld(mini_graph::CanvasVector(30.0, 10.0));
    QCOMPARE(image.pixelColor(qRound(start.x()), qRound(start.y())),
             QColor(0, 170, 255));
    QCOMPARE(image.pixelColor(qRound(end.x()), qRound(end.y())),
             QColor(0, 170, 255));
    QCOMPARE(image.pixelColor(qRound(firstArcJoin.x()),
                              qRound(firstArcJoin.y())),
             Qt::black);
    QCOMPARE(image.pixelColor(qRound(secondArcJoin.x()),
                              qRound(secondArcJoin.y())),
             Qt::black);
}

void CanvasCoreTest::CanvasViewSupportsCustomRendererAssignment()
{
    CanvasViewHarness view;
    TestCanvasRenderer renderer;

    QCOMPARE(view.GetCustomRenderer(), static_cast<mini_graph::CanvasRenderer *>(nullptr));

    view.SetCustomRenderer(&renderer);
    QCOMPARE(view.GetCustomRenderer(), &renderer);

    view.SetCustomRenderer(nullptr);
    QCOMPARE(view.GetCustomRenderer(), static_cast<mini_graph::CanvasRenderer *>(nullptr));
}

void CanvasCoreTest::CanvasViewRendersCustomRenderersInRegistrationOrder()
{
    QStringList events;
    CanvasViewHarness view;
    view.resize(160, 120);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    RecordingCanvasRenderer first(QStringLiteral("first"), &events);
    RecordingCanvasRenderer second(QStringLiteral("second"), &events);

    QVERIFY(!view.AddCustomRenderer(nullptr));
    QVERIFY(view.AddCustomRenderer(&first));
    QVERIFY(!view.AddCustomRenderer(&first));
    QVERIFY(view.AddCustomRenderer(&second));

    QVector<mini_graph::CanvasRenderer *> renderers;
    renderers << &first << &second;
    QCOMPARE(view.GetCustomRenderers(), renderers);
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(&first));

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    QStringList expectedEvents;
    expectedEvents << QStringLiteral("first.background")
                   << QStringLiteral("second.background")
                   << QStringLiteral("first.geometry")
                   << QStringLiteral("second.geometry")
                   << QStringLiteral("first.overlay")
                   << QStringLiteral("second.overlay");
    QCOMPARE(events, expectedEvents);
}

void CanvasCoreTest::CanvasViewSetCustomRendererReplacesRendererCollection()
{
    CanvasViewHarness view;
    TestCanvasRenderer first;
    TestCanvasRenderer second;
    TestCanvasRenderer replacement;

    view.SetCustomRenderer(&first);
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(&first));
    QCOMPARE(view.GetCustomRenderers().size(), 1);

    QVERIFY(view.AddCustomRenderer(&second));
    QCOMPARE(view.GetCustomRenderers().size(), 2);

    view.SetCustomRenderer(&replacement);
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(&replacement));
    QCOMPARE(view.GetCustomRenderers().size(), 1);
    QCOMPARE(view.GetCustomRenderers().first(),
             static_cast<mini_graph::CanvasRenderer *>(&replacement));

    view.SetCustomRenderer(nullptr);
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(nullptr));
    QVERIFY(view.GetCustomRenderers().isEmpty());
}

void CanvasCoreTest::CanvasViewCanRemoveAndClearCustomRenderers()
{
    QStringList events;
    CanvasViewHarness view;
    view.resize(160, 120);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    RecordingCanvasRenderer first(QStringLiteral("first"), &events);
    RecordingCanvasRenderer second(QStringLiteral("second"), &events);
    QVERIFY(view.AddCustomRenderer(&first));
    QVERIFY(view.AddCustomRenderer(&second));
    QVERIFY(view.RemoveCustomRenderer(&first));
    QVERIFY(!view.RemoveCustomRenderer(&first));
    QCOMPARE(view.GetCustomRenderers().size(), 1);
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(&second));

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    QStringList expectedEvents;
    expectedEvents << QStringLiteral("second.background")
                   << QStringLiteral("second.geometry")
                   << QStringLiteral("second.overlay");
    QCOMPARE(events, expectedEvents);

    view.ClearCustomRenderers();
    QVERIFY(view.GetCustomRenderers().isEmpty());
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(nullptr));
}

void CanvasCoreTest::CanvasViewDefersRendererCollectionChangesUntilNextFrame()
{
    QStringList events;
    CanvasViewHarness view;
    view.resize(160, 120);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    RecordingCanvasRenderer first(QStringLiteral("first"), &events);
    RecordingCanvasRenderer second(QStringLiteral("second"), &events);
    RecordingCanvasRenderer third(QStringLiteral("third"), &events);
    QVERIFY(view.AddCustomRenderer(&first));
    QVERIFY(view.AddCustomRenderer(&second));

    bool changedCollection = false;
    first.geometryCallback = [&]() {
        if (changedCollection) {
            return;
        }
        changedCollection = true;
        QVERIFY(view.RemoveCustomRenderer(&second));
        QVERIFY(view.AddCustomRenderer(&third));
    };

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    QStringList firstFrameEvents;
    firstFrameEvents << QStringLiteral("first.background")
                     << QStringLiteral("second.background")
                     << QStringLiteral("first.geometry")
                     << QStringLiteral("second.geometry")
                     << QStringLiteral("first.overlay")
                     << QStringLiteral("second.overlay");
    QCOMPARE(events, firstFrameEvents);

    QVector<mini_graph::CanvasRenderer *> renderers;
    renderers << &first << &third;
    QCOMPARE(view.GetCustomRenderers(), renderers);

    events.clear();
    view.render(&image);

    QStringList secondFrameEvents;
    secondFrameEvents << QStringLiteral("first.background")
                      << QStringLiteral("third.background")
                      << QStringLiteral("first.geometry")
                      << QStringLiteral("third.geometry")
                      << QStringLiteral("first.overlay")
                      << QStringLiteral("third.overlay");
    QCOMPARE(events, secondFrameEvents);
}

void CanvasCoreTest::CanvasViewRemovesDestroyedCustomRenderers()
{
    QStringList events;
    CanvasViewHarness view;
    view.resize(160, 120);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    RecordingCanvasRenderer survivor(QStringLiteral("survivor"), &events);
    {
        RecordingCanvasRenderer primary(QStringLiteral("primary"), &events);
        QVERIFY(view.AddCustomRenderer(&primary));
        QVERIFY(view.AddCustomRenderer(&survivor));
        QCOMPARE(view.GetCustomRenderers().size(), 2);
    }

    QCOMPARE(view.GetCustomRenderers().size(), 1);
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(&survivor));
    {
        RecordingCanvasRenderer temporary(QStringLiteral("temporary"), &events);
        QVERIFY(view.AddCustomRenderer(&temporary));
        QCOMPARE(view.GetCustomRenderers().size(), 2);
    }
    QCOMPARE(view.GetCustomRenderers().size(), 1);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    QStringList expectedEvents;
    expectedEvents << QStringLiteral("survivor.background")
                   << QStringLiteral("survivor.geometry")
                   << QStringLiteral("survivor.overlay");
    QCOMPARE(events, expectedEvents);
}

void CanvasCoreTest::CanvasViewCanBeDestroyedWithParentedCustomRenderers()
{
    int destroyedCount = 0;
    std::unique_ptr<CanvasViewHarness> view(new CanvasViewHarness);
    TestCanvasRenderer *first = new TestCanvasRenderer(view.get());
    TestCanvasRenderer *second = new TestCanvasRenderer(view.get());
    connect(first, &QObject::destroyed, [&destroyedCount]() {
        ++destroyedCount;
    });
    connect(second, &QObject::destroyed, [&destroyedCount]() {
        ++destroyedCount;
    });

    QVERIFY(view->AddCustomRenderer(first));
    QVERIFY(view->AddCustomRenderer(second));
    view.reset();

    QCOMPARE(destroyedCount, 2);
}

void CanvasCoreTest::CanvasViewRefreshesForEachCustomRenderer()
{
    CanvasViewHarness view;
    view.resize(120, 90);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    TestCanvasRenderer first;
    TestCanvasRenderer second;
    QVERIFY(view.AddCustomRenderer(&first));
    QVERIFY(view.AddCustomRenderer(&second));

    view.show();
    QTRY_VERIFY(view.paintEventCount > 0);
    QCoreApplication::processEvents();

    int previousPaintEventCount = view.paintEventCount;
    first.RequestUpdate();
    QTRY_VERIFY(view.paintEventCount > previousPaintEventCount);

    QCoreApplication::processEvents();
    previousPaintEventCount = view.paintEventCount;
    second.RequestUpdate();
    QTRY_VERIFY(view.paintEventCount > previousPaintEventCount);
}

void CanvasCoreTest::CanvasViewIsolatesPainterStateBetweenCustomRenderers()
{
    CanvasViewHarness view;
    view.resize(100, 80);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    PainterStateMutatingRenderer mutatingRenderer;
    SolidGeometryCanvasRenderer drawingRenderer;
    QVERIFY(view.AddCustomRenderer(&mutatingRenderer));
    QVERIFY(view.AddCustomRenderer(&drawingRenderer));

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    QCOMPARE(image.pixelColor(25, 25), QColor(Qt::cyan));
}

void CanvasCoreTest::CanvasViewDispatchesCurrentActionAndRefreshesPreview()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-30.0, 0.0),
                mini_graph::CanvasVector(30.0, 0.0));
    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    view.show();
    QCoreApplication::processEvents();

    int destructionCount = 0;
    TestCanvasAction *action = new TestCanvasAction(&destructionCount);
    QVERIFY(view.SetCurrentAction(action));
    QCOMPARE(action->parent(), static_cast<QObject *>(&view));
    QCOMPARE(action->GetCanvasView(), &view);
    QCOMPARE(view.GetCurrentAction(), action);
    QVERIFY(view.hasFocus());

    const QPointF linePosition = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    QMouseEvent press(QEvent::MouseButtonPress,
                      linePosition,
                      linePosition,
                      linePosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        linePosition,
                        linePosition,
                        linePosition,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    view.mouseReleaseEvent(&release);
    QCOMPARE(action->mousePressCount, 1);
    QCOMPARE(action->mouseReleaseCount, 1);
    QCOMPARE(action->lastButton, Qt::LeftButton);
    QVERIFY(!document.IsItemSelected(lineId));

    QSignalSpy contextMenuSpy(&view, &mini_graph::CanvasView::ContextMenuRequested);
    QMouseEvent rightPress(QEvent::MouseButtonPress,
                           linePosition,
                           linePosition,
                           linePosition,
                           Qt::RightButton,
                           Qt::RightButton,
                           Qt::NoModifier);
    view.mousePressEvent(&rightPress);
    QMouseEvent rightRelease(QEvent::MouseButtonRelease,
                             linePosition,
                             linePosition,
                             linePosition,
                             Qt::RightButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    view.mouseReleaseEvent(&rightRelease);
    QCOMPARE(action->mousePressCount, 2);
    QCOMPARE(action->mouseReleaseCount, 2);
    QCOMPARE(contextMenuSpy.count(), 0);

    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,
                            linePosition,
                            linePosition,
                            linePosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mouseDoubleClickEvent(&doubleClick);
    QCOMPARE(action->mouseDoubleClickCount, 1);

    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    view.keyPressEvent(&keyPress);
    QKeyEvent keyRelease(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    view.keyReleaseEvent(&keyRelease);
    QCOMPARE(action->keyPressCount, 1);
    QCOMPARE(action->keyReleaseCount, 1);

    QMouseEvent move(QEvent::MouseMove,
                     linePosition + QPointF(12.0, 9.0),
                     linePosition + QPointF(12.0, 9.0),
                     linePosition + QPointF(12.0, 9.0),
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);
    QCOMPARE(action->mouseMoveCount, 1);
    QCOMPARE(action->lastMousePosition, move.localPos());

    const int previousOverlayRenderCount = action->overlayRenderCount;
    action->RequestUpdate();
    QTRY_VERIFY(action->overlayRenderCount > previousOverlayRenderCount);
    QVERIFY(destructionCount == 0);
}

void CanvasCoreTest::CanvasViewEscapeCancelsCurrentActionWithoutClearingSelection()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId selectedId = document.AddPoint(
                mini_graph::CanvasVector(0.0, 0.0));
    document.SelectItem(selectedId);
    QVERIFY(document.IsItemSelected(selectedId));

    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    TestCanvasAction *action = new TestCanvasAction;
    QVERIFY(view.SetCurrentAction(action));
    QSignalSpy cancelledSpy(&view, &mini_graph::CanvasView::InteractionCancelled);

    QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    view.keyPressEvent(&deleteKey);
    QCOMPARE(action->keyPressCount, 1);
    QCOMPARE(document.GetItemCount(), 1);
    QVERIFY(document.IsItemSelected(selectedId));

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    view.keyPressEvent(&escape);

    QVERIFY(escape.isAccepted());
    QCOMPARE(action->keyPressCount, 2);
    QCOMPARE(action->lastKey, static_cast<int>(Qt::Key_Escape));
    QCOMPARE(action->cancelCount, 1);
    QVERIFY(!view.GetCurrentAction());
    QVERIFY(document.IsItemSelected(selectedId));
    QCOMPARE(cancelledSpy.count(), 1);
}

void CanvasCoreTest::CanvasViewDefaultActionCanBeOverriddenAndReset()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-30.0, 0.0),
                mini_graph::CanvasVector(30.0, 0.0));
    CanvasViewHarness view;
    view.resize(320, 240);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QPointer<TestCanvasAction> action = new TestCanvasAction;
    QVERIFY(view.SetDefaultAction(action.data()));
    QCOMPARE(action->parent(), static_cast<QObject *>(&view));
    QCOMPARE(view.GetDefaultAction(), action.data());

    const QPointF position = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::PreviewNavigation);
    QMouseEvent previewPress(QEvent::MouseButtonPress, position, position, position,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&previewPress);
    QMouseEvent previewRelease(QEvent::MouseButtonRelease,
                               position, position, position,
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&previewRelease);
    QKeyEvent previewKeyRelease(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    view.keyReleaseEvent(&previewKeyRelease);
    QCOMPARE(action->mousePressCount, 0);
    QCOMPARE(action->keyReleaseCount, 0);

    view.SetInteractionMode(mini_graph::CanvasView::InteractionMode::Edit);
    QMouseEvent press(QEvent::MouseButtonPress, position, position, position,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, position, position,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&release);
    QCOMPARE(action->mousePressCount, 1);
    QVERIFY(!document.IsItemSelected(lineId));

    view.ResetDefaultAction();
    QVERIFY(!view.GetDefaultAction());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(action.isNull());

    QMouseEvent builtInPress(QEvent::MouseButtonPress, position, position, position,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&builtInPress);
    QMouseEvent builtInRelease(QEvent::MouseButtonRelease, position, position, position,
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&builtInRelease);
    QVERIFY(document.IsItemSelected(lineId));
}

void CanvasCoreTest::CanvasViewFinishedActionReturnsToBuiltinInteraction()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-30.0, 0.0),
                mini_graph::CanvasVector(30.0, 0.0));
    CanvasViewHarness view;
    view.resize(320, 240);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QPointer<TestCanvasAction> action = new TestCanvasAction;
    QVERIFY(view.SetCurrentAction(action.data()));
    action->Finish();
    QTRY_VERIFY(!view.GetCurrentAction());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(action.isNull());

    const QPointF position = view.MapFromWorld(mini_graph::CanvasVector(0.0, 0.0));
    QMouseEvent press(QEvent::MouseButtonPress, position, position, position,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, position, position,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    view.mouseReleaseEvent(&release);
    QVERIFY(document.IsItemSelected(lineId));
}

void CanvasCoreTest::CanvasViewKeepsNavigationAvailableDuringAction()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    TestCanvasAction *action = new TestCanvasAction;
    QVERIFY(view.SetCurrentAction(action));

    const mini_graph::CanvasVector initialCenter = view.GetTransform().GetCenter();
    QMouseEvent panPress(QEvent::MouseButtonPress,
                         QPointF(100.0, 100.0),
                         QPointF(100.0, 100.0),
                         QPointF(100.0, 100.0),
                         Qt::MiddleButton,
                         Qt::MiddleButton,
                         Qt::NoModifier);
    view.mousePressEvent(&panPress);
    QMouseEvent panMove(QEvent::MouseMove,
                        QPointF(130.0, 120.0),
                        QPointF(130.0, 120.0),
                        QPointF(130.0, 120.0),
                        Qt::NoButton,
                        Qt::MiddleButton,
                        Qt::NoModifier);
    view.mouseMoveEvent(&panMove);
    QMouseEvent panRelease(QEvent::MouseButtonRelease,
                           QPointF(130.0, 120.0),
                           QPointF(130.0, 120.0),
                           QPointF(130.0, 120.0),
                           Qt::MiddleButton,
                           Qt::NoButton,
                           Qt::NoModifier);
    view.mouseReleaseEvent(&panRelease);
    QVERIFY(view.GetTransform().GetCenter() != initialCenter);
    QCOMPARE(action->mousePressCount, 0);

    const qreal zoomBefore = view.GetZoom();
    QWheelEvent wheel(QPointF(400.0, 300.0),
                      QPointF(400.0, 300.0),
                      QPoint(0, 0),
                      QPoint(0, 120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::ScrollUpdate,
                      false);
    view.wheelEvent(&wheel);
    QVERIFY(view.GetZoom() > zoomBefore);
}

void CanvasCoreTest::BasicPointPlacementActionAddsPointAndFinishes()
{
    mini_graph::CanvasDocument document;
    CanvasViewHarness view;
    view.resize(320, 240);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QVERIFY(view.SetCurrentAction(new BasicPointPlacementAction));
    const mini_graph::CanvasVector expectedPosition(23.5, -17.25);
    const QPointF viewportPosition = view.MapFromWorld(expectedPosition);
    QMouseEvent press(QEvent::MouseButtonPress,
                      viewportPosition,
                      viewportPosition,
                      viewportPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    view.mousePressEvent(&press);

    QCOMPARE(document.GetItemCount(), 1);
    QTRY_VERIFY(!view.GetCurrentAction());
    const QVector<mini_graph::ItemId> itemIds = document.GetItemIds();
    QCOMPARE(itemIds.size(), 1);
    mini_graph::StruPointGeometry pointGeometry;
    QVERIFY(document.GetPointGeometry(itemIds.first(), &pointGeometry));
    QCOMPARE(pointGeometry.position, expectedPosition);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QPointF selectionPosition = view.MapFromWorld(expectedPosition);
    QMouseEvent selectionPress(QEvent::MouseButtonPress,
                               selectionPosition,
                               selectionPosition,
                               selectionPosition,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
    view.mousePressEvent(&selectionPress);
    QMouseEvent selectionRelease(QEvent::MouseButtonRelease,
                                 selectionPosition,
                                 selectionPosition,
                                 selectionPosition,
                                 Qt::LeftButton,
                                 Qt::NoButton,
                                 Qt::NoModifier);
    view.mouseReleaseEvent(&selectionRelease);
    QVERIFY(document.IsItemSelected(itemIds.first()));
}

void CanvasCoreTest::BasicLineCreationActionAddsLineAfterTwoPointClicks()
{
    mini_graph::CanvasDocument document;
    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);

    BasicLineCreationAction *action = new BasicLineCreationAction;
    QVERIFY(view.SetCurrentAction(action));

    const mini_graph::CanvasVector start(-24.0, -12.0);
    const mini_graph::CanvasVector end(36.0, 18.0);
    const QPointF startPosition = view.MapFromWorld(start);
    const QPointF endPosition = view.MapFromWorld(end);

    QMouseEvent firstClick(QEvent::MouseButtonPress,
                           startPosition,
                           startPosition,
                           startPosition,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    view.mousePressEvent(&firstClick);

    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(view.GetCurrentAction(), action);

    QMouseEvent move(QEvent::MouseMove,
                     endPosition,
                     endPosition,
                     endPosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasRenderContext context(
                &document, view.GetTransform(), view.size());
    QImage preview(view.size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    action->RenderOverlay(painter, context);
    painter.end();

    const mini_graph::CanvasVector midpoint(
                (start.GetX() + end.GetX()) * 0.5,
                (start.GetY() + end.GetY()) * 0.5);
    QVERIFY(HasVisiblePixelNear(preview, context.MapFromWorld(midpoint)));

    QMouseEvent secondClick(QEvent::MouseButtonPress,
                            endPosition,
                            endPosition,
                            endPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&secondClick);

    QCOMPARE(document.GetItemCount(), 1);
    QTRY_VERIFY(!view.GetCurrentAction());
    const QVector<mini_graph::ItemId> itemIds = document.GetItemIds();
    QCOMPARE(itemIds.size(), 1);
    mini_graph::StruLineGeometry geometry;
    QVERIFY(document.GetLineGeometry(itemIds.first(), &geometry));
    QCOMPARE(geometry.startPoint, start);
    QCOMPARE(geometry.endPoint, end);
}

void CanvasCoreTest::BasicLineCreationActionCompletesOnDoubleClickEvent()
{
    mini_graph::CanvasDocument document;
    const mini_graph::CanvasVector end(25.0, 15.0);
    document.AddPoint(end);

    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    view.SetSnapEnabled(true);
    view.SetGeometrySnapEnabled(true);
    view.SetGridSnapEnabled(false);
    QVERIFY(view.SetCurrentAction(new BasicLineCreationAction));

    const mini_graph::CanvasVector start(-10.0, -5.0);
    const QPointF startPosition = view.MapFromWorld(start);
    const mini_graph::CanvasVector nearEnd(
                end.GetX() + 3.0, end.GetY() + 2.0);
    const QPointF endPosition = view.MapFromWorld(nearEnd);
    QMouseEvent firstClick(QEvent::MouseButtonPress,
                           startPosition,
                           startPosition,
                           startPosition,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    view.mousePressEvent(&firstClick);
    QCOMPARE(document.GetItemCount(), 1);

    QMouseEvent secondClick(QEvent::MouseButtonDblClick,
                            endPosition,
                            endPosition,
                            endPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mouseDoubleClickEvent(&secondClick);

    QCOMPARE(document.GetItemCount(), 2);
    QTRY_VERIFY(!view.GetCurrentAction());
    const QVector<mini_graph::ItemId> itemIds = document.GetItemIds();
    QCOMPARE(itemIds.size(), 2);
    mini_graph::StruLineGeometry geometry;
    QVERIFY(document.GetLineGeometry(itemIds.last(), &geometry));
    QCOMPARE(geometry.startPoint, start);
    QCOMPARE(geometry.endPoint, end);
}

void CanvasCoreTest::BasicLineCreationActionUsesGeometrySnap()
{
    mini_graph::CanvasDocument document;
    const mini_graph::CanvasVector start(40.0, 20.0);
    const mini_graph::CanvasVector end(120.0, -30.0);
    document.AddPoint(start);
    document.AddPoint(end);

    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    view.SetSnapEnabled(true);
    view.SetGeometrySnapEnabled(true);
    view.SetGridSnapEnabled(false);
    QVERIFY(view.SetCurrentAction(new BasicLineCreationAction));

    const mini_graph::CanvasVector nearStart(
                start.GetX() + 3.0, start.GetY() + 2.0);
    const QPointF startPosition = view.MapFromWorld(nearStart);
    QMouseEvent firstClick(QEvent::MouseButtonPress,
                           startPosition,
                           startPosition,
                           startPosition,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    view.mousePressEvent(&firstClick);
    QCOMPARE(document.GetItemCount(), 2);

    const mini_graph::CanvasVector nearEnd(
                end.GetX() - 4.0, end.GetY() + 3.0);
    const QPointF endPosition = view.MapFromWorld(nearEnd);
    QMouseEvent move(QEvent::MouseMove,
                     endPosition,
                     endPosition,
                     endPosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);
    QMouseEvent secondClick(QEvent::MouseButtonPress,
                            endPosition,
                            endPosition,
                            endPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&secondClick);

    QCOMPARE(document.GetItemCount(), 3);
    QTRY_VERIFY(!view.GetCurrentAction());
    const QVector<mini_graph::ItemId> itemIds = document.GetItemIds();
    QCOMPARE(itemIds.size(), 3);
    mini_graph::StruLineGeometry geometry;
    QVERIFY(document.GetLineGeometry(itemIds.last(), &geometry));
    QCOMPARE(geometry.startPoint, start);
    QCOMPARE(geometry.endPoint, end);
}

void CanvasCoreTest::BasicLineCreationActionEscapeCancelsWithoutInsertion()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId selectedId = document.AddPoint(
                mini_graph::CanvasVector(0.0, 0.0));
    document.SelectItem(selectedId);

    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    BasicLineCreationAction *action = new BasicLineCreationAction;
    QVERIFY(view.SetCurrentAction(action));
    QSignalSpy updateSpy(action,
                         &mini_graph::CanvasAction::UpdateRequested);

    const QPointF startPosition = view.MapFromWorld(
                mini_graph::CanvasVector(15.0, 10.0));
    QMouseEvent firstClick(QEvent::MouseButtonPress,
                           startPosition,
                           startPosition,
                           startPosition,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    view.mousePressEvent(&firstClick);
    QCOMPARE(updateSpy.count(), 1);

    const QPointF previewPosition = view.MapFromWorld(
                mini_graph::CanvasVector(35.0, 25.0));
    QMouseEvent move(QEvent::MouseMove,
                     previewPosition,
                     previewPosition,
                     previewPosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);
    QCOMPARE(updateSpy.count(), 2);

    const mini_graph::CanvasRenderContext context(
                &document, view.GetTransform(), view.size());
    const mini_graph::CanvasVector midpoint(
                25.0, 17.5);
    QImage preview(view.size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    action->RenderOverlay(painter, context);
    painter.end();
    QVERIFY(HasVisiblePixelNear(preview, context.MapFromWorld(midpoint)));

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    view.keyPressEvent(&escape);

    QVERIFY(escape.isAccepted());
    QCOMPARE(document.GetItemCount(), 1);
    QVERIFY(document.IsItemSelected(selectedId));
    QTRY_VERIFY(!view.GetCurrentAction());

    preview.fill(Qt::transparent);
    QPainter cancelledPainter(&preview);
    action->RenderOverlay(cancelledPainter, context);
    cancelledPainter.end();
    QVERIFY(!HasVisiblePixelNear(preview, context.MapFromWorld(midpoint)));
}

void CanvasCoreTest::BasicCircleCreationActionAddsCircleAndPreviews()
{
    mini_graph::CanvasDocument document;
    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);

    BasicCircleCreationAction *action = new BasicCircleCreationAction;
    QVERIFY(view.SetCurrentAction(action));

    const mini_graph::CanvasVector center(-20.0, 10.0);
    const mini_graph::CanvasVector radiusPoint(25.0, 10.0);
    const QPointF centerPosition = view.MapFromWorld(center);
    const QPointF radiusPosition = view.MapFromWorld(radiusPoint);
    QMouseEvent centerClick(QEvent::MouseButtonPress,
                            centerPosition,
                            centerPosition,
                            centerPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&centerClick);

    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(view.GetCurrentAction(), action);

    QMouseEvent move(QEvent::MouseMove,
                     radiusPosition,
                     radiusPosition,
                     radiusPosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    const mini_graph::CanvasRenderContext context(
                &document, view.GetTransform(), view.size());
    QImage preview(view.size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    action->RenderOverlay(painter, context);
    painter.end();
    QVERIFY(HasVisiblePixelNear(preview, context.MapFromWorld(radiusPoint)));

    QMouseEvent radiusClick(QEvent::MouseButtonPress,
                            radiusPosition,
                            radiusPosition,
                            radiusPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&radiusClick);

    QCOMPARE(document.GetItemCount(), 1);
    QTRY_VERIFY(!view.GetCurrentAction());
    const QVector<mini_graph::ItemId> itemIds = document.GetItemIds();
    QCOMPARE(itemIds.size(), 1);
    mini_graph::StruCircleGeometry geometry;
    QVERIFY(document.GetCircleGeometry(itemIds.first(), &geometry));
    QCOMPARE(geometry.center, center);
    QCOMPARE(geometry.radius, qreal(45.0));
}

void CanvasCoreTest::BasicCircleCreationActionCompletesOnDoubleClickEvent()
{
    mini_graph::CanvasDocument document;
    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    QVERIFY(view.SetCurrentAction(new BasicCircleCreationAction));

    const mini_graph::CanvasVector center(-10.0, -5.0);
    const mini_graph::CanvasVector radiusPoint(25.0, -5.0);
    const QPointF centerPosition = view.MapFromWorld(center);
    const QPointF radiusPosition = view.MapFromWorld(radiusPoint);
    QMouseEvent centerClick(QEvent::MouseButtonPress,
                            centerPosition,
                            centerPosition,
                            centerPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&centerClick);
    QMouseEvent radiusDoubleClick(QEvent::MouseButtonDblClick,
                                  radiusPosition,
                                  radiusPosition,
                                  radiusPosition,
                                  Qt::LeftButton,
                                  Qt::LeftButton,
                                  Qt::NoModifier);
    view.mouseDoubleClickEvent(&radiusDoubleClick);

    QCOMPARE(document.GetItemCount(), 1);
    QTRY_VERIFY(!view.GetCurrentAction());
    mini_graph::StruCircleGeometry geometry;
    QVERIFY(document.GetCircleGeometry(document.GetItemIds().first(), &geometry));
    QCOMPARE(geometry.center, center);
    QCOMPARE(geometry.radius, qreal(35.0));
}

void CanvasCoreTest::BasicCircleCreationActionUsesGeometrySnap()
{
    mini_graph::CanvasDocument document;
    const mini_graph::CanvasVector center(40.0, 20.0);
    const mini_graph::CanvasVector radiusPoint(100.0, 20.0);
    document.AddPoint(center);
    document.AddPoint(radiusPoint);

    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    view.SetSnapEnabled(true);
    view.SetGeometrySnapEnabled(true);
    view.SetGridSnapEnabled(false);
    QVERIFY(view.SetCurrentAction(new BasicCircleCreationAction));

    const mini_graph::CanvasVector nearCenter(
                center.GetX() + 3.0, center.GetY() + 2.0);
    const QPointF nearCenterPosition = view.MapFromWorld(nearCenter);
    QMouseEvent centerClick(QEvent::MouseButtonPress,
                            nearCenterPosition,
                            nearCenterPosition,
                            nearCenterPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&centerClick);

    const mini_graph::CanvasVector nearRadiusPoint(
                radiusPoint.GetX() - 4.0, radiusPoint.GetY() + 3.0);
    const QPointF nearRadiusPosition = view.MapFromWorld(nearRadiusPoint);
    QMouseEvent move(QEvent::MouseMove,
                     nearRadiusPosition,
                     nearRadiusPosition,
                     nearRadiusPosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);
    QMouseEvent radiusClick(QEvent::MouseButtonPress,
                            nearRadiusPosition,
                            nearRadiusPosition,
                            nearRadiusPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&radiusClick);

    QCOMPARE(document.GetItemCount(), 3);
    QTRY_VERIFY(!view.GetCurrentAction());
    const QVector<mini_graph::ItemId> itemIds = document.GetItemIds();
    mini_graph::StruCircleGeometry geometry;
    QVERIFY(document.GetCircleGeometry(itemIds.last(), &geometry));
    QCOMPARE(geometry.center, center);
    QCOMPARE(geometry.radius, qreal(60.0));
}

void CanvasCoreTest::BasicCircleCreationActionRejectsZeroRadiusAndCanRetry()
{
    mini_graph::CanvasDocument document;
    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    BasicCircleCreationAction *action = new BasicCircleCreationAction;
    QVERIFY(view.SetCurrentAction(action));

    const mini_graph::CanvasVector center(15.0, -10.0);
    const QPointF centerPosition = view.MapFromWorld(center);
    QMouseEvent centerClick(QEvent::MouseButtonPress,
                            centerPosition,
                            centerPosition,
                            centerPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&centerClick);
    QMouseEvent zeroRadiusClick(QEvent::MouseButtonPress,
                                centerPosition,
                                centerPosition,
                                centerPosition,
                                Qt::LeftButton,
                                Qt::LeftButton,
                                Qt::NoModifier);
    view.mousePressEvent(&zeroRadiusClick);

    QCOMPARE(document.GetItemCount(), 0);
    QCOMPARE(view.GetCurrentAction(), action);

    const mini_graph::CanvasVector radiusPoint(15.0, 30.0);
    const QPointF radiusPosition = view.MapFromWorld(radiusPoint);
    QMouseEvent radiusClick(QEvent::MouseButtonPress,
                            radiusPosition,
                            radiusPosition,
                            radiusPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&radiusClick);

    QCOMPARE(document.GetItemCount(), 1);
    QTRY_VERIFY(!view.GetCurrentAction());
    mini_graph::StruCircleGeometry geometry;
    QVERIFY(document.GetCircleGeometry(document.GetItemIds().last(), &geometry));
    QCOMPARE(geometry.center, center);
    QCOMPARE(geometry.radius, qreal(40.0));
}

void CanvasCoreTest::BasicCircleCreationActionEscapeCancelsWithoutInsertion()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId selectedId = document.AddPoint(
                mini_graph::CanvasVector(0.0, 0.0));
    document.SelectItem(selectedId);

    CanvasViewHarness view;
    view.resize(320, 240);
    view.SetDocument(&document);
    BasicCircleCreationAction *action = new BasicCircleCreationAction;
    QVERIFY(view.SetCurrentAction(action));

    const mini_graph::CanvasVector center(10.0, 15.0);
    const QPointF centerPosition = view.MapFromWorld(center);
    QMouseEvent centerClick(QEvent::MouseButtonPress,
                            centerPosition,
                            centerPosition,
                            centerPosition,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    view.mousePressEvent(&centerClick);
    const QPointF previewPosition = view.MapFromWorld(
                mini_graph::CanvasVector(50.0, 15.0));
    QMouseEvent move(QEvent::MouseMove,
                     previewPosition,
                     previewPosition,
                     previewPosition,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    view.mouseMoveEvent(&move);

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    view.keyPressEvent(&escape);

    QVERIFY(escape.isAccepted());
    QCOMPARE(document.GetItemCount(), 1);
    QVERIFY(document.IsItemSelected(selectedId));
    QVERIFY(!view.GetCurrentAction());

    const mini_graph::CanvasRenderContext context(
                &document, view.GetTransform(), view.size());
    QImage preview(view.size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    action->RenderOverlay(painter, context);
    painter.end();
    QVERIFY(!HasVisiblePixelNear(
                preview, context.MapFromWorld(mini_graph::CanvasVector(50.0, 15.0))));
}

void CanvasCoreTest::CanvasViewLifecycleCanBeCreatedAndDestroyed()
{
    mini_graph::CanvasDocument document;
    document.AddPoint(mini_graph::CanvasVector(20.0, 30.0));
    document.AddLine(mini_graph::CanvasVector(-80.0, -40.0),
                     mini_graph::CanvasVector(80.0, -40.0));
    document.AddArc(mini_graph::CanvasVector(-40.0, 40.0),
                    20.0, 0.0, 1.5707963267948966);
    document.AddCircle(mini_graph::CanvasVector(50.0, 40.0), 15.0);

    for (int index = 0; index < 2; ++index) {
        CanvasViewHarness *view = new CanvasViewHarness;
        view->resize(640, 480);
        view->SetDocument(&document);
        view->show();
        QTest::qWait(50);
        delete view;
        QCoreApplication::processEvents();
    }
}

void CanvasCoreTest::BasicExampleGeometryDialogCreatesCircle()
{
    mini_graph::CanvasDocument document;
    GeometryCreateDialog dialog(&document);

    QComboBox *typeComboBox = dialog.findChild<QComboBox*>(
                QStringLiteral("geometryTypeComboBox"));
    QDoubleSpinBox *centerXSpinBox = dialog.findChild<QDoubleSpinBox*>(
                QStringLiteral("circleCenterXSpinBox"));
    QDoubleSpinBox *centerYSpinBox = dialog.findChild<QDoubleSpinBox*>(
                QStringLiteral("circleCenterYSpinBox"));
    QDoubleSpinBox *radiusSpinBox = dialog.findChild<QDoubleSpinBox*>(
                QStringLiteral("circleRadiusSpinBox"));
    QDialogButtonBox *buttonBox = dialog.findChild<QDialogButtonBox*>(
                QStringLiteral("geometryDialogButtonBox"));

    QVERIFY(typeComboBox);
    QVERIFY(centerXSpinBox);
    QVERIFY(centerYSpinBox);
    QVERIFY(radiusSpinBox);
    QVERIFY(buttonBox);

    const int circleIndex = typeComboBox->findData(
                static_cast<int>(mini_graph::CanvasEntityType::Circle));
    QVERIFY(circleIndex >= 0);
    typeComboBox->setCurrentIndex(circleIndex);
    centerXSpinBox->setValue(12.0);
    centerYSpinBox->setValue(-8.0);
    radiusSpinBox->setValue(4.0);

    buttonBox->button(QDialogButtonBox::Ok)->click();

    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(document.GetItemCount(), 1);
    QVERIFY(document.GetCirclePrimitives().at(0).geometry.center
            == mini_graph::CanvasVector(12.0, -8.0));
    QCOMPARE(document.GetCirclePrimitives().at(0).geometry.radius, 4.0);
}

void CanvasCoreTest::BasicExampleGeometryDialogCreatesPolyline()
{
    mini_graph::CanvasDocument document;
    GeometryCreateDialog dialog(&document);

    QComboBox *typeComboBox = dialog.findChild<QComboBox *>(
                QStringLiteral("geometryTypeComboBox"));
    QPlainTextEdit *verticesTextEdit = dialog.findChild<QPlainTextEdit *>(
                QStringLiteral("polylineVerticesTextEdit"));
    QCheckBox *closedCheckBox = dialog.findChild<QCheckBox *>(
                QStringLiteral("polylineClosedCheckBox"));
    QDialogButtonBox *buttonBox = dialog.findChild<QDialogButtonBox *>(
                QStringLiteral("geometryDialogButtonBox"));

    QVERIFY(typeComboBox);
    QVERIFY(verticesTextEdit);
    QVERIFY(closedCheckBox);
    QVERIFY(buttonBox);

    const int polylineIndex = typeComboBox->findData(
                static_cast<int>(mini_graph::CanvasEntityType::Polyline));
    QVERIFY(polylineIndex >= 0);
    typeComboBox->setCurrentIndex(polylineIndex);
    verticesTextEdit->setPlainText(QStringLiteral("-10,2\n\n3.5,4.25\n6,-7"));
    closedCheckBox->setChecked(true);

    buttonBox->button(QDialogButtonBox::Ok)->click();

    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(document.GetItemCount(), 1);
    const mini_graph::StruPolylineGeometry geometry =
            document.GetPolylinePrimitives().at(0).geometry;
    QVERIFY(geometry.vertices == (QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-10.0, 2.0)
            << mini_graph::CanvasVector(3.5, 4.25)
            << mini_graph::CanvasVector(6.0, -7.0)));
    QVERIFY(geometry.closed);
}

void CanvasCoreTest::BasicExampleGeometryEditDialogUpdatesPolyline()
{
    mini_graph::CanvasDocument document;
    const QVector<mini_graph::CanvasVector> originalVertices =
            QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(1.0, 2.0)
            << mini_graph::CanvasVector(4.0, 5.0)
            << mini_graph::CanvasVector(7.0, 8.0);
    const mini_graph::ItemId polylineId = document.AddPolyline(originalVertices, true);
    GeometryEditDialog dialog(&document, polylineId);

    QPlainTextEdit *verticesTextEdit = dialog.findChild<QPlainTextEdit *>(
                QStringLiteral("editPolylineVerticesTextEdit"));
    QCheckBox *closedCheckBox = dialog.findChild<QCheckBox *>(
                QStringLiteral("editPolylineClosedCheckBox"));
    QDialogButtonBox *buttonBox = dialog.findChild<QDialogButtonBox *>(
                QStringLiteral("geometryEditDialogButtonBox"));

    QVERIFY(verticesTextEdit);
    QVERIFY(closedCheckBox);
    QVERIFY(buttonBox);
    QCOMPARE(verticesTextEdit->toPlainText(),
             QStringLiteral("1,2\n4,5\n7,8"));
    QVERIFY(closedCheckBox->isChecked());

    verticesTextEdit->setPlainText(QStringLiteral("-8,3\n0,12\n11,-4"));
    closedCheckBox->setChecked(false);
    buttonBox->button(QDialogButtonBox::Ok)->click();

    QCOMPARE(dialog.result(), QDialog::Accepted);
    mini_graph::StruPolylineGeometry geometry;
    QVERIFY(document.GetPolylineGeometry(polylineId, &geometry));
    QVERIFY(geometry.vertices == (QVector<mini_graph::CanvasVector>()
            << mini_graph::CanvasVector(-8.0, 3.0)
            << mini_graph::CanvasVector(0.0, 12.0)
            << mini_graph::CanvasVector(11.0, -4.0)));
    QVERIFY(!geometry.closed);
}

void CanvasCoreTest::BasicExampleGeometryEditDialogUpdatesArcAndStyle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId arcId = document.AddArc(
                                                     mini_graph::CanvasVector(10.0, 20.0),
                                                     5.0,
                                                     0.0,
                                                     1.5707963267948966);
    GeometryEditDialog dialog(&document, arcId);

    QDoubleSpinBox *centerXSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editArcCenterXSpinBox"));
    QDoubleSpinBox *centerYSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editArcCenterYSpinBox"));
    QDoubleSpinBox *radiusSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editArcRadiusSpinBox"));
    QDoubleSpinBox *startAngleSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editArcStartAngleSpinBox"));
    QDoubleSpinBox *endAngleSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editArcEndAngleSpinBox"));
    QLineEdit *strokeColorLineEdit = dialog.findChild<QLineEdit *>(
                QStringLiteral("editStrokeColorLineEdit"));
    QLineEdit *selectedStrokeColorLineEdit = dialog.findChild<QLineEdit *>(
                QStringLiteral("editSelectedStrokeColorLineEdit"));
    QDoubleSpinBox *strokeWidthSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editStrokeWidthSpinBox"));
    QDoubleSpinBox *pointSizeSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editPointSizeSpinBox"));
    QCheckBox *visibleCheckBox = dialog.findChild<QCheckBox *>(
                QStringLiteral("editVisibleCheckBox"));
    QDialogButtonBox *buttonBox = dialog.findChild<QDialogButtonBox *>(
                QStringLiteral("geometryEditDialogButtonBox"));

    QVERIFY(centerXSpinBox);
    QVERIFY(centerYSpinBox);
    QVERIFY(radiusSpinBox);
    QVERIFY(startAngleSpinBox);
    QVERIFY(endAngleSpinBox);
    QVERIFY(strokeColorLineEdit);
    QVERIFY(selectedStrokeColorLineEdit);
    QVERIFY(strokeWidthSpinBox);
    QVERIFY(pointSizeSpinBox);
    QVERIFY(visibleCheckBox);
    QVERIFY(buttonBox);

    centerXSpinBox->setValue(-12.0);
    centerYSpinBox->setValue(32.0);
    radiusSpinBox->setValue(18.0);
    startAngleSpinBox->setValue(0.25);
    endAngleSpinBox->setValue(2.5);
    strokeColorLineEdit->setText(QStringLiteral("#112233"));
    selectedStrokeColorLineEdit->setText(QStringLiteral("#AABBCC"));
    strokeWidthSpinBox->setValue(4.0);
    pointSizeSpinBox->setValue(12.0);
    visibleCheckBox->setChecked(false);
    buttonBox->button(QDialogButtonBox::Ok)->click();

    QCOMPARE(dialog.result(), QDialog::Accepted);

    mini_graph::StruArcGeometry geometry;
    QVERIFY(document.GetArcGeometry(arcId, &geometry));
    QVERIFY(geometry.center == mini_graph::CanvasVector(-12.0, 32.0));
    QCOMPARE(geometry.radius, 18.0);
    QCOMPARE(geometry.startAngle, 0.25);
    QCOMPARE(geometry.endAngle, 2.5);

    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(arcId, &style));
    QCOMPARE(style.strokeColor, QColor(QStringLiteral("#112233")));
    QCOMPARE(style.selectedStrokeColor, QColor(QStringLiteral("#AABBCC")));
    QCOMPARE(style.strokeWidth, 4.0);
    QCOMPARE(style.pointSize, 12.0);
    QVERIFY(!style.visible);
}

void CanvasCoreTest::BasicExampleGeometryEditDialogCancelKeepsGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                mini_graph::CanvasVector(-2.0, -3.0),
                mini_graph::CanvasVector(4.0, 5.0));
    GeometryEditDialog dialog(&document, lineId);

    QDoubleSpinBox *startXSpinBox = dialog.findChild<QDoubleSpinBox *>(
                QStringLiteral("editLineStartXSpinBox"));
    QDialogButtonBox *buttonBox = dialog.findChild<QDialogButtonBox *>(
                QStringLiteral("geometryEditDialogButtonBox"));
    QVERIFY(startXSpinBox);
    QVERIFY(buttonBox);

    startXSpinBox->setValue(100.0);
    buttonBox->button(QDialogButtonBox::Cancel)->click();

    QCOMPARE(dialog.result(), QDialog::Rejected);
    mini_graph::StruLineGeometry geometry;
    QVERIFY(document.GetLineGeometry(lineId, &geometry));
    QVERIFY(geometry.startPoint == mini_graph::CanvasVector(-2.0, -3.0));
    QVERIFY(geometry.endPoint == mini_graph::CanvasVector(4.0, 5.0));
}

QTEST_MAIN(CanvasCoreTest)
#include "TstCanvasCore.moc"
