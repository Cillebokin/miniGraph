#include "MiniGraph/CanvasVector.h"

#include <cmath>

namespace mini_graph
{

namespace
{

// 返回非有限标量查询的统一结果。
qreal InvalidScalar()
{
    return qQNaN();
}

// 将弧度角规范化到 [0, 2π)。
qreal NormalizeAngle(qreal radians)
{
    const qreal fullTurn = 6.28318530717958647692;
    qreal normalized = std::fmod(radians, fullTurn);
    if (normalized < 0.0)
    {
        normalized += fullTurn;
    }
    if (normalized >= fullTurn)
    {
        normalized -= fullTurn;
    }
    return normalized;
}

} // namespace

CanvasVector::CanvasVector()
    : m_X(0.0)
    , m_Y(0.0)
    , m_Valid(false)
{
}

CanvasVector::CanvasVector(qreal x, qreal y)
    : m_X(0.0)
    , m_Y(0.0)
    , m_Valid(false)
{
    Set(x, y);
}

qreal CanvasVector::GetX() const
{
    return m_X;
}

qreal CanvasVector::GetY() const
{
    return m_Y;
}

bool CanvasVector::IsValid() const
{
    return m_Valid;
}

bool CanvasVector::Set(qreal x, qreal y)
{
    if (!std::isfinite(x) || !std::isfinite(y))
    {
        return false;
    }

    m_X = x;
    m_Y = y;
    m_Valid = true;
    return true;
}

void CanvasVector::Invalidate()
{
    m_X = 0.0;
    m_Y = 0.0;
    m_Valid = false;
}

CanvasVector CanvasVector::operator+(const CanvasVector &other) const
{
    if (!m_Valid || !other.m_Valid)
    {
        return CanvasVector();
    }
    return CanvasVector(m_X + other.m_X, m_Y + other.m_Y);
}

CanvasVector CanvasVector::operator-(const CanvasVector &other) const
{
    if (!m_Valid || !other.m_Valid)
    {
        return CanvasVector();
    }
    return CanvasVector(m_X - other.m_X, m_Y - other.m_Y);
}

CanvasVector CanvasVector::operator-() const
{
    if (!m_Valid)
    {
        return CanvasVector();
    }
    return CanvasVector(-m_X, -m_Y);
}

CanvasVector CanvasVector::operator*(qreal scale) const
{
    if (!m_Valid || !std::isfinite(scale))
    {
        return CanvasVector();
    }
    return CanvasVector(m_X * scale, m_Y * scale);
}

CanvasVector CanvasVector::operator/(qreal divisor) const
{
    if (!m_Valid || !std::isfinite(divisor) || divisor == 0.0)
    {
        return CanvasVector();
    }
    return CanvasVector(m_X / divisor, m_Y / divisor);
}

CanvasVector &CanvasVector::operator+=(const CanvasVector &other)
{
    *this = *this + other;
    return *this;
}

CanvasVector &CanvasVector::operator-=(const CanvasVector &other)
{
    *this = *this - other;
    return *this;
}

CanvasVector &CanvasVector::operator*=(qreal scale)
{
    *this = *this * scale;
    return *this;
}

CanvasVector &CanvasVector::operator/=(qreal divisor)
{
    *this = *this / divisor;
    return *this;
}

bool CanvasVector::operator==(const CanvasVector &other) const
{
    if (m_Valid != other.m_Valid)
    {
        return false;
    }
    if (!m_Valid)
    {
        return true;
    }
    return m_X == other.m_X && m_Y == other.m_Y;
}

bool CanvasVector::operator!=(const CanvasVector &other) const
{
    return !(*this == other);
}

qreal CanvasVector::Length() const
{
    if (!m_Valid)
    {
        return InvalidScalar();
    }

    const qreal length = std::hypot(m_X, m_Y);
    return std::isfinite(length) ? length : InvalidScalar();
}

qreal CanvasVector::SquaredLength() const
{
    if (!m_Valid)
    {
        return InvalidScalar();
    }

    const qreal squaredLength = m_X * m_X + m_Y * m_Y;
    return std::isfinite(squaredLength) ? squaredLength : InvalidScalar();
}

qreal CanvasVector::DistanceTo(const CanvasVector &other) const
{
    if (!m_Valid || !other.m_Valid)
    {
        return InvalidScalar();
    }

    const qreal deltaX = m_X - other.m_X;
    const qreal deltaY = m_Y - other.m_Y;
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY))
    {
        return InvalidScalar();
    }

    const qreal distance = std::hypot(deltaX, deltaY);
    return std::isfinite(distance) ? distance : InvalidScalar();
}

