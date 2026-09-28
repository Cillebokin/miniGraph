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
#include <QPushButton>
#include <QTest>

#include <limits>

#include "MiniGraph/MiniGraph.h"
#include "MiniGraph/CanvasGeometry.h"
#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasSnap.h"
#include "MiniGraph/CanvasRenderer.h"
#include "MiniGraph/CanvasTransform.h"
#include "MiniGraph/CanvasView.h"
#include "BasicCustomRenderer.h"
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
    using CanvasView::DrawAxes;
    using CanvasView::DrawEditHandles;
    using CanvasView::DrawGrid;
    using CanvasView::DrawRulerLabels;
    using CanvasView::DrawSnapMarker;
    using CanvasView::DrawSelectionBox;
    using CanvasView::wheelEvent;
};

class CanvasCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void DefaultTransformIsIdentityLike();
    void MinimumZoomSupportsMicroScale();
    void ZoomAtKeepsTheWorldPointUnderTheCursor();
    void PanByMovesWorldPointWithViewportDelta();
    void FitRectCentersAndFitsViewport();
    void FitRectRejectsInvalidViewport();
    void DocumentTracksLinesAndBounds();
    void DocumentUpdatesGeometryByType();
    void DocumentCalculatesSelectedBounds();
    void DocumentChangesEmitSignals();
    void DocumentStoresMixedGeometryPools();
    void DocumentMixedGeometryBoundsAndOrder();
    void DocumentRejectsInvalidCircularGeometry();
    void DocumentStoresPolylineAndPreservesOrder();
    void DocumentRejectsInvalidPolylineGeometry();
    void DocumentBoundsAndTranslatesPolyline();
    void DocumentSupportsParameterizedCreation();
    void CanvasStyleHasStableDefaultsAndValueSemantics();
    void DocumentStoresAndUpdatesItemStyle();
    void DocumentNormalizesLineStyleCompatibilityFields();
    void GeometryArcSweepFollowsDirection();
    void GeometryBoundsCoverAllPrimitiveTypes();
    void GeometryValidatesPolylineAndBounds();
    void GeometryTessellatesArcAndCircle();
    void CanvasRenderContextProvidesWorldHelpers();
    void CanvasRendererSupportsBackgroundLayer();
    void CanvasRendererSupportsPainterGeometryLayer();
    void UmbrellaHeaderExposesCanvasApi();
    void CanvasRendererRequestUpdateEmitsSignal();
    void BasicCustomRendererDrawsPainterLayers();
    void CanvasSnapperPrefersVisibleGeometry();
    void CanvasSnapperSnapsToGrid();
    void CanvasSnapperSnapsToPolylineVertices();
    void CanvasViewReportsCurrentSnap();
    void CanvasViewDrawsSnapMarker();
    void CanvasViewRendersDocumentWithPainter();
    void CanvasRendererDrawsOpenAndClosedPolylines();
    void CanvasViewWheelUsesCadLabScale();
    void CanvasViewMiddlePanUsesDeadband();
    void CanvasViewKeyboardAndContextSignals();
    void CanvasViewDisplayPropertiesAndMapping();
    void CanvasViewFitAllUsesDocumentBounds();
    void CanvasViewFitSelectionUsesSelectedBounds();
    void CanvasViewFitCommandsRejectEmptyDocument();
    void CanvasViewCenterOnWorldMovesViewportCenter();
    void CanvasViewZoomInShowsFinerGridUnits();
    void CanvasViewRendersCadLabOverlays();
    void CanvasViewAxesStayAttachedToWorldAfterPan();
    void DocumentSelectionTogglesAndSignals();
    void CanvasViewSingleClickSelectionFollowsCadLabRules();
    void CanvasViewSingleClickSelectsAllGeometryTypes();
    void CanvasViewSelectsPolylineByStroke();
    void CanvasViewHitTestIgnoresInvisibleGeometry();
    void CanvasViewHitTestUsesPointDisplaySize();
    void CanvasViewHitTestUsesLineDisplayWidth();
    void CanvasViewWindowSelectionUsesDragDirection();
    void CanvasViewWindowSelectionSelectsAllGeometryTypes();
    void CanvasViewWindowSelectionSelectsPolylines();
    void CanvasViewWindowSelectionIgnoresInvisibleGeometry();
    void CanvasViewSelectionBoxUsesCadLabStyle();
    void CanvasViewDeleteKeyRemovesSelectedGeometry();
    void CanvasViewEscapeClearsSelection();
    void CanvasViewDragMovesSelectedGeometry();
    void CanvasViewEscapeCancelsGeometryMove();
    void CanvasViewDragMovesLineEndpointHandle();
    void CanvasViewMovesPolylineVerticesTogether();
    void CanvasViewDragsPolylineVertexHandle();
    void CanvasViewEscapeRestoresPolylineVertexEdit();
    void CanvasViewDragChangesCircleRadiusHandle();
    void CanvasViewDragChangesArcEndpointHandle();
    void CanvasViewEscapeCancelsGeometryHandleEdit();
    void CanvasViewDrawsEditHandlesForSingleSelection();
    void CanvasViewDrawsPolylineVertexHandles();
    void CanvasViewSupportsCustomRendererAssignment();
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

void CanvasCoreTest::DefaultTransformIsIdentityLike()
{
    mini_graph::CanvasTransform transform;
    const QSizeF viewport(800.0, 600.0);

    QCOMPARE(transform.GetZoom(), 1.0);
    QCOMPARE(transform.GetCenter(), QPointF(0.0, 0.0));
    QCOMPARE(transform.MapToWorld(QPointF(400.0, 300.0), viewport), QPointF(0.0, 0.0));
    QCOMPARE(transform.MapFromWorld(QPointF(0.0, 0.0), viewport), QPointF(400.0, 300.0));
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

    const QPointF worldBefore = transform.MapToWorld(cursor, viewport);
    transform.ZoomAt(cursor, viewport, 2.0);
    const QPointF worldAfter = transform.MapToWorld(cursor, viewport);

    QVERIFY(qAbs(worldBefore.x() - worldAfter.x()) < 0.000001);
    QVERIFY(qAbs(worldBefore.y() - worldAfter.y()) < 0.000001);
    QCOMPARE(transform.GetZoom(), 2.0);
}

