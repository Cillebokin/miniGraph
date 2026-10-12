#pragma once

#include <MiniGraph/CanvasRenderer.h>

#include <QStringList>

class RendererModuleA : public mini_graph::CanvasRenderer
{
public:
    explicit RendererModuleA(QStringList *events,
                             QObject *parent = nullptr);

    void Render(QPainter &painter,
                const mini_graph::CanvasRenderContext &context) override;

private:
    QStringList *m_Events;
};
