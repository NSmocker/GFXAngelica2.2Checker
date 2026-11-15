# GFXAngelica2.2Checker

Переглядач GFX-ефектів Perfect World на готових бібліотеках Angelica 2.2 / Direct3D 9.
Поточна версія **0.4.0**: інтерактивне вікно з орбітальною камерою й ImGui-статистикою,
одиночний PNG, 3D/2D-черги, SMD-моделі та читання списків. Пакетний прогін із PNG,
CSV і перевіркою порожніх зображень доступний через `scripts/check-gfx.py`.
Стан документації: **2026-11-15**; рядок версії exe залишається `0.4.0 (orbit viewer)`.

## Збірка

Потрібні VS 2022 C++ (v143), Windows SDK, готові бібліотеки та DirectX SDK у
`B:\Git\PW_ClientSource`. З кореня проєкту:

```powershell
.\scripts\build.ps1
```

Результат: `bin\x64\Release\GFXAngelica2.2Checker.exe`.
Для поточного файлового runtime і китайських імен потрібний Windows ACP **936**.
Пояснення: [середовище](docs/02-environment-and-paths.md), [збірка](docs/04-build.md).

## Використання

```powershell
$checker = '.\bin\x64\Release\GFXAngelica2.2Checker.exe'
& $checker --interactive --root 'B:\Git\PW146RUUnpackedAssets' --gfx 'npc\中式婚礼凤凰花炮.gfx' --width 960 --height 720
& $checker --preview --root 'B:\Git\PW146RUUnpackedAssets' --gfx 'npc\中式婚礼凤凰花炮.gfx' --output preview.png
& $checker --self-test --root 'B:\Git\PW146RUUnpackedAssets'
& $checker --list gfx_list.txt
```

Також можна перетягнути `.gfx` на `run-interactive.bat` або `run-preview.bat`.
Працююче інтерактивне вікно приймає один `.gfx` з активного кореня ассетів.

| Керування | Дія |
|---|---|
| Межа вікна | Змінити розмір; рендер і панель підлаштовуються після завершення зміни |
| Середня кнопка миші + перетягування | Орбіта навколо центра ефекту |
| W / S | Ближче / далі |
| Frame effect у панелі | Повернути кадрування |
| Esc поза захопленням клавіатури ImGui / закриття вікна | Завершити перегляд |

Панель **GFX statistics** показує елементи, частинки, draw calls, примітиви, стан,
час ефекту, FPS і CPU-таймінги. Розділ **Effect details** містить AABB та параметри GFX.
Курсор можна використовувати для панелі; захоплений нею ввід не рухає камеру.
PNG зберігається без ImGui, а `checker.log` — біля exe.
Рушій читає GBK-шляхи з `LC_CTYPE=".936"`, а дробові числа GFX — з `LC_NUMERIC="C"`.
Offscreen PNG накопичує N кроків по 50 мс; інтерактивне вікно очищується щокадру.

## Пакетна перевірка

Потрібні Python 3 і Pillow; для самого exe Python не потрібний. Запуск із кореня:

```powershell
python scripts\check-gfx.py --out build\gfx-audit\sample-new --limit 240 --seed 20261115 --retry-frames 100
```

Скрипт бере вибірку з `B:\Git\PW146RUUnpackedAssets\gfx`, запускає кожен GFX в окремому
процесі, зберігає PNG і логи, формує CSV та оглядові зображення. Типовий рендер —
10 кроків; порожній кадр у цій команді повторно перевіряється зі 100 кроками.
Для кожного прогону потрібна нова вихідна тека. Повторення точної вибірки, параметри
та схема звіту — у [протоколі GFX-перевірки](docs/07-gfx-audit.md).

Перевірений результат: **43/48** непорожніх PNG у контрольній вибірці та **212/240**
у розширеній; в обох немає збоїв/таймаутів. Вибірки перекриваються — разом це
265 різних файлів із 9019. `VISIBLE` означає наявність ненульових RGB-пікселів,
а не підтвердження повної відповідності грі.

## Межі поточної реалізації

Завантаження SMD та 2D-рендер реалізовані; повний ECM/runtime світу, warp-прохід,
сценарії навичок, рух для слідів зброї й відновлення втраченого D3D-пристрою ще не реалізовані.
Порожні кадри розширеної вибірки потребують подальшого розбору. Великий/нульовий
AABB та накопичення кадрів можуть давати невдале кадрування або пересвічений PNG.
`--list` лише друкує записи; відновлення після SEH усередині одного exe відсутнє,
а пакетний скрипт ізолює файли окремими процесами.
Відомі помилки CLI/списків та точна семантика PNG описані у
[специфікації](docs/01-project-overview.md); стан робіт — у [плані](docs/05-implementation-plan.md).

## Тести

У Developer PowerShell для VS 2022:

```powershell
msbuild tests\orbit-camera-tests.vcxproj /p:Configuration=Release /p:Platform=x64
.\build\tests\orbit-camera-tests.exe
```

Dear ImGui **v1.91.9b** постачається як вихідний код з [MIT-ліцензією](third_party/imgui/LICENSE.txt).
Джерело та склад: [third_party/imgui/README.md](third_party/imgui/README.md).
