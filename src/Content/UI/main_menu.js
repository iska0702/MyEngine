/**
 * MAIN MENU CONTROLLER (main_menu.js)
 * Clean, interactive script for main_menu.html
 * Handles Continue, New Game, Multiplayer Direct Connect, Saves, AAA Settings, Credits, and Exit.
 */

document.addEventListener('DOMContentLoaded', () => {
  new BackgroundCanvas('bg-canvas');

  const modal = document.getElementById('menu-modal');
  const modalTitle = document.getElementById('modal-title');
  const modalBody = document.getElementById('modal-content-body');
  const btnCloseModal = document.getElementById('btn-modal-close');

  function openModal(title, htmlContent) {
    if (!modal) return;
    modalTitle.textContent = title;
    modalBody.innerHTML = htmlContent;
    modal.classList.remove('hidden');
    bindModalInternalEvents();
  }

  function closeModal() {
    if (!modal) return;
    modal.classList.add('hidden');
  }

  if (btnCloseModal) {
    btnCloseModal.addEventListener('click', () => {
      closeModal();
    });
  }

  modal?.addEventListener('click', (e) => {
    if (e.target === modal) {
      closeModal();
    }
  });

  // Global keybindings (Shift+Tab for Steam overlay, ESC to close modal)
  window.addEventListener('keydown', (e) => {
    if (e.shiftKey && (e.key === 'Tab' || e.keyCode === 9)) {
      e.preventDefault();
      if (window.EngineBridge && window.EngineBridge.send) {
        window.EngineBridge.send('open_steam_overlay');
      }
    } else if (e.key === 'Escape') {
      if (modal && !modal.classList.contains('hidden')) {
        closeModal();
      }
    }
  });

  // 1. Continue Game
  document.getElementById('btn-continue')?.addEventListener('click', () => {
    EngineBridge.send('RESUME_GAME');
    EngineBridge.send('continue_game');
  });

  // 2. New Game
  document.getElementById('btn-new-game')?.addEventListener('click', () => {
    EngineBridge.send('RESET_SCENE');
    EngineBridge.send('new_game', { difficulty: 'normal' });
    EngineBridge.send('RESUME_GAME');
  });

  // 3. Multiplayer (Direct IP Connect + Quick LAN Presets + Live Session)
  document.getElementById('btn-multiplayer')?.addEventListener('click', () => {
    EngineBridge.send('open_multiplayer');
    openModal('Мультиплеер & Dedicated Server', `
      <div class="multiplayer-layout">
        <div class="modal-mp-panel active" id="tab-mp-direct">
          <div class="ip-connect-form">
            <div class="ip-form-group">
              <label>Имя игрока (Никнейм)</label>
              <input type="text" class="ip-input-field" id="mp-player-nick" value="Player_1" placeholder="Введите ваш ник...">
            </div>

            <div class="ip-form-grid">
              <div class="ip-form-group">
                <label>IP-адрес сервера (IPv4 или DNS)</label>
                <input type="text" class="ip-input-field" id="mp-server-ip" value="127.0.0.1" placeholder="127.0.0.1 или 192.168.1.100">
              </div>
              <div class="ip-form-group">
                <label>UDP Порт</label>
                <input type="text" class="ip-input-field" id="mp-server-port" value="7777" placeholder="7777">
              </div>
            </div>

            <div style="margin-top: 6px; margin-bottom: 14px;">
              <label style="font-size: 12px; color: #64748b;">Быстрые пресеты для тестирования:</label>
              <div style="display: flex; gap: 8px; margin-top: 6px;">
                <button class="settings-btn secondary" type="button" id="btn-preset-localhost" style="padding: 6px 12px; font-size: 12px;">🏠 Localhost (127.0.0.1:7777)</button>
                <button class="settings-btn secondary" type="button" id="btn-preset-lan" style="padding: 6px 12px; font-size: 12px;">🏢 Локальная сеть (LAN)</button>
              </div>
            </div>

            <div style="display: flex; gap: 10px;">
              <button class="connect-direct-btn" id="btn-mp-connect-direct" style="flex: 1;">
                <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2.2"><polygon points="5 3 19 12 5 21 5 3"/></svg>
                <span>Подключиться к серверу</span>
              </button>
              <button class="settings-btn secondary" id="btn-mp-disconnect" style="display: none; padding: 12px 20px;">
                <span>🔴 Отключиться</span>
              </button>
            </div>
          </div>
        </div>
      </div>
    `);
  });

  // 4. Saves Management
  document.getElementById('btn-saves')?.addEventListener('click', () => {
    EngineBridge.send('open_saves');
    openModal('Управление сохранениями', `
      <div class="saves-list">
        <div class="save-slot-card">
          <div class="save-slot-meta">
            <h4>[ Слот 1 ] — Автосохранение</h4>
            <span>Уровень 5 • Королевство Эфирия • 12 мин. назад</span>
          </div>
          <div style="display: flex; gap: 6px;">
            <button class="settings-btn primary" onclick="EngineBridge.send('LOAD_SCENE', { slot: 1 }); EngineBridge.send('RESUME_GAME'); closeModal();" style="padding: 6px 14px; font-size: 12px;">Загрузить</button>
            <button class="settings-btn secondary" onclick="EngineBridge.send('SAVE_SCENE', { slot: 1 });" style="padding: 6px 14px; font-size: 12px;">Сохранить</button>
          </div>
        </div>

        <div class="save-slot-card">
          <div class="save-slot-meta">
            <h4>[ Слот 2 ] — Подземелье Древних</h4>
            <span>Уровень 4 • Зал Стражей • 1 час назад</span>
          </div>
          <div style="display: flex; gap: 6px;">
            <button class="settings-btn primary" onclick="EngineBridge.send('LOAD_SCENE', { slot: 2 }); EngineBridge.send('RESUME_GAME'); closeModal();" style="padding: 6px 14px; font-size: 12px;">Загрузить</button>
            <button class="settings-btn secondary" onclick="EngineBridge.send('SAVE_SCENE', { slot: 2 });" style="padding: 6px 14px; font-size: 12px;">Сохранить</button>
          </div>
        </div>

        <div class="save-slot-card empty-slot">
          <div class="save-slot-meta">
            <h4>[ Слот 3 ] — Свободно</h4>
            <span>Пустой слот для сохранения прогресса</span>
          </div>
          <div style="display: flex; gap: 6px;">
            <button class="settings-btn primary" onclick="EngineBridge.send('SAVE_SCENE', { slot: 3 });" style="padding: 6px 14px; font-size: 12px;">Сохранить сюда</button>
          </div>
        </div>
      </div>
    `);
  });

  // 5. Settings Modal (Complete Grouped AAA Settings)
  document.getElementById('btn-settings-menu')?.addEventListener('click', () => {
    EngineBridge.send('open_settings');
    openModal('Настройки игры', `
      <div class="settings-modal-layout">
        
        <!-- Clean Tabs Header inside Modal -->
        <div class="poi-category-tabs" id="modal-settings-nav" style="margin-bottom: 16px;">
          <button class="poi-tab active" data-modtab="m-display">Дисплей</button>
          <button class="poi-tab" data-modtab="m-graphics">Графика</button>
          <button class="poi-tab" data-modtab="m-audio">Звук</button>
          <button class="poi-tab" data-modtab="m-gameplay">Геймплей & Управление</button>
        </div>

        <!-- Panel 1: Display -->
        <div class="modal-settings-tab active" id="tab-m-display">
          <div class="settings-group-header">Экран и рендер</div>
          <div class="setting-group">
            <div class="setting-item">
              <div class="setting-info">
                <label>Режим экрана</label>
                <span>Формат отображения окна</span>
              </div>
              <select class="setting-control" id="m-set-display-mode">
                <option value="fullscreen">Полноэкранный (Fullscreen)</option>
                <option value="borderless">В окне без рамок (Borderless)</option>
                <option value="windowed">Оконный режим (Windowed)</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Разрешение рендера</label>
                <span>Выходное разрешение монитора</span>
              </div>
              <select class="setting-control" id="m-set-res">
                <option value="3840x2160">3840 × 2160 (4K UHD)</option>
                <option value="2560x1440" selected>2560 × 1440 (2K QHD)</option>
                <option value="1920x1080">1920 × 1080 (FHD 1080p)</option>
                <option value="1280x720">1280 × 720 (HD 720p)</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Частота обновления (Hz)</label>
                <span>Герцовка панели</span>
              </div>
              <select class="setting-control" id="m-set-refresh">
                <option value="240">240 Hz</option>
                <option value="165">165 Hz</option>
                <option value="144" selected>144 Hz</option>
                <option value="120">120 Hz</option>
                <option value="60">60 Hz</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Вертикальная синхронизация (V-Sync)</label>
                <span>Устранение разрывов кадров</span>
              </div>
              <select class="setting-control" id="m-set-vsync">
                <option value="off" selected>Выключено (Минимальный Input Lag)</option>
                <option value="on">Включено (Adaptive V-Sync)</option>
                <option value="triple">Тройная буферизация</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Ограничение FPS</label>
                <span>Лимит частоты кадров</span>
              </div>
              <select class="setting-control" id="m-set-fps-limit">
                <option value="unlimited">Без ограничений</option>
                <option value="240">240 FPS</option>
                <option value="144" selected>144 FPS</option>
                <option value="120">120 FPS</option>
                <option value="60">60 FPS</option>
              </select>
            </div>

            <div class="setting-item slider-row">
              <div class="setting-info">
                <label>Поле зрения камеры (FOV)</label>
                <span>Угол обзора игрока</span>
              </div>
              <div class="range-wrapper">
                <input type="range" id="m-set-fov" min="70" max="120" value="95">
                <span class="range-val" id="m-val-fov">95°</span>
              </div>
            </div>
          </div>

          <div class="settings-group-header" style="margin-top: 14px;">Масштабирование & Сглаживание</div>
          <div class="setting-group">
            <div class="setting-item">
              <div class="setting-info">
                <label>Технология масштабирования</label>
                <span>Аппаратный суперсемплинг</span>
              </div>
              <select class="setting-control" id="m-set-upscale-tech">
                <option value="dlss">NVIDIA DLSS</option>
                <option value="fsr" selected>AMD FSR</option>
                <option value="off">Выключено (Нативное)</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Профиль масштабирования</label>
                <span>Качество нейросетевого апскейла</span>
              </div>
              <select class="setting-control" id="m-set-upscale-mode">
                <option value="dlaa">DLAA</option>
                <option value="quality" selected>Качество</option>
                <option value="balanced">Баланс</option>
                <option value="performance">Производительность</option>
                <option value="ultra_performance">Ультра Производительность</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Генерация кадров (Frame Generation)</label>
                <span>Интерполяция оптического потока GPU</span>
              </div>
              <select class="setting-control" id="m-set-frame-gen">
                <option value="dlss_mfg_4x">NVIDIA DLSS 4.5 Multi-Frame Gen (4x кадров)</option>
                <option value="dlss_mfg_3x">NVIDIA DLSS 4.5 Multi-Frame Gen (3x кадров)</option>
                <option value="dlss_fg_2x" selected>NVIDIA DLSS 4.5 Frame Gen (2x кадров)</option>
                <option value="amd_afmf_2">AMD Fluid Motion Frames 2 (AFMF 2)</option>
                <option value="off">Выключено</option>
              </select>
            </div>

            <div class="setting-item" id="row-m-ray-recon">
              <div class="setting-info">
                <label>DLSS 4.5 Ray Reconstruction <span class="tech-badge" style="background: rgba(16, 185, 129, 0.15); color: #10b981; border: 1px solid rgba(16, 185, 129, 0.3); font-size: 10px; padding: 2px 6px; border-radius: 4px; margin-left: 6px;">DLSS 4.5 AI v2.0</span></label>
                <span>Реконструкция лучей нейросетью (Next-Gen Transformer 2.0)</span>
              </div>
              <select class="setting-control" id="m-set-ray-recon">
                <option value="v2" selected>DLSS 4.5 Ray Reconstruction 2.0 (Transformer)</option>
                <option value="v1">DLSS 4.5 Ray Reconstruction 1.0 (Classic Model)</option>
                <option value="off">Выключено</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>NVIDIA Reflex / Anti-Lag 2</label>
                <span>Снижение задержки ввода</span>
              </div>
              <select class="setting-control" id="m-set-reflex">
                <option value="off">Выкл</option>
                <option value="on">Вкл</option>
                <option value="boost" selected>Вкл + Boost</option>
              </select>
            </div>
          </div>
        </div>

        <!-- Panel 2: Graphics -->
        <div class="modal-settings-tab" id="tab-m-graphics" style="display: none;">
          <div class="settings-group-header">Общий профиль & Текстуры</div>
          <div class="setting-group">
            <div class="setting-item">
              <div class="setting-info">
                <label>Пресет графики</label>
                <span>Общий уровень качества</span>
              </div>
              <select class="setting-control" id="m-set-preset">
                <option value="cinematic">Кинематографический (Cinematic)</option>
                <option value="ultra" selected>Ультра (Ultra)</option>
                <option value="high">Высокое (High)</option>
                <option value="medium">Среднее (Medium)</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Качество текстур</label>
                <span>Разрешение текстур (VRAM)</span>
              </div>
              <select class="setting-control" id="m-set-textures">
                <option value="cinematic">8K Ultra HD</option>
                <option value="ultra" selected>4K High-Res</option>
                <option value="high">2K</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Освещение & Трассировка</label>
                <span>Технология рендеринга света</span>
              </div>
              <select class="setting-control" id="m-set-rt-mode">
                <option value="path_tracing">Трассировка путей (Path Tracing)</option>
                <option value="ray_tracing">Трассировка лучей (Ray Tracing)</option>
                <option value="off" selected>Выключено (Растеризация)</option>
              </select>
            </div>

            <div class="setting-item">
              <div class="setting-info">
                <label>Объемный туман (Volumetric Fog)</label>
                <span>3D объемный туман и лучи света</span>
              </div>
              <select class="setting-control" id="m-set-volumetrics">
                <option value="ultra" selected>Ультра</option>
                <option value="high">Высокое</option>
                <option value="medium">Среднее</option>
              </select>
            </div>
          </div>
        </div>

        <!-- Panel 3: Audio -->
        <div class="modal-settings-tab" id="tab-m-audio" style="display: none;">
          <div class="settings-group-header">Громкость и тестирование</div>
          <div class="setting-group">
            <div class="setting-item">
              <div class="setting-info">
                <label>Тестирование звука</label>
                <span>Проверка работы аудиосистемы</span>
              </div>
              <button class="settings-btn secondary" type="button" id="modal-btn-test-sound" style="padding: 8px 16px;">
                <span>🔊 Тест звука</span>
              </button>
            </div>

            <div class="setting-item slider-row">
              <div class="setting-info">
                <label>Общая громкость</label>
                <span>Master Volume</span>
              </div>
              <div class="range-wrapper">
                <input type="range" id="m-vol-master" min="0" max="100" value="85">
                <span class="range-val" id="m-val-master">85%</span>
              </div>
            </div>

            <div class="setting-item slider-row">
              <div class="setting-info">
                <label>Музыка</label>
                <span>Music Volume</span>
              </div>
              <div class="range-wrapper">
                <input type="range" id="m-vol-music" min="0" max="100" value="70">
                <span class="range-val" id="m-val-music">70%</span>
              </div>
            </div>

            <div class="setting-item slider-row">
              <div class="setting-info">
                <label>Звуковые эффекты</label>
                <span>SFX Volume</span>
              </div>
              <div class="range-wrapper">
                <input type="range" id="m-vol-sfx" min="0" max="100" value="90">
                <span class="range-val" id="m-val-sfx">90%</span>
              </div>
            </div>
          </div>
        </div>

        <!-- Panel 4: Gameplay -->
        <div class="modal-settings-tab" id="tab-m-gameplay" style="display: none;">
          <div class="settings-group-header">Геймплей и управление</div>
          <div class="setting-group">
            <div class="setting-item">
              <div class="setting-info">
                <label>Сложность игры</label>
                <span>Баланс урона</span>
              </div>
              <select class="setting-control" id="modal-set-diff">
                <option value="story">Сюжетный</option>
                <option value="normal" selected>Приключение (Нормальный)</option>
                <option value="hard">Героический</option>
              </select>
            </div>

            <div class="setting-item slider-row">
              <div class="setting-info">
                <label>Чувствительность мыши</label>
                <span>Скорость вращения камеры</span>
              </div>
              <div class="range-wrapper">
                <input type="range" id="modal-mouse-sens" min="1" max="10" step="0.5" value="3.5">
                <span class="range-val" id="modal-val-mouse-sens">3.5</span>
              </div>
            </div>
          </div>
        </div>

        <!-- Settings Modal Footer -->
        <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 20px;">
          <button class="settings-btn secondary" type="button" onclick="closeModal();">Отмена</button>
          <button class="settings-btn primary" type="button" id="btn-modal-save-settings">Сохранить настройки</button>
        </div>

      </div>
    `);
  });

  // 6. Credits Modal
  document.getElementById('btn-credits')?.addEventListener('click', () => {
    EngineBridge.send('open_credits');
    openModal('Авторы и технологии движка', `
      <div style="line-height: 1.6; color: #334155; font-size: 14px;">
        <h3 style="color: #0f172a; font-family: var(--font-game); font-size: 1.3rem; margin-bottom: 8px;">MyEngine / Aetheria Core</h3>
        <p style="margin-bottom: 14px;">Высокопроизводительный игровой движок нового поколения на DirectX 12, C++20 и передовых алгоритмах физической симуляции.</p>
        
        <div style="background: rgba(241, 245, 249, 0.8); border: 1px solid #cbd5e1; border-radius: 10px; padding: 14px; margin-bottom: 14px;">
          <h4 style="color: #d97706; margin-bottom: 6px;">Ключевые подсистемы:</h4>
          <ul style="padding-left: 18px; display: flex; flex-direction: column; gap: 4px;">
            <li><strong>Графический конвейер:</strong> DirectX 12, DXIL Shaders, DXR Ray Tracing & Path Tracing, Hi-Z SSR, Volumetric Fog.</li>
            <li><strong>Физика & Симуляции:</strong> XPBD Cloth System, XPBD Cosserat Rods Hair System, NVIDIA PhysX 3.4.</li>
            <li><strong>Сетевой стек:</strong> Server-Authoritative Multiplayer, UDP Winsock2, Client Prediction, Remote Interpolation, Headless Dedicated Server.</li>
            <li><strong>Пользовательский интерфейс:</strong> Microsoft WebView2 Chromium UI & Fallback Dear ImGui.</li>
            <li><strong>Контроллеры & Аудио:</strong> Sony DualSense Direct HID (Adaptive Triggers, Haptics) & WASAPI Spatial Audio.</li>
          </ul>
        </div>
        <p style="font-size: 12px; color: #64748b; text-align: center;">© 2026 MyEngine Development Team. All rights reserved.</p>
      </div>
    `);
  });

  // 7. Exit Game with Confirmation Modal
  document.getElementById('btn-exit')?.addEventListener('click', () => {
    openModal('Выход из игры', `
      <div style="text-align: center; padding: 24px 16px;">
        <div style="width: 56px; height: 56px; border-radius: 50%; background: rgba(239, 68, 68, 0.15); border: 2px solid rgba(239, 68, 68, 0.4); display: flex; align-items: center; justify-content: center; margin: 0 auto 16px auto;">
          <svg viewBox="0 0 24 24" width="28" height="28" fill="none" stroke="#ef4444" stroke-width="2.2"><path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/></svg>
        </div>
        <h3 style="font-size: 20px; font-weight: 600; color: #f8fafc; margin-bottom: 8px;">Выйти из MyEngine?</h3>
        <p style="font-size: 14px; color: #94a3b8; margin-bottom: 24px; line-height: 1.5;">Вы действительно хотите закрыть игру? Несохраненные изменения текущей сессии могут быть утеряны.</p>
        <div style="display: flex; gap: 12px; justify-content: center;">
          <button class="settings-btn danger" id="btn-modal-confirm-exit" style="padding: 12px 32px; font-size: 14px; font-weight: 600; background: #ef4444; color: #ffffff; border: none; border-radius: 8px; cursor: pointer; transition: all 0.2s ease;">
            ✕ Выйти
          </button>
          <button class="settings-btn secondary" id="btn-modal-cancel-exit" style="padding: 12px 32px; font-size: 14px; font-weight: 500; border-radius: 8px; cursor: pointer; transition: all 0.2s ease;">
            Отмена
          </button>
        </div>
      </div>
    `);
  });

  // Helper to bind events inside dynamically injected modals
  function bindModalInternalEvents() {
    function populateResolutionDropdown(selectId, selectedVal) {
      const select = document.getElementById(selectId);
      if (!select) return;

      const resList = (window._supportedResolutions && window._supportedResolutions.length > 0)
        ? window._supportedResolutions
        : [ `${window.screen.width}x${window.screen.height}`, "1920x1080", "1600x900", "1280x720" ];

      const unique = Array.from(new Set(resList));
      select.innerHTML = '';
      unique.forEach(r => {
        const opt = document.createElement('option');
        opt.value = r;
        const parts = r.split('x');
        const w = parts[0];
        const h = parts[1];
        let label = `${w} × ${h}`;
        if (w === '3840' && h === '2160') label += ' (4K UHD)';
        else if (w === '2560' && h === '1440') label += ' (2K QHD)';
        else if (w === '1920' && h === '1080') label += ' (FHD 1080p)';
        else if (w === '1600' && h === '900') label += ' (HD+)';
        else if (w === '1366' && h === '768') label += ' (WXGA)';
        else if (w === '1280' && h === '720') label += ' (HD 720p)';
        if (r === `${window.screen.width}x${window.screen.height}`) label += ' ★ Native';
        opt.textContent = label;
        select.appendChild(opt);
      });

      const currentRes = selectedVal || window._currentEngineSettings?.resolution;
      if (currentRes && unique.includes(currentRes)) {
        select.value = currentRes;
      } else {
        select.value = unique[0];
      }
    }

    populateResolutionDropdown('m-set-res', window._currentEngineSettings?.resolution);

    function applyHardwareFilter() {
      const caps = window._capabilities || {};

      // 1. DLSS & Reflex hiding if unsupported
      const upscaleSelect = document.getElementById('m-set-upscale-tech');
      if (upscaleSelect) {
        const dlssOpt = upscaleSelect.querySelector('option[value="dlss"]');
        if (dlssOpt) {
          if (!caps.dlssSupported) {
            dlssOpt.style.display = 'none';
            if (upscaleSelect.value === 'dlss') upscaleSelect.value = 'fsr';
          } else {
            dlssOpt.style.display = '';
          }
        }
      }

      const fgRow = document.getElementById('m-set-frame-gen')?.closest('.setting-item');
      if (fgRow) fgRow.style.display = caps.dlssSupported ? '' : 'none';

      const reflexRow = document.getElementById('m-set-reflex')?.closest('.setting-item');
      if (reflexRow) reflexRow.style.display = caps.dlssSupported ? '' : 'none';

      const rrRow = document.getElementById('row-m-ray-recon');
      if (rrRow) {
        const rrSupported = caps.dlssSupported && caps.dxrSupported;
        rrRow.style.display = rrSupported ? 'flex' : 'none';
      }

      // 2. Ray Tracing & Path Tracing hiding if DXR unsupported
      const rtSelect = document.getElementById('m-set-rt-mode');
      if (rtSelect) {
        const ptOpt = rtSelect.querySelector('option[value="path_tracing"]');
        const rtOpt = rtSelect.querySelector('option[value="ray_tracing"]');
        if (!caps.dxrSupported) {
          if (ptOpt) ptOpt.style.display = 'none';
          if (rtOpt) rtOpt.style.display = 'none';
          rtSelect.value = 'off';
        } else {
          if (ptOpt) ptOpt.style.display = '';
          if (rtOpt) rtOpt.style.display = '';
        }
      }
    }

    applyHardwareFilter();

    // Synchronize UI values with current active engine state
    if (window._currentEngineSettings) {
      const s = window._currentEngineSettings;
      if (document.getElementById('m-set-display-mode') && s.mode) document.getElementById('m-set-display-mode').value = s.mode;
      if (document.getElementById('m-set-vsync') && s.vsync !== undefined) document.getElementById('m-set-vsync').value = s.vsync ? 'on' : 'off';
      if (document.getElementById('m-set-fov') && s.fov) {
        document.getElementById('m-set-fov').value = s.fov;
        if (document.getElementById('m-val-fov')) document.getElementById('m-val-fov').textContent = s.fov + '°';
      }
      if (document.getElementById('m-set-upscale-tech') && s.upscaler) document.getElementById('m-set-upscale-tech').value = s.upscaler;
      if (document.getElementById('m-set-ray-recon') && s.rayReconstruction !== undefined) document.getElementById('m-set-ray-recon').value = (s.rayReconstruction === true || s.rayReconstruction === 'true' || s.rayReconstruction === 'on') ? 'v2' : s.rayReconstruction;
      if (document.getElementById('m-set-rt-mode') && s.raytracingMode) document.getElementById('m-set-rt-mode').value = s.raytracingMode;
      if (document.getElementById('m-set-volumetrics') && s.volumetricFog !== undefined) document.getElementById('m-set-volumetrics').value = s.volumetricFog ? 'ultra' : 'off';
    }

    // Multiplayer Presets & Connect
    document.getElementById('btn-preset-localhost')?.addEventListener('click', () => {
      const ip = document.getElementById('mp-server-ip');
      const port = document.getElementById('mp-server-port');
      if (ip) ip.value = '127.0.0.1';
      if (port) port.value = '7777';
    });

    document.getElementById('btn-preset-lan')?.addEventListener('click', () => {
      const ip = document.getElementById('mp-server-ip');
      const port = document.getElementById('mp-server-port');
      if (ip) ip.value = '192.168.1.100';
      if (port) port.value = '7777';
    });

    document.getElementById('btn-mp-connect-direct')?.addEventListener('click', () => {
      const ip = document.getElementById('mp-server-ip')?.value || '127.0.0.1';
      const port = parseInt(document.getElementById('mp-server-port')?.value || '7777', 10);
      const nick = document.getElementById('mp-player-nick')?.value || 'Player';

      EngineBridge.send('CONNECT_SERVER', { ip, port, name: nick, nickname: nick });
      EngineBridge.send('multiplayer_connect', { ip, port, nickname: nick });
      closeModal();
    });

    document.getElementById('btn-mp-disconnect')?.addEventListener('click', () => {
      EngineBridge.send('DISCONNECT_SERVER');
      closeModal();
    });

    // Settings Subtabs
    const navButtons = document.querySelectorAll('#modal-settings-nav .poi-tab');
    navButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        navButtons.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');

        document.querySelectorAll('.modal-settings-tab').forEach(tab => {
          tab.style.display = 'none';
        });

        const target = document.getElementById(`tab-${btn.dataset.modtab}`);
        if (target) target.style.display = 'block';
      });
    });

    const bindModalSlider = (id, valId, unit = '%') => {
      const el = document.getElementById(id);
      const valEl = document.getElementById(valId);
      if (el && valEl) {
        el.addEventListener('input', (e) => {
          valEl.textContent = `${e.target.value}${unit}`;
        });
      }
    };

    bindModalSlider('m-set-fov', 'm-val-fov', '°');
    bindModalSlider('modal-mouse-sens', 'modal-val-mouse-sens', '');
    bindModalSlider('m-vol-master', 'm-val-master', '%');
    bindModalSlider('m-vol-music', 'm-val-music', '%');
    bindModalSlider('m-vol-sfx', 'm-val-sfx', '%');

    // Sound test button inside modal
    document.getElementById('modal-btn-test-sound')?.addEventListener('click', () => {
      if (window.Audio && window.Audio.playTestSound) {
        window.Audio.playTestSound();
      }
    });

    // Save Settings button inside modal
    document.getElementById('btn-modal-save-settings')?.addEventListener('click', () => {
      const mode = document.getElementById('m-set-display-mode')?.value || 'windowed';
      const resolution = document.getElementById('m-set-res')?.value || '1920x1080';
      const vsync = document.getElementById('m-set-vsync')?.value || 'on';
      const fov = document.getElementById('m-set-fov')?.value || '95';
      const upscaler = document.getElementById('m-set-upscale-tech')?.value || 'fsr';
      const upscaleMode = document.getElementById('m-set-upscale-mode')?.value || 'quality';
      const frameGen = document.getElementById('m-set-frame-gen')?.value || 'dlss_fg_2x';
      const reflex = document.getElementById('m-set-reflex')?.value || 'boost';
      const rayReconstruction = document.getElementById('m-set-ray-recon')?.value || 'off';
      const raytracingMode = document.getElementById('m-set-rt-mode')?.value || 'off';
      const volumetricFog = document.getElementById('m-set-volumetrics')?.value || 'ultra';
      const master = document.getElementById('m-vol-master')?.value || '85';

      const payload = {
        action: 'save_settings',
        event: 'save_settings',
        mode: mode,
        resolution: resolution,
        vsync: vsync,
        fov: parseFloat(fov),
        upscaler: upscaler,
        upscaleMode: upscaleMode,
        mfg: frameGen,
        reflex: reflex,
        rayReconstruction: rayReconstruction,
        raytracingMode: raytracingMode,
        volumetricFog: volumetricFog,
        master: parseFloat(master)
      };

      // Store in memory for instant modal updates
      window._currentEngineSettings = {
        mode: mode,
        resolution: resolution,
        vsync: vsync === 'on',
        fov: parseFloat(fov),
        upscaler: upscaler,
        rayReconstruction: rayReconstruction,
        raytracingMode: raytracingMode,
        volumetricFog: volumetricFog !== 'off'
      };

      EngineBridge.send('save_settings', payload);
      EngineBridge.send('SET_SETTINGS', payload);
      closeModal();
    });

    // Exit Game Confirmation buttons
    document.getElementById('btn-modal-confirm-exit')?.addEventListener('click', () => {
      EngineBridge.send('EXIT_GAME');
    });
    document.getElementById('btn-modal-cancel-exit')?.addEventListener('click', () => {
      closeModal();
    });
  }

  // Live Hardware Capability & Current Settings Synchronization
  EngineBridge.on('updateTelemetry', (data) => {
    if (!data) return;
    if (data.capabilities) {
      window._capabilities = data.capabilities;
      if (data.capabilities.supportedResolutions) {
        window._supportedResolutions = data.capabilities.supportedResolutions;
      }
    }

    if (data.currentSettings) {
      window._currentEngineSettings = data.currentSettings;
      const s = data.currentSettings;
      if (document.getElementById('m-set-res') && s.resolution) document.getElementById('m-set-res').value = s.resolution;
      if (document.getElementById('m-set-vsync')) document.getElementById('m-set-vsync').value = s.vsync ? 'on' : 'off';
      if (document.getElementById('m-set-fov') && s.fov) {
        document.getElementById('m-set-fov').value = s.fov;
        if (document.getElementById('m-val-fov')) document.getElementById('m-val-fov').textContent = s.fov + '°';
      }
      if (document.getElementById('m-set-upscale-tech') && s.upscaler) document.getElementById('m-set-upscale-tech').value = s.upscaler;
      if (document.getElementById('m-set-ray-recon') && s.rayReconstruction !== undefined) {
        document.getElementById('m-set-ray-recon').value = (s.rayReconstruction === true || s.rayReconstruction === 'true' || s.rayReconstruction === 'on') ? 'v2' : s.rayReconstruction;
      }
      if (document.getElementById('m-set-rt-mode') && s.raytracingMode) document.getElementById('m-set-rt-mode').value = s.raytracingMode;
    }
  });

  // Global Alt+F4 handler for exit confirmation dialog
  window.addEventListener('keydown', (e) => {
    if (e.altKey && (e.key === 'F4' || e.keyCode === 115)) {
      e.preventDefault();
      EngineBridge.send('REQUEST_EXIT');
    }
  });
});
