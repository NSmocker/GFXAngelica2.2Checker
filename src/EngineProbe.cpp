#include "EngineProbe.h"
#include "OrbitCamera.h"
#include "PreviewOverlay.h"

#include <AAssist.h>
#include <A3DCamera.h>
#include <A3DConfig.h>
#include <A3DEngine.h>
#include <A3DGFXExMan.h>
#include <A3DLightMan.h>
#include <A3DRenderTarget.h>
#include <A3DSkinMan.h>
#include <A3DViewport.h>
#include <AFI.h>
#include <ALog.h>
#include <Windows.h>
#include <d2d1.h>
#include <shellapi.h>
#include <Wincodec.h>
#include <objbase.h>
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

extern void AfxSetA3DDevice(A3DDevice* pDevice);
extern void AfxSetA3DCamera(A3DCamera* pCamera);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// Джерело: PW_ClientSource/SDK/AngelicaCommon/header/ABaseDef.h:47.
// Готові бібліотеки мають wchar_t у структурах ACHAR, попри MBCS у клієнті.
static_assert(sizeof(ACHAR) == sizeof(wchar_t), "Angelica library ABI requires UNICODE.");

namespace
{
A3DGFXExMan* gfxManager = nullptr;

struct GfxManagerDeleter
{
    void operator()(A3DGFXExMan* manager) const
    {
        if (!manager)
            return;

        manager->Release();
        if (gfxManager == manager)
            gfxManager = nullptr;
        delete manager;
    }
};

struct GfxDeleter
{
    void operator()(A3DGFXEx* gfx) const
    {
        if (!gfx)
            return;

        gfx->Stop();
        gfx->Release();
        delete gfx;
    }
};

struct RenderTargetDeleter
{
    void operator()(A3DRenderTarget* renderTarget) const
    {
        if (renderTarget)
        {
            renderTarget->Release();
            delete renderTarget;
        }
    }
};

class RenderTargetBinding
{
public:
    RenderTargetBinding(A3DDevice* device, A3DRenderTarget* renderTarget)
        : m_device(device), m_renderTarget(renderTarget)
    {
        if (!m_device->SetRenderTarget(renderTarget))
            throw std::runtime_error("A3DDevice::SetRenderTarget failed.");
        m_bound = true;
    }

    ~RenderTargetBinding()
    {
        if (m_bound && !m_device->RestoreRenderTarget())
            a_LogOutput(1, "Failed to restore the previous render target (error %lu).", GetLastError());
    }

    bool Restore()
    {
        if (!m_bound)
            return true;
        if (!m_device->RestoreRenderTarget())
            return false;
        m_bound = false;
        return true;
    }

    // Повторна прив'язка для багатокадрового захоплення: між BeginRender/EndRender.
    bool Rebind()
    {
        if (m_bound)
            return true;
        if (!m_device->SetRenderTarget(m_renderTarget))
            return false;
        m_bound = true;
        return true;
    }

    RenderTargetBinding(const RenderTargetBinding&) = delete;
    RenderTargetBinding& operator=(const RenderTargetBinding&) = delete;

private:
    A3DDevice* m_device;
    A3DRenderTarget* m_renderTarget;
    bool m_bound = false;
};

class ComApartment
{
public:
    ComApartment()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE)
            throw std::runtime_error("CoInitializeEx failed.");
        m_initialized = SUCCEEDED(result);
    }

    ~ComApartment()
    {
        if (m_initialized)
            CoUninitialize();
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    bool m_initialized = false;
};

void CheckHresult(HRESULT result, const char* operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + " failed (HRESULT " +
                                 std::to_string(static_cast<unsigned long>(result)) + ").");
}

void SaveRenderTargetAsPng(A3DDevice* device,
                           A3DRenderTarget* renderTarget,
                           const std::wstring& outputPath)
{
    IDirect3DSurface9* targetSurface = renderTarget->GetTargetSurface();
    if (!targetSurface)
        throw std::runtime_error("Render target has no color surface.");

    D3DSURFACE_DESC description{};
    CheckHresult(targetSurface->GetDesc(&description), "IDirect3DSurface9::GetDesc");
    if (description.Format != D3DFMT_A8R8G8B8)
        throw std::runtime_error("Render target surface format is not A8R8G8B8.");
    if (description.Width == 0 || description.Height == 0)
        throw std::runtime_error("Render target surface has zero dimensions.");
    if (description.Width > (std::numeric_limits<UINT>::max)() / 4u)
        throw std::runtime_error("Render target row size is too large for PNG encoding.");

    const UINT stride = description.Width * 4u;
    if (description.Height > (std::numeric_limits<UINT>::max)() / stride)
        throw std::runtime_error("Render target image size is too large for PNG encoding.");
    const UINT bufferSize = stride * description.Height;

    Microsoft::WRL::ComPtr<IDirect3DSurface9> systemSurface;
    CheckHresult(device->GetD3DDevice()->CreateOffscreenPlainSurface(
                     description.Width, description.Height, description.Format,
                     D3DPOOL_SYSTEMMEM, systemSurface.GetAddressOf(), nullptr),
                 "IDirect3DDevice9::CreateOffscreenPlainSurface");
    CheckHresult(device->GetD3DDevice()->GetRenderTargetData(targetSurface, systemSurface.Get()),
                 "IDirect3DDevice9::GetRenderTargetData");

    D3DLOCKED_RECT lockedRect{};
    CheckHresult(systemSurface->LockRect(&lockedRect, nullptr, D3DLOCK_READONLY),
                 "IDirect3DSurface9::LockRect");
    if (lockedRect.Pitch < 0 || static_cast<UINT>(lockedRect.Pitch) < stride)
    {
        systemSurface->UnlockRect();
        throw std::runtime_error("Locked render target pitch is smaller than the image row.");
    }

    std::vector<BYTE> pixels;
    try
    {
        pixels.resize(bufferSize);
        const auto* source = static_cast<const BYTE*>(lockedRect.pBits);
        for (UINT y = 0; y < description.Height; ++y)
        {
            BYTE* row = pixels.data() + static_cast<size_t>(y) * stride;
            CopyMemory(row, source + static_cast<size_t>(y) * lockedRect.Pitch, stride);
            for (UINT x = 0; x < description.Width; ++x)
                row[x * 4u + 3u] = 0xFF;
        }
    }
    catch (...)
    {
        systemSurface->UnlockRect();
        throw;
    }
    CheckHresult(systemSurface->UnlockRect(), "IDirect3DSurface9::UnlockRect");

    size_t nonblack = 0;
    unsigned maximum = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        const unsigned value = (std::max)({pixels[offset], pixels[offset + 1], pixels[offset + 2]});
        nonblack += value != 0;
        maximum = (std::max)(maximum, value);
    }
    a_LogOutput(nonblack ? 0 : 1, "PNG pixels: nonblack=%zu total=%lu maximum=%u status=%s",
                nonblack, description.Width * description.Height, maximum, nonblack ? "VISIBLE" : "EMPTY");

    ComApartment apartment;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    CheckHresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf())),
                 "CoCreateInstance(WICImagingFactory)");

    Microsoft::WRL::ComPtr<IWICStream> stream;
    CheckHresult(factory->CreateStream(stream.GetAddressOf()), "IWICImagingFactory::CreateStream");
    CheckHresult(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE),
                 "IWICStream::InitializeFromFilename");

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    CheckHresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()),
                 "IWICImagingFactory::CreateEncoder");
    CheckHresult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
                 "IWICBitmapEncoder::Initialize");

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    CheckHresult(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf()),
                 "IWICBitmapEncoder::CreateNewFrame");
    CheckHresult(frame->Initialize(properties.Get()), "IWICBitmapFrameEncode::Initialize");
    CheckHresult(frame->SetSize(description.Width, description.Height),
                 "IWICBitmapFrameEncode::SetSize");

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    CheckHresult(frame->SetPixelFormat(&pixelFormat), "IWICBitmapFrameEncode::SetPixelFormat");
    if (!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
        throw std::runtime_error("WIC PNG encoder does not support 32bpp BGRA pixels.");
    CheckHresult(frame->WritePixels(description.Height, stride, bufferSize, pixels.data()),
                 "IWICBitmapFrameEncode::WritePixels");
    CheckHresult(frame->Commit(), "IWICBitmapFrameEncode::Commit");
    CheckHresult(encoder->Commit(), "IWICBitmapEncoder::Commit");
}

