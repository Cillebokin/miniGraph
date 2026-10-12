#pragma once

#include "MiniGraph/CanvasData.h"
#include "MiniGraph/CanvasGlobalData.h"

// Entity-attached extension data used by the basic example.
class BasicCanvasData : public mini_graph::CanvasData
{
public:
    std::shared_ptr<mini_graph::CanvasData> Clone() override;
    QStringList CalculateToQStringList() override;
    void Update(mini_graph::CanvasEntity *) override;

    void AdvanceRevision();

private:
    int m_Revision = 0;
};

// Document-wide extension data used by the basic example.
class BasicCanvasGlobalData : public mini_graph::CanvasGlobalData
{
public:
    std::shared_ptr<mini_graph::CanvasGlobalData> Clone() override;
    QStringList CalToQStringListPre() override;
    QStringList CalToQStringListPost() override;

    void AdvanceRevision();

private:
    int m_Revision = 0;
};
