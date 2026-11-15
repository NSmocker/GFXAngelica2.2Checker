#pragma once

#include <Windows.h>
#include <string>
#include <vector>

class A3DGFXEx;
class OrbitCamera;
struct IDirect3DDevice9;
struct ImGuiContext;

class PreviewOverlay
{
public:
    PreviewOverlay(HWND window, IDirect3DDevice9* device);
    ~PreviewOverlay();
    void SetEffect(const std::string& path);
    void InvalidateDeviceObjects();
    bool CreateDeviceObjects();
    void BeginFrame();
    bool Draw(const A3DGFXEx& gfx, const OrbitCamera& camera, float tickMs, float renderMs);
    void Render();
    PreviewOverlay(const PreviewOverlay&) = delete;
    PreviewOverlay& operator=(const PreviewOverlay&) = delete;

private:
    ImGuiContext* m_context = nullptr;
    std::string m_path;
    std::vector<unsigned short> m_glyphRanges;
};