class FileModule
{
public:
    explicit FileModule(const std::string& assetRoot)
    {
        // Джерело: PW_ClientSource/SDK/AngelicaFile/Header/AFI.h:21.
        if (!af_Initialize())
        {
            af_Finalize();
            throw std::runtime_error("af_Initialize failed.");
        }
        if (!af_SetBaseDir(assetRoot.c_str()))
        {
            af_Finalize();
            throw std::runtime_error("af_SetBaseDir failed.");
        }
    }
    ~FileModule() { af_Finalize(); }
    FileModule(const FileModule&) = delete;
    FileModule& operator=(const FileModule&) = delete;
};

class WorkingDirectory
{
public:
    explicit WorkingDirectory(const std::string& path)
    {
        const DWORD length = GetCurrentDirectoryA(MAX_PATH, m_previous);
        if (length == 0 || length >= MAX_PATH)
            throw std::runtime_error("Unable to read the current working directory.");

        if (!SetCurrentDirectoryA(path.c_str()))
            throw std::runtime_error("Unable to set the asset root as the current working directory.");
    }

    ~WorkingDirectory()
    {
        if (!SetCurrentDirectoryA(m_previous))
            a_LogOutput(1, "Failed to restore the previous working directory (error %lu).", GetLastError());
    }

    WorkingDirectory(const WorkingDirectory&) = delete;
    WorkingDirectory& operator=(const WorkingDirectory&) = delete;

private:
    char m_previous[MAX_PATH]{};
};

std::string GetAssetGfxRoot(const std::string& assetRoot)
{
    return assetRoot + "\\gfx";
}

std::string NormalizeAngelicaPath(const std::string& path)
{
    std::string normalized = path;
    if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"')
        normalized = normalized.substr(1, normalized.size() - 2);
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    while (!normalized.empty() && (normalized.front() == '.' && normalized.size() > 1 && normalized[1] == '\\'))
        normalized.erase(0, 2);
    return normalized;
}

std::string ResolveGfxRelativePath(const std::string& assetRoot, const std::string& gfxPath)
{
    if (gfxPath.empty())
        throw std::runtime_error("Preview requires a GFX path.");

    std::string normalizedPath = NormalizeAngelicaPath(gfxPath);
    // Спершу перевіряємо шлях від кореня gfx: у наборі є справжня gfx/gfx/.
    if (normalizedPath.size() >= 4 && _strnicmp(normalizedPath.c_str(), "Gfx\\", 4) == 0 &&
        !af_IsFileExist((GetAssetGfxRoot(assetRoot) + "\\" + normalizedPath).c_str()))
        normalizedPath.erase(0, 4);

    const bool isAbsolute = normalizedPath.size() > 1 &&
        ((normalizedPath[0] == '\\' || normalizedPath[0] == '/') || normalizedPath[1] == ':');

    if (!isAbsolute)
        return normalizedPath;

    char fullPath[MAX_PATH]{};
    const DWORD fullLength = GetFullPathNameA(normalizedPath.c_str(), MAX_PATH, fullPath, nullptr);
    if (fullLength > 0 && fullLength < MAX_PATH)
    {
        std::string absolutePath = NormalizeAngelicaPath(fullPath);
        const std::string gfxRoot = NormalizeAngelicaPath(GetAssetGfxRoot(assetRoot));
        if (_strnicmp(absolutePath.c_str(), gfxRoot.c_str(), gfxRoot.size()) == 0)
        {
            std::string relative = absolutePath.substr(gfxRoot.size());
            while (!relative.empty() && (relative.front() == '\\' || relative.front() == '/'))
                relative.erase(0, 1);
            return relative;
        }
    }

    const std::string gfxRoot = GetAssetGfxRoot(assetRoot);
    AString relative;
    af_GetRelativePathNoBase(normalizedPath.c_str(), gfxRoot.c_str(), relative);
    const char* relativePtr = static_cast<const char*>(relative);
    return std::string(relativePtr ? relativePtr : "");
}

HWND CreateHiddenPreviewWindow(unsigned width, unsigned height)
{
    const HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = instance;
    wc.lpszClassName = "GFXCheckerPreviewClass";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassA(&wc))
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;
    }

    HWND hwnd = CreateWindowExA(0, "GFXCheckerPreviewClass", "GFXCheckerPreview",
                                WS_POPUP, 0, 0, width, height, nullptr, nullptr, instance, nullptr);
    if (!hwnd)
        return nullptr;

    return hwnd;
}

struct InteractiveWindowState
{
    bool closeRequested = false;
    bool focused = false;
    bool resizing = false;
    bool resizePending = false;
    bool dropPending = false;
    bool dropHasMultipleFiles = false;
    bool dropPathTooLong = false;
    bool dropPathEmpty = false;
    unsigned resizeWidth = 0;
    unsigned resizeHeight = 0;
    wchar_t droppedPath[MAX_PATH]{};
};

