#include <QtTest>
#include <QSignalSpy>

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasTransform.h"

class CanvasCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void DefaultTransformIsIdentityLike();
    void ZoomAtKeepsTheWorldPointUnderTheCursor();
    void PanByMovesWorldPointWithViewportDelta();
    void DocumentTracksLinesAndBounds();
    void DocumentChangesEmitSignals();
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

QTEST_MAIN(CanvasCoreTest)
#include "TstCanvasCore.moc"