void CanvasCoreTest::PanByMovesWorldPointWithViewportDelta()
{
    mini_graph::CanvasTransform transform;
    const QSizeF viewport(800.0, 600.0);
    const QPointF worldPoint(35.0, -20.0);
    const QPointF viewportDelta(45.0, -18.0);

    const QPointF viewportBefore = transform.MapFromWorld(worldPoint, viewport);
    transform.PanBy(viewportDelta);
    const QPointF viewportAfter = transform.MapFromWorld(worldPoint, viewport);

    QVERIFY(qAbs((viewportAfter.x() - viewportBefore.x()) - viewportDelta.x()) < 0.000001);
    QVERIFY(qAbs((viewportAfter.y() - viewportBefore.y()) - viewportDelta.y()) < 0.000001);
}

void CanvasCoreTest::FitRectCentersAndFitsViewport()
{
    mini_graph::CanvasTransform transform;
    const QSizeF viewport(800.0, 600.0);

    QVERIFY(transform.FitRect(QRectF(-100.0, -50.0, 200.0, 100.0),
                              viewport,
                              40.0));
    QCOMPARE(transform.GetCenter(), QPointF(0.0, 0.0));
    QVERIFY(qAbs(transform.GetZoom() - 3.6) < 0.000001);

    const QPointF topLeft = transform.MapFromWorld(QPointF(-100.0, 50.0), viewport);
    const QPointF bottomRight = transform.MapFromWorld(QPointF(100.0, -50.0), viewport);
    QVERIFY(qAbs(topLeft.x() - 40.0) < 0.000001);
    QVERIFY(qAbs(topLeft.y() - 120.0) < 0.000001);
    QVERIFY(qAbs(bottomRight.x() - 760.0) < 0.000001);
    QVERIFY(qAbs(bottomRight.y() - 480.0) < 0.000001);
}

void CanvasCoreTest::FitRectRejectsInvalidViewport()
{
    mini_graph::CanvasTransform transform;
    transform.SetCenter(QPointF(12.0, -8.0));
    transform.SetZoom(2.0);

    QVERIFY(!transform.FitRect(QRectF(0.0, 0.0, 10.0, 10.0),
                               QSizeF(0.0, 600.0),
                               40.0));
    QCOMPARE(transform.GetCenter(), QPointF(12.0, -8.0));
    QCOMPARE(transform.GetZoom(), 2.0);
}

void CanvasCoreTest::DocumentTracksLinesAndBounds()
{
    mini_graph::CanvasDocument document;

    const mini_graph::ItemId firstId = document.AddLine(QPointF(-5.0, 2.0), QPointF(10.0, 8.0));
    const mini_graph::ItemId secondId = document.AddLine(QPointF(-2.0, -4.0), QPointF(3.0, 1.0));

    QVERIFY(firstId != secondId);
    QCOMPARE(document.GetItemCount(), 2);
    QCOMPARE(document.GetBounds(), QRectF(-5.0, -4.0, 15.0, 12.0));
    QCOMPARE(document.GetLinePrimitives().size(), 2);
}

void CanvasCoreTest::DocumentUpdatesGeometryByType()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(QPointF(1.0, 2.0));
    const mini_graph::ItemId lineId = document.AddLine(QPointF(0.0, 0.0),
                                                       QPointF(1.0, 1.0));
    const mini_graph::ItemId arcId = document.AddArc(QPointF(0.0, 0.0),
                                                     5.0,
                                                     0.0,
                                                     1.0);
    const mini_graph::ItemId circleId = document.AddCircle(QPointF(0.0, 0.0), 3.0);
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    mini_graph::StruPointGeometry pointGeometry;
    pointGeometry.position = QPointF(10.0, 20.0);
    QVERIFY(document.SetPointGeometry(pointId, pointGeometry));

    mini_graph::StruLineGeometry lineGeometry;
    lineGeometry.startPoint = QPointF(-4.0, -5.0);
    lineGeometry.endPoint = QPointF(6.0, 7.0);
    QVERIFY(document.SetLineGeometry(lineId, lineGeometry));

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = QPointF(8.0, 9.0);
    arcGeometry.radius = 12.0;
    arcGeometry.startAngle = 0.5;
    arcGeometry.endAngle = 2.0;
    arcGeometry.reversed = true;
    QVERIFY(document.SetArcGeometry(arcId, arcGeometry));

    mini_graph::StruCircleGeometry circleGeometry;
    circleGeometry.center = QPointF(-8.0, -9.0);
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
    const mini_graph::ItemId firstId = document.AddLine(QPointF(-10.0, -2.0),
                                                        QPointF(4.0, 6.0));
    const mini_graph::ItemId secondId = document.AddCircle(QPointF(100.0, 100.0), 5.0);
    const mini_graph::ItemId thirdId = document.AddPoint(QPointF(-80.0, -80.0));

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

    const mini_graph::ItemId id = document.AddLine(QPointF(0.0, 0.0), QPointF(1.0, 1.0));
    document.AddLine(QPointF(2.0, 2.0), QPointF(3.0, 3.0));
    QVERIFY(document.RemoveItem(id));
    document.Clear();

    QCOMPARE(changedSpy.count(), 4);
    QCOMPARE(document.GetItemCount(), 0);
}

