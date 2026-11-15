# 04 — Збірка (build)

## 1. Інструменти

- **MSBuild** VS 2022 (`v143`), C++17, Windows SDK `10.0`.
- Приклад команди (у корені рішення):
  ```
  msbuild gfxchecker.sln /m /p:Configuration=Release /p:Platform=x64
  ```
  (клієнтські логи успіху: `PW_ClientSource\msbuild_release_x86_success.log`,
  `msbuild_release_x64_final.log`; довідка: `PW_ClientSource\project_info.html`).
- Платформи: `x64` (основна) та `Win32`. Конфігурації: `Debug`, `Release`.
- Поточна перевірена конфігурація — **x64 Release**. `scripts/build.ps1`
  знаходить VS 2022 через `vswhere`; типовий запуск із кореня: `powershell -File scripts/build.ps1`.
  Бінарні файли: `bin\x64\Release\`; проміжні: `build\x64\Release\`;
  журнал: `build\logs\x64-Release.log`. Готові бібліотеки не перезбираються.
- Остання перевірена збірка виправлень рендеру від **2026-11-15** виконана прямим
  MSBuild; її лог — `build\audit-build.log`. Цей exe використаний у завершених
  прогонах `regression` та `expanded`, його SHA-256 записано в їхніх `run.json`.
  Рядок версії лишився `0.4.0 (orbit viewer)`.

## 2. Проєкт Application (наш checker) — параметри

### 2.1 Defines (Reference: ElementClient.vcxproj Release, але без клієнтських)
```
WIN32;_WINDOWS;_CONSOLE;DX9;ANGELICA_2_2;UNICODE;_CRT_SECURE_NO_WARNINGS;NDEBUG
```
- `DX9` — обов'язково (ветки рушія).
- `ANGELICA_2_2` — шлях до шейдерів `Configs\\2.2\\skinmodelvs.cfg`.
- **CharacterSet = MultiByte** додає `_MBCS`; явний `UNICODE` потрібний для ABI готових
  бібліотек (`ACHAR=wchar_t`), як у ElementClient. `RuntimeLibrary` = /MD (Release), /MDd (Debug).
  Для Debug замість `NDEBUG` використовується `_DEBUG`. Власні джерела — UTF-8 (`/utf-8`).
- Поточна адаптація ICL викликає API SDK напряму; `_ANGELICA2` і `_ANGELICA22` не задані.
- **Точність-гарантія для GFX**: якщо перевірка на реальному клієнтському патерні показує, що логіка рендера/ініціалізації/камери/RT повинна бути ідентичною, допустиме копіювання вихідних файлів або їх фрагментів з `PW_ClientSource` в цей проєкт. Копіювання не є "порушенням" — це спосіб зберегти ідентичну поведінку ефектів.

### 2.2 Include dirs (відносно каталогу проєкту, див. GfxCommon.vcxproj)
```
B:\Git\PW_ClientSource\SDK\AngelicaCommon\header
B:\Git\PW_ClientSource\SDK\AngelicaFile\header
B:\Git\PW_ClientSource\SDK\Angelica3D\Header
B:\Git\PW_ClientSource\SDK\AngelicaMedia\header
B:\Git\PW_ClientSource\SDK\ImmWrapper\header
B:\Git\PW_ClientSource\SDK\Include
B:\Git\PW_ClientSource\client\CElement\include\GfxCommon
$(WindowsSDK_IncludePath)
B:\Git\PW_ClientSource\DXSDK\Include
$(MSBuildProjectDirectory)\third_party\imgui
```
Windows SDK має передувати старому DirectX SDK у пошуку системних заголовків.
`ClientSourceRoot` і `DirectXSdkRoot` можна перевизначити властивостями MSBuild;
типовий `DirectXSdkRoot=$(ClientSourceRoot)\DXSDK`.

### 2.3 Lib dirs
```
B:\Git\PW_ClientSource\SDK\lib\$(Platform)\$(Configuration)
B:\Git\PW_ClientSource\SDK\3rdSDK\lib\$(Platform)\$(Configuration)
B:\Git\PW_ClientSource\DXSDK\Lib\$(DxPlatform)
```
`DxPlatform=x86` для Win32, `DxPlatform=x64` для x64.

### 2.4 Поточні link dependencies
```
AngelicaCommon.lib AngelicaFile.lib AngelicaMedia.lib Angelica3D.lib GfxCommon.lib
AudioEngine.lib FWEditor.lib LuaWrapper.lib ElementSkill.lib ImmWrapper.lib AUInterface.lib
CHBasedCD.lib FTDriver.lib AutoMoveImp.lib SpeedTreeRT.lib mpg123lib.lib ImageCacheLib.lib
ogg_static.lib vorbis_static.lib vorbisfile_static.lib strmbase.lib async.lib lua5.1.lib
zlibwapi.lib zlib.lib d3d9.lib d3dx9.lib dxguid.lib windowscodecs.lib ole32.lib shell32.lib
dsound.lib winmm.lib ws2_32.lib wininet.lib shlwapi.lib psapi.lib imm32.lib ddraw.lib
version.lib imagehlp.lib glu32.lib
```
Джерело актуального набору — `gfxchecker.vcxproj`. Частина бібліотек потрібна через
транзитивні посилання готового GfxCommon; їх наявність у списку не означає підтримку
відповідних клієнтських підсистем. Звукові callback-и checker є заглушками,
але `A3DEngine::Init` ініціалізує AngelicaMedia; повне вимкнення звуку ще в плані.

### 2.5 ImGui

У `third_party/imgui` збережено вихідні файли Dear ImGui **v1.91.9b** і MIT-ліцензію.
Core та бекенди Win32/DX9 компілюються у цей exe; окрема DLL/готова `.lib` не потрібна.
Сторонній код не змінений. Збірка не завантажує залежності з мережі.

## 3. Runtime (щоб exe запустився)

Preview приймає явний `--root` і тимчасово встановлює його робочою папкою процесу.
Відносний шлях PNG розв'язується до цього переходу, від теки запуску.
Ціль MSBuild копіює поряд з exe DLL з `PW_ClientSource\<Platform>\<Configuration>\`:
```
FTDriver.dll  SpeedTreeRT.dll  ImmWrapper.dll
```
Для перевіреного x64 exe потрібна системна `d3dx9_43.dll` та MSVC runtime.
Імпорти можна перевірити `dumpbin /dependents bin\x64\Release\GFXAngelica2.2Checker.exe`.
Не підкладати DLL іншої архітектури. Для китайських шляхів поточного runtime потрібний ACP 936;
див. `02-environment-and-paths.md` і `06-gbk-setup.html`.
Числова CRT-локаль окремо фіксується як `C`, а GBK — лише в `LC_CTYPE`.
Зміна на `setlocale(LC_ALL, ".936")` поверне помилку читання дробових координат
на хості з десятковою комою.

## 4. Чекліст збірки

1. `msbuild gfxchecker.sln /p:Configuration=Release /p:Platform=x64`
2. Помилки лінкера типу `LNK2019: unresolved A3D…` → бракує .lib у 2.4 або неправильна конфігурація lib (брати Release|<платформа> з того ж набору).
3. Помилки типу `cannot open file 'd3dx9.lib'` → перевірити `DXSDK\Lib\x64` у lib dirs.
4. `LNK4098` (runtime mismatch) → усі бібліотеки /MD (Release), не змішувати Debug-рухомі .lib з Release-exe.
5. CRT/STL: GfxCommon/Angelica3D зібрані C++17 (/std:c++17, /MD) — зберігати однаково.
6. `SAFESEH:NO` для Win32 (як у ElementClient) через старі об'єктники.
7. Відомі попередження готового SDK: C4828 у GBK-коментарях заголовків при `/utf-8`,
   LNK4099 через відсутній `lua5.1.pdb`, C4700 у `A3DTexture.cpp` під час LTCG.
   Вихідні SDK-файли для їх усунення не змінювати.
8. Окремий тест орбіти:
   `msbuild tests\orbit-camera-tests.vcxproj /p:Configuration=Release /p:Platform=x64`,
   потім `build\tests\orbit-camera-tests.exe`.

PNG-кодування в checker використовує системний Windows Imaging Component (WIC);
`windowscodecs.lib` та `ole32.lib` додаються як системні залежності, без `CxImage.lib`.
`shell32.lib` потрібна інтерактивному вікну для обробки перетягнутих файлів (`WM_DROPFILES`).

## 5. Залежності та запуск пакетного прогону

`scripts/check-gfx.py` не компілюється MSBuild. Потрібні Python 3 і Pillow
(перевірене середовище: Python 3.14); оглядові PNG використовують системний Segoe UI.
Для середовища без Pillow встановлення залежності: `python -m pip install Pillow`.

```powershell
python scripts\check-gfx.py --help
python scripts\check-gfx.py --out build\gfx-audit\sample-new --limit 48 --retry-frames 100
```

Збірка має завершитися до старту прогону. Скрипт копіює готові exe та DLL у
власну папку `runtime`; подальші білди вже не змінюють цей прогін.
Рендер напряму з `bin`/інтерактивне вікно треба закрити перед перезаписом того ж exe,
інакше linker може повернути `LNK1104`.
Структура артефактів, CSV, команди повторення та перевірені результати — у
[07-gfx-audit.md](07-gfx-audit.md). Python/Pillow не потрібні для одиночного preview.
