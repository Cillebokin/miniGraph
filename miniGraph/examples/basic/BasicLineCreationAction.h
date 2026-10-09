#pragma once

#include "MiniGraph/CanvasAction.h"
#include "MiniGraph/CanvasVector.h"

// Basic example Action that creates a line from two clicked points.
class BasicLineCreationAction : public mini_graph::CanvasAction
{
public:
    // Creates a two-point line creation action.
    explicit BasicLineCreationAction(QObject *parent = nullptr);

    // Receives the two clicks used to define the line endpoints.
    void MousePressEvent(QMouseEvent *event) override;

    // Treats Qt's double-click event as the second endpoint click.
    void MouseDoubleClickEvent(QMouseEvent *event) override;

    // Updates the temporary line preview endpoint.
    void MouseMoveEvent(QMouseEvent *event) override;

    // Draws the temporary line preview.
    void RenderOverlay(QPainter &painter,
                       const mini_graph::CanvasRenderContext &context) override;

    // Clears temporary line creation state.
    void Cancel() override;

private:
    // First point captured by the initial click.
    mini_graph::CanvasVector m_StartPoint;

    // Current endpoint displayed by the temporary preview.
    mini_graph::CanvasVector m_PreviewEndPoint;

    // Whether the first point has been captured.
    bool m_HasStartPoint;

    // Whether a mouse move has provided a preview endpoint.
    bool m_HasPreviewEndPoint;
};