void CanvasCoreTest::DocumentStoresMixedGeometryPools()
{
    mini_graph::CanvasDocument document;

    mini_graph::StruPointGeometry pointGeometry;
    pointGeometry.position = QPointF(1.0, 2.0);
    const mini_graph::ItemId pointId = document.AddPoint(pointGeometry);

    mini_graph::StruLineGeometry lineGeometry;
    lineGeometry.startPoint = QPointF(-2.0, 0.0);
    lineGeometry.endPoint = QPointF(3.0, 4.0);
    const mini_graph::ItemId lineId = document.AddLine(lineGeometry);

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = QPointF(10.0, 10.0);
    arcGeometry.radius = 5.0;
    arcGeometry.startAngle = 0.0;
    arcGeometry.endAngle = 1.5707963267948966;
    arcGeometry.reversed = false;
    const mini_graph::ItemId arcId = document.AddArc(arcGeometry);

    mini_graph::StruCircleGeometry circleGeometry;
    circleGeometry.center = QPointF(-10.0, -5.0);
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
    pointGeometry.position = QPointF(30.0, 40.0);
    const mini_graph::ItemId pointId = document.AddPoint(pointGeometry);

    mini_graph::StruLineGeometry lineGeometry;
    lineGeometry.startPoint = QPointF(-5.0, -2.0);
    lineGeometry.endPoint = QPointF(2.0, 3.0);
    const mini_graph::ItemId lineId = document.AddLine(lineGeometry);

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = QPointF(10.0, 10.0);
    arcGeometry.radius = 5.0;
    arcGeometry.startAngle = 0.0;
    arcGeometry.endAngle = 1.5707963267948966;
    arcGeometry.reversed = false;
    const mini_graph::ItemId arcId = document.AddArc(arcGeometry);

    mini_graph::StruCircleGeometry circleGeometry;
    circleGeometry.center = QPointF(-10.0, -5.0);
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

void CanvasCoreTest::DocumentRejectsInvalidCircularGeometry()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, &mini_graph::CanvasDocument::Changed);

    mini_graph::StruCircleGeometry zeroRadiusCircle;
    zeroRadiusCircle.center = QPointF(0.0, 0.0);
    zeroRadiusCircle.radius = 0.0;
    QCOMPARE(document.AddCircle(zeroRadiusCircle), mini_graph::ItemId(0));

    mini_graph::StruCircleGeometry negativeRadiusCircle;
    negativeRadiusCircle.center = QPointF(0.0, 0.0);
    negativeRadiusCircle.radius = -1.0;
    QCOMPARE(document.AddCircle(negativeRadiusCircle), mini_graph::ItemId(0));

    mini_graph::StruArcGeometry zeroSweepArc;
    zeroSweepArc.center = QPointF(0.0, 0.0);
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

    const mini_graph::ItemId pointId = document.AddPoint(QPointF(0.0, 0.0));
    const QVector<QPointF> openVertices = QVector<QPointF>()
            << QPointF(1.0, 2.0) << QPointF(4.0, 2.0);
    const mini_graph::ItemId openPolylineId = document.AddPolyline(openVertices);
    const mini_graph::ItemId lineId = document.AddLine(QPointF(5.0, 5.0),
                                                       QPointF(6.0, 6.0));

    mini_graph::StruPolylineGeometry closedGeometry;
    closedGeometry.vertices = QVector<QPointF>()
            << QPointF(-1.0, -2.0) << QPointF(2.0, -2.0) << QPointF(2.0, 1.0);
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

    QCOMPARE(document.AddPolyline(QVector<QPointF>() << QPointF(0.0, 0.0)),
             mini_graph::ItemId(0));

    mini_graph::StruPolylineGeometry closedWithTwoVertices;
    closedWithTwoVertices.vertices = QVector<QPointF>()
            << QPointF(0.0, 0.0) << QPointF(1.0, 1.0);
    closedWithTwoVertices.closed = true;
    QCOMPARE(document.AddPolyline(closedWithTwoVertices), mini_graph::ItemId(0));

    mini_graph::StruPolylineGeometry validGeometry;
    validGeometry.vertices = QVector<QPointF>()
            << QPointF(0.0, 0.0) << QPointF(1.0, 1.0);
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
    nonFiniteGeometry.vertices[1].setX(std::numeric_limits<qreal>::infinity());
    QCOMPARE(document.AddPolyline(nonFiniteGeometry), mini_graph::ItemId(0));
    QCOMPARE(document.GetItemCount(), 1);
    QCOMPARE(changedSpy.count(), 0);
}

void CanvasCoreTest::DocumentBoundsAndTranslatesPolyline()
{
    mini_graph::CanvasDocument document;
    mini_graph::StruPolylineGeometry geometry;
    geometry.vertices = QVector<QPointF>()
            << QPointF(-2.0, 1.0) << QPointF(4.0, -5.0) << QPointF(10.0, 3.0);
    geometry.closed = true;
    const mini_graph::ItemId id = document.AddPolyline(geometry);

    QVERIFY(id != 0);
    QCOMPARE(document.GetBounds(), QRectF(-2.0, -5.0, 12.0, 8.0));
    QVERIFY(document.TranslateItems(QVector<mini_graph::ItemId>() << id,
                                    QPointF(3.0, -2.0)));

    mini_graph::StruPolylineGeometry translatedGeometry;
    QVERIFY(document.GetPolylineGeometry(id, &translatedGeometry));
    QCOMPARE(translatedGeometry.vertices,
             QVector<QPointF>() << QPointF(1.0, -1.0)
                                << QPointF(7.0, -7.0)
                                << QPointF(13.0, 1.0));
    QVERIFY(translatedGeometry.closed);
    QCOMPARE(document.GetBounds(), QRectF(1.0, -7.0, 12.0, 8.0));
}

void CanvasCoreTest::DocumentSupportsParameterizedCreation()
{
    mini_graph::CanvasDocument document;
    QSignalSpy changedSpy(&document, SIGNAL(Changed()));

    const mini_graph::ItemId pointId = document.AddPoint(QPointF(1.0, 2.0));
    const mini_graph::ItemId arcId = document.AddArc(QPointF(3.0, 4.0),
                                                     5.0,
                                                     0.0,
                                                     1.5707963267948966,
                                                     true);
    const mini_graph::ItemId circleId = document.AddCircle(QPointF(-2.0, -1.0),
                                                           6.0);

    QVERIFY(pointId != 0);
    QVERIFY(arcId != 0);
    QVERIFY(circleId != 0);
    QCOMPARE(document.GetItemCount(), 3);
    QCOMPARE(document.GetPointPrimitives().at(0).geometry.position,
             QPointF(1.0, 2.0));
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.center,
             QPointF(3.0, 4.0));
    QCOMPARE(document.GetArcPrimitives().at(0).geometry.radius, 5.0);
    QVERIFY(document.GetArcPrimitives().at(0).geometry.reversed);
    QCOMPARE(document.GetCirclePrimitives().at(0).geometry.center,
             QPointF(-2.0, -1.0));
    QCOMPARE(document.GetCirclePrimitives().at(0).geometry.radius, 6.0);
    QCOMPARE(changedSpy.count(), 3);

    QCOMPARE(document.AddCircle(QPointF(0.0, 0.0), 0.0), mini_graph::ItemId(0));
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

