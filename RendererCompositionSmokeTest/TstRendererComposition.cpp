#include <QtTest>

#include <QImage>
#include <QPaintEvent>
#include <QPainter>

#include <MiniGraph/MiniGraph.h>

#include "RendererModuleA.h"
#include "RendererModuleB.h"

class SmokeCanvasView : public mini_graph::CanvasView
{
public:
    int PaintEventCount() const
    {
        return m_PaintEventCount;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        ++m_PaintEventCount;
        mini_graph::CanvasView::paintEvent(event);
    }

private:
    int m_PaintEventCount = 0;
};

class RendererCompositionSmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void independentRenderersComposeAndRemoveIndependently();
    void eachRendererCanRequestRefresh();
};

void RendererCompositionSmokeTest::independentRenderersComposeAndRemoveIndependently()
{
    QStringList events;
    SmokeCanvasView view;
    view.resize(160, 120);
    view.setUpdatesEnabled(false);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    RendererModuleA moduleA(&events);
    RendererModuleB moduleB(&events);
    QVERIFY(view.AddCustomRenderer(&moduleA));
    QVERIFY(view.AddCustomRenderer(&moduleB));

    QVector<mini_graph::CanvasRenderer *> renderers;
    renderers << &moduleA << &moduleB;
    QCOMPARE(view.GetCustomRenderers(), renderers);

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    view.render(&image);

    QStringList expectedEvents;
    expectedEvents << QStringLiteral("module-a")
                   << QStringLiteral("module-b");
    QCOMPARE(events, expectedEvents);

    const QPoint center = view.MapFromWorld(
                mini_graph::CanvasVector(0.0, 0.0)).toPoint();
    QCOMPARE(image.pixelColor(center), QColor(38, 139, 210));

    QVERIFY(view.RemoveCustomRenderer(&moduleA));
    QCOMPARE(view.GetCustomRenderer(),
             static_cast<mini_graph::CanvasRenderer *>(&moduleB));

    events.clear();
    image.fill(Qt::black);
    view.render(&image);

    QStringList remainingEvents;
    remainingEvents << QStringLiteral("module-b");
    QCOMPARE(events, remainingEvents);
    QCOMPARE(image.pixelColor(center), QColor(38, 139, 210));
}

void RendererCompositionSmokeTest::eachRendererCanRequestRefresh()
{
    SmokeCanvasView view;
    view.resize(160, 120);
    view.SetGridVisible(false);
    view.SetAxisVisible(false);
    view.SetRulerVisible(false);

    RendererModuleA moduleA(nullptr);
    RendererModuleB moduleB(nullptr);
    QVERIFY(view.AddCustomRenderer(&moduleA));
    QVERIFY(view.AddCustomRenderer(&moduleB));

    view.show();
    QTRY_VERIFY(view.PaintEventCount() > 0);

    int previousPaintEventCount = view.PaintEventCount();
    moduleA.RequestUpdate();
    QTRY_VERIFY(view.PaintEventCount() > previousPaintEventCount);

    previousPaintEventCount = view.PaintEventCount();
    moduleB.RequestUpdate();
    QTRY_VERIFY(view.PaintEventCount() > previousPaintEventCount);
}

QTEST_MAIN(RendererCompositionSmokeTest)

#include "TstRendererComposition.moc"
