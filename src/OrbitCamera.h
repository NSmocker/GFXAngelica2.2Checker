#pragma once

#include <A3DVector.h>

// Орбітальний стан переглядача; структури самого GFX залишаються в рушії.
class OrbitCamera
{
public:
    void Frame(const A3DVECTOR3& pivot, const A3DVECTOR3& position,
               float minimumDistance, float maximumDistance);
    void Rotate(float deltaX, float deltaY);
    void Zoom(float direction, float deltaSeconds);
    A3DVECTOR3 GetPosition() const;
    const A3DVECTOR3& GetPivot() const { return m_pivot; }
    float GetDistance() const { return m_distance; }
    float GetYaw() const { return m_yaw; }
    float GetPitch() const { return m_pitch; }

private:
    A3DVECTOR3 m_pivot{0.0f, 0.0f, 0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_distance = 5.0f;
    float m_minimumDistance = 0.1f;
    float m_maximumDistance = 1600.0f;
};
