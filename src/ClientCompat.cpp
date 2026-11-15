#include <A3DGFXExMan.h>
#include <A3DEngine.h>
#include <A3DSkinModel.h>
#include <A3DGFXLight.h>
#include <ALog.h>

class CECModelMan;

namespace
{
A3DDevice* g_afxDevice = nullptr;
A3DCamera* g_afxCamera = nullptr;
}

// Обгортка клієнтських callback-ів GfxCommon; камеру й пристрій задає preview.
// Джерело контрактів: PW_ClientSource/client/CElement/include/GfxCommon/A3DGFXExMan.h:628-676.

void AfxSetA3DDevice(A3DDevice* pDevice) { g_afxDevice = pDevice; }
A3DCamera* AfxGetA3DCamera() { return g_afxCamera; }
void AfxSetA3DCamera(A3DCamera* pCamera) { g_afxCamera = pCamera; }
A3DDevice* AfxGetA3DDevice() { return g_afxDevice; }
A3DSkinModel* AfxLoadA3DSkinModel(const char* file, int skinFlag)
{
    // Редакторський runtime: GfxCommon/A3DGFXExMan.cpp:1387-1409.
    auto* model = new A3DSkinModel;
    if (!model->Init(g_afxDevice->GetA3DEngine()) || !model->Load(file, skinFlag))
    {
        a_LogOutput(1, "Skin model load failed: %s", file);
        model->Release();
        delete model;
        return nullptr;
    }
    a_LogOutput(0, "Skin model loaded: %s", file);
    return model;
}
void AfxReleaseA3DSkinModel(A3DSkinModel* model)
{
    if (model) { model->Release(); delete model; }
}
bool AfxGetModelUpdateFlag() { return true; }
const A3DLIGHTPARAM& AfxGetLightparam()
{
    // Редакторські значення без світла світу: A3DGFXExMan.cpp:1450-1473.
    static A3DLIGHTPARAM lightParam{};
    lightParam.Type = A3DLIGHT_DIRECTIONAL;
    lightParam.Diffuse = A3DCOLORVALUE(0.8f, 0.8f, 0.9f, 1.0f);
    lightParam.Specular = A3DCOLORVALUE(1.0f, 1.0f, 1.0f, 1.0f);
    lightParam.Ambient = A3DCOLORVALUE(0.0f, 0.0f, 0.0f, 1.0f);
    lightParam.Direction = Normalize(A3DVECTOR3(0.0f, -0.7f, 1.0f));
    return lightParam;
}
void AfxSetA3DSkinModelSceneLightInfo(A3DSkinModel* model)
{
    // Джерело: GfxCommon/A3DGFXExMan.cpp:1412-1433.
    auto light = model->GetLightInfo();
    auto& lights = AfxGetGFXExMan()->GetLightParamList();
    if (lights.size())
    {
        const auto& point = lights[0]->GetLightParam();
        light.bPtLight = true;
        light.colPtDiff = point.Diffuse * point.Diffuse.a * 2.0f;
        light.colPtAmb = point.Ambient * point.Ambient.a * 2.0f;
        light.fPtRange = point.Range;
        light.vPtAtten = A3DVECTOR3(point.Attenuation0, point.Attenuation1, point.Attenuation2);
        light.vPtLightPos = point.Position;
    }
    model->SetLightInfo(light);
}
void AfxECModelAddShadower(const A3DVECTOR3&, const A3DAABB&, A3DSkinModel*) {}
float AfxGetGrndNorm(const A3DVECTOR3&, A3DVECTOR3* normal)
{
    // Редакторська площина: A3DGFXExMan.cpp:1439-1447.
    if (normal) *normal = A3DVECTOR3(0.0f, 1.0f, 0.0f);
    return 0.0f;
}
void AfxEndShakeCam() {}
bool AfxRayTrace(const A3DVECTOR3&, const A3DVECTOR3&) { return false; }
AM3DSoundBuffer* AfxLoadNonLoopSound(const char*, int) { return nullptr; }
AM3DSoundBuffer* AfxLoadLoopSound(const char*) { return nullptr; }
void AfxReleaseSoundNonLoop(AM3DSoundBuffer*&) {}
void AfxReleaseSoundLoop(AM3DSoundBuffer*&) {}
void AfxSetCamOffset(const A3DVECTOR3&) {}
void AfxBeginShakeCam() {}
void AfxSkillGfxShowDamage(clientid_t, clientid_t, int, int, DWORD) {}
void AfxSkillGfxShowCaster(clientid_t, DWORD) {}
bool AfxSkillGfxAddDamageData(clientid_t, clientid_t, unsigned char, DWORD, int) { return false; }
float AfxGetAverageFrameRate() { return 30.0f; } // A3DGFXExMan.cpp:1522.
bool gGfxGetSurfaceData(const A3DVECTOR3& center, float radius, A3DVECTOR3* vertices,
                        int& vertexCount, WORD* indices, int& indexCount)
{
    // Редакторська поверхня декалей: A3DGFXExMan.cpp:1527-1546.
    vertices[0] = A3DVECTOR3(center.x - radius, center.y, center.z + radius);
    vertices[1] = A3DVECTOR3(center.x + radius, center.y, center.z + radius);
    vertices[2] = A3DVECTOR3(center.x - radius, center.y, center.z - radius);
    vertices[3] = A3DVECTOR3(center.x + radius, center.y, center.z - radius);
    indices[0] = 0; indices[1] = 1; indices[2] = 2;
    indices[3] = 1; indices[4] = 3; indices[5] = 2;
    vertexCount = 4;
    indexCount = 6;
    return true;
}
CECModelMan* AfxGetECModelMan() { return nullptr; }
const char* AfxGetECMHullPath() { return ""; }
