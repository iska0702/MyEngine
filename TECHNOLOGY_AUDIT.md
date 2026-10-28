# Обновление интеграций — 18.10.2026

После первоначального аудита подключены temporal FSR / Native AA, FSR FG, XeFG, Ray Reconstruction, полный цикл Reflex/XeLL и чтение GPU-ready текстур GDeflate через DirectStorage. Описание актуального кода: [INTEGRATIONS.md](INTEGRATIONS.md). Повторная проверка выполняется `tools/Diagnostics/AuditTechnologies.ps1 -IncludeExtended`; результаты находятся в `Diagnostics/technology-audit.csv`.

DLSS-G реализован с proxy swapchain, HUD-less/UI buffers и синхронизацией, но на RTX 3060 SDK сообщает об отсутствии аппаратной поддержки. Его генерация на этой машине не считается проверенной. RVT/VSM page management остаются прототипами; это отдельные системы движка.

Ниже сохранён первоначальный аудит, проведённый **до** этих изменений. Его утверждения о spatial FSR, неподключённых FG/RR, отсутствующих latency markers и незадействованном DirectStorage описывают прежнее состояние.

---
# Аудит технологий MyEngine — 18.10.2026

Проверяется Windows x64 / DirectX 12 на NVIDIA GeForce RTX 3060. Здесь «SDK лежит в ThirdParty», «устройство поддерживает функцию», «SDK успешно обработал кадр» и «функция подключена к игровому конвейеру» — разные статусы. Установка свежей DLL сама по себе не реализует технологию.

## Основные результаты по коду

| Технология | Что фактически подключено | Статус и ограничения |
|---|---|---|
| D3D12 / Mesh Shaders | Meshlet AS/MS/PS, реальные Draw/DispatchMesh | Используются в основном рендере. Требуется аппаратная поддержка; tier записывается в диагностике. |
| DXR | BLAS/TLAS, DispatchRays, тени/отражения | Реальная интеграция. Статус и режим проверяются на кадрах. |
| Path tracing | Один путь на пиксель с несколькими отскоками | Реальная экспериментальная ветка. Межкадровое накопление/полноценный denoiser не обнаружены; поле samplesPerPixel передаётся, но шейдер его не использует. Контрольный кадр заметно шумит. Не эталон физической корректности. |
| NVIDIA DLSS SR / DLAA | Streamline, оценка SDK, depth/motion/color/output tags | Реальный вызов slEvaluateFeature. Версия DLL фиксируется отдельно; рекламные обозначения не доказывают режим модели. |
| DLSS Frame Generation | Опции SDK и флаги, отсутствует полный кадровый контракт FG | Не интегрирован полностью. Включение заблокировано, вместо ложного ON. Поддержка SDK устройством сохранена как отдельная диагностика. |
| DLSS Ray Reconstruction | Обёртки параметров без полного набора входов G-buffer | Не интегрирован полностью. Включение защищено, по умолчанию Off. Обычный DLSS SR остаётся доступен. |
| AMD FSR | Собственный пространственный EASU/RCAS fullscreen shader | Это не FSR 2/3/4 temporal upscaler. Наличие amd_fidelityfx_upscaler_dx12.dll не означает её вызов. Native AA отсутствует. |
| AMD FSR Frame Generation | Только сохраняемый булев флаг и множитель | Рендер не использует эти поля для интерполяции. Управление отключено. |
| Intel XeSS SR / Native AA | xessD3D12Execute, quality settings и re-init при resize | Реальный SDK. Исправлены единицы jitter и velocity scale. |
| XeSS Frame Generation | Создание контекста и обёртки | Нет полноценного tag-frame/present-id и использования proxy swapchain в игровом Present. Включение защищено. |
| NVIDIA Reflex | Проверка SDK, slReflexSetOptions | Частично: нет полного жизненного цикла sleep/input/simulation/submit/present markers. Снижение задержки этим аудитом не подтверждено. |
| Intel XeLL | Контекст, sleep и submit/present markers | Частично: нет полного набора simulation/input markers; статус включения теперь берётся из успешного состояния SDK. Снижение задержки не измерялось. |
| DirectStorage | Factory, file/memory queues, API enqueue | Исправлена загрузка core runtime. Отдельный тест действительно читает файл. Основная загрузка текстур/PAK пока не направлена через DirectStorage. |
| GPU GDeflate | QueryCompressionSupport | Это проверка возможности, а не выполненная GPU-распаковка. GDeflate read/decompress в контентном потоке отсутствует. |
| BypassIO | Ранее безусловно отмечался Supported | Исправлено на not verified: создание очереди не проверяет поддержку конкретного файла/тома/драйвера. |
| TAA | Compute accumulation | Исправлено одновременное чтение/запись одной истории, добавлена инициализация истории и корректные переходы ресурсов. |
| Тень от солнца | Реальная depth shadow map 2048x2048 | Работает. Название Virtual Shadow Maps было неточным: виртуальное распределение страниц не реализовано. |
| VSM page management | Прототип очистки таблицы | Не полный VSM. Бесполезный dispatch убран; рабочая обычная карта тени сохранена. |
| RVT / Astra surface radiosity | Прототип атласов и compute solver | Итоговый шейдер объявляет RVT-текстуры, но не читает их. Проходы отключены, неиспользуемые 8k-атласы заменены минимальными ресурсами с валидными дескрипторами. |
| SSR | Функция ApplyReflections и PSO | В текущем Render нет вызова ApplyReflections. Наличие функции не означает активные SSR. DXR-отражения — отдельная рабочая ветка. |
| SSAO / SSGI | Реальные GPU-проходы и composite | Подключены. Выключенный SSGI не запускает compute pass. Художественная точность и все комбинации настроек отдельно не сертифицируются. |
| Volumetric fog | Inject/accumulate volume passes | Реализованы; фактическое включение пишется в снимке. Полный перебор всех настроек тумана не выполнен. |
| Облака / их тени | Общая функция плотности, ray marching / освещение | Подключены; все углы солнца, погода и временные артефакты не исчерпываются этой матрицей. |
| Post-process | Экспозиция, tone mapping, volumes | Собственная система; параметры сериализуются. Отдельный GPU color-reference тест не проводился. |