LRESULT CALLBACK InteractiveWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
        return TRUE;

    auto* state = reinterpret_cast<InteractiveWindowState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* createInfo = reinterpret_cast<const CREATESTRUCTA*>(lParam);
        state = static_cast<InteractiveWindowState*>(createInfo->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message)
    {
    case WM_CLOSE:
        if (state)
            state->closeRequested = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SETFOCUS:
        if (state)
            state->focused = true;
        return 0;
    case WM_KILLFOCUS:
        if (state)
            state->focused = false;
        return 0;
    case WM_ENTERSIZEMOVE:
        if (state)
            state->resizing = true;
        return 0;
    case WM_EXITSIZEMOVE:
        if (state)
            state->resizing = false;
        return 0;
    case WM_SIZE:
        if (state && wParam != SIZE_MINIMIZED)
        {
            const unsigned width = LOWORD(lParam);
            const unsigned height = HIWORD(lParam);
            if (width > 0 && height > 0 &&
                (width != state->resizeWidth || height != state->resizeHeight))
            {
                state->resizeWidth = width;
                state->resizeHeight = height;
                state->resizePending = true;
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && state &&
            (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureKeyboard))
            state->closeRequested = true;
        return 0;
    case WM_DROPFILES:
    {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        if (state)
        {
            const UINT fileCount = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            state->dropPending = true;
            state->dropHasMultipleFiles = fileCount > 1;
            state->dropPathTooLong = false;
            state->dropPathEmpty = fileCount == 0;
            state->droppedPath[0] = L'\0';
            if (fileCount == 1)
            {
                const UINT pathLength = DragQueryFileW(drop, 0, nullptr, 0);
                state->dropPathTooLong = pathLength >= ARRAYSIZE(state->droppedPath);
                if (!state->dropPathTooLong)
                {
                    const UINT copied = DragQueryFileW(
                        drop, 0, state->droppedPath, ARRAYSIZE(state->droppedPath));
                    state->dropPathEmpty = copied == 0;
                }
            }
        }
        DragFinish(drop);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

HWND CreateInteractivePreviewWindow(unsigned width,
                                    unsigned height,
                                    InteractiveWindowState& state)
{
    if (width > static_cast<unsigned>(LONG_MAX) || height > static_cast<unsigned>(LONG_MAX))
        return nullptr;

    state.resizeWidth = width;
    state.resizeHeight = height;
    const HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.lpfnWndProc = InteractiveWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "GFXCheckerInteractiveClass";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT windowRect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (!AdjustWindowRect(&windowRect, style, FALSE))
        return nullptr;

    HWND hwnd = CreateWindowExA(WS_EX_APPWINDOW, wc.lpszClassName,
                                "GFX Preview - MMB orbit, W/S zoom, Esc exit",
                                style, CW_USEDEFAULT, CW_USEDEFAULT,
                                windowRect.right - windowRect.left,
                                windowRect.bottom - windowRect.top,
                                nullptr, nullptr, instance, &state);
    if (hwnd)
    {
        DragAcceptFiles(hwnd, TRUE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
    }
    return hwnd;
}

std::string ConvertDroppedGfxPath(const std::wstring& droppedPath)
{
    wchar_t fullPath[MAX_PATH]{};
    const DWORD fullPathLength = GetFullPathNameW(
        droppedPath.c_str(), ARRAYSIZE(fullPath), fullPath, nullptr);
    if (fullPathLength == 0 || fullPathLength >= ARRAYSIZE(fullPath))
        throw std::runtime_error("Dropped GFX path is unavailable or too long.");

    wchar_t currentDirectory[MAX_PATH]{};
    const DWORD currentDirectoryLength = GetCurrentDirectoryW(
        ARRAYSIZE(currentDirectory), currentDirectory);
    if (currentDirectoryLength == 0 || currentDirectoryLength >= ARRAYSIZE(currentDirectory))
        throw std::runtime_error("Unable to read the asset root while processing dropped GFX.");

    const std::wstring gfxDirectory =
        std::wstring(currentDirectory, currentDirectoryLength) + L"\\gfx\\";
    if (wcslen(fullPath) <= gfxDirectory.size() ||
        _wcsnicmp(fullPath, gfxDirectory.c_str(), gfxDirectory.size()) != 0)
        throw std::runtime_error("Drop a GFX file from the asset root's gfx directory.");

    const size_t pathLength = wcslen(fullPath);
    if (pathLength < 4 || _wcsicmp(fullPath + pathLength - 4, L".gfx") != 0)
        throw std::runtime_error("The dropped file must have a .gfx extension.");

    const DWORD attributes = GetFileAttributesW(fullPath);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("The dropped GFX file does not exist.");

    const int ansiLength = WideCharToMultiByte(936, 0, fullPath, -1, nullptr, 0, nullptr, nullptr);
    if (ansiLength <= 0)
        throw std::runtime_error("Dropped GFX path cannot be converted to GBK.");

    std::string ansiPath(static_cast<size_t>(ansiLength), '\0');
    if (WideCharToMultiByte(936, 0, fullPath, -1, ansiPath.data(), ansiLength, nullptr, nullptr) <= 0)
        throw std::runtime_error("Dropped GFX path cannot be converted to GBK.");
    ansiPath.resize(static_cast<size_t>(ansiLength - 1));
    return ansiPath;
}

void ConfigureCameraFromAabb(A3DCamera& camera, const A3DGFXEx* gfx);

void Position2DEffect(A3DGFXEx& gfx, A3DViewport& viewport)
{
    if (!gfx.Is2DRender()) return;
    // Екранні координати: GfxCommon/A3DGFXExMan.cpp:1186-1193.
    const auto& box = gfx.GetAABBOrg();
    const A3DVECTOR3 center = (box.Mins + box.Maxs) * (0.5f * gfx.GetActualScale());
    gfx.SetPos(A3DVECTOR3(viewport.GetParam()->Width * 0.5f - center.x,
                        viewport.GetParam()->Height * 0.5f - center.y, 0.0f));
}

void RenderPreviewGfx(A3DGFXExMan& manager, A3DGFXEx& gfx, A3DViewport& viewport)
{
    // RegisterGfx маршрутизує 2D окремо: A3DGFXExMan.cpp:169-188,554-623.
    manager.RegisterGfx(&gfx);
    manager.RenderAllGfx(&viewport);
    manager.Render2DGfx(true);
    manager.Render2DGfx(false);
}

bool ProcessDroppedGfx(InteractiveWindowState& windowState,
                       HWND hwnd,
                       const std::string& assetRoot,
                       A3DDevice* device,
                       A3DCamera& camera,
                       A3DGFXExMan& gfxManager,
                       std::unique_ptr<A3DGFXEx, GfxDeleter>& activeGfx,
                       std::string& activeGfxPath)
{
    if (!windowState.dropPending)
        return false;

    windowState.dropPending = false;
    if (windowState.dropHasMultipleFiles)
    {
        a_LogOutput(1, "Drop rejected: drop one GFX file at a time.");
        return false;
    }
    if (windowState.dropPathTooLong)
    {
        a_LogOutput(1, "Drop rejected: GFX path exceeds MAX_PATH.");
        return false;
    }
    if (windowState.dropPathEmpty)
    {
        a_LogOutput(1, "Drop rejected: no file path was received.");
        return false;
    }

    try
    {
        const std::string droppedPath = ConvertDroppedGfxPath(windowState.droppedPath);
        std::string relativePath = ResolveGfxRelativePath(assetRoot, droppedPath);
        if (relativePath.empty())
            throw std::runtime_error("Unable to resolve the dropped GFX path under the asset root.");

        AString pathString(relativePath.c_str());
        std::unique_ptr<A3DGFXEx, GfxDeleter> nextGfx(
            gfxManager.LoadGfx(device, pathString, false, false));
        if (!nextGfx)
            throw std::runtime_error("Failed to load dropped GFX: " + relativePath);
        if (!nextGfx->Start())
            throw std::runtime_error("A3DGFXEx::Start failed for dropped GFX: " + relativePath);

        Position2DEffect(*nextGfx, *device->GetA3DEngine()->GetActiveViewport());
        ConfigureCameraFromAabb(camera, nextGfx.get());
        activeGfx = std::move(nextGfx);
        activeGfxPath = std::move(relativePath);
        a_LogOutput(0, "Replaced active GFX with dropped file: %s", activeGfxPath.c_str());
        SetWindowTextA(hwnd, "GFX Preview - GFX replaced | MMB orbit, W/S zoom, Esc exit");
        return true;
    }
    catch (const std::exception& error)
    {
        a_LogOutput(1, "Dropped GFX was not opened; keeping the current GFX. %s", error.what());
        SetWindowTextA(hwnd, "GFX Preview - drop failed; see checker.log");
    }
    return false;
}

void FrameOrbitCamera(OrbitCamera& orbit, A3DCamera& camera, const A3DGFXEx& gfx)
{
    ConfigureCameraFromAabb(camera, &gfx);
    const auto& box = gfx.GetAABBOrg();
    A3DVECTOR3 center((box.Mins.x + box.Maxs.x) * 0.5f,
                      (box.Mins.y + box.Maxs.y) * 0.5f,
                      (box.Mins.z + box.Maxs.z) * 0.5f);
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
        center = A3DVECTOR3(0.0f, 0.0f, 0.0f);
    center *= gfx.GetActualScale();
    center += gfx.GetPos();
    orbit.Frame(center, camera.GetPos(), camera.GetZFront() * 1.1f, camera.GetZBack() * 0.8f);
}

void UpdateInteractiveCamera(A3DCamera& camera,
                             OrbitCamera& orbit,
                             HWND hwnd,
                             const InteractiveWindowState& windowState,
                             bool& orbitDragging,
                             float deltaSeconds)
{
    const bool active = windowState.focused && GetForegroundWindow() == hwnd && !IsIconic(hwnd);
    if (!active)
    {
        orbitDragging = false;
        return;
    }

    const auto& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || io.WantCaptureMouse)
        orbitDragging = false;
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        orbitDragging = true;
    // Не застосовуємо переміщення до натискання кнопки як перший крок орбіти.
    else if (orbitDragging)
        orbit.Rotate(io.MouseDelta.x, io.MouseDelta.y);
    if (!io.WantCaptureKeyboard)
    {
        const int zoom = static_cast<int>(ImGui::IsKeyDown(ImGuiKey_W)) -
                         static_cast<int>(ImGui::IsKeyDown(ImGuiKey_S));
        orbit.Zoom(static_cast<float>(zoom), deltaSeconds);
    }

    // Джерело API: PW_ClientSource/SDK/Angelica3D/Header/A3DCameraBase.h:103-108.
    // Камера завжди дивиться на центр ефекту; W/S змінюють тільки радіус орбіти.
    const A3DVECTOR3 position = orbit.GetPosition();
    camera.SetPos(position);
    camera.SetDirAndUp(orbit.GetPivot() - position, A3DVECTOR3(0.0f, 1.0f, 0.0f));
}

void RunInteractivePreview(A3DEngine& engine,
                           A3DViewport& viewport,
                           A3DCamera& camera,
                           std::unique_ptr<A3DGFXEx, GfxDeleter>& gfx,
                           A3DGFXExMan& gfxManager,
                           const std::string& assetRoot,
                           std::string& gfxPath,
                           HWND hwnd,
                           InteractiveWindowState& windowState)
{
    PreviewOverlay overlay(hwnd, engine.GetA3DDevice()->GetD3DDevice());
    overlay.SetEffect(gfxPath);
    OrbitCamera orbit;
    FrameOrbitCamera(orbit, camera, *gfx);
    bool orbitDragging = false;
    auto previousFrame = std::chrono::steady_clock::now();

    while (!windowState.closeRequested)
    {
        MSG message{};
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                windowState.closeRequested = true;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        if (windowState.closeRequested)
            break;

        if (windowState.resizePending && !windowState.resizing)
        {
            windowState.resizePending = false;
            const unsigned resizedWidth = windowState.resizeWidth;
            const unsigned resizedHeight = windowState.resizeHeight;
            overlay.InvalidateDeviceObjects();
            if (!engine.SetDisplayMode(static_cast<int>(resizedWidth),
                                       static_cast<int>(resizedHeight),
                                       A3DFMT_UNKNOWN, true, false, SDM_WIDTH | SDM_HEIGHT))
                throw std::runtime_error("A3DEngine::SetDisplayMode failed while resizing the preview window.");

            A3DVIEWPORTPARAM viewportParam = *viewport.GetParam();
            viewportParam.Width = resizedWidth;
            viewportParam.Height = resizedHeight;
            if (!viewport.SetParam(&viewportParam))
                throw std::runtime_error("A3DViewport::SetParam failed while resizing the preview window.");

            if (!camera.SetProjectionParam(camera.GetFOV(), camera.GetZFront(), camera.GetZBack(),
                                           static_cast<float>(resizedWidth) / resizedHeight))
                throw std::runtime_error("A3DCamera::SetProjectionParam failed while resizing the preview window.");

            gfxManager.Resize2DViewport();
            Position2DEffect(*gfx, viewport);
            if (!overlay.CreateDeviceObjects())
                throw std::runtime_error("ImGui Direct3D9 device objects could not be recreated after resizing.");
        }

        const auto now = std::chrono::steady_clock::now();
        const auto frameDeadline = now + std::chrono::milliseconds(16);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - previousFrame).count();
        previousFrame = now;
        if (elapsedMs < 1)
            elapsedMs = 1;
        if (elapsedMs > 100)
            elapsedMs = 100;
        const float deltaSeconds = static_cast<float>(elapsedMs) / 1000.0f;

        if (ProcessDroppedGfx(windowState, hwnd, assetRoot, engine.GetA3DDevice(), camera,
                              gfxManager, gfx, gfxPath))
        {
            FrameOrbitCamera(orbit, camera, *gfx);
            overlay.SetEffect(gfxPath);
            orbitDragging = false;
        }
        if (IsIconic(hwnd))
        {
            Sleep(10);
            continue;
        }

        overlay.BeginFrame();
        UpdateInteractiveCamera(camera, orbit, hwnd, windowState, orbitDragging, deltaSeconds);
        if (!engine.BeginRender())
            throw std::runtime_error("A3DEngine::BeginRender failed during interactive preview.");
        viewport.Active();
        if (!viewport.ClearDevice())
            throw std::runtime_error("A3DViewport::ClearDevice failed during interactive preview.");
        A3DGFXExMan::ResetStatCount();
        const auto tickBegin = std::chrono::steady_clock::now();
        gfx->TickAnimation(static_cast<DWORD>(elapsedMs));
        const auto renderBegin = std::chrono::steady_clock::now();
        RenderPreviewGfx(gfxManager, *gfx, viewport);
        const auto renderEnd = std::chrono::steady_clock::now();
        const float tickMs = std::chrono::duration<float, std::milli>(renderBegin - tickBegin).count();
        const float renderMs = std::chrono::duration<float, std::milli>(renderEnd - renderBegin).count();
        if (overlay.Draw(*gfx, orbit, tickMs, renderMs))
            FrameOrbitCamera(orbit, camera, *gfx);
        overlay.Render();
        if (!engine.EndRender())
            throw std::runtime_error("A3DEngine::EndRender failed during interactive preview.");
        if (!engine.Present())
            throw std::runtime_error("A3DEngine::Present failed during interactive preview.");

        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            frameDeadline - std::chrono::steady_clock::now()).count();
        if (remainingMs > 0)
            Sleep(static_cast<DWORD>(remainingMs));
    }
}

void ConfigureCameraFromAabb(A3DCamera& camera, const A3DGFXEx* gfx)
{
    // Центр і відстань за сферою AABB: AUInterface/AUIModelPicture.cpp:164-169.
    // Для GFX враховуємо DefaultScale та позицію; плоскі ефекти дивимося згори,
    // як у ImageCacheLib/ICLImgImplGfx.cpp:103-107.
    const A3DAABB& aabb = gfx->GetAABBOrg();
    const float scale = gfx->GetActualScale();
    A3DVECTOR3 target = (aabb.Mins + aabb.Maxs) * (0.5f * scale) + gfx->GetPos();
    const A3DVECTOR3 extents = (aabb.Maxs - aabb.Mins) * (0.5f * scale);
    float radius = extents.Magnitude();
    if (!std::isfinite(radius) || radius < 0.001f)
    {
        radius = 1.0f;
        target = gfx->GetPos();
    }
    const float distance = (std::max)(radius * 2.2f, radius + camera.GetZFront() * 1.1f);
    const A3DVECTOR3 offset = extents.y < 0.1f
        ? A3DVECTOR3(0.0f, distance * std::cos(0.3f), distance * std::sin(0.3f))
        : A3DVECTOR3(0.0f, 0.0f, distance);
    const A3DVECTOR3 position = target + offset;
    camera.SetPos(position);
    camera.SetDirAndUp(target - position, A3DVECTOR3(0.0f, 1.0f, 0.0f));
}
}

