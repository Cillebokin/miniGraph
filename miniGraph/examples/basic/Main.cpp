#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QToolBar>

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasView.h"

int main(int argumentCount, char *argumentValues[])
{
    QApplication app(argumentCount, argumentValues);

    mini_graph::CanvasDocument canvasDocument;

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

    QAction *actReset = tlbCanvas->addAction(QObject::tr("Reset view"));
    QObject::connect(actReset,
                     &QAction::triggered,
                     widCanvas,
                     &mini_graph::CanvasView::ResetView);

    wndMain.resize(900, 600);
    wndMain.show();
    return app.exec();
}
