# 03 — API-референс: рушій Angelica та GFX (по PW_ClientSource)

> Шляхи до оригіналів — від `B:\Git\PW_ClientSource\`. Нижче розділено вихідні
> патерни та їх адаптацію в checker станом на 2026-11-15; оригінали не змінюються.

## 1. Файлова система (AngelicaFile)

`SDK\AngelicaFile\Header\AFI.h` — обов'язковий порядок:
```cpp
af_Initialize();                 // ініт файлового модуля
af_SetBaseDir(szBaseDir);        // корінь ассетів (де лежать gfx\, textures\, models\ …)
af_Finalize();                   // при завершенні
```
Корисні: `af_GetBaseDir()`, `af_GetRelativePathNoBase(full, base, rel)`,
`af_IsFileExist()`, `af_CheckFileExt()`. Приклад використання у тулах:
`client\CElement\CElementIconTool\ElementIconToolDlg.cpp:271-282` (Init/Release).

## 2. Ініціалізація движка (Direct3D 9)

### 2.1 Мінімальний зразок — CElementIconTool (найпростіший у репо)
`client\CElement\CElementIconTool\ElementIconToolDlg.cpp:231-264`:
```cpp
m_pA3DEngine = new A3DEngine();
A3DDEVFMT devFmt;
devFmt.bWindowed = true;  devFmt.nWidth = Rect.right;  devFmt.nHeight = Rect.bottom;
devFmt.fmtTarget = A3DFMT_UNKNOWN; devFmt.fmtDepth = A3DFMT_UNKNOWN;
DWORD dwDevFlags = A3DDEV_ALLOWMULTITHREAD | A3DDEV_FORCESOFTWARETL; // SW T&L для тула
if (!m_pA3DEngine->Init(hInst, hWnd, &devFmt, dwDevFlags)) { ... }
m_pA3DDevice = m_pA3DEngine->GetA3DDevice();
```

### 2.2 Повний порядок як у клієнті — CECGame::InitA3DEngine
`client\CElement\CElementClient\EC_Game.cpp:1568-1730`:
1. `new A3DEngine` → `Init(hInstance, hWnd, &devFmt, devFlags)` (1571-1596)
2. `m_pA3DDevice = m_pA3DEngine->GetA3DDevice()` (1603)
3. Viewport: `CECViewport::INITPARAMS` (поля rcView, fFOV, fNearPlane/fFarPlane,
   bOrthoCamera + орто-план) → `m_pViewport->Init(ViewParams)` → `Activate(false)` (1617-1661)
4. Світло (1664-1689):
   ```cpp
   A3DLIGHTPARAM LightParams; ZeroMemory(&LightParams, sizeof(LightParams));
   LightParams.Type = A3DLIGHT_DIRECTIONAL;
   LightParams.Direction = <вектор>; LightParams.Diffuse = …; LightParams.Specular = …;
   LightParams.Attenuation0 = 1.0f;
   m_pDirLight = CreateA3DLight(&LightParams); m_pDirLight->TurnOn();
   ```
   ( CreateA3DLight → `m_pA3DEngine->GetA3DLightMan()->CreateLight(i, &pLight)`, EC_Game.cpp:1755-1787 )
5. Ембієнт/туман: `m_pA3DDevice->SetAmbient(...)` тощо (1692-1697)
6. Скін-шейдери (1701-1714):
   ```cpp
   A3DSkinMan* pSkinMan = m_pA3DEngine->GetA3DSkinMan();
   #ifdef ANGELICA_2_2
   pSkinMan->LoadSkinModelVertexShaders("Configs\\2.2\\skinmodelvs.cfg")
   #endif
   pSkinMan->SetDirLight(m_pDirLight); pSkinMan->SetDPointLight(m_pDynamicLight);
   ```
7. Скін-рендери (411-414, EC_Game::Init):
   ```cpp
   m_pSkinRender1 = m_pA3DEngine->GetA3DSkinMan()->GetDefSkinRender();
   m_pSkinRender2 = new A3DSkinRender; m_pSkinRender2->Init(m_pA3DEngine);
   ```

## 3. GFX-система (GfxCommon)

### 3.1 Класи
- `A3DGFXEx` — один ефект: `client\CElement\include\GfxCommon\A3DGFXEx.h:45`
  - `Load(A3DDevice*, const char* szFile)` (149-150, реалізація A3DGFXEx.cpp:717)
  - `Start()/Pause()/Stop()/TickAnimation(DWORD dwTickTime)` (165-169)
  - `GetAABBOrg()` — збережений локальний AABB ефекту (для наведення камери)
  - `SetPos/SetDir/GetPos`, `SetPlaySpeed/SetScale/SetAlpha`
- `A3DGFXExMan` — менеджер: `client\CElement\include\GfxCommon\A3DGFXExMan.h:214`
  - `Init(A3DDevice* pDev, int nPoolSize=20000)` (реалізація A3DGFXExMan.cpp:284-309:
    StreamMan, TexMan, RenderSlotMan, SurfaceRender, ParticlePool, 2D-viewport)
  - `LoadGfx(A3DDevice* pDev, const AString& strPath, bool bCache=true, bool bLoadInThread=true)` (356)
  - `Tick(DWORD dwDeltaTime)` (359), `RegisterGfx(A3DGFXEx*)` (373), `RenderAllGfx(A3DViewport*)` (502)
  - клієнт створює його у `CECGame::Init` (EC_Game.cpp:391-396)

### 3.2 Шлях до файла
`A3DGFXExMan::LoadGfx` (A3DGFXExMan.cpp:702, 772) **додає префікс**:
```cpp
pGfx->Load(pDev, "Gfx\\" + strPath)
```
→ аргумент = шлях відносно кореня папки `gfx\`. Приклад: файл
`<root>\gfx\npc\boss_x.gfx` → `LoadGfx(pDev, "npc\\boss_x.gfx")`.
Або завантаження напряму, повним шляхом: `A3DGFXEx::Load(pDev, szFullpath)`.
Для реального `<root>\gfx\gfx\…` відносний аргумент починається з `gfx\…` —
це частина шляху, яку не можна безумовно видаляти. Resolver checker перевіряє
існування повного відносного шляху перед видаленням необов'язкового префікса.

Для файлового доступу також важлива поточна робоча папка процесу. `A3DGFXEx::Load` передає
`Gfx\...` в `AFileImage::Open`, а `AFileImage::Init` відкриває відносний шлях через `fopen`;
тому робочою папкою має бути корінь ассетів. Самого `af_SetBaseDir(root)` недостатньо.

### 3.3 Кадровий цикл ефекту (референс — ImageCacheLib)
`client\CElement\ImageCacheLib\ICLImgImplGfx.cpp:62-151` (`ImgImpl_Gfx::ReadFromFile`) —
**готовий алгоритм "завантажити → навести камеру → offscreen-рендер → зняти знімок"**:
```cpp
// 1) відносний шлях від <basedir>\Gfx
AString strGfxBase = af_GetBaseDir(); strGfxBase += "\\Gfx";
af_GetRelativePathNoBase(strFilename, strGfxBase, m_strGfx);
// 2) завантаження (параметри → LoadGfx helper, PlatformIndependFuncs.cpp:150)
m_pGfx = LoadGfx(m_pDevice, m_pGfxMan, m_strGfx);   // = pGfxExMan->LoadGfx(pDev, szGfxFile)
if (!m_pGfx || !m_pGfx->Start()) return false;
// 3) камера по AABB оригіналу
A3DAABB aabb = m_pGfx->GetAABBOrg(); … // обчислення vPos, vLookAt
m_pCamera->SetPos(vPos); m_pCamera->SetDirAndUp(vDir, A3DVECTOR3(0,1,0));
// 4) рендер в offscreen RT
m_pEngine->BeginRender();
ApplyRenderTarget(m_pDevice, m_pRenderTarget);     // pDev->SetRenderTarget(pRt)
m_pViewport->Active(); m_pViewport->ClearDevice();
for (int i = 0; i < 10; ++i) {
    m_pGfx->TickAnimation(50);                     // 10 кадрів по 50 мс
    m_pGfxMan->RegisterGfx(m_pGfx);
    m_pGfxMan->RenderAllGfx(m_pViewport);
}
m_pEngine->EndRender();
RestoreRenderTarget(m_pDevice, m_pRenderTarget);   // pDev->RestoreRenderTarget()
m_pGfx->Stop();
CreateXImageFromRT(m_pDevice, m_pRenderTarget, m_pXImage); // → CxImage → PNG
```
`A3DGFXEx::TickAnimation` отримує активну камеру через `AfxGetA3DCamera()` і використовує її для обчислення напрямку/відстані до ефекту (`GfxCommon\A3DGFXEx.cpp`). У клієнті callback повертає камеру viewport (`CElementClient\EC_Utility.cpp`); обгортка має встановити його на камеру preview до першого тіку.

Важливо: `A3DGFXExMan::Tick` (`A3DGFXExMan.cpp:897`) обслуговує fade-out і 2D-map,
а не всі вручну завантажені 3D-ефекти. `InnerRenderGfx` (`:137`) очищує чергу після
рендеру, тому `RegisterGfx` потрібний на кожному кадрі. Інтерактивний режим очищує
backbuffer щокадру. Offscreen-референс очищує RT лише перед усім циклом і накопичує
результат N кроків; це не ізольований знімок лише останнього кроку.

Поточний `RenderPreviewGfx` після реєстрації викликає `RenderAllGfx(viewport)`,
`Render2DGfx(true)`, `Render2DGfx(false)`. `RegisterGfx` маршрутизує 2D в окремі
черги (`A3DGFXExMan.cpp:169-188`), які обслуговує `Render2DGfx` (`:554-623`).
Ця послідовність використовується і у вікні, і для PNG. Внутрішній 2D-viewport
бере розміри з `GetClientRect` (`:327-354`), тому приховане вікно створюється
з розмірами RT, а не 1×1. `Position2DEffect` розташовує центр масштабованого
локального AABB в центрі екрана; світову камеру для цього не використовує.

### 3.4 Offscreen render target (варіант Angelica 2.x)
`client\CElement\ImageCacheLib\PlatformIndependFuncs.cpp`:
- `CreateRenderTarget` (11-33): під `_ANGELICA2` → `A3DDEVFMT rtfmt{ nWidth, nHeight,
  fmtTarget=A3DFMT_A8R8G8B8, fmtDepth=A3DFMT_D24X8 };` → `pRT->Init(pDev, rtfmt, true, false)`
- `ApplyRenderTarget` (36-45): `pDev->SetRenderTarget(pRt)`
- `RestoreRenderTarget` (47-54): `pDev->RestoreRenderTarget()`
- `CreateXImageFromRT` (57-147): `CreateOffscreenPlainSurface(…, D3DPOOL_SYSTEMMEM)` →
  `GetRenderTargetData()` → `LockRect(D3DLOCK_READONLY)` → `CxImage::CreateFromArray(...,32,Pitch,true)`
  → `pTarget->AlphaSet(255)`; далі зберігання CxImage→PNG.
- Під `_ANGELICA22`-варіант: `A3DRenderTarget::RTFMT{ iWidth, iHeight, fmtTarget, fmtDepth }`
  та `pRt->ApplyToDevice()/WithdrawFromDevice()` — див. `client\CElement\GfxCommon\A3DGFXPIFuncs.cpp:52-104`.
- У поточному checker використовується фактичний API SDK Angelica 2.2:
  `A3DRenderTarget::Init(device, A3DDEVFMT&, true, false)`, `SetRenderTarget`,
  `RestoreRenderTarget` і `GetTargetSurface`. Readback повторює D3D9-послідовність
  вище, а Alpha примусово встановлюється в 255, як у клієнтському `AlphaSet(255)`.
  FPS overlay вимикається через `A3DConfig::RT_SetShowFPSFlag(false)`, як у
  `CElementClient/EC_Game.cpp:1598`, щоб службовий текст рушія не потрапляв у кадр.
  Для PNG-кодування обрано системний Windows Imaging Component (WIC).
  `CxImage.lib` є в SDK, але поточний exe не використовує її як PNG-кодек.
  Перед записом checker рахує ненульові RGB-пікселі, максимум каналу та логує
  `VISIBLE/EMPTY`; альфа 255 не враховується у визначенні порожнього зображення.
- Інтерактивна орбітальна камера використовує `A3DCamera::SetPos/SetDirAndUp`
  (`SDK/Angelica3D/Header/A3DCameraBase.h`). Центр береться з `GetAABBOrg()`,
  `GetActualScale()` та `GetPos()`. Початкове кадрування описане в §3.6;
  MMB-drag змінює yaw/pitch, W/S — радіус.
  Кути біля полюсів обмежені, відстань не переходить через pivot.
  Рендер іде у віконний backbuffer, ImGui малюється після GFX до `EndRender`.
- Інтерактивне вікно приймає `WM_DROPFILES` (Shell `DragAcceptFiles`); шлях перевіряється
  відносно активного asset root, конвертується у GBK і завантажується через той самий
  `A3DGFXExMan::LoadGfx`. Після успішного `Start` попередній екземпляр зупиняється та
  звільняється; помилки завантаження логуються, а поточний GFX лишається активним.
- Інтерактивне вікно має `WS_OVERLAPPEDWINDOW` і змінюваний розмір. Після завершення
  перетягування рамки `A3DEngine::SetDisplayMode(..., SDM_WIDTH | SDM_HEIGHT)` скидає
  backbuffer; перед Reset звільняються DX9-ресурси ImGui, після нього оновлюються
  параметри viewport, aspect ratio камери, 2D-viewport GFX та ресурси ImGui. Під час
  live resize Reset відкладений до `WM_EXITSIZEMOVE`, щоб не скидати пристрій на кожному
  проміжному `WM_SIZE`.

### 3.5 Статистика та ImGui

`A3DGFXEx.h`: `GetElementCount/GetElement` (:299), `GetAABBOrg` (:331),
`GetTimeElapse` (:342), `GetState`, `IsResourceReady`, `GetActualScale/Alpha/Speed`.
Активність елемента — `A3DGFXElement::IsActive()`.

`A3DGFXExMan.cpp:1235-1273`: `GetTotalDrawCount`, `GetTotalPrimCount`,
`GetParticleTotalCount`, `GetParticleRenderCount`, `ResetStatCount`.
У `RunInteractivePreview` лічильники скидаються перед tick/render, читаються після
GFX та до ImGui. `GetParticleTotalCount` рахує виділені частинки
(`A3DParticleSystemEx.cpp:190,268`), а не лише живі. Час CPU вимірюється через
`steady_clock` окремо навколо `TickAnimation` і реєстрації/рендеру GFX.

Dear ImGui v1.91.9b: офіційні `imgui_impl_win32` + `imgui_impl_dx9`.
`PreviewOverlay` володіє контекстом/бекендами, перенаправляє Win32-повідомлення,
враховує `WantCaptureMouse/Keyboard`; DX9-бекенд зберігає та відновлює D3D-стан.
Патерн: upstream `examples/example_win32_directx9/main.cpp:44-55,113-116,158-169`.
Файлове ім'я для UI конвертується GBK → UTF-16 → UTF-8; `imgui.ini` не записується.

### 3.6 Хто створює камеру/viewport/RT для прев'ю gfx
`client\CElement\ImageCacheLib\ICLImgImpl.cpp:105-176` (`CreateImg(...)`): для `.gfx`
створюється `new ImgImpl_Gfx(pDevice, AfxGetGFXExMan(), pDCamera, pDViewport, pRenderTarget)`.
Клієнтські місця, де ці об'єкти створюються, шукати по `ICLTaskProcFolder`/`ICLImgMan`
(`client\CElement\ImageCacheLib\ICLTaskProcFolder.cpp:88`).

Поточна адаптація в `src/EngineProbe.cpp`:

- `A3DCamera::Init(device, DEG2RAD(65), 0.2f, 2000.0f, width/height)`;
  near/far — з `CElementClient/EC_Game.cpp:1636-1637`, aspect — за
  `A3DPlatform/ECViewport.cpp:84`.
- Центр масштабованого AABB та відстань `2.2 * |extents|` — за патерном
  `AUInterface/AUIModelPicture.cpp:164-169`, із нижньою межею `radius + near*1.1`.
  Плоскі ефекти (`extents.y < 0.1`) оглядаються згори за напрямком із
  `ImageCacheLib/ICLImgImplGfx.cpp:103-107`.
- Для нульового/нечислового радіуса береться 1 і позиція GFX як центр.
  Це запасне кадрування не гарантує, що ефект без коректного AABB вміститься у кадр.
  Великі 2D-ефекти центруються, але автоматично до розміру вікна не масштабуються.

### 3.7 Реалізовані callback-и та залишкові заглушки

`src/ClientCompat.cpp` адаптує редакторські реалізації `GfxCommon/A3DGFXExMan.cpp`:

| Callback | Поточна поведінка | Джерело |
|---|---|---|
| `AfxLoadA3DSkinModel/AfxReleaseA3DSkinModel` | `Init` → `Load`, `Release` → `delete`; лог результату | :1387-1405 |
| `AfxGetModelUpdateFlag` | `true` | :1407-1410 |
| `AfxSetA3DSkinModelSceneLightInfo` | Точкове світло зі списку GFX | :1412-1433 |
| `AfxGetLightparam` | Редакторські directional diffuse/specular/direction | :1450-1473 |
| `AfxGetGrndNorm` | Висота 0, нормаль `(0,1,0)` | :1439-1447 |
| `gGfxGetSurfaceData` | Редакторська площина: 4 вершини, 6 індексів | :1527-1546 |
| `AfxGetAverageFrameRate` | Редакторське значення 30, не виміряний FPS | :1522-1525 |

`AfxGetECModelMan()` досі повертає `nullptr`, `AfxGetECMHullPath()` — порожній рядок.
Звук, raytrace, тіні, camera shake та бойові callback-и не реалізують runtime світу.
`AfxPlaySkillGfx` логує відсутність підтримки й повертає `false`.
Warp-черга не має окремого проходу в checker. Підключення SMD не означає підтримку ECM.

## 4. Формат файла .gfx (сумарно)

- Перевірені текстові файли починаються з **`MOXTVersion`**, далі йдуть поля:
  `DedaultScale, PlaySpeed, DefaultAlpha, Raytrace, UseAABB, ShakeCam, 2DRender,
  GFXELEMENTCOUNT, GFXELEMENTID, Name, SrcBlend, DestBlend, TexFile, TexRow/TexCol,
  BindEle, ZEnable, MatchGrnd …` (елементи: партикли, декалі, моделі, звуки, блискавки, кільця).
- Рушій має текстову та бінарну гілки читання; розбір залишено готовій бібліотеці.
  Для текстової гілки `LC_NUMERIC="C"` обов'язковий: координати з крапкою читаються
  через `sscanf`, зокрема `GfxCommon/A3DGFXEx.cpp:534-537`.
- `TexFile` може містити GBK-підпапки. `A3DGFXSharedTexMan::QueryTexture`
  (`GfxCommon/A3DGFXTexMan.cpp:6-16`) використовує папку `A3DGFXMan`, типово
  `Gfx\Textures` (`SDK/Angelica3D/Source/A3DGFXMan.cpp:44`).
- Елемент `A3DGFXModel` завантажує **SMD** через `A3DGFXModelMan` і
  `AfxLoadA3DSkinModel`; префікс `gfx\Models\` додається в `A3DGFXModel.cpp:377-383`.
  **ECM** — окремий `A3DGFXECModel`/`CECModel`, чий менеджер у checker ще не підключено.
- Розширення `.gfx` не гарантує формат окремого `A3DGFXEx`: у `sgc\` знайдено
  сценарій навички з `MOXTVersion: 4`, `Path`, `MoveMode`, `TargetMode`. Поточний
  loader дав для нього 0 елементів та порожній PNG, а не помилку формату.
- Звук — `A3DGFXSound::Load` завжди `true` (GfxCommon\A3DGFXSound.cpp:27-33) → звук
  не блокує завантаження; відтворення вимагає AM-звукового движка.

## 5. Константи/дефайни версії

- Клієнт ElementClient (Application): `NDEBUG;_WINDOWS;UNICODE;_ELEMENTCLIENT;DX9;ANGELICA_2_2;D3DPTEXTURECAPS_NONPOW2CONDITIONAL;LOGIC_CHECK`
- ImageCacheLib: `…;_ANGELICA2;…` (ветка DX9-2.x у ICL)
- A3DPlatform: `…;_ANGELICA_2_2;…`
- Наш checker: `DX9;ANGELICA_2_2;UNICODE;_MBCS`. `UNICODE` потрібний для ABI `ACHAR`
  у готових `.lib`; `CharacterSet=MultiByte` не скасовує цей явний дефайн.
  `_ANGELICA2`/`_ANGELICA22` не визначені: адаптований RT-код напряму викликає API
  фактичних SDK-заголовків. Не вмикати іншу гілку лише через назву версії рушія.
