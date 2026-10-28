# Windows SDK integrations

Редактор запускается через `EditorBuild/MyEngine.exe`. Вместе с ним открывается консоль. Сообщения движка, stdout/stderr и callbacks SDK сохраняются в `%LOCALAPPDATA%/MyEngine/Diagnostics/Editor/editor-*.log`. `Ctrl+C` запрашивает завершение редактора. Для подробной покадровой трассировки перед запуском установи `MYENGINE_RENDER_TRACE=1`; обычный режим не печатает ожидание GPU каждый кадр.

Настройки сглаживания и апскейлинга находятся в Window → Graphics:

| Функция | Реализация |
|---|---|
| DLSS / DLAA | Streamline, jitter, depth, velocity, матрицы камеры, output texture |
| DLSS Ray Reconstruction | Diffuse/specular albedo, normal + roughness, расстояние отражённого луча, depth/velocity, DLSSD evaluation |
| DLSS-G | Streamline proxy swapchain, Reflex, HUD-less color, отдельный UI buffer, кадровые идентификаторы, completion fence; доступность проверяет SDK |
| FSR temporal / Native AA | Официальный FidelityFX API; фактический provider определяется при создании контекста |
| FSR Frame Generation | SDK swapchain, prepare + camera data, generation callback, HUD-less color; режим 2× |
| XeSS / Native AA | Intel SDK с разрешением входа, запрошенным у SDK |
| XeFG | Intel proxy swapchain, depth/motion/HUD-less tags, frame constants, Present ID и статус фактически показанных кадров; режим 2× |
| Reflex / XeLL | Sleep и полный цикл simulation / submit / present markers; взаимоисключающие режимы |
| DirectStorage | Чтение блоков контента и GPU-ready текстур GDeflate с mip-уровнями; fallback для неподготовленных PNG/JPG |

`build.bat editor standalone` и `build.bat release standalone` подготавливают текстуры из `src/Content` в `Contents/content10.pak` (индекс и блоки выровнены по 4 КБ). Cooked-текстуры используют RGBA8, mip chain и layout `GetCopyableFootprints`. Кодек проверяет обратную распаковку перед записью. Изменённые исходные изображения не подменяются устаревшим cooked-файлом. Зашифрованные EPAK по-прежнему расшифровываются на CPU; их Deflate не объявляется GPU GDeflate.

Проверка на RTX 3060: доступен FSR provider 3.1.5; DLSS-G SDK считает неподдерживаемым. Реальную генерацию NVIDIA на этом устройстве проверить нельзя. BypassIO не объявляется поддерживаемым без отдельной проверки файла/драйвера. RVT и виртуальные страницы VSM остаются прототипами; обычная карта теней сохранена.

`tools/Diagnostics/AuditTechnologies.ps1 -IncludeExtended` проверяет SDK на кадрах, включая переключение FSR ↔ XeFG и resize. Отчёты находятся в `Diagnostics/Benchmarks`, не рядом с EXE. Это проверка работоспособности на данном устройстве, а не сравнение FPS или качества на всех GPU.

## Результат проверки 18.10.2026

- Editor и Release собраны через build.bat; последние исправленные единицы компиляции пересобраны и перелинкованы теми же флагами MSVC.
- 17/17 runtime-проверок SDK: Diagnostics/technology-audit.csv. Цикл переключения подтвердил 87 кадров XeFG и 29 кадров FSR после возврата; выход штатный.
- 8/8 CTest: сеть/сервер, DXR-трансформации, ввод мыши, персонаж/камера, данные редактора и чтение контента.
- Консоль Editor проверена через Win32: HWND существует, процесс прикреплён, Ctrl+C завершает редактор с кодом 0, лог содержит завершение подсистем. Отчёт: Diagnostics/LaunchVerification/console.txt.
- Release: игровое окно создано, штатное закрытие с кодом 0. Save1.save восстановлен без изменения содержимого.
- Повреждённый заголовок content10.pak: обычное декодирование текстур работает, GDeflate-счётчик 0; оригинальный архив восстановлен побайтно. При штатном архиве проверено 2 GDeflate-текстуры / 73736 распакованных байт.
- В корне EditorBuild и ReleaseBuild остался только MyEngine.exe; данные и зависимости находятся в подпапках.

Проверка SDK означает успешную обработку кадров на этой машине. Снижение задержки не измерялось, качество на всех сценах и других GPU не подтверждается. Надпись индикатора NVIDIA при RR определяется глобальным ShowDlssIndicator; production DLL сверены с комплектом SDK, настройки драйвера не изменялись.

## DLSS Ray Reconstruction — 21.10.2026

Window → Graphics → DLSS Ray Reconstruction включает DXR и выбирает DLSS с текущим качеством (включая Native/DLAA). Требуется поддержка RR, подтверждённая Streamline. Настройка сохраняется в сцене строкой `DLSS_RR`; старые сцены остаются совместимыми. При выборе другого апскейлера или отключении DXR запрос сохраняется, но RR не обрабатывает кадры. Индикатор `RR: active` означает успешный вызов SDK в последнем кадре. Переключение RR сбрасывает историю DLSS. SDK presets D/E не являются версиями продукта «RR 1.0/2.0».

### FG/MFG controls and history

NVIDIA FG selection tracks the requested mode independently from SDK presentation activity. Changing the multiplier updates DLSS-G options on the existing swapchain. The multiplier list uses `DLSSGState::numFramesToGenerateMax`, rather than assuming every NVIDIA GPU supports MFG. HUD/UI recomposition is explicitly enabled for the separately tagged UI image. Actual generated-frame counts and SDK status are included in benchmark metadata.

FSR FG, XeFG and NVIDIA FG remain mutually exclusive. Provider changes and render-size changes reset interpolation history; FSR applies the reset to the matching frame in its generation callback; XeFG and NVIDIA receive it in their frame constants. Required Reflex/XeLL modes remain enabled while their generator is requested. FSR and XeFG integrations currently generate one intermediate frame (2x). MFG availability is determined by NVIDIA's SDK/driver, independently of DLSS SR/RR support.

Runtime checks: `tools/Diagnostics/AuditTechnologies.ps1 -IncludeExtended -CaseNames audit_fsr_fg,audit_xefg,audit_fg_cycle`. NVIDIA FG/MFG requires supported hardware for end-to-end validation; RTX 3060 cannot validate that path.