// Джерело: PW_ClientSource/client/CElement/CElementClient/EC_Utility.cpp:510.
// Менеджер належить утиліті; глобального клієнтського CECGame тут немає.
A3DGFXExMan* AfxGetGFXExMan()
{
    return gfxManager;
}

// Контракт: PW_ClientSource/client/CElement/include/GfxCommon/A3DGFXExMan.h:651.
// Сценарії бойових навичок потребують об'єктів світу і не входять у self-test.
bool AfxPlaySkillGfx(const AString&, unsigned char, clientid_t, clientid_t,
                     const A3DVECTOR3*, int, TargetDataVec&)
{
    a_LogOutput(1, "Skill playback is unavailable in the checker bootstrap.");
    return false;
}

void RunEngineProbe(const std::string& assetRoot)
{
    FileModule files(assetRoot);
    a_LogOutput(0, "AFI initialized; ACP=%u; engine character size=%u.",
                GetACP(), static_cast<unsigned>(sizeof(ACHAR)));

    // Джерела: PW_ClientSource/SDK/Angelica3D/Source/A3DEngine.cpp:49;
    // PW_ClientSource/client/CElement/GfxCommon/A3DGFXExMan.cpp:210.
    // Справжні конструктори перевіряють ABI і лінк, а не лише наявність .lib.
    // Init ще не викликаємо: пристрій і кадри реалізуються на етапі E2.
    auto engine = std::make_unique<A3DEngine>();
    auto manager = std::make_unique<A3DGFXExMan>();
    gfxManager = manager.get();
    a_LogOutput(0, "A3DEngine and A3DGFXExMan constructed successfully.");
    manager.reset();
    gfxManager = nullptr;
    engine.reset();
    a_LogOutput(0, "Self-test passed: library construction and AFI. Rendering not tested.");
}

