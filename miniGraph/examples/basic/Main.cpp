#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMainWindow>
#include <QToolBar>

#include "MiniGraph/MiniGraph.h"
#include "BasicCustomRenderer.h"
#include "BasicPointPlacementAction.h"
#include "GeometryCreateDialog.h"
#include "GeometryEditDialog.h"

int main(int argumentCount, char *argumentValues[])
{
    QApplication app(argumentCount, argumentValues);
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/minigraph-icon.png")));

    mini_graph::CanvasDocument canvasDocument;
    canvasDocument.AddPoint(mini_graph::CanvasVector(0.0, 0.0));
    canvasDocument.AddLine(mini_graph::CanvasVector(-140.0, -80.0),
                           mini_graph::CanvasVector(140.0, -80.0),
                           QColor(255, 220, 80));

    canvasDocument.AddArc(mini_graph::CanvasVector(-80.0, 40.0),
                          50.0,
                          0.0,
                          4.7123889803846899);
    canvasDocument.AddCircle(mini_graph::CanvasVector(100.0, 40.0), 40.0);

    BasicCustomRenderer customRenderer;
    QMainWindow wndMain;
    auto *widCanvas = new mini_graph::CanvasView;
    widCanvas->SetDocument(&canvasDocument);
    wndMain.setCentralWidget(widCanvas);

    QToolBar *tlbCanvas = wndMain.addToolBar(QObject::tr("Canvas"));
    QAction *actGrid = tlbCanvas->addAction(QObject::tr("Grid"));
    actGrid->setCheckable(true);
    actGrid->setChecked(widCanvas->IsGridVisible());
    QObject::connect(actGrid,
                     &QAction::toggled,
                     widCanvas,
                     &mini_graph::CanvasView::SetGridVisible);

    QAction *actAxis = tlbCanvas->addAction(QObject::tr("Axes"));
    actAxis->setCheckable(true);
    actAxis->setChecked(widCanvas->IsAxisVisible());
    QObject::connect(actAxis,
                     &QAction::toggled,
                     widCanvas,
                     &mini_graph::CanvasView::SetAxisVisible);

    QAction *actRuler = tlbCanvas->addAction(QObject::tr("Ruler"));
    actRuler->setCheckable(true);
    actRuler->setChecked(widCanvas->IsRulerVisible());
    QObject::connect(actRuler,
                     &QAction::toggled,
                     widCanvas,
                     &mini_graph::CanvasView::SetRulerVisible);

    QAction *actSnap = tlbCanvas->addAction(QObject::tr("Snap"));
    actSnap->setCheckable(true);
    actSnap->setChecked(widCanvas->IsSnapEnabled());
    QObject::connect(actSnap,
                     &QAction::toggled,
                     widCanvas,
                     &mini_graph::CanvasView::SetSnapEnabled);

    QAction *actGridSnap = tlbCanvas->addAction(QObject::tr("Grid snap"));
    actGridSnap->setCheckable(true);
    actGridSnap->setChecked(widCanvas->IsGridSnapEnabled());
    QObject::connect(actGridSnap,
                     &QAction::toggled,
                     widCanvas,
                     &mini_graph::CanvasView::SetGridSnapEnabled);

    QAction *actGeometrySnap = tlbCanvas->addAction(QObject::tr("Geometry snap"));
    actGeometrySnap->setCheckable(true);
    actGeometrySnap->setChecked(widCanvas->IsGeometrySnapEnabled());
    QObject::connect(actGeometrySnap,
                     &QAction::toggled,
                     widCanvas,
                     &mini_graph::CanvasView::SetGeometrySnapEnabled);

    QAction *actCustomRenderer = tlbCanvas->addAction(QObject::tr("Custom layer"));
    actCustomRenderer->setCheckable(true);
    QObject::connect(actCustomRenderer,
                     &QAction::toggled,
                     [&customRenderer, widCanvas](bool enabled) {
        widCanvas->SetCustomRenderer(enabled ? &customRenderer : nullptr);
    });

    QAction *actAdvanceCustom = tlbCanvas->addAction(QObject::tr("Advance custom"));
    QObject::connect(actAdvanceCustom,
                     &QAction::triggered,
                     [&customRenderer]() {
        customRenderer.Advance();
    });

    QAction *actReset = tlbCanvas->addAction(QObject::tr("Reset view"));
    QObject::connect(actReset,
                     &QAction::triggered,
                     widCanvas,
                     &mini_graph::CanvasView::ResetView);

    QAction *actFitAll = tlbCanvas->addAction(QObject::tr("Fit all"));
    QObject::connect(actFitAll,
                     &QAction::triggered,
                     [widCanvas]() {
        widCanvas->FitAll();
    });

    QAction *actFitSelection = tlbCanvas->addAction(QObject::tr("Fit selection"));
    QObject::connect(actFitSelection,
                     &QAction::triggered,
                     [widCanvas]() {
        widCanvas->FitSelection();
    });

    QAction *actCenterOrigin = tlbCanvas->addAction(QObject::tr("Center origin"));
    QObject::connect(actCenterOrigin,
                     &QAction::triggered,
                     [widCanvas]() {
        widCanvas->CenterOnWorld(mini_graph::CanvasVector(0.0, 0.0));
    });

    QAction *actAddGeometry = tlbCanvas->addAction(QObject::tr("Add geometry"));
    QObject::connect(actAddGeometry,
                     &QAction::triggered,
                     [&canvasDocument, &wndMain]() {
        GeometryCreateDialog dialog(&canvasDocument, &wndMain);
        dialog.exec();
    });

    QAction *actPlacePoint = tlbCanvas->addAction(QObject::tr("Place point"));
    QObject::connect(actPlacePoint,
                     &QAction::triggered,
                     [widCanvas]() {
        widCanvas->SetCurrentAction(new BasicPointPlacementAction);
    });

    QAction *actEditGeometry = tlbCanvas->addAction(QObject::tr("Edit selected"));
    QObject::connect(actEditGeometry,
                     &QAction::triggered,
                     [&canvasDocument, &wndMain]() {
        const QVector<mini_graph::ItemId> selectedIds =
                canvasDocument.GetSelectedItemIds();
        if (selectedIds.size() != 1) {
            return;
        }

        GeometryEditDialog dialog(&canvasDocument, selectedIds.first(), &wndMain);
        dialog.exec();
    });

    wndMain.resize(900, 600);
    wndMain.show();
    return app.exec();
}