void CanvasCoreTest::DocumentStoresAndUpdatesItemStyle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId pointId = document.AddPoint(QPointF(1.0, 2.0));
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
    const mini_graph::ItemId lineId = document.AddLine(QPointF(0.0, 0.0),
                                                       QPointF(1.0, 1.0),
                                                       QColor(),
                                                       -2.0);
    QVERIFY(lineId != 0);

    const mini_graph::StruLinePrimitive &line = document.GetLinePrimitives().at(0);
    QCOMPARE(line.style.strokeColor, QColor(Qt::white));
    QCOMPARE(line.color, line.style.strokeColor);
    QCOMPARE(line.style.strokeWidth, 0.0);
    QCOMPARE(line.width, line.style.strokeWidth);
}

void CanvasCoreTest::GeometryArcSweepFollowsDirection()
{
    const qreal halfPi = 1.5707963267948966;

    mini_graph::StruArcGeometry counterClockwiseArc;
    counterClockwiseArc.center = QPointF(0.0, 0.0);
    counterClockwiseArc.radius = 10.0;
    counterClockwiseArc.startAngle = 0.0;
    counterClockwiseArc.endAngle = halfPi;
    counterClockwiseArc.reversed = false;

    QVERIFY(qAbs(mini_graph::GetArcSweep(counterClockwiseArc) - halfPi)
            < 0.000000000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, 0.0).x() - 10.0)
            < 0.000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, 0.0).y())
            < 0.000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, halfPi).x())
            < 0.000001);
    QVERIFY(qAbs(mini_graph::CalculateArcPoint(counterClockwiseArc, halfPi).y() - 10.0)
            < 0.000001);

    mini_graph::StruArcGeometry clockwiseArc;
    clockwiseArc.center = QPointF(0.0, 0.0);
    clockwiseArc.radius = 10.0;
    clockwiseArc.startAngle = halfPi;
    clockwiseArc.endAngle = 0.0;
    clockwiseArc.reversed = true;

    QVERIFY(qAbs(mini_graph::GetArcSweep(clockwiseArc) - halfPi)
            < 0.000000000001);
    QVERIFY(mini_graph::IsValidGeometry(clockwiseArc));
}

void CanvasCoreTest::GeometryBoundsCoverAllPrimitiveTypes()
{
    mini_graph::StruPointGeometry point;
    point.position = QPointF(2.0, -3.0);
    QCOMPARE(mini_graph::GetGeometryBounds(point), QRectF(2.0, -3.0, 0.0, 0.0));

    mini_graph::StruLineGeometry line;
    line.startPoint = QPointF(-2.0, 1.0);
    line.endPoint = QPointF(4.0, -5.0);
    QCOMPARE(mini_graph::GetGeometryBounds(line), QRectF(-2.0, -5.0, 6.0, 6.0));

    mini_graph::StruArcGeometry arc;
    arc.center = QPointF(10.0, 10.0);
    arc.radius = 5.0;
    arc.startAngle = 0.0;
    arc.endAngle = 1.5707963267948966;
    arc.reversed = false;
    QCOMPARE(mini_graph::GetGeometryBounds(arc), QRectF(10.0, 10.0, 5.0, 5.0));

    mini_graph::StruCircleGeometry circle;
    circle.center = QPointF(-3.0, -4.0);
    circle.radius = 2.0;
    QCOMPARE(mini_graph::GetGeometryBounds(circle), QRectF(-5.0, -6.0, 4.0, 4.0));
}

void CanvasCoreTest::GeometryValidatesPolylineAndBounds()
{
    mini_graph::StruPolylineGeometry openGeometry;
    openGeometry.vertices = QVector<QPointF>()
            << QPointF(1.0, 2.0) << QPointF(5.0, 2.0) << QPointF(5.0, 8.0);
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
    oneVertexGeometry.vertices << QPointF(0.0, 0.0);
    QVERIFY(!mini_graph::IsValidGeometry(oneVertexGeometry));

    mini_graph::StruPolylineGeometry closedWithTwoVertices;
    closedWithTwoVertices.vertices = QVector<QPointF>()
            << QPointF(0.0, 0.0) << QPointF(1.0, 1.0);
    closedWithTwoVertices.closed = true;
    QVERIFY(!mini_graph::IsValidGeometry(closedWithTwoVertices));

    mini_graph::StruPolylineGeometry nonFiniteGeometry = openGeometry;
    nonFiniteGeometry.vertices[0].setY(std::numeric_limits<qreal>::quiet_NaN());
    QVERIFY(!mini_graph::IsValidGeometry(nonFiniteGeometry));
}

void CanvasCoreTest::GeometryTessellatesArcAndCircle()
{
    mini_graph::StruArcGeometry arc;
    arc.center = QPointF(1.0, -2.0);
    arc.radius = 10.0;
    arc.startAngle = 0.0;
    arc.endAngle = 1.5707963267948966;
    arc.reversed = false;

    const QVector<QPointF> arcPoints = mini_graph::TessellateArc(arc, 0.01);
    QVERIFY(arcPoints.size() >= 2);
    QVERIFY(QLineF(arcPoints.first(),
                   mini_graph::CalculateArcPoint(arc, arc.startAngle)).length()
            < 0.000001);
    QVERIFY(QLineF(arcPoints.last(),
                   mini_graph::CalculateArcPoint(arc, arc.endAngle)).length()
            < 0.000001);
    for (const QPointF &point : arcPoints) {
        QVERIFY(qAbs(QLineF(arc.center, point).length() - arc.radius) < 0.01);
    }

    mini_graph::StruCircleGeometry circle;
    circle.center = QPointF(4.0, 5.0);
    circle.radius = 12.0;
    const QVector<QPointF> circlePoints = mini_graph::TessellateCircle(circle, 0.01);
    QVERIFY(circlePoints.size() >= 17);
    QCOMPARE(circlePoints.first(), circlePoints.last());
    for (const QPointF &point : circlePoints) {
        QVERIFY(qAbs(QLineF(circle.center, point).length() - circle.radius) < 0.01);
    }
}

