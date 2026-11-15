#include "PreviewOverlay.h"
#include "OrbitCamera.h"

#include <A3DGFXExMan.h>
#include <imgui.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>
#include <algorithm>
#include <stdexcept>

namespace
{
std::string GbkToUtf8(const std::string& input)
{
    const int count = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0);
    if (count <= 0)
        return "[invalid GBK path]";
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, input.data(),
                        static_cast<int>(input.size()), wide.data(), count);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, utf8.data(), bytes, nullptr, nullptr);
    return utf8;
}

const char* StateName(GFX_STATE state)
{
    switch (state)
    {
    case ST_PLAY: return "Playing";
    case ST_PAUSE: return "Paused";
    case ST_STOP: return "Stopped";
    default: return "Unknown";
    }
}
}

PreviewOverlay::PreviewOverlay(HWND window, IDirect3DDevice9* device)
{
    // Патерн upstream: imgui v1.91.9b/examples/example_win32_directx9/main.cpp:44-55.
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    if (!ImGui_ImplWin32_Init(window))
    {
        ImGui::DestroyContext(m_context);
        throw std::runtime_error("ImGui Win32 initialization failed.");
    }
    if (!ImGui_ImplDX9_Init(device))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_context);
        throw std::runtime_error("ImGui Direct3D9 initialization failed.");
    }
}

PreviewOverlay::~PreviewOverlay()
{
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(m_context);
}

void PreviewOverlay::SetEffect(const std::string& path)
{
    m_path = GbkToUtf8(path);
    auto& io = ImGui::GetIO();
    ImGui_ImplDX9_InvalidateDeviceObjects();
    io.Fonts->Clear();

    // Будуємо лише потрібні гліфи китайського імені, щоб не створювати великий атлас.
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddText(m_path.c_str());
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);
    m_glyphRanges.assign(ranges.begin(), ranges.end());
    char windowsDirectory[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryA(windowsDirectory, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
    {
        for (const char* font : {"\\Fonts\\msyh.ttc", "\\Fonts\\simsun.ttc", "\\Fonts\\segoeui.ttf"})
        {
            const std::string fontPath = std::string(windowsDirectory) + font;
            if (GetFileAttributesA(fontPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
                io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f, nullptr, m_glyphRanges.data()))
                break;
        }
    }
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
    // Діапазони зберігаються до наступної зміни шрифту, зокрема для перебудови атласу.
    if (!io.Fonts->Build() || !ImGui_ImplDX9_CreateDeviceObjects())
        throw std::runtime_error("ImGui font atlas creation failed.");
}

void PreviewOverlay::InvalidateDeviceObjects()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
}

bool PreviewOverlay::CreateDeviceObjects()
{
    return ImGui_ImplDX9_CreateDeviceObjects();
}

void PreviewOverlay::BeginFrame()
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

bool PreviewOverlay::Draw(const A3DGFXEx& gfx, const OrbitCamera& camera, float tickMs, float renderMs)
{
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(252, (std::min)(450.0f, io.DisplaySize.y - 24.0f)), ImGuiCond_FirstUseEver);
    bool resetCamera = false;
    if (ImGui::Begin("GFX statistics", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextWrapped("%s", m_path.c_str());
        ImGui::Separator();
        ImGui::Text("%s  |  %s", StateName(gfx.GetState()), gfx.Is2DRender() ? "2D" : "3D");
        ImGui::Text("Resources: %s", gfx.IsResourceReady() ? "ready" : "pending");

        // Джерела: A3DGFXEx.h:299, A3DGFXElement.h:668;
        // GfxCommon/A3DGFXExMan.cpp:1235-1273. Лічильники скидаються щокадру.
        int active = 0;
        for (int i = 0; i < gfx.GetElementCount(); ++i)
            if (gfx.GetElement(i)->IsActive())
                ++active;
        ImGui::Text("Elements: %d (%d active)", gfx.GetElementCount(), active);
        ImGui::Text("Particles allocated: %d", A3DGFXExMan::GetParticleTotalCount());
        ImGui::Text("Particles rendered: %d", A3DGFXExMan::GetParticleRenderCount());
        ImGui::Text("Draw calls: %d", A3DGFXExMan::GetTotalDrawCount());
        ImGui::Text("Primitives: %d", A3DGFXExMan::GetTotalPrimCount());
        ImGui::Text("Time: %.2f s  |  Speed: %.2fx", gfx.GetTimeElapse() / 1000.0f, gfx.GetActualSpeed());
        ImGui::Separator();
        ImGui::Text("%.1f FPS  |  %.2f ms/frame", io.Framerate,
                     io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        ImGui::Text("CPU tick: %.3f ms", tickMs);
        ImGui::Text("CPU render: %.3f ms", renderMs);

        if (ImGui::CollapsingHeader("Effect details"))
        {
            const auto& box = gfx.GetAABBOrg();
            ImGui::Text("AABB: %.2f x %.2f x %.2f", box.Maxs.x - box.Mins.x,
                         box.Maxs.y - box.Mins.y, box.Maxs.z - box.Mins.z);
            ImGui::Text("Scale: %.2f  |  Alpha: %.2f", gfx.GetActualScale(), gfx.GetActualAlpha());
            ImGui::Text("File version: %lu", gfx.GetVersion());
            ImGui::Text("Visible: %s  |  Infinite: %s", gfx.IsVisible() ? "yes" : "no",
                         gfx.IsInfinite() ? "yes" : "no");
        }
        ImGui::Separator();
        ImGui::Text("Distance: %.2f", camera.GetDistance());
        ImGui::Text("Yaw: %.1f  Pitch: %.1f", camera.GetYaw() * 57.29578f, camera.GetPitch() * 57.29578f);
        resetCamera = ImGui::Button("Frame effect");
        ImGui::TextWrapped("Middle mouse drag: orbit\nW: closer  S: farther\nEsc: close");
    }
    ImGui::End();
    return resetCamera;
}

void PreviewOverlay::Render()
{
    ImGui::Render();
    // DX9-бекенд зберігає та відновлює D3D state block, не ламаючи кеш станів Angelica.
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}