qreal CanvasVector::SquaredDistanceTo(const CanvasVector &other) const
{
    if (!m_Valid || !other.m_Valid)
    {
        return InvalidScalar();
    }

    const qreal deltaX = m_X - other.m_X;
    const qreal deltaY = m_Y - other.m_Y;
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY))
    {
        return InvalidScalar();
    }

    const qreal squaredDistance = deltaX * deltaX + deltaY * deltaY;
    return std::isfinite(squaredDistance) ? squaredDistance : InvalidScalar();
}

qreal CanvasVector::Dot(const CanvasVector &other) const
{
    if (!m_Valid || !other.m_Valid)
    {
        return InvalidScalar();
    }

    const qreal dot = m_X * other.m_X + m_Y * other.m_Y;
    return std::isfinite(dot) ? dot : InvalidScalar();
}

qreal CanvasVector::Cross(const CanvasVector &other) const
{
    if (!m_Valid || !other.m_Valid)
    {
        return InvalidScalar();
    }

    const qreal cross = m_X * other.m_Y - m_Y * other.m_X;
    return std::isfinite(cross) ? cross : InvalidScalar();
}

qreal CanvasVector::Angle() const
{
    if (!m_Valid || (m_X == 0.0 && m_Y == 0.0))
    {
        return InvalidScalar();
    }
    return NormalizeAngle(std::atan2(m_Y, m_X));
}

qreal CanvasVector::AngleTo(const CanvasVector &other) const
{
    if (!m_Valid || !other.m_Valid)
    {
        return InvalidScalar();
    }
    return (other - *this).Angle();
}

CanvasVector CanvasVector::Rotated(qreal radians) const
{
    if (!m_Valid || !std::isfinite(radians))
    {
        return CanvasVector();
    }

    const qreal cosine = std::cos(radians);
    const qreal sine = std::sin(radians);
    return CanvasVector(m_X * cosine - m_Y * sine,
                        m_X * sine + m_Y * cosine);
}

CanvasVector CanvasVector::Normalized() const
{
    const qreal length = Length();
    if (!std::isfinite(length) || length == 0.0)
    {
        return CanvasVector();
    }
    return *this / length;
}

bool CanvasVector::IsNearTo(const CanvasVector &other, qreal tolerance) const
{
    if (!m_Valid || !other.m_Valid || !std::isfinite(tolerance)
            || tolerance < 0.0)
    {
        return false;
    }

    const qreal distance = DistanceTo(other);
    return std::isfinite(distance) && distance <= tolerance;
}

CanvasVector CanvasVector::FromPolar(qreal radius, qreal radians)
{
    if (!std::isfinite(radius) || !std::isfinite(radians))
    {
        return CanvasVector();
    }
    return CanvasVector(radius * std::cos(radians),
                        radius * std::sin(radians));
}

CanvasVector CanvasVector::Midpoint(const CanvasVector &first,
                                   const CanvasVector &second)
{
    if (!first.m_Valid || !second.m_Valid)
    {
        return CanvasVector();
    }
    return CanvasVector(first.m_X * 0.5 + second.m_X * 0.5,
                        first.m_Y * 0.5 + second.m_Y * 0.5);
}

} // namespace mini_graph