void CanvasCoreTest::CanvasRenderContextProvidesWorldHelpers()
{
    mini_graph::CanvasTransform transform;
    transform.SetZoom(2.0);
    transform.SetCenter(QPointF(10.0, -5.0));

    const mini_graph::CanvasRenderContext context(nullptr,
                                                  transform,
                                                  QSize(800, 600));
    QCOMPARE(context.MapToWorld(QPointF(400.0, 300.0)), QPointF(10.0, -5.0));
    QCOMPARE(context.MapFromWorld(QPointF(10.0, -5.0)), QPointF(400.0, 300.0));
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
    const QPointF anchor = context.MapFromWorld(QPointF(0.0, 25.0));
    QVERIFY(image.pixelColor(qRound(anchor.x()), qRound(anchor.y())) != Qt::black);

    renderer.RenderOverlay(painter, context);
    painter.end();

    QVERIFY(image.pixelColor(16, 22) != Qt::black);
}

void CanvasCoreTest::CanvasSnapperPrefersVisibleGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(QPointF(3.0, 3.0),
                                                       QPointF(20.0, 3.0));

    const mini_graph::CanvasSnapResult result = mini_graph::CanvasSnapper::FindSnap(
                &document,
                QPointF(3.4, 3.2),
                1.0,
                10.0,
                true,
                true);
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::LineEndpoint));
    QCOMPARE(result.itemId, lineId);
    QCOMPARE(result.position, QPointF(3.0, 3.0));

    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(lineId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(lineId, style));

    const mini_graph::CanvasSnapResult hiddenResult = mini_graph::CanvasSnapper::FindSnap(
                &document,
                QPointF(3.4, 3.2),
                1.0,
                10.0,
                true,
                false);
    QCOMPARE(static_cast<int>(hiddenResult.type),
             static_cast<int>(mini_graph::CanvasSnapType::None));
}

void CanvasCoreTest::CanvasSnapperSnapsToGrid()
{
    const mini_graph::CanvasSnapResult result = mini_graph::CanvasSnapper::FindSnap(
                nullptr,
                QPointF(4.6, 5.2),
                1.0,
                10.0,
                false,
                true);
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::Grid));
    QCOMPARE(result.itemId, static_cast<mini_graph::ItemId>(0));
    QCOMPARE(result.position, QPointF(0.0, 10.0));
}

void CanvasCoreTest::CanvasSnapperSnapsToPolylineVertices()
{
    mini_graph::CanvasDocument document;
    const QVector<QPointF> vertices = QVector<QPointF>()
            << QPointF(1.0, 2.0) << QPointF(5.0, 6.0) << QPointF(9.0, 2.0);
    const mini_graph::ItemId polylineId = document.AddPolyline(vertices);

    QVERIFY(polylineId != 0);
    const mini_graph::CanvasSnapResult result = mini_graph::CanvasSnapper::FindSnap(
                &document,
                QPointF(5.1, 6.05),
                0.25,
                1.0,
                true,
                false);
    QVERIFY(result.IsValid());
    QCOMPARE(static_cast<int>(result.type),
             static_cast<int>(mini_graph::CanvasSnapType::PolylineVertex));
    QCOMPARE(result.itemId, polylineId);
    QCOMPARE(result.position, vertices.at(1));

    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(polylineId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(polylineId, style));
    const mini_graph::CanvasSnapResult hiddenResult = mini_graph::CanvasSnapper::FindSnap(
                &document,
                QPointF(5.1, 6.05),
                0.25,
                1.0,
                true,
                false);
    QVERIFY(!hiddenResult.IsValid());
}

