#include "OrbitCamera.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

float Distance(const A3DVECTOR3& a, const A3DVECTOR3& b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}
}

int main()
{
    OrbitCamera orbit;
    const A3DVECTOR3 pivot(3.0f, 4.0f, -2.0f);
    orbit.Frame(pivot, A3DVECTOR3(3.0f, 4.0f, 8.0f), 0.5f, 100.0f);
    orbit.Rotate(160.0f, -75.0f);
    Require(std::abs(Distance(orbit.GetPosition(), pivot) - 10.0f) < 0.0001f,
            "orbit must preserve radius and pivot");
    const float yaw = orbit.GetYaw();
    const float pitch = orbit.GetPitch();
    orbit.Zoom(1.0f, 0.1f);
    Require(orbit.GetDistance() < 10.0f, "W must move closer");
    orbit.Zoom(-1.0f, 0.1f);
    Require(std::abs(orbit.GetDistance() - 10.0f) < 0.0001f, "S must reverse equal W input");
    Require(orbit.GetYaw() == yaw && orbit.GetPitch() == pitch, "zoom must preserve viewing angles");

    for (int i = 0; i < 1000; ++i)
    {
        orbit.Rotate(100000.0f, 100000.0f);
        orbit.Zoom(1.0f, 0.1f);
    }
    Require(orbit.GetDistance() >= 0.5f, "zoom must not cross the pivot");
    Require(std::abs(orbit.GetPitch()) < 1.5708f, "pitch must not cross a pole");
    Require(std::abs(orbit.GetYaw()) <= 3.141593f, "yaw must remain bounded");
    const auto position = orbit.GetPosition();
    Require(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z),
            "extreme mouse input must leave a finite camera position");
    for (int i = 0; i < 1000; ++i)
        orbit.Zoom(-1.0f, 0.1f);
    Require(orbit.GetDistance() <= 100.0f, "zoom must respect the far limit");

    orbit.Frame(pivot, pivot, 0.5f, 100.0f);
    Require(orbit.GetDistance() > 0.0f && std::isfinite(orbit.GetPosition().x),
            "zero-sized framing must produce a valid camera");

    OrbitCamera coarse;
    OrbitCamera fine;
    coarse.Frame(pivot, A3DVECTOR3(3, 4, 8), 0.5f, 100);
    fine.Frame(pivot, A3DVECTOR3(3, 4, 8), 0.5f, 100);
    for (int i = 0; i < 10; ++i) coarse.Zoom(1, 0.1f);
    for (int i = 0; i < 100; ++i) fine.Zoom(1, 0.01f);
    Require(std::abs(coarse.GetDistance() - fine.GetDistance()) < 0.001f,
            "zoom speed must not depend on frame rate");
    std::puts("Orbit camera tests passed.");
    return 0;
}
