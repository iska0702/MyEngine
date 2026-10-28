# Сглаживание

В Window → Graphics доступны Off, TAA, NVIDIA DLAA, Intel XeSS Native AA и AMD FSR Native AA. Нативное сглаживание сохраняет разрешение экрана; Quality/Balanced/Performance уменьшают входное разрешение апскейлера. Недоступные текущему GPU режимы блокируются.

FSR использует официальный temporal SDK, depth, velocity и jitter. Фактическая версия provider записывается в диагностику; на RTX 3060 это 3.1.5. Старый пространственный EASU/RCAS больше не является рабочей веткой FSR. TAA использует отдельный выход и историю, без одновременного чтения и записи одной текстуры.

Параметр редактора `--benchmark-aa`: 0 Off, 1 TAA, 2 DLAA, 3 XeSS AA, 4 FSR Native AA. Фактический статус SDK и размеры записываются в `benchmark_frame.bmp.txt`. Полная схема интеграций и ограничения описаны в INTEGRATIONS.md.