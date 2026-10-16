#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVector>

#include "MiniGraph/CanvasVector.h"

#include <cmath>

namespace basic_example
{

// Parse one x,y world-coordinate pair per non-empty line.
inline bool ParsePolylineText(
        const QString &text,
        QVector<mini_graph::CanvasVector> *vertices)
{
    if (!vertices)
    {
        return false;
    }

    QVector<mini_graph::CanvasVector> parsedVertices;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
        {
            continue;
        }

        const int separator = line.indexOf(QLatin1Char(','));
        if (separator <= 0 || line.indexOf(QLatin1Char(','), separator + 1) >= 0)
        {
            return false;
        }

        bool xValid = false;
        bool yValid = false;
        const QLocale cLocale = QLocale::c();
        const double x = cLocale.toDouble(line.left(separator).trimmed(), &xValid);
        const double y = cLocale.toDouble(line.mid(separator + 1).trimmed(), &yValid);
        if (!xValid || !yValid || !std::isfinite(x) || !std::isfinite(y))
        {
            return false;
        }

        parsedVertices.append(mini_graph::CanvasVector(x, y));
    }

    if (parsedVertices.size() < 2)
    {
        return false;
    }

    *vertices = parsedVertices;
    return true;
}

// Format vertices as locale-independent x,y lines with round-trip precision.
inline QString FormatPolylineText(
        const QVector<mini_graph::CanvasVector> &vertices)
{
    QStringList lines;
    lines.reserve(vertices.size());
    for (const mini_graph::CanvasVector &vertex : vertices)
    {
        lines.append(QString::number(vertex.GetX(), 'g', 17)
                     + QLatin1Char(',')
                     + QString::number(vertex.GetY(), 'g', 17));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace basic_example