void RunGfxPreview(const std::string& assetRoot,
                   const std::string& gfxPath,
                   const std::wstring& outputPath,
                   unsigned width,
                   unsigned height,
                   unsigned frames,
                   bool interactive)
{
    if (width == 0 || height == 0)
        throw std::runtime_error("Preview resolution must be non-zero.");
    if (!interactive && frames == 0)
        throw std::runtime_error("Preview frame count must be non-zero.");
    if (interactive && !outputPath.empty())
        throw std::runtime_error("Interactive preview does not accept a PNG output path.");

    FileModule files(assetRoot);
    // AFileImage::Init відкриває відносний шлях "Gfx\\..." через fopen.
    WorkingDirectory workingDirectory(assetRoot);
    a_LogOutput(0, "Starting preview for %s", gfxPath.c_str());

    std::string relativePath = ResolveGfxRelativePath(assetRoot, gfxPath);
    InteractiveWindowState windowState;
    HWND hwnd = interactive
        ? CreateInteractivePreviewWindow(width, height, windowState)
        : CreateHiddenPreviewWindow(width, height);
    if (!hwnd)
        throw std::runtime_error(interactive
            ? "Failed to create interactive preview window."
            : "Failed to create hidden preview window.");
    a_LogOutput(0, interactive
        ? "Created visible interactive preview window."
        : "Created hidden window for preview.");

    try
    {
        A3DDEVFMT devFmt;
        devFmt.bWindowed = true;
        devFmt.nWidth = static_cast<int>(width);
        devFmt.nHeight = static_cast<int>(height);
        devFmt.fmtAdapter = A3DFMT_A8R8G8B8;
        devFmt.fmtTarget = A3DFMT_A8R8G8B8;
        devFmt.fmtDepth = A3DFMT_D24X8;
        devFmt.bVSync = false;
        devFmt.bSndGlobal = false;

        auto engine = std::make_unique<A3DEngine>();
        a_LogOutput(0, interactive
            ? "Initializing A3DEngine with interactive window."
            : "Initializing A3DEngine with hidden window.");
        if (!engine->Init(GetModuleHandleA(nullptr), hwnd, &devFmt, A3DDEV_ALLOWMULTITHREAD))
            throw std::runtime_error("A3DEngine::Init failed for preview mode.");
        a_LogOutput(0, "A3DEngine initialized.");

        if (!g_pA3DConfig)
            throw std::runtime_error("A3D runtime configuration is unavailable.");
        // Джерело: PW_ClientSource/client/CElement/CElementClient/EC_Game.cpp:1598.
        g_pA3DConfig->RT_SetShowFPSFlag(false);

        A3DDevice* device = engine->GetA3DDevice();
        if (!device)
            throw std::runtime_error("A3DEngine created without a device.");
        AfxSetA3DDevice(device);

        A3DLightMan* lightMan = engine->GetA3DLightMan();
        if (!lightMan)
            throw std::runtime_error("A3DLightMan unavailable for preview scene.");
        a_LogOutput(0, "Creating directional light.");

        A3DLight* dirLight = nullptr;
        if (!lightMan->CreateDirecionalLight(0, &dirLight,
                                            A3DVECTOR3(0.0f, 120.0f, 0.0f),
                                            A3DVECTOR3(0.0f, -1.0f, -1.0f),
                                            A3DCOLORVALUE(1.0f, 1.0f, 1.0f, 1.0f),
                                            A3DCOLORVALUE(0.8f, 0.8f, 0.8f, 1.0f),
                                            A3DCOLORVALUE(0.2f, 0.2f, 0.2f, 1.0f)))
            throw std::runtime_error("Directional light creation failed.");

        A3DSkinMan* skinMan = engine->GetA3DSkinMan();
        if (!skinMan)
            throw std::runtime_error("A3DSkinMan unavailable for preview scene.");
        skinMan->SetDirLight(dirLight);
        a_LogOutput(0, "Loading skin shader config.");
        if (!skinMan->LoadSkinModelVertexShaders("Configs\\2.2\\skinmodelvs.cfg"))
            a_LogOutput(1, "Warning: configs\\2.2\\skinmodelvs.cfg was not available; preview may not render skinned models.");

        A3DVIEWPORTPARAM viewportParam{};
        viewportParam.X = 0;
        viewportParam.Y = 0;
        viewportParam.Width = static_cast<DWORD>(width);
        viewportParam.Height = static_cast<DWORD>(height);
        viewportParam.MinZ = 0.0f;
        viewportParam.MaxZ = 1.0f;

        A3DViewport viewport;
        a_LogOutput(0, "Initializing viewport.");
        if (!viewport.Init(device, &viewportParam, true, true, 0xFF000000))
            throw std::runtime_error("A3DViewport::Init failed for preview.");

        A3DCamera camera;
        a_LogOutput(0, "Initializing camera.");
        // Near/far як у CElementClient/EC_Game.cpp:1636-1637; пропорції viewport
        // передаються в камеру за A3DPlatform/ECViewport.cpp:84.
        if (!camera.Init(device, DEG2RAD(65.0f), 0.2f, 2000.0f, static_cast<float>(width) / height))
            throw std::runtime_error("A3DCamera::Init failed for preview.");

        viewport.SetCamera(&camera);
        engine->SetActiveCamera(&camera);
        engine->SetActiveViewport(&viewport);
        AfxSetA3DCamera(&camera);

        std::unique_ptr<A3DGFXExMan, GfxManagerDeleter> gfxManagerLocal(new A3DGFXExMan());
        gfxManager = gfxManagerLocal.get();
        a_LogOutput(0, "Initializing GFX manager.");
        if (!gfxManagerLocal->Init(device))
            throw std::runtime_error("A3DGFXExMan::Init failed for preview.");

        const AString relativePathString(relativePath.c_str());
        const char* relativePathRaw = static_cast<const char*>(relativePathString);
        const std::string gfxWithPrefix = std::string("Gfx\\") + relativePath;
        a_LogOutput(0, "Checking GFX file existence: direct=%d prefixed=%d absolute=%d",
                    af_IsFileExist(relativePath.c_str()),
                    af_IsFileExist(gfxWithPrefix.c_str()),
                    af_IsFileExist((std::string(assetRoot) + "\\gfx\\" + relativePath).c_str()));
        a_LogOutput(0, "Loading GFX: %s", relativePathRaw ? relativePathRaw : "<null>");
        std::unique_ptr<A3DGFXEx, GfxDeleter> gfx(
            gfxManagerLocal->LoadGfx(device, relativePathString, false, false));
        if (!gfx)
            throw std::runtime_error("Failed to load GFX: " + relativePath);

        if (!gfx->Start())
            throw std::runtime_error("A3DGFXEx::Start failed for: " + relativePath);

        Position2DEffect(*gfx, viewport);
        ConfigureCameraFromAabb(camera, gfx.get());
        const auto& bounds = gfx->GetAABBOrg();
        const auto& eye = camera.GetPos();
        a_LogOutput(0, "GFX diagnostic: elements=%d ready=%d 2d=%d state=%d alpha=%.3f scale=%.3f",
                    gfx->GetElementCount(), gfx->IsResourceReady(), gfx->Is2DRender(),
                    gfx->GetState(), gfx->GetActualAlpha(), gfx->GetActualScale());
        a_LogOutput(0, "Camera diagnostic: eye=(%.3f,%.3f,%.3f) near=%.4f far=%.1f local_bounds=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)",
                    eye.x, eye.y, eye.z, camera.GetZFront(), camera.GetZBack(),
                    bounds.Mins.x, bounds.Mins.y, bounds.Mins.z,
                    bounds.Maxs.x, bounds.Maxs.y, bounds.Maxs.z);

        if (interactive)
        {
            RunInteractivePreview(*engine, viewport, camera, gfx, *gfxManagerLocal,
                                  assetRoot, relativePath, hwnd, windowState);
        }
        else
        {
            A3DDEVFMT renderTargetFormat = device->GetDevFormat();
            renderTargetFormat.nWidth = static_cast<int>(width);
            renderTargetFormat.nHeight = static_cast<int>(height);
            renderTargetFormat.fmtTarget = A3DFMT_A8R8G8B8;
            renderTargetFormat.fmtDepth = A3DFMT_D24X8;

            std::unique_ptr<A3DRenderTarget, RenderTargetDeleter> renderTarget(new A3DRenderTarget());
            if (!renderTarget->Init(device, renderTargetFormat, true, false))
                throw std::runtime_error("A3DRenderTarget::Init failed for preview.");

            if (!engine->BeginRender())
                throw std::runtime_error("A3DEngine::BeginRender failed during preview.");
            RenderTargetBinding renderTargetBinding(device, renderTarget.get());
            viewport.Active();
            if (!viewport.ClearDevice())
                throw std::runtime_error("A3DViewport::ClearDevice failed during preview.");

            A3DGFXExMan::ResetStatCount();
            for (unsigned frame = 0; frame < frames; ++frame)
            {
                gfx->TickAnimation(50);
                RenderPreviewGfx(*gfxManagerLocal, *gfx, viewport);
            }
            a_LogOutput(0, "Render diagnostic: draws=%d primitives=%d particles=%d time_ms=%lu state=%d",
                        A3DGFXExMan::GetTotalDrawCount(), A3DGFXExMan::GetTotalPrimCount(),
                        A3DGFXExMan::GetParticleRenderCount(), gfx->GetTimeElapse(), gfx->GetState());
            for (int i = 0; i < gfx->GetElementCount(); ++i)
            {
                const auto* element = gfx->GetElement(i);
                a_LogOutput(0, "Element %d: type=%d init=%d active=%d dummy=%d path=%s", i,
                            element->GetEleTypeId(), element->IsInit(), element->IsActive(),
                            element->IsDummyEle(),
                            static_cast<const char*>(element->GetTexPath()));
            }

            if (!engine->EndRender())
                throw std::runtime_error("A3DEngine::EndRender failed during preview.");
            if (!renderTargetBinding.Restore())
                throw std::runtime_error("A3DDevice::RestoreRenderTarget failed.");
            SaveRenderTargetAsPng(device, renderTarget.get(), outputPath);
            a_LogOutput(0, "Saved preview PNG to the requested output path.");
        }

        gfx.reset();
        a_LogOutput(0, "Preview successful for %s", relativePath.c_str());
    }
    catch (...)
    {
        AfxSetA3DCamera(nullptr);
        gfxManager = nullptr;
        AfxSetA3DDevice(nullptr);
        DestroyWindow(hwnd);
        throw;
    }

    AfxSetA3DCamera(nullptr);
    gfxManager = nullptr;
    AfxSetA3DDevice(nullptr);
    DestroyWindow(hwnd);
}

