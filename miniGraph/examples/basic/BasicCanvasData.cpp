#include "BasicCanvasData.h"

std::shared_ptr<mini_graph::CanvasData> BasicCanvasData::Clone()
{
    std::shared_ptr<BasicCanvasData> clone =
            std::make_shared<BasicCanvasData>();
    clone->m_Revision = m_Revision;
    return clone;
}

QStringList BasicCanvasData::CalculateToQStringList()
{
    return QStringList()
            << QStringLiteral("Pen,50,190,240,255,1.5,1")
            << QStringLiteral("Text,Item-data-%1,12,12,0,0").arg(m_Revision);
}

void BasicCanvasData::Update(mini_graph::CanvasEntity *)
{
}

void BasicCanvasData::AdvanceRevision()
{
    ++m_Revision;
}

std::shared_ptr<mini_graph::CanvasGlobalData> BasicCanvasGlobalData::Clone()
{
    std::shared_ptr<BasicCanvasGlobalData> clone =
            std::make_shared<BasicCanvasGlobalData>();
    clone->m_Revision = m_Revision;
    return clone;
}

QStringList BasicCanvasGlobalData::CalToQStringListPre()
{
    return QStringList()
            << QStringLiteral("Pen,150,190,215,255,1,1")
            << QStringLiteral("Text,Global-pre-%1,-140,150,0,0").arg(m_Revision);
}

QStringList BasicCanvasGlobalData::CalToQStringListPost()
{
    return QStringList()
            << QStringLiteral("Pen,150,190,215,255,1,1")
            << QStringLiteral("Text,Global-post-%1,90,150,0,0").arg(m_Revision);
}

void BasicCanvasGlobalData::AdvanceRevision()
{
    ++m_Revision;
}
