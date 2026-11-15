#include "OrbitCamera.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float pitchLimit = pi * 0.495f;
constexpr float radiansPerPixel = 0.005f;
}

void OrbitCamera::Frame(const A3DVECTOR3& pivot, const A3DVECTOR3& position,
                        float minimumDistance, float maximumDistance)
{
    m_pivot = pivot;
    m_minimumDistance = (std::max)(0.01f, minimumDistance);
    m_maximumDistance = (std::max)(m_minimumDistance, maximumDistance);
    const float x = position.x - pivot.x;
    const float y = position.y - pivot.y;
    const float z = position.z - pivot.z;
    const float distance = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(distance) || distance < 0.0001f)
    {
        m_yaw = 0.0f;
        m_pitch = 0.3f;
        m_distance = std::clamp(5.0f, m_minimumDistance, m_maximumDistance);
        return;
    }
    m_yaw = std::atan2(x, z);
    m_pitch = std::clamp(std::asin(std::clamp(y / distance, -1.0f, 1.0f)),
                         -pitchLimit, pitchLimit);
    m_distance = std::clamp(distance, m_minimumDistance, m_maximumDistance);
}

void OrbitCamera::Rotate(float deltaX, float deltaY)
{
    // Обмеження біля полюсів запобігає перевертанню вертикалі камери.
    m_yaw = std::remainder(m_yaw - deltaX * radiansPerPixel, 2.0f * pi);
    m_pitch = std::clamp(m_pitch + deltaY * radiansPerPixel, -pitchLimit, pitchLimit);
}

void OrbitCamera::Zoom(float direction, float deltaSeconds)
{
    // Експоненційний крок дає однакову чутливість на різних масштабах ефекту.
    const float step = direction * std::clamp(deltaSeconds, 0.0f, 0.1f) * 1.5f;
    m_distance = std::clamp(m_distance * std::exp(-step), m_minimumDistance, m_maximumDistance);
}

A3DVECTOR3 OrbitCamera::GetPosition() const
{
    const float horizontal = m_distance * std::cos(m_pitch);
    return A3DVECTOR3(m_pivot.x + horizontal * std::sin(m_yaw),
                     m_pivot.y + m_distance * std::sin(m_pitch),
                     m_pivot.z + horizontal * std::cos(m_yaw));
}