// ================= Покадровий захоплювач (перевірка C++ ↔ Unity) =================

namespace
{
// GBK → UTF-8: у camera.json/effect.json шляхи ассетів зберігаються як UTF-8.
std::string GbkToUtf8(const std::string& gbk)
{
    if (gbk.empty())
        return {};
    const int wideLength = MultiByteToWideChar(936, 0, gbk.c_str(), static_cast<int>(gbk.size()), nullptr, 0);
    if (wideLength <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(936, 0, gbk.c_str(), static_cast<int>(gbk.size()), wide.data(), wideLength);
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLength, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0)
        return {};
    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLength, utf8.data(), utf8Length, nullptr, nullptr);
    return utf8;
}

// Мінімальний JSON-рядок: лапки, зворотні косі риски та контрольні символи.
std::string JsonString(const std::string& utf8)
{
    std::string out = "\"";
    for (const unsigned char c : utf8)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", c);
                out += buffer;
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

std::string JsonBool(bool value) { return value ? "true" : "false"; }

// Тривалість елемента за правилом планувальника Unity (GfxRuntimePlan.FillTiming):
// StartTime + сума додатних TimeSpan ключових точок; TimeSpan<0 — endless;
// якщо старт >= сумарного часу і немає endless — старт + 3000.
// Джерела полів: A3DGFXElement.h:652-664 (GetTimeStart, GetKeyPoint),
// A3DGFXKeyPoint.h:686 (GetTimeSpan, мс).
unsigned ElementTotalTimeMs(A3DGFXElement& element, bool& endless)
{
    DWORD start = element.GetTimeStart();
    DWORD total = start;
    endless = false;
    for (int i = 0; i < element.GetKeyPointCount(); ++i)
    {
        A3DGFXKeyPoint* keyPoint = element.GetKeyPoint(i);
        if (!keyPoint)
            continue;
        const DWORD span = keyPoint->GetTimeSpan();
        if (static_cast<int>(span) < 0)
            endless = true;
        else
            total += span;
    }
    if (total <= start && !endless)
        total = start + 3000;
    return endless ? 0 : total;
}

AString WriteUtf8File(const std::wstring& path, const std::string& content)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return AString("CreateFile failed for frame capture metadata.");
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);
    if (!ok)
        return AString("WriteFile failed for frame capture metadata.");
    return AString();
}
} // namespace