void CanvasCoreTest::CanvasViewReportsCurrentSnap()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(QPointF(3.0, 3.0),
                                                       QPointF(20.0, 3.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    view.SetSnapEnabled(true);

    const QPointF mousePosition = view.MapFromWorld(QPointF(3.4, 3.2));
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
    QCOMPARE(result.position, QPointF(3.0, 3.0));

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

    const QPointF mousePosition = view.MapFromWorld(QPointF(4.6, 5.2));
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

    const QPointF snapPosition = view.MapFromWorld(QPointF(0.0, 10.0));
    QVERIFY(image.pixelColor(qRound(snapPosition.x()),
                             qRound(snapPosition.y())).yellow() > 150);
}

void CanvasCoreTest::CanvasViewRendersDocumentWithPainter()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(
                QPointF(-30.0, 0.0),
                QPointF(30.0, 0.0),
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

void CanvasCoreTest::CanvasRendererDrawsOpenAndClosedPolylines()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId openId = document.AddPolyline(
                QVector<QPointF>() << QPointF(-30.0, 20.0)
                                   << QPointF(0.0, 50.0)
                                   << QPointF(30.0, 20.0));
    const mini_graph::ItemId closedId = document.AddPolyline(
                QVector<QPointF>() << QPointF(-30.0, -10.0)
                                   << QPointF(30.0, -10.0)
                                   << QPointF(0.0, -50.0),
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

    QVERIFY(hasColorNear(view.MapFromWorld(QPointF(-15.0, 35.0)), QColor(Qt::blue)));
    QVERIFY(hasColorNear(view.MapFromWorld(QPointF(-15.0, -30.0)), QColor(Qt::green)));
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
    const QPointF initialCenter = view.GetTransform().GetCenter();

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
    QCOMPARE(view.GetTransform().GetCenter(), initialCenter);

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

void CanvasCoreTest::CanvasViewKeyboardAndContextSignals()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    QSignalSpy contextSpy(&view, SIGNAL(ContextMenuRequested(QPointF,QPoint)));
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
    QCOMPARE(view.MapToWorld(QPointF(400.0, 300.0)), QPointF(0.0, 0.0));
    QCOMPARE(view.MapFromWorld(QPointF(0.0, 0.0)), QPointF(400.0, 300.0));
}

void CanvasCoreTest::CanvasViewFitAllUsesDocumentBounds()
{
    mini_graph::CanvasDocument document;
    document.AddLine(QPointF(-100.0, -50.0), QPointF(100.0, 50.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QVERIFY(view.FitAll(40.0));
    QCOMPARE(view.GetTransform().GetCenter(), QPointF(0.0, 0.0));
    QVERIFY(qAbs(view.GetZoom() - 3.6) < 0.000001);
    QVERIFY(view.MapFromWorld(QPointF(-100.0, 50.0)).x() >= 40.0);
    QVERIFY(view.MapFromWorld(QPointF(100.0, -50.0)).x() <= 760.0);
}

void CanvasCoreTest::CanvasViewFitSelectionUsesSelectedBounds()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId selectedId = document.AddCircle(QPointF(100.0, 50.0),
                                                             10.0);
    document.AddCircle(QPointF(-500.0, -500.0), 20.0);
    document.SelectItem(selectedId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    QVERIFY(view.FitSelection(40.0));
    QCOMPARE(view.GetTransform().GetCenter(), QPointF(100.0, 50.0));
    QVERIFY(view.MapFromWorld(QPointF(100.0, 50.0)) == QPointF(400.0, 300.0));
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
    QCOMPARE(view.GetTransform().GetCenter(), initialTransform.GetCenter());
    QCOMPARE(view.GetTransform().GetZoom(), initialTransform.GetZoom());
}

void CanvasCoreTest::CanvasViewCenterOnWorldMovesViewportCenter()
{
    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);

    view.CenterOnWorld(QPointF(125.0, -75.0));

    QCOMPARE(view.GetTransform().GetCenter(), QPointF(125.0, -75.0));
    QCOMPARE(view.MapFromWorld(QPointF(125.0, -75.0)), QPointF(400.0, 300.0));
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

    const QPointF origin = view.MapFromWorld(QPointF(0.0, 0.0));
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

void CanvasCoreTest::DocumentSelectionTogglesAndSignals()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId firstId = document.AddLine(QPointF(-1.0, 0.0), QPointF(1.0, 0.0));
    const mini_graph::ItemId secondId = document.AddLine(QPointF(-1.0, 1.0), QPointF(1.0, 1.0));
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
    const mini_graph::ItemId firstId = document.AddLine(QPointF(-100.0, 50.0), QPointF(100.0, 50.0));
    const mini_graph::ItemId secondId = document.AddLine(QPointF(-100.0, -50.0), QPointF(100.0, -50.0));

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
    const mini_graph::ItemId pointId = document.AddPoint(QPointF(-150.0, 100.0));
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-80.0, -30.0),
                                                       QPointF(80.0, -30.0));
    const mini_graph::ItemId arcId = document.AddArc(QPointF(0.0, 100.0),
                                                     40.0,
                                                     0.0,
                                                     1.5707963267948966);
    const mini_graph::ItemId circleId = document.AddCircle(QPointF(150.0, 100.0),
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

    clickAt(view.MapFromWorld(QPointF(-150.0, 100.0)));
    QVERIFY(document.IsItemSelected(pointId));
    document.ClearSelection();

    clickAt(view.MapFromWorld(QPointF(0.0, -30.0)));
    QVERIFY(document.IsItemSelected(lineId));
    document.ClearSelection();

    mini_graph::StruArcGeometry arcGeometry;
    arcGeometry.center = QPointF(0.0, 100.0);
    arcGeometry.radius = 40.0;
    arcGeometry.startAngle = 0.0;
    arcGeometry.endAngle = 1.5707963267948966;
    const QPointF arcPoint = mini_graph::CalculateArcPoint(arcGeometry,
                                                           0.7853981633974483);
    clickAt(view.MapFromWorld(arcPoint));
    QVERIFY(document.IsItemSelected(arcId));
    document.ClearSelection();

    clickAt(view.MapFromWorld(QPointF(185.0, 100.0)));
    QVERIFY(document.IsItemSelected(circleId));
}

void CanvasCoreTest::CanvasViewSelectsPolylineByStroke()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId openId = document.AddPolyline(
                QVector<QPointF>() << QPointF(-100.0, 0.0)
                                   << QPointF(100.0, 0.0));
    const mini_graph::ItemId closedId = document.AddPolyline(
                QVector<QPointF>() << QPointF(-30.0, -10.0)
                                   << QPointF(30.0, -10.0)
                                   << QPointF(0.0, -50.0),
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
    clickAt(view.MapFromWorld(QPointF(0.0, 0.0)));
    QVERIFY(document.IsItemSelected(openId));

    document.ClearSelection();
    clickAt(view.MapFromWorld(QPointF(-15.0, -30.0)));
    QVERIFY(document.IsItemSelected(closedId));
}

void CanvasCoreTest::CanvasViewHitTestIgnoresInvisibleGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-20.0, 0.0),
                                                       QPointF(20.0, 0.0));
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(lineId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(lineId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF position = view.MapFromWorld(QPointF(0.0, 0.0));
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
    const mini_graph::ItemId pointId = document.AddPoint(QPointF(0.0, 0.0));
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(pointId, &style));
    style.pointSize = 24.0;
    QVERIFY(document.SetItemStyle(pointId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF pointPosition = view.MapFromWorld(QPointF(0.0, 0.0));
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

void CanvasCoreTest::CanvasViewHitTestUsesLineDisplayWidth()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-40.0, 0.0),
                                                       QPointF(40.0, 0.0));
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(lineId, &style));
    style.strokeWidth = 20.0;
    QVERIFY(document.SetItemStyle(lineId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF clickPosition = view.MapFromWorld(QPointF(0.0, 9.0));
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

void CanvasCoreTest::CanvasViewWindowSelectionUsesDragDirection()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId containedId = document.AddLine(QPointF(-100.0, 50.0),
                                                            QPointF(100.0, 50.0));
    const mini_graph::ItemId crossingId = document.AddLine(QPointF(100.0, 50.0),
                                                            QPointF(220.0, 50.0));
    const mini_graph::ItemId outsideId = document.AddLine(QPointF(220.0, 180.0),
                                                          QPointF(300.0, 180.0));

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
    const mini_graph::ItemId pointId = document.AddPoint(QPointF(-150.0, 100.0));
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-100.0, -50.0),
                                                       QPointF(100.0, -50.0));
    const mini_graph::ItemId arcId = document.AddArc(QPointF(0.0, 100.0),
                                                     40.0,
                                                     0.0,
                                                     1.5707963267948966);
    const mini_graph::ItemId circleId = document.AddCircle(QPointF(150.0, 100.0),
                                                            30.0);
    const mini_graph::ItemId outsideId = document.AddPoint(QPointF(300.0, 300.0));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    auto dragSelect = [&view](const QPointF &worldStart,
                              const QPointF &worldEnd) {
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

    dragSelect(QPointF(-200.0, -100.0), QPointF(220.0, 180.0));
    QVERIFY(document.IsItemSelected(pointId));
    QVERIFY(document.IsItemSelected(lineId));
    QVERIFY(document.IsItemSelected(arcId));
    QVERIFY(document.IsItemSelected(circleId));
    QVERIFY(!document.IsItemSelected(outsideId));

    document.ClearSelection();
    dragSelect(QPointF(220.0, 180.0), QPointF(-200.0, -100.0));
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
                QVector<QPointF>() << QPointF(-30.0, 0.0)
                                   << QPointF(0.0, 30.0)
                                   << QPointF(30.0, 0.0));
    const mini_graph::ItemId crossingClosedId = document.AddPolyline(
                QVector<QPointF>() << QPointF(60.0, -10.0)
                                   << QPointF(0.0, -60.0)
                                   << QPointF(-60.0, -10.0),
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

    dragSelect(view.MapFromWorld(QPointF(-50.0, 50.0)),
               view.MapFromWorld(QPointF(50.0, -50.0)));
    QVERIFY(document.IsItemSelected(containedOpenId));
    QVERIFY(!document.IsItemSelected(crossingClosedId));

    document.ClearSelection();
    dragSelect(view.MapFromWorld(QPointF(50.0, -50.0)),
               view.MapFromWorld(QPointF(-50.0, 50.0)));
    QVERIFY(document.IsItemSelected(crossingClosedId));
}

