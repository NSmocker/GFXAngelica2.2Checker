# 02 — Середовище, шляхи, залежності

## 1. Абсолютні шляхи

| Що | Шлях |
|---|---|
| Код движка і клієнта (джерело правди) | `B:\Git\PW_ClientSource` |
| Розпаковані ассети гри | `B:\Git\PW146RUUnpackedAssets` |
| GFX-файли для тестів | `B:\Git\PW146RUUnpackedAssets\gfx\` — **9019 файлів**, підпапки `gfx\, npc\, models\...`; імена файлів **GBK** |
| Готові статичні .lib (Release, x64 і Win32) | `B:\Git\PW_ClientSource\SDK\lib\{Win32,x64}\{Debug,Release}\` |
| Сторонні .lib (збірка SDK) | `B:\Git\PW_ClientSource\SDK\3rdSDK\lib\{Win32,x64}\{Debug,Release}\` |
| DirectX SDK (June 2010), локальна копія | `B:\Git\PW_ClientSource\DXSDK` (Include\, Lib\x86, Lib\x64); є і змінна середовища `DXSDK_DIR = C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\` |
| Рішення клієнта | `B:\Git\PW_ClientSource\perfectworld.sln` (toolset v143, VS 2022) |
| Конфіг шейдерів скін-моделей | `B:\Git\PW146RUUnpackedAssets\configs\2.2\skinmodelvs.cfg` (ієрархія `configs\` повна) |

## 2. Структура ассетів (корінь `B:\Git\PW146RUUnpackedAssets`)

```
gfx\          — GFX-ефекти (наші тестові файли)
gfx\Textures\ — стандартна база TexFile для GFX
gfx\Models\   — SMD-моделі елемента A3DGFXModel
models\       — ECM-моделі, на які посилаються gfx
textures\     — текстури (.dds/.png/.tga)
shaders\      — HLSL-шейдери движка
surfaces\     — матеріали поверхонь
data\, maps\, sfx\, configs\, interfaces\, font\, fonts\, cursors\,
grasses\, litmodels\, loddata\, physx\, trees\, bbsfiles\, BBSPages\, facedata\, info\...
```

**Важливо:**
- Windows зберігає імена в Unicode; вузькі рядки шляхів у GFX/AFI — **GBK (936)**.
  Поточний CLI читає UTF-16 через `wmain`, перевіряє шляхи Win32 `W`-функціями та
  передає в рушій GBK. `setlocale(LC_CTYPE, ".936")` і консольний CP 936 не змінюють
  системний ACP для Win32 `A`-викликів. Конвертація китайських імен у CP1251 втрачає
  символи й не є розв'язанням проблеми. Поточний runtime перевірено з ACP 936.
- **Числа:** `LC_NUMERIC="C"` задається окремо. Текстові GFX містять десяткову крапку,
  а `sscanf` рушія залежить від CRT-локалі. Попередній `setlocale(LC_ALL, ".936")`
  у середовищі з десятковою комою ламав координати й параметри; виправлення перевірене PNG-прогоном.
- У перевірених ассетах є текстові GFX із заголовком `MOXTVersion`, полями
  `GFXELEMENTCOUNT`, `TexFile` та GBK-рядками. Рушій підтримує також бінарну гілку;
  сам префікс `MOXT` не робить файл бінарним. Формат читає готовий GfxCommon.
- `A3DGFXSharedTexMan::QueryTexture` (`GfxCommon/A3DGFXTexMan.cpp:6-16`) використовує
  `GetA3DGFXMan()->GetTextureFolder()`. Типова база — `Gfx\Textures`
  (`SDK/Angelica3D/Source/A3DGFXMan.cpp:44`), тобто `TexFile` може містити підпапки
  відносно `<root>\gfx\Textures`, а не лише коротке ім'я.
- Підпапка `<root>\gfx\gfx` справді існує. Resolver спершу перевіряє шлях від кореня
  `gfx`, і лише за його відсутності прибирає необов'язковий префікс `gfx\`.
  Пакетний скрипт передає абсолютні шляхи, щоб однозначно обрати файл.
- Для повної коректності в реальному середовищі необхідний GBK-локалізований Windows (zh-CN / codepage 936).
  У цьому середовищі `char*`-шляхи AFI/GFX інтерпретуються через ACP, тому `встановлення` UTF-8/1251 на хості
  знищує розпізнавання китайських імен asset-файлів. Для перевірки `--preview` треба запускати на машині з
  GBK локаллю; інакше рушій відкриє файли неправильно навіть при правильному відносному шляху.
- `af_SetBaseDir()` не замінює поточну робочу папку для всіх файлових операцій: `A3DGFXEx::Load` передає
  відносний шлях `Gfx\...` до `AFileImage::Open`, а `AFileImage::Init` відкриває його через `fopen`.
  Перед ініціалізацією рушія поточна робоча папка має бути коренем ассетів (де є `Gfx\`, `textures\` тощо).

## 3. Залежності збірки (лінк)

Бібліотеки, які потрібні мінімально для `.gfx` (всі готові у `SDK\lib\…\Release\`):

| .lib | Що дає |
|---|---|
| `Angelica3D.lib` (163 МБ) | движок: A3DEngine, A3DDevice, A3DCamera/Ortho, A3DViewport, A3DRenderTarget, A3DTexture/Surface/TextureMan, A3DSkinMan, A3DLitModelRender |
| `GfxCommon.lib` (225 МБ) | A3DGFXEx, A3DGFXExMan, всі A3DGFX* елементи, EC_Model (CECModel), GfxEditorUtil |
| `AngelicaCommon.lib` | AString, математика, лог, базові об'єкти |
| `AngelicaFile.lib` | AFI (af_Initialize…), AFileImage, паки |
| `AngelicaMedia.lib` | AMMedia — звук движка (опціонально для чекера) |
| `ImmWrapper.lib` | IME-обгортка (тягнеться компілятивно) |
| `FTDriver.lib` + `freetype.lib` | шрифти (потрібні, якщо рендеримо текст у прев'ю) |
| WIC (`windowscodecs.lib`, `ole32.lib`) | PNG-кодування кадру; D3D9 readback наслідує патерн ICL |
| `zlibwapi_dynamic.lib`/`zlibwapi.lib` | zlib-пакунки |
| `d3d9.lib`, `d3dx9.lib`, `dxguid.lib` | Direct3D 9 |
| `dsound.lib`, `winmm.lib`, `msimg32/imm32.lib` | системні |

**DLL під час запуску** (копіювати біля exe; є у `B:\Git\PW_ClientSource\Win32\Release\` і `x64\Release\`):
Поточна ціль `CopyRuntimeLibraries` копіює `FTDriver.dll`, `SpeedTreeRT.dll`,
`ImmWrapper.dll` з відповідної платформи/конфігурації. У перевіреного x64 exe
прямий імпорт із цього набору — `ImmWrapper.dll`; `d3dx9_43.dll` і MSVC CRT
мають бути встановлені в системі. Додаткові клієнтські DLL не слід копіювати
автоматично без перевірки фактичних імпортів і динамічного завантаження.

**ImGui:** вихідний код v1.91.9b у `third_party/imgui`, офіційні Win32/DX9-бекенди,
MIT-ліцензія. Для китайських назв панель пробує системні `msyh.ttc`, `simsun.ttc`,
потім `segoeui.ttf`/стандартний шрифт. Атлас містить лише потрібні гліфи поточного імені.

**Пакетний скрипт:** Python 3 + Pillow (перевірено з Python 3.14). Оглядові PNG
використовують `C:/Windows/Fonts/segoeui.ttf`. На початку прогону скрипт копіює exe
та DLL поруч із ним у `<out>/runtime`, фіксує SHA-256 exe в `run.json` і зберігає
список файлів у `manifest.json`. Це не залежності самого C++-переглядача.

## 4. Середовище збірки

- `scripts/build.ps1` обирає VS 2022 (v17, toolset v143), навіть якщо встановлено VS 18.
  Команда для checker: `powershell -File scripts/build.ps1`.
  (документовано у `PW_ClientSource\project_info.html`; логи успішних білдів у корені:
  `msbuild_release_x86_success.log`, `msbuild_release_x64_final.log`).
- Бібліотеки клієнта вже **зібрані** — новий exe лінкується напряму, без перезбірки рушія.
- У рішенні checker використовуються `Win32` і `x64`; лише шлях DirectX SDK
  відображає `Win32` у `Lib\x86` через властивість `DxPlatform`.

## 5. Ризики/підводні камені

1. **GBK-шляхи** — головний ризик; тестувати на файлах з китайськими іменами.
2. `LoadGfx` з кешем (`bCache=true`) і мультипотоком (`bLoadInThread=true`) — для
   детермінованого чекера використовувати `LoadGfx(pDev, path, false, false)`.
3. Звук: `A3DGFXSound::Load` повертає `true` завжди (читання полів), але відтворення
   вимагає ініціалізованого AM-звукового движка; для чекера звук вимикати.
4. Скін-шейдери: без `LoadSkinModelVertexShaders("Configs\\2.2\\skinmodelvs.cfg")` ефекти з
   моделями рендеряться чорними/не рендеряться. Файл є в ассетах — орієнтуватись на
   корінь ассетів як робочу директорію (или af_SetBaseDir на корінь ассетів).
5. У ICL-референсі `AfxGetGFXExMan()` — глобальний менеджер клієнта; у чекері створюємо
   власний `A3DGFXExMan` (як `CECGame::Init`, `EC_Game.cpp:391-396`).
6. Непорожній PNG не підтверджує завантаження всіх залежностей. Відсутні компоненти,
   непідтримуваний runtime, затримка старту та невдале кадрування розбираються за
   PNG і логами; поточні результати — у [07-gfx-audit.md](07-gfx-audit.md).