void RunGfxFrameCapture(const std::string& assetRoot,
                        const std::string& gfxPath,
                        const std::wstring& framesDirectory,
                        unsigned width,
                        unsigned height,
                        const std::vector<unsigned>& timePointsMs)
{
    if (width == 0 || height == 0)
        throw std::runtime_error("Frame capture resolution must be non-zero.");
    if (timePointsMs.empty())
        throw std::runtime_error("Frame capture requires at least one time point.");

    FileModule files(assetRoot);
    WorkingDirectory workingDirectory(assetRoot);
    a_LogOutput(0, "Starting frame capture for %s", gfxPath.c_str());

    const std::string relativePath = ResolveGfxRelativePath(assetRoot, gfxPath);
    const HWND hwnd = CreateHiddenPreviewWindow(width, height);
    if (!hwnd)
        throw std::runtime_error("Failed to create hidden window for frame capture.");
    a_LogOutput(0, "Created hidden window for frame capture.");

    try
    {
        A3DDEVFMT devFmt;
        devFmt.bWindowed = true;
        devFmt.nWidth = static_cast<int>(width);
        devFmt.nHeight = static_cast<int>(height);
        devFmt.fmtAdapter = A3DFMT_A8R8G8B8;
        devFmt.fmtTarget = A3DFMT_A8R8G8B8;
        devFmt.fmtDepth = A3DFMT_D24X8;
        devFmt.bVSync = false;
        devFmt.bSndGlobal = false;

        auto engine = std::make_unique<A3DEngine>();
        if (!engine->Init(GetModuleHandleA(nullptr), hwnd, &devFmt, A3DDEV_ALLOWMULTITHREAD))
            throw std::runtime_error("A3DEngine::Init failed for frame capture mode.");
        a_LogOutput(0, "A3DEngine initialized for frame capture.");

        if (!g_pA3DConfig)
            throw std::runtime_error("A3D runtime configuration is unavailable.");
        g_pA3DConfig->RT_SetShowFPSFlag(false);

        A3DDevice* device = engine->GetA3DDevice();
        if (!device)
            throw std::runtime_error("A3DEngine created without a device.");
        AfxSetA3DDevice(device);

        A3DLightMan* lightMan = engine->GetA3DLightMan();
        if (!lightMan)
            throw std::runtime_error("A3DLightMan unavailable for frame capture scene.");
        A3DLight* dirLight = nullptr;
        if (!lightMan->CreateDirecionalLight(0, &dirLight,
                                            A3DVECTOR3(0.0f, 120.0f, 0.0f),
                                            A3DVECTOR3(0.0f, -1.0f, -1.0f),
                                            A3DCOLORVALUE(1.0f, 1.0f, 1.0f, 1.0f),
                                            A3DCOLORVALUE(0.8f, 0.8f, 0.8f, 1.0f),
                                            A3DCOLORVALUE(0.2f, 0.2f, 0.2f, 1.0f)))
            throw std::runtime_error("Directional light creation failed.");

        A3DSkinMan* skinMan = engine->GetA3DSkinMan();
        if (!skinMan)
            throw std::runtime_error("A3DSkinMan unavailable for frame capture scene.");
        skinMan->SetDirLight(dirLight);
        if (!skinMan->LoadSkinModelVertexShaders("Configs\\2.2\\skinmodelvs.cfg"))
            a_LogOutput(1, "Warning: configs\\2.2\\skinmodelvs.cfg was not available; preview may not render skinned models.");

        A3DVIEWPORTPARAM viewportParam{};
        viewportParam.X = 0;
        viewportParam.Y = 0;
        viewportParam.Width = static_cast<DWORD>(width);
        viewportParam.Height = static_cast<DWORD>(height);
        viewportParam.MinZ = 0.0f;
        viewportParam.MaxZ = 1.0f;

        A3DViewport viewport;
        if (!viewport.Init(device, &viewportParam, true, true, 0xFF000000))
            throw std::runtime_error("A3DViewport::Init failed for frame capture.");

        A3DCamera camera;
        // Near/far і FOV як у RunGfxPreview: EC_Game.cpp:1636-1637.
        if (!camera.Init(device, DEG2RAD(65.0f), 0.2f, 2000.0f, static_cast<float>(width) / height))
            throw std::runtime_error("A3DCamera::Init failed for frame capture.");

        viewport.SetCamera(&camera);
        engine->SetActiveCamera(&camera);
        engine->SetActiveViewport(&viewport);
        AfxSetA3DCamera(&camera);

        std::unique_ptr<A3DGFXExMan, GfxManagerDeleter> gfxManagerLocal(new A3DGFXExMan());
        gfxManager = gfxManagerLocal.get();
        if (!gfxManagerLocal->Init(device))
            throw std::runtime_error("A3DGFXExMan::Init failed for frame capture.");

        const AString relativePathString(relativePath.c_str());
        std::unique_ptr<A3DGFXEx, GfxDeleter> gfx(
            gfxManagerLocal->LoadGfx(device, relativePathString, false, false));
        if (!gfx)
            throw std::runtime_error("Failed to load GFX: " + relativePath);
        if (!gfx->Start())
            throw std::runtime_error("A3DGFXEx::Start failed for: " + relativePath);

        Position2DEffect(*gfx, viewport);
        ConfigureCameraFromAabb(camera, gfx.get());
        const auto& bounds = gfx->GetAABBOrg();

        // Тривалість: максимум за елементами, той самий діалог, що у Unity-планувальника.
        bool anyEndless = false;
        unsigned totalMs = 0;
        for (int i = 0; i < gfx->GetElementCount(); ++i)
        {
            A3DGFXElement* element = gfx->GetElement(i);
            if (!element)
                continue;
            bool endless = false;
            const unsigned total = ElementTotalTimeMs(*element, endless);
            anyEndless = anyEndless || endless;
            totalMs = (std::max)(totalMs, total);
        }
        // Endless-елементи: як у Unity (default 3000; <= start → start+3000 уже застосовано
        // для не-endless; endless не збільшує суму). Якщо все ж 0 — страховка.
        if (totalMs == 0)
            totalMs = 3000;

        float actualSpeed = gfx->GetPlaySpeed() * gfx->GetDefPlaySpeed();
        if (!std::isfinite(actualSpeed) || std::fabs(actualSpeed) < 0.001f)
        {
            a_LogOutput(1, "Warning: PlaySpeed %.4f is not usable; capture uses 1.0.", actualSpeed);
            actualSpeed = 1.0f;
        }

        std::vector<unsigned> targets = timePointsMs;
        const unsigned maxTarget = targets.back();
        if (maxTarget > totalMs)
        {
            // Масштабуємо сітку часів до тривалості конкретного ефекту.
            for (unsigned& t : targets)
                t = static_cast<unsigned>(static_cast<unsigned long long>(t) * totalMs / maxTarget);
            // Дедуплікація з сортуванням.
            std::sort(targets.begin(), targets.end());
            targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        }
        if (targets.size() != timePointsMs.size())
            a_LogOutput(1, "Warning: time points deduplicated from %zu to %zu.", timePointsMs.size(), targets.size());

        a_LogOutput(0, "Frame capture plan: elements=%d total_ms=%u speed=%.4f points=%zu",
                    gfx->GetElementCount(), totalMs, actualSpeed, targets.size());

        A3DDEVFMT renderTargetFormat = device->GetDevFormat();
        renderTargetFormat.nWidth = static_cast<int>(width);
        renderTargetFormat.nHeight = static_cast<int>(height);
        renderTargetFormat.fmtTarget = A3DFMT_A8R8G8B8;
        renderTargetFormat.fmtDepth = A3DFMT_D24X8;

        std::unique_ptr<A3DRenderTarget, RenderTargetDeleter> renderTarget(new A3DRenderTarget());
        if (!renderTarget->Init(device, renderTargetFormat, true, false))
            throw std::runtime_error("A3DRenderTarget::Init failed for frame capture.");

        if (!engine->BeginRender())
            throw std::runtime_error("A3DEngine::BeginRender failed during frame capture.");
        RenderTargetBinding renderTargetBinding(device, renderTarget.get());
        viewport.Active();

        DWORD previous = 0;
        std::string framesJson = "[";
        for (size_t i = 0; i < targets.size(); ++i)
        {
            const unsigned target = targets[i];
            DWORD wall = target >= previous
                ? static_cast<DWORD>(static_cast<unsigned long long>(target - previous) / actualSpeed)
                : 0;
            previous = target;
            // TickAnimation множить на швидкість: подаємо настінний час, щоб
            // внутрішній час точно досяг цілі. Чанки до 4000 мс.
            while (wall > 0)
            {
                const DWORD chunk = wall > 4000 ? 4000 : wall;
                gfx->TickAnimation(chunk);
                wall -= chunk;
            }
            if (!viewport.ClearDevice())
                throw std::runtime_error("A3DViewport::ClearDevice failed during frame capture.");

            A3DGFXExMan::ResetStatCount();
            RenderPreviewGfx(*gfxManagerLocal, *gfx, viewport);
            if (!engine->EndRender())
                throw std::runtime_error("A3DEngine::EndRender failed during frame capture.");
            if (!renderTargetBinding.Restore())
                throw std::runtime_error("A3DDevice::RestoreRenderTarget failed during frame capture.");

            wchar_t framePath[MAX_PATH];
            if (_snwprintf(framePath, MAX_PATH, L"%s\\frame-%02zu.png", framesDirectory.c_str(), i + 1) >= MAX_PATH)
                throw std::runtime_error("Frame output path is too long.");
            SaveRenderTargetAsPng(device, renderTarget.get(), framePath);
            a_LogOutput(0, "Frame capture: index=%zu time_ms=%u draws=%d primitives=%d particles=%d state=%d",
                        i + 1, target, A3DGFXExMan::GetTotalDrawCount(), A3DGFXExMan::GetTotalPrimCount(),
                        A3DGFXExMan::GetParticleRenderCount(), gfx->GetState());
            char frameLine[512];
            std::snprintf(frameLine, sizeof(frameLine),
                          "{\"index\":%zu,\"time_ms\":%u,\"draws\":%d,\"primitives\":%d,\"particles\":%d,\"state\":%d,\"elements\":%d}%s",
                          i + 1, target, A3DGFXExMan::GetTotalDrawCount(), A3DGFXExMan::GetTotalPrimCount(),
                          A3DGFXExMan::GetParticleRenderCount(), gfx->GetState(), gfx->GetElementCount(),
                          i + 1 < targets.size() ? "," : "");
            framesJson += frameLine;

            if (i + 1 < targets.size())
            {
                // Наступний кадр: повний цикл BEGIN → прив'язка RT → clear → render.
                if (!engine->BeginRender())
                    throw std::runtime_error("A3DEngine::BeginRender failed between frames.");
                if (!renderTargetBinding.Rebind())
                    throw std::runtime_error("A3DDevice::SetRenderTarget failed between frames.");
                viewport.Active();
            }
        }

        // ---- camera.json ----
        const A3DVECTOR3 eye = camera.GetPos();
        const A3DVECTOR3 center = (bounds.Mins + bounds.Maxs) * (0.5f * gfx->GetActualScale()) + gfx->GetPos();
        char buffer[1024];
        std::string cameraJson = "{";
        std::snprintf(buffer, sizeof(buffer), "\"eye\":[%.6f,%.6f,%.6f]", eye.x, eye.y, eye.z);
        cameraJson += buffer;
        std::snprintf(buffer, sizeof(buffer), ",\"target\":[%.6f,%.6f,%.6f]", center.x, center.y, center.z);
        cameraJson += buffer;
        cameraJson += ",\"up\":[0,1,0]";
        std::snprintf(buffer, sizeof(buffer), ",\"fov_deg\":%.3f", 65.0f);
        cameraJson += buffer;
        std::snprintf(buffer, sizeof(buffer), ",\"near\":%.4f,\"far\":%.1f,\"aspect\":%.6f",
                      camera.GetZFront(), camera.GetZBack(), static_cast<float>(width) / height);
        cameraJson += buffer;
        cameraJson += "}";
        WriteUtf8File(framesDirectory + L"\\camera.json", cameraJson);

        // ---- effect.json ----
        std::string effectJson = "{";
        effectJson += "\"gfx\":" + JsonString(GbkToUtf8(relativePath)) + ",";
        std::snprintf(buffer, sizeof(buffer),
                      "\"total_ms\":%u,\"any_endless\":%s,\"play_speed\":%.6f,\"def_play_speed\":%.6f,\"actual_speed\":%.6f,"
                      "\"scale\":%.6f,\"alpha\":%.3f,\"is_2d\":%s,\"element_count\":%d,\"time_points\":[",
                      totalMs, JsonBool(anyEndless).c_str(), gfx->GetPlaySpeed(), gfx->GetDefPlaySpeed(), actualSpeed,
                      gfx->GetActualScale(), gfx->GetActualAlpha(), JsonBool(gfx->Is2DRender()).c_str(),
                      gfx->GetElementCount());
        effectJson += buffer;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            std::snprintf(buffer, sizeof(buffer), "%s%u", i ? "," : "", targets[i]);
            effectJson += buffer;
        }
        effectJson += "],\"aabb_min\":[";
        std::snprintf(buffer, sizeof(buffer), "%.6f,%.6f,%.6f", bounds.Mins.x, bounds.Mins.y, bounds.Mins.z);
        effectJson += buffer;
        effectJson += "],\"aabb_max\":[";
        std::snprintf(buffer, sizeof(buffer), "%.6f,%.6f,%.6f", bounds.Maxs.x, bounds.Maxs.y, bounds.Maxs.z);
        effectJson += buffer;
        effectJson += "],\"frames\":" + framesJson + "],\"elements\":[";
        for (int i = 0; i < gfx->GetElementCount(); ++i)
        {
            A3DGFXElement* element = gfx->GetElement(i);
            if (!element)
                continue;
            bool endless = false;
            const unsigned elementTotal = ElementTotalTimeMs(*element, endless);
            std::snprintf(buffer, sizeof(buffer),
                          "%s{\"index\":%d,\"type\":%d,\"init\":%d,\"active\":%d,\"dummy\":%d,"
                          "\"keypoints\":%d,\"start_ms\":%lu,\"total_ms\":%u,\"endless\":%s,\"tex\":",
                          i ? "," : "", i, element->GetEleTypeId(), element->IsInit(), element->IsActive(),
                          element->IsDummyEle(), element->GetKeyPointCount(),
                          static_cast<unsigned long>(element->GetTimeStart()), elementTotal,
                          JsonBool(endless).c_str());
            effectJson += buffer;
            effectJson += JsonString(GbkToUtf8(static_cast<const char*>(element->GetTexPath())));
            effectJson += "}";
        }
        effectJson += "]}";
        WriteUtf8File(framesDirectory + L"\\effect.json", effectJson);

        gfx.reset();
        a_LogOutput(0, "Frame capture successful for %s (%zu frames).", relativePath.c_str(), targets.size());
    }
    catch (...)
    {
        AfxSetA3DCamera(nullptr);
        gfxManager = nullptr;
        AfxSetA3DDevice(nullptr);
        DestroyWindow(hwnd);
        throw;
    }

    AfxSetA3DCamera(nullptr);
    gfxManager = nullptr;
    AfxSetA3DDevice(nullptr);
    DestroyWindow(hwnd);
}
