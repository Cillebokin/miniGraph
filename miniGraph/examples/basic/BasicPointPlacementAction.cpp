#include "BasicPointPlacementAction.h"

#include "MiniGraph/CanvasDocument.h"
#include "MiniGraph/CanvasSnap.h"
#include "MiniGraph/CanvasView.h"

#include <QMouseEvent>

BasicPointPlacementAction::BasicPointPlacementAction(QObject *parent)
    : mini_graph::CanvasAction(parent)
{
}

void BasicPointPlacementAction::MousePressEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton) {
        return;
    }

    mini_graph::CanvasView *view = GetCanvasView();
    mini_graph::CanvasDocument *document = view ? view->GetDocument() : nullptr;
    if (!view || !document) {
        return;
    }

    const mini_graph::CanvasSnapResult snap = view->GetCurrentSnapResult();
    const mini_graph::CanvasVector position = snap.IsValid()
            ? snap.position
            : view->MapToWorld(event->localPos());
    if (document->AddPoint(position) != 0) {
        Finish();
    }
}
