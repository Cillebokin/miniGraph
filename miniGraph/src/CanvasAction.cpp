#include "MiniGraph/CanvasAction.h"
#include "MiniGraph/CanvasView.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace mini_graph {

CanvasAction::CanvasAction(QObject *parent)
    : QObject(parent)
{
}

CanvasAction::~CanvasAction() = default;

CanvasView *CanvasAction::GetCanvasView() const
{
    return m_CanvasView.data();
}

void CanvasAction::RequestUpdate()
{
    emit UpdateRequested();
}

void CanvasAction::Finish()
{
    emit FinishRequested();
}

void CanvasAction::MousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
}

void CanvasAction::MouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
}

void CanvasAction::MouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
}

void CanvasAction::MouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
}

void CanvasAction::KeyPressEvent(QKeyEvent *event)
{
    Q_UNUSED(event)
}

void CanvasAction::KeyReleaseEvent(QKeyEvent *event)
{
    Q_UNUSED(event)
}

void CanvasAction::RenderOverlay(QPainter &painter,
                                 const CanvasRenderContext &context)
{
    Q_UNUSED(painter)
    Q_UNUSED(context)
}

void CanvasAction::Cancel()
{
}

void CanvasAction::SetCanvasView(CanvasView *view)
{
    m_CanvasView = view;
}

} // namespace mini_graph
