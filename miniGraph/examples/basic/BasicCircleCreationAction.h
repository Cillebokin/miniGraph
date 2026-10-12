#pragma once

#include "MiniGraph/CanvasAction.h"
#include "MiniGraph/CanvasVector.h"

// Basic example Action that creates a circle from its center and radius point.
class BasicCircleCreationAction : public mini_graph::CanvasAction
{
public:
    // Creates a two-click circle creation action.
    explicit BasicCircleCreationAction(QObject *parent = nullptr);

    // Captures the center or completes the circle using the radius point.
    void MousePressEvent(QMouseEvent *event) override;

    // Treats Qt's double-click event as a click in the creation flow.
    void MouseDoubleClickEvent(QMouseEvent *event) override;

    // Updates the temporary radius point displayed by the preview.
    void MouseMoveEvent(QMouseEvent *event) override;

    // Draws the temporary circle preview.
    void RenderOverlay(QPainter &painter,
                       const mini_graph::CanvasRenderContext &context) override;

    // Clears temporary circle creation state.
    void Cancel() override;

private:
    // Center captured by the first click.
    mini_graph::CanvasVector m_Center;

    // Current radius point displayed by the temporary preview.
    mini_graph::CanvasVector m_PreviewRadiusPoint;

    // Whether the center has been captured.
    bool m_HasCenter;

    // Whether a mouse move has provided a preview radius point.
    bool m_HasPreviewRadiusPoint;
};