void CanvasCoreTest::CanvasViewWindowSelectionIgnoresInvisibleGeometry()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId circleId = document.AddCircle(QPointF(0.0, 0.0),
                                                            5.0);
    mini_graph::StruCanvasStyle style;
    QVERIFY(document.GetItemStyle(circleId, &style));
    style.visible = false;
    QVERIFY(document.SetItemStyle(circleId, style));

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(QPointF(-20.0, -20.0));
    const QPointF end = view.MapFromWorld(QPointF(20.0, 20.0));
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
    const mini_graph::ItemId pointId = document.AddPoint(QPointF(-20.0, 10.0));
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-10.0, 0.0),
                                                       QPointF(10.0, 0.0));
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

    const mini_graph::ItemId circleId = document.AddCircle(QPointF(30.0, 0.0),
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
    const mini_graph::ItemId firstId = document.AddLine(QPointF(-20.0, 0.0),
                                                        QPointF(20.0, 0.0));
    const mini_graph::ItemId secondId = document.AddCircle(QPointF(40.0, 0.0),
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
    const mini_graph::ItemId pointId = document.AddPoint(QPointF(-20.0, 10.0));
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-10.0, 0.0),
                                                       QPointF(10.0, 0.0));
    const mini_graph::ItemId arcId = document.AddArc(QPointF(0.0, 30.0),
                                                     5.0,
                                                     0.0,
                                                     1.5707963267948966);
    const mini_graph::ItemId circleId = document.AddCircle(QPointF(30.0, 0.0),
                                                            4.0);
    document.SelectItems(QVector<mini_graph::ItemId>()
                          << pointId << lineId << arcId << circleId,
                          false,
                          false);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(QPointF(0.0, 0.0));
    const QPointF end = start + QPointF(80.0, -40.0);
    const QPointF worldDelta = view.MapToWorld(end) - view.MapToWorld(start);

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
    QVERIFY(qAbs(point.geometry.position.x() - (-20.0 + worldDelta.x())) < 0.000001);
    QVERIFY(qAbs(point.geometry.position.y() - (10.0 + worldDelta.y())) < 0.000001);
    QVERIFY(qAbs(line.geometry.startPoint.x() - (-10.0 + worldDelta.x())) < 0.000001);
    QVERIFY(qAbs(line.geometry.startPoint.y() - worldDelta.y()) < 0.000001);
    QVERIFY(qAbs(line.geometry.endPoint.x() - (10.0 + worldDelta.x())) < 0.000001);
    QVERIFY(qAbs(line.geometry.endPoint.y() - worldDelta.y()) < 0.000001);
    QVERIFY(qAbs(arc.geometry.center.x() - worldDelta.x()) < 0.000001);
    QVERIFY(qAbs(arc.geometry.center.y() - (30.0 + worldDelta.y())) < 0.000001);
    QVERIFY(qAbs(circle.geometry.center.x() - (30.0 + worldDelta.x())) < 0.000001);
    QVERIFY(qAbs(circle.geometry.center.y() - worldDelta.y()) < 0.000001);

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
    geometry.vertices = QVector<QPointF>()
            << QPointF(-20.0, 0.0) << QPointF(20.0, 0.0) << QPointF(20.0, 20.0);
    geometry.closed = true;
    const mini_graph::ItemId polylineId = document.AddPolyline(geometry);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(QPointF(0.0, 0.0));
    const QPointF end = start + QPointF(40.0, -30.0);
    const QPointF worldDelta = view.MapToWorld(end) - view.MapToWorld(start);
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
             QVector<QPointF>() << QPointF(-20.0, 0.0) + worldDelta
                                << QPointF(20.0, 0.0) + worldDelta
                                << QPointF(20.0, 20.0) + worldDelta);
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
    geometry.vertices = QVector<QPointF>()
            << QPointF(-20.0, 0.0) << QPointF(0.0, 20.0) << QPointF(20.0, 0.0);
    geometry.closed = true;
    const mini_graph::ItemId polylineId = document.AddPolyline(geometry);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(geometry.vertices.at(1));
    const QPointF editedVertex(5.0, 30.0);
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
    geometry.vertices = QVector<QPointF>()
            << QPointF(-20.0, 0.0) << QPointF(0.0, 20.0) << QPointF(20.0, 0.0);
    geometry.closed = true;
    const mini_graph::ItemId polylineId = document.AddPolyline(geometry);
    document.SelectItem(polylineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(geometry.vertices.at(1));
    const QPointF end = view.MapFromWorld(QPointF(5.0, 30.0));
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
    QCOMPARE(changedGeometry.vertices.at(1), QPointF(5.0, 30.0));
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

void CanvasCoreTest::CanvasViewEscapeCancelsGeometryMove()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-10.0, 0.0),
                                                       QPointF(10.0, 0.0));
    document.SelectItem(lineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    QSignalSpy cancelSpy(&view, &mini_graph::CanvasView::InteractionCancelled);

    const QPointF start = view.MapFromWorld(QPointF(0.0, 0.0));
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
    QVERIFY(document.GetLinePrimitives().at(0).geometry.startPoint != QPointF(-10.0, 0.0));

    QKeyEvent escapeEvent(QEvent::KeyPress,
                          Qt::Key_Escape,
                          Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);

    QCOMPARE(document.GetLinePrimitives().at(0).geometry.startPoint,
             QPointF(-10.0, 0.0));
    QCOMPARE(document.GetLinePrimitives().at(0).geometry.endPoint,
             QPointF(10.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QCOMPARE(cancelSpy.count(), 1);
}

void CanvasCoreTest::CanvasViewDragMovesLineEndpointHandle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-10.0, 0.0),
                                                       QPointF(10.0, 0.0));
    document.SelectItem(lineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(QPointF(-10.0, 0.0));
    const QPointF end = view.MapFromWorld(QPointF(-20.0, 15.0));
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
    QCOMPARE(geometry.startPoint, QPointF(-20.0, 15.0));
    QCOMPARE(geometry.endPoint, QPointF(10.0, 0.0));

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
    const mini_graph::ItemId circleId = document.AddCircle(QPointF(0.0, 0.0),
                                                           10.0);
    document.SelectItem(circleId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(QPointF(10.0, 0.0));
    const QPointF end = view.MapFromWorld(QPointF(20.0, 0.0));
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
    QCOMPARE(geometry.center, QPointF(0.0, 0.0));
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
    const mini_graph::ItemId arcId = document.AddArc(QPointF(0.0, 0.0),
                                                     10.0,
                                                     0.0,
                                                     1.5707963267948966);
    document.SelectItem(arcId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);

    const QPointF start = view.MapFromWorld(QPointF(10.0, 0.0));
    const QPointF end = view.MapFromWorld(QPointF(20.0, 0.0));
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
    QCOMPARE(geometry.center, QPointF(0.0, 0.0));
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
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-10.0, 0.0),
                                                       QPointF(10.0, 0.0));
    document.SelectItem(lineId);

    CanvasViewHarness view;
    view.resize(800, 600);
    view.setUpdatesEnabled(false);
    view.SetDocument(&document);
    QSignalSpy cancelSpy(&view, &mini_graph::CanvasView::InteractionCancelled);

    const QPointF start = view.MapFromWorld(QPointF(-10.0, 0.0));
    const QPointF end = view.MapFromWorld(QPointF(-20.0, 15.0));
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

    QCOMPARE(document.GetLinePrimitives().at(0).geometry.startPoint,
             QPointF(-20.0, 15.0));
    QCOMPARE(document.GetLinePrimitives().at(0).geometry.endPoint,
             QPointF(10.0, 0.0));

    QKeyEvent escapeEvent(QEvent::KeyPress,
                          Qt::Key_Escape,
                          Qt::NoModifier);
    view.keyPressEvent(&escapeEvent);

    QCOMPARE(document.GetLinePrimitives().at(0).geometry.startPoint,
             QPointF(-10.0, 0.0));
    QCOMPARE(document.GetLinePrimitives().at(0).geometry.endPoint,
             QPointF(10.0, 0.0));
    QCOMPARE(document.GetSelectedItemCount(), 0);
    QCOMPARE(cancelSpy.count(), 1);
}

