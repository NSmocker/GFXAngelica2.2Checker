# AGENTS.md — інструкція для AI-агентів

> **Проєкт:** GFXAngelica2.2Checker — утиліта-перевірник (.exe) для рендерингу GFX-ефектів
> клієнта Perfect World (движок **Angelica 2.2**, Direct3D 9).
> Вхід: текстовий файл-список з повними шляхами до `.gfx` файлів.
> Вихід: рендер кожного ефекту (offscreen → PNG) + протокол перевірки (лог).

---

## 1. Джерело правди та ассети (АБСОЛЮТНІ ШЛЯХИ)

| Призначення | Шлях |
|---|---|
| **Джерело правди (код движка/клієнта)** | `B:\Git\PW_ClientSource` |
| **Ассети гри (розпаковані)** | `B:\Git\PW146RUUnpackedAssets` |
| Тестові GFX-файли | `B:\Git\PW146RUUnpackedAssets\gfx\**\*.gfx` (9019 шт., імена в **GBK**) |
| Цей проєкт (утилита) | `B:\Git\GFXAngelica2.2Checker` |
| Рішення клієнта (збірка) | `B:\Git\PW_ClientSource\perfectworld.sln` |

**Правила:**
1. Ніколи не винаходь власні структури GFX — **копіюй патерни з PW_ClientSource** ( див. `docs/03-gfx-api-reference.md`).
2. Не змінюй вихідники в `PW_ClientSource`. Якщо щось треба адаптувати — копіюй код у цей проєкт і зазначай походження (шлях + рядок).
3. Пріоритетність: **точна поведінка GFX у переглядачі має відповідати оригінальній грі**. Якщо для цього потрібні точні ініціалізації, алгоритми, хуки, фрагменти runtime або обробка render target/viewport/light — **копіюємо вихідні файли або їх фрагменти в цей проєкт**, не вигадуємо альтернативну логіку.
4. Не перейменовуй/не перезбирай великі готові `.lib` — вони вже зібрані в `PW_ClientSource\SDK\lib\{Win32,x64}\{Debug,Release}\`.

## 2. Швидкий старт (найважливіші факти)

- **Мова/тулчейн:** C++, MSBuild, PlatformToolset `v143` (VS 2022), C++17, Windows SDK `10.0`, `DX9`, дефайн клієнта `ANGELICA_2_2`. Direct3D **9** (лише), DirectX SDK June 2010 (`DXSDK_DIR` або локально `PW_ClientSource\DXSDK`).
- **Головні бібліотеки для .gfx:** `GfxCommon.lib` (225 МБ, усі `A3DGFXEx/ExMan/Element…`), `Angelica3D.lib` (движок: `A3DEngine`, `A3DDevice`, `A3DCamera`, `A3DViewport`, `A3DRenderTarget`, `A3DTexture…`), `AngelicaCommon.lib`, `AngelicaFile.lib` (AFI файлова система), `AngelicaMedia.lib` (звук/субтитри). Готові у `PW_ClientSource\SDK\lib\<plat>\<cfg>\`.
- **Послідовність ініціалізації (перевірена по клієнту):**
  1. `af_Initialize(); af_SetBaseDir(<корінь ассетів>);`
  2. `new A3DEngine` → `engine->Init(hInst, hWnd, &devFmt, A3DDEV_ALLOWMULTITHREAD)`
  3. вікно: `A3DDEVICE`, viewport `A3DViewport::Init(pDev, &A3DVIEWPORTPARAM, …)`, камера `A3DCamera::Init(pDev)`
  4. світло: `CreateA3DLight(&A3DLIGHTPARAM)` + `pSkinMan->SetDirLight(...)`
  5. шейдери скін-моделей: `pSkinMan->LoadSkinModelVertexShaders("Configs\\2.2\\skinmodelvs.cfg")`
  6. `new A3DGFXExMan` → `pGFXExMan->Init(pDevice)`
- **Завантаження ефекту:** `pGFXExMan->LoadGfx(pDevice, strRelPath)` — шлях **ВІДНОСНО від кореня папки `gfx\`** (менеджер сам додає префікс `Gfx\`). Див. `A3DGFXExMan::LoadGfx` у `PW_ClientSource\client\CElement\GfxCommon\A3DGFXExMan.cpp:653`.
- **Кадровий цикл окремого 3D-ефекту:** `pGfx->Start()`; на кожен кадр `pGfx->TickAnimation(dt)` → `pGfxMan->RegisterGfx(pGfx)` → `RenderAllGfx(pViewport)`; зупинка `pGfx->Stop()`. `Tick()` менеджера обробляє fade-out/2D-списки і не замінює тік окремого 3D GFX; черга рендеру очищується після кадру (`A3DGFXExMan.cpp:137,897`).
- **Offscreen-рендер у PNG** — готовий референс: `PW_ClientSource\client\CElement\ImageCacheLib\ICLImgImplGfx.cpp` (клас `ImgImpl_Gfx::ReadFromFile`) + `PlatformIndependFuncs.cpp` (`CreateXImageFromRT` через `CxImage`).
- **Поточний preview:** після `RenderAllGfx` викликає `Render2DGfx(true/false)`; приховане вікно має розміри RT для внутрішнього 2D-viewport. PNG кодується через WIC, RT накопичує N кроків по 50 мс. `PNG pixels: ... status=EMPTY` не змінює успішний код виходу exe.
- **Поточне кадрування:** центр масштабованого AABB, відстань за `AUInterface/AUIModelPicture.cpp:164-169`, погляд згори для плоских ефектів за ICL. Near/far = 0.2/2000, aspect = width/height. SMD/light/ground callback-и адаптовані з редакторської гілки `GfxCommon/A3DGFXExMan.cpp:1387-1546`; `AfxGetECModelMan()` досі повертає `nullptr`.
- **Пакетна перевірка:** `scripts/check-gfx.py` запускає окремий процес на файл, зберігає PNG/логи/CSV та може повторити порожні кадри з більшим `--retry-frames`. `--list` у exe лише читає список. Результати та межі перевірки — у `docs/07-gfx-audit.md`.

## 3. Документація проєкту (читай у такому порядку)

1. `docs/01-project-overview.md` — специфікація утилити (txt + exe, режими, вихід).
2. `docs/02-environment-and-paths.md` — всі шляхи, залежності, DLL, особливості ассетів (GBK!).
3. `docs/03-gfx-api-reference.md` — API движка/GFX з точними файлами та рядками у PW_ClientSource.
4. `docs/04-build.md` — як зібрати: vcxproj-шаблон, defines, include/lib dirs, DLL для запуску.
5. `docs/05-implementation-plan.md` — план робіт і статус.
6. `docs/07-gfx-audit.md` — відтворення пакетних прогонів, підтверджені результати й нерозв'язані порожні кадри. Номер 06 зайнятий наявним `docs/06-gbk-setup.html`.

## 4. Робочі правила

- **Мова коду/коментарів:** українська в коментарях нового коду; ідентифікатори — англійською. Лог-повідомлення — англійською (ASCII).
- **Кодування даних:** рядки шляхів усередині GFX/рушія — **GBK (936)**. CLI використовує `wmain` і Win32 `W`-функції; на межі з рушієм передає `char*` у GBK. `CharacterSet=MultiByte` разом з явним `UNICODE` зберігає ABI готових `.lib` (`ACHAR=wchar_t`, як у ElementClient). `setlocale` не змінює системний ACP; для поточного файлового runtime потрібний ACP 936. Не перетворюй китайські імена у CP1251 із втратою символів.
- **Числова локаль:** зберігай `LC_NUMERIC="C"`; GBK задається лише через `LC_CTYPE=".936"`. `setlocale(LC_ALL, ".936")` може залишити регіональну десяткову кому й зламати `sscanf` для координат/параметрів GFX з крапкою. Це підтверджена причина порожніх кадрів.
- **Формат звіту перевірки:** див. `docs/01-project-overview.md` §3 (CSV + лог).
- Перед збіркою читай `docs/04-build.md`; перший білд роби для **x64|Release**, потім Win32 за потреби.
- Не комить у репозиторій бінарні ассети чи скопійовані `.lib`; посилайся на шляхи з PW_ClientSource.
- **Інтерактивний перегляд:** розмір вікна змінюється стандартною рамкою Windows; після завершення зміни розміру оновлюються D3D backbuffer, viewport, перспектива камери та 2D-viewport GFX. MMB-drag обертає камеру навколо центра GFX; W/S змінюють відстань. ImGui Win32/DX9 виводить статистику; `WantCaptureMouse/Keyboard` блокує керування камерою під час взаємодії з панеллю. Вихідні файли Dear ImGui зафіксовані в `third_party/imgui` разом із MIT-ліцензією.

## 5. Куди не лізти

- `PW_ClientSource\DXSDK\**`, `PW_ClientSource\SDK\FREETYPE-2.1.10\**` — сторонні бібліотеки, не редагувати.
- Згенеровані об'єктники (`*.obj`, `*.pch`) та `__pycache__` — ігнорувати.
