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
    canvasDocument.AddLine(QPointF(-100.0, -50.0), QPointF(100.0, -50.0), QColor(80, 190, 255));
    canvasDocument.AddLine(QPointF(100.0, -50.0), QPointF(100.0, 50.0), QColor(80, 190, 255));
    canvasDocument.AddLine(QPointF(100.0, 50.0), QPointF(-100.0, 50.0), QColor(80, 190, 255));
    canvasDocument.AddLine(QPointF(-100.0, 50.0), QPointF(-100.0, -50.0), QColor(80, 190, 255));
    canvasDocument.AddLine(QPointF(-100.0, -50.0), QPointF(100.0, 50.0), QColor(255, 180, 80));

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

    QAction *actReset = tlbCanvas->addAction(QObject::tr("Reset view"));
    QObject::connect(actReset,
                     &QAction::triggered,
                     widCanvas,
                     &mini_graph::CanvasView::ResetView);

    wndMain.resize(900, 600);
    wndMain.show();
    return app.exec();
}