## Другие SDK и подсистемы

| Система | Результат проверки |
|---|---|
| PhysX | Реальный SDK 5.12.0; CharacterCameraRegression создаёт PhysX-сцену, коллайдеры и контроллер. Старое сообщение «PhysX 3.4» убрано. |
| Ozz | В src/Animation/Ozz находится собственный runtime с Ozz-подобными структурами и сериализацией; вызовов официального ozz:: runtime не найдено. Совместимость с официальными .ozz не подтверждена. |
| Lua | Собранные исходники Lua 5.4.7 и ScriptSystem. Пользовательские игровые скрипты в полном объёме не тестировались. |
| miniaudio | Реальная аудиосистема и listener; проверяется инициализация при запуске, прослушивание и все аудиоустройства не проверены. |
| DualSense / DualShock | Windows HID/XInput пути присутствуют. Проверка физического контроллера, Bluetooth, адаптивных триггеров и вибрации требует устройства; этим аудитом не подтверждена. |
| WebView2 | Реальный Windows SDK loader; работа всех HTML-экранов и вариантов установленного runtime не проверена. |
| Steamworks | Отключён в standalone согласно запросу пользователя. Онлайн-функции не объявляются работающими. |
| Epic EOS | В standalone не активируется. Другой платформенный таргет не запускался. |
| Discord Social SDK | DLL загружается; в прежних проверках соединение завершалось кодом 4004. Авторизация и presence не подтверждены; секреты в отчёт не копируются. |
| Сеть | Собственная Winsock/replication система; локальные lifecycle/network regression входят в CTest. Работа через Интернет/NAT не проверялась. |
| Импорт ресурсов | STB + tinyobj/ufbx/cgltf, CPU decode и D3D12 copy queue. Файлы PAK читаются/расшифровываются CPU-путём; это не GPU GDeflate. |
| DeepDVC / DirectSR / NIS / NVPerf / AMD AGS | DLL находятся в поставке, полноценных вызовов соответствующих функций в рендере не найдено. Не считаются активными технологиями. |
| macOS MetalFX / Metal RT / PS4 | Исходники других платформ не равны проверке на устройстве. В этом Windows-аудите не запускались. |

## Подтверждённые версии файлов

Streamline 2.14.1.0; nvngx_dlss / dlssd / dlssg 310.9.1.0; XeSS SR libxess 2.0.2.68 (пакет SDK 3.0.2), XeFG 1.3.1.78, XeLL 1.3.2.10; DirectStorage 1.3.2506.2501; PhysX 5.12.0.0. AMD runtime upscaler 4.1.1.2740 и framegeneration 4.0.1.2740 поставляются, но текущая пространственная ветка их не вызывает. Версии определены по FileVersion, не по подписи кнопок.

## Объём исправлений

