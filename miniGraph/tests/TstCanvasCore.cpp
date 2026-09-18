#include <QtTest>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QImage>
#include <QPainter>
#include <QTest>

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasTransform.h"
#include "MiniGraph/CanvasView.h"

class CanvasViewHarness : public mini_graph::CanvasView
{
public:
    using CanvasView::keyPressEvent;
    using CanvasView::keyReleaseEvent;
    using CanvasView::mouseMoveEvent;
    using CanvasView::mousePressEvent;
    using CanvasView::mouseReleaseEvent;
    using CanvasView::DrawAxes;
    using CanvasView::DrawGrid;
    using CanvasView::DrawRulerLabels;
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
    void DocumentTracksLinesAndBounds();
    void DocumentChangesEmitSignals();
    void CanvasViewWheelUsesCadLabScale();
    void CanvasViewMiddlePanUsesDeadband();
    void CanvasViewKeyboardAndContextSignals();
    void CanvasViewDisplayPropertiesAndMapping();
    void CanvasViewZoomInShowsFinerGridUnits();
    void CanvasViewRendersCadLabOverlays();
    void DocumentSelectionTogglesAndSignals();
    void CanvasViewSingleClickSelectionFollowsCadLabRules();
    void CanvasViewWindowSelectionUsesDragDirection();
    void CanvasViewSelectionBoxUsesCadLabStyle();
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

    view.mousePressEvent(&firstPress);
    view.mouseReleaseEvent(&firstRelease);
    QVERIFY(!document.IsItemSelected(firstId));

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

QTEST_MAIN(CanvasCoreTest)
#include "TstCanvasCore.moc"
