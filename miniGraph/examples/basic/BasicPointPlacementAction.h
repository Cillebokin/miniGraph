#pragma once

#include "MiniGraph/CanvasAction.h"

// Basic example Action that places one point on the canvas.
class BasicPointPlacementAction : public mini_graph::CanvasAction
{
public:
    // Creates a one-shot point placement action.
    explicit BasicPointPlacementAction(QObject *parent = nullptr);

    // Places a point at the snapped or mapped world position and finishes.
    void MousePressEvent(QMouseEvent *event) override;
};