void CanvasCoreTest::CanvasViewDrawsEditHandlesForSingleSelection()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-10.0, 0.0),
                                                       QPointF(10.0, 0.0));
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

    const QPointF start = view.MapFromWorld(QPointF(-10.0, 0.0));
    const QPointF end = view.MapFromWorld(QPointF(10.0, 0.0));
    QCOMPARE(image.pixelColor(qRound(start.x()), qRound(start.y())),
             QColor(0, 170, 255));
    QCOMPARE(image.pixelColor(qRound(end.x()), qRound(end.y())),
             QColor(0, 170, 255));
}

void CanvasCoreTest::CanvasViewDrawsPolylineVertexHandles()
{
    mini_graph::CanvasDocument document;
    const QVector<QPointF> vertices = QVector<QPointF>()
            << QPointF(-30.0, -20.0) << QPointF(0.0, 10.0) << QPointF(30.0, -20.0);
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

    for (const QPointF &vertex : vertices) {
        const QPointF position = view.MapFromWorld(vertex);
        QCOMPARE(image.pixelColor(qRound(position.x()), qRound(position.y())),
                 QColor(0, 170, 255));
    }
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

void CanvasCoreTest::CanvasViewLifecycleCanBeCreatedAndDestroyed()
{
    mini_graph::CanvasDocument document;
    document.AddPoint(QPointF(20.0, 30.0));
    document.AddLine(QPointF(-80.0, -40.0), QPointF(80.0, -40.0));
    document.AddArc(QPointF(-40.0, 40.0), 20.0, 0.0, 1.5707963267948966);
    document.AddCircle(QPointF(50.0, 40.0), 15.0);

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
    QCOMPARE(document.GetCirclePrimitives().at(0).geometry.center,
             QPointF(12.0, -8.0));
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
    QCOMPARE(geometry.vertices,
             QVector<QPointF>() << QPointF(-10.0, 2.0)
                                << QPointF(3.5, 4.25)
                                << QPointF(6.0, -7.0));
    QVERIFY(geometry.closed);
}

void CanvasCoreTest::BasicExampleGeometryEditDialogUpdatesPolyline()
{
    mini_graph::CanvasDocument document;
    const QVector<QPointF> originalVertices = QVector<QPointF>()
            << QPointF(1.0, 2.0) << QPointF(4.0, 5.0) << QPointF(7.0, 8.0);
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
    QCOMPARE(geometry.vertices,
             QVector<QPointF>() << QPointF(-8.0, 3.0)
                                << QPointF(0.0, 12.0)
                                << QPointF(11.0, -4.0));
    QVERIFY(!geometry.closed);
}

void CanvasCoreTest::BasicExampleGeometryEditDialogUpdatesArcAndStyle()
{
    mini_graph::CanvasDocument document;
    const mini_graph::ItemId arcId = document.AddArc(QPointF(10.0, 20.0),
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
    QCOMPARE(geometry.center, QPointF(-12.0, 32.0));
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
    const mini_graph::ItemId lineId = document.AddLine(QPointF(-2.0, -3.0),
                                                       QPointF(4.0, 5.0));
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
    QCOMPARE(geometry.startPoint, QPointF(-2.0, -3.0));
    QCOMPARE(geometry.endPoint, QPointF(4.0, 5.0));
}

QTEST_MAIN(CanvasCoreTest)
#include "TstCanvasCore.moc"