Загрузка DirectStorage core по точному пути; честный статус BypassIO; защита неполных FG/RR; разделение состояния запроса и успешной активности; исправление XeSS jitter/motion units; безопасная история TAA; удаление неиспользуемых GPU-проходов и больших атласов; исправление названий DLSS/FSR/теней. Четыре неиспользуемых 8192x8192 атласа по 4 байта на пиксель занимали около 1 ГиБ до выравнивания. Минимальные заглушки сохраняют валидность дескрипторов, не отключая рабочую карту тени 2048.

Это аудит и исправление найденных дефектов, а не реализация отсутствующих FSR temporal/FG, RR, RVT или SSR с нуля. Не заявляется, что все комбинации драйверов, устройств и эффектов проверены.

## Как повторить проверки

- tools/Graphics/DirectStorageProbe.cpp: отдельный D3D12/DirectStorage тест. Явно загружает поставляемый core, создаёт очередь, читает 16384 байта и сравнивает с исходными данными. Лог: Diagnostics/directstorage-probe.txt.
- tools/Diagnostics/AuditTechnologies.ps1 -IncludeExtended: последовательные проверки Editor; отчёты вне папок запуска, таблица Diagnostics/technology-audit.csv.
- MYENGINE_D3D12_DEBUG=1 включает проверку ресурсов D3D12. Предупреждения пишутся в %LOCALAPPDATA%/MyEngine/Diagnostics/Editor/gpu_validation.log.
- CTest: локальные проверки сети, преобразований DXR, ввода, физики/персонажа, сериализации и хранения.

Дополнительные уточнения: в фактических тестовых запусках включены SSAO, SSGI и volumetric fog (это отражено в graphics-строке снимка). Legacy-подписи коэффициентов Quality/Balanced в UI не заменяют ответ SDK: например, XeSS Quality на этой версии runtime выбрал 753x424 для 1280x720, тогда как DLSS Quality выбрал 853x480. Апскейлеры реально используют размеры, полученные от SDK.

Проверка импортов Editor EXE подтвердила отсутствие Steam/EOS в standalone. Присутствуют D3D12, Streamline, XeSS/XeFG/XeLL, DirectStorage, PhysX и Discord; delay-import/наличие библиотеки всё равно не доказывает использование каждой функции.

Ограничение D3D12 validation: переменная MYENGINE_D3D12_DEBUG была включена при матрице, однако отдельный вызов D3D12GetDebugInterface вернул 0x887A002D (DXGI_ERROR_SDK_COMPONENT_MISSING). Debug layer на этой машине отсутствует, D3D12SDKLayers не был загружен. Поэтому отсутствие gpu_validation.log нельзя считать доказательством валидации всех resource barriers. Исправления переходов проверены по коду, запуску, результатам SDK и снимкам; повторная полноценная D3D12 debug-layer проверка остаётся ограничением среды.

## Финальная проверка

Полные build.bat editor standalone и build.bat release standalone успешно завершены. Все 8 CTest regression прошли; Editor --startup-check завершился exit 0. Матрица tools/Diagnostics/AuditTechnologies.ps1 -IncludeExtended: 10/10 успешных запусков с проверкой состояния SDK, технологий и размеров. Это не автоматическая оценка качества изображения и не полная аппаратная сертификация. Снимки TAA, DLSS Quality, пространственного FSR и path tracing просмотрены. DXR capability на текущем драйвере: tier 12; Mesh Shader tier 10.

Результаты масштабирования: DLSS Quality — 853x480 → 1280x720, FSR spatial Quality — 853x480 → 1280x720, XeSS Quality — 753x424 → 1280x720. DLAA и XeSS AA — 1280x720 → 1280x720, соответствующий SDK active=1. Factory/queues DirectStorage активны и в запущенном движке; отдельный read-test подтвердил read_equal=1, error=0. GPU GDeflate и BypassIO реальными запросами в этой матрице не проверены. Во всех снимках frame_generation_integrated=0 и ray_reconstruction_integrated=0, что соответствует найденному состоянию кода.

Таблица запусков: Diagnostics/technology-audit.csv. Детали и снимки: Diagnostics/Benchmarks/EditorBuild/audit_*. Журналы и тестовые ресурсы не помещаются рядом с exe. В корне EditorBuild и ReleaseBuild остался только MyEngine.exe.

Release smoke: приложение достигло сообщения Initialize COMPLETED SUCCESSFULLY, оставалось работающим 15 секунд; в его модулях подтверждены d3d12.dll, dstoragecore.dll и PhysX.dll. После проверки остановлен только собственный тестовый процесс. Это startup smoke, а не нормальное завершение с exit 0 и не отдельная полная Release-графическая матрица. Лог: Diagnostics/technology-release-smoke.log.
