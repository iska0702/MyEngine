/**
 * TAB MENU CONTROLLER (tab_menu.js)
 * Clean, high-performance controller for C++ Game Engine Integration
 * Manages Inventory, World Map, Character Attributes & Combat Stats, AAA Settings
 */

// =============================================================================
// 1. INVENTORY MANAGER
// =============================================================================
class TabInventoryManager {
  constructor() {
    // Default starter item database for immediate interactivity
    this.items = [
      {
        id: 'wpn_01',
        name: 'Меч Солнечного Света',
        category: 'weapon',
        slot: 'mainhand',
        rarity: 'legendary',
        iconKey: 'sword',
        lvl: 10,
        weight: 3.5,
        price: 850,
        equipped: true,
        primaryStat: { label: 'Физ. Урон', value: '45 - 62' },
        stats: [
          { name: 'Сила (STR)', value: '+12' },
          { name: 'Крит. шанс', value: '+8%' },
          { name: 'Урон светом', value: '+24' }
        ],
        lore: 'Древний клинок, выкованный в лучах Первого Рассвета. Излучает мягкое золотое сияние.'
      },
      {
        id: 'arm_01',
        name: 'Латы Звёздного Рыцаря',
        category: 'armor',
        slot: 'chest',
        rarity: 'epic',
        iconKey: 'armor',
        lvl: 8,
        weight: 12.0,
        price: 620,
        equipped: true,
        primaryStat: { label: 'Защита', value: '+75' },
        stats: [
          { name: 'Здоровье (HP)', value: '+150' },
          { name: 'Живучесть (VIT)', value: '+8' },
          { name: 'Сопротивление магии', value: '+15%' }
        ],
        lore: 'Тяжелый панцирь из закаленной мифриловой стали с гравировкой созвездий.'
      },
      {
        id: 'shd_01',
        name: 'Щит Эфирного Стража',
        category: 'armor',
        slot: 'offhand',
        rarity: 'rare',
        iconKey: 'shield',
        lvl: 6,
        weight: 6.0,
        price: 340,
        equipped: true,
        primaryStat: { label: 'Блок урона', value: '35%' },
        stats: [
          { name: 'Защита', value: '+30' },
          { name: 'Шанс блока', value: '+18%' }
        ],
        lore: 'Прочный ростовой щит, зачарованный на отражение кинетических ударов.'
      },
      {
        id: 'hlm_01',
        name: 'Шлем Рассветного Дозора',
        category: 'armor',
        slot: 'head',
        rarity: 'rare',
        iconKey: 'helmet',
        lvl: 5,
        weight: 2.5,
        price: 210,
        equipped: true,
        primaryStat: { label: 'Защита', value: '+28' },
        stats: [
          { name: 'Обзор в темноте', value: '+20м' },
          { name: 'Интеллект (INT)', value: '+4' }
        ],
        lore: 'Стальной шлем с защитным забралом и символом ордена.'
      },
      {
        id: 'bot_01',
        name: 'Сапоги Легкого Шага',
        category: 'armor',
        slot: 'boots',
        rarity: 'common',
        iconKey: 'boots',
        lvl: 3,
        weight: 1.8,
        price: 95,
        equipped: true,
        primaryStat: { label: 'Скорость', value: '+10%' },
        stats: [
          { name: 'Ловкость (DEX)', value: '+3' }
        ],
        lore: 'Кожаные сапоги с укрепленной подошвой для долгих переходов.'
      },
      {
        id: 'glv_01',
        name: 'Перчатки Стрелка',
        category: 'armor',
        slot: 'gloves',
        rarity: 'common',
        iconKey: 'gloves',
        lvl: 2,
        weight: 0.8,
        price: 60,
        equipped: true,
        primaryStat: { label: 'Скорость атаки', value: '+5%' },
        stats: [
          { name: 'Ловкость (DEX)', value: '+2' }
        ],
        lore: 'Мягкие кожаные перчатки, обеспечивающие надежный хват оружия.'
      },
      {
        id: 'am_01',
        name: 'Амулет Вечного Пламени',
        category: 'armor',
        slot: 'amulet',
        rarity: 'epic',
        iconKey: 'amulet',
        lvl: 7,
        weight: 0.2,
        price: 480,
        equipped: true,
        primaryStat: { label: 'Мана (MP)', value: '+80' },
        stats: [
          { name: 'Реген. маны', value: '+3.5/сек' },
          { name: 'Урон огнем', value: '+15%' }
        ],
        lore: 'В кулоне заключена неугасимая искра из сердца вулкана.'
      },
      {
        id: 'rng_01',
        name: 'Кольцо Храбрости',
        category: 'armor',
        slot: 'ring',
        rarity: 'rare',
        iconKey: 'ring',
        lvl: 4,
        weight: 0.1,
        price: 180,
        equipped: true,
        primaryStat: { label: 'Сила атаки', value: '+8' },
        stats: [
          { name: 'Удача (LCK)', value: '+5' }
        ],
        lore: 'Золотой перстень с рунической гравировкой непоколебимости.'
      },
      {
        id: 'pot_hp_01',
        name: 'Эликсир Здоровья',
        category: 'consumable',
        rarity: 'common',
        iconKey: 'potion_hp',
        quantity: 5,
        weight: 0.5,
        price: 25,
        primaryStat: { label: 'Восстановление', value: '+250 HP' },
        stats: [
          { name: 'Длительность', value: 'Мгновенно' }
        ],
        lore: 'Целебный отвар из трав Солнечной долины. Быстро залечивает раны.'
      },
      {
        id: 'pot_mp_01',
        name: 'Эликсир Маны',
        category: 'consumable',
        rarity: 'common',
        iconKey: 'potion_mp',
        quantity: 3,
        weight: 0.5,
        price: 30,
        primaryStat: { label: 'Восстановление', value: '+180 MP' },
        stats: [
          { name: 'Длительность', value: 'Мгновенно' }
        ],
        lore: 'Дистиллированная эфирная вода для восполнения магической энергии.'
      },
      {
        id: 'scr_tp_01',
        name: 'Свиток Телепортации',
        category: 'consumable',
        rarity: 'rare',
        iconKey: 'scroll',
        quantity: 2,
        weight: 0.1,
        price: 150,
        primaryStat: { label: 'Перемещение', value: 'В столицу' },
        stats: [
          { name: 'Время каста', value: '2.5 сек' }
        ],
        lore: 'Переносит владельца к ближайшему городскому порталу.'
      },
      {
        id: 'crys_01',
        name: 'Кристалл Чистого Эфира',
        category: 'misc',
        rarity: 'epic',
        iconKey: 'crystal',
        quantity: 12,
        weight: 0.1,
        price: 200,
        primaryStat: { label: 'Ресурс', value: 'Материал' },
        stats: [
          { name: 'Качество', value: 'Высшее' }
        ],
        lore: 'Редкий минерал, используемый для улучшения снаряжения и зачарования.'
      }
    ];

    this.activeCategory = 'all';
    this.searchQuery = '';
    this.selectedItem = null;
    this.gold = 1450;
    this.maxWeight = 100.0;
    this.init();
  }

  init() {
    this.renderEquippedSlots();
    this.renderGrid();
    this.bindEvents();
    this.updateWeightAndGold();
    this.bindEngineEvents();

    // Select first weapon by default if available
    if (this.items.length > 0) {
      this.selectItem(this.items[0]);
    }
  }

  bindEngineEvents() {
    EngineBridge.on('set_inventory', (data) => {
      if (Array.isArray(data.items)) this.items = data.items;
      if (typeof data.gold === 'number') this.gold = data.gold;
      if (typeof data.maxWeight === 'number') this.maxWeight = data.maxWeight;
      this.renderEquippedSlots();
      this.renderGrid();
      this.updateWeightAndGold();
    });

    EngineBridge.on('add_item', (item) => {
      this.items.push(item);
      this.renderEquippedSlots();
      this.renderGrid();
      this.updateWeightAndGold();
    });

    EngineBridge.on('update_gold', (data) => {
      this.gold = data.gold || 0;
      this.updateWeightAndGold();
    });
  }

  bindEvents() {
    const catButtons = document.querySelectorAll('#inv-category-filters .cat-btn');
    catButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        catButtons.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        this.activeCategory = btn.dataset.category;
        this.renderGrid();
      });
    });

    const searchInput = document.getElementById('inv-search-input');
    if (searchInput) {
      searchInput.addEventListener('input', (e) => {
        this.searchQuery = e.target.value.toLowerCase().trim();
        this.renderGrid();
      });
    }

    document.getElementById('btn-sort-inv')?.addEventListener('click', () => {
      this.items.sort((a, b) => (b.price || 0) - (a.price || 0));
      this.renderGrid();
    });

    document.getElementById('btn-equip-item')?.addEventListener('click', () => {
      if (this.selectedItem) this.toggleEquip(this.selectedItem);
    });

    document.getElementById('btn-use-item')?.addEventListener('click', () => {
      if (this.selectedItem) this.useItem(this.selectedItem);
    });

    document.getElementById('btn-drop-item')?.addEventListener('click', () => {
      if (this.selectedItem) this.dropItem(this.selectedItem);
    });

    document.querySelectorAll('.equip-slot').forEach(slot => {
      slot.addEventListener('click', () => {
        const slotType = slot.dataset.slot;
        const item = this.items.find(i => i.equipped && i.slot === slotType);
        if (item) {
          this.selectItem(item);
        }
      });
    });
  }

  getFilteredItems() {
    return this.items.filter(item => {
      const matchCat = this.activeCategory === 'all' || item.category === this.activeCategory;
      const matchSearch = !this.searchQuery || item.name.toLowerCase().includes(this.searchQuery);
      return matchCat && matchSearch;
    });
  }

  renderGrid() {
    const grid = document.getElementById('inventory-grid');
    if (!grid) return;
    grid.innerHTML = '';

    const filtered = this.getFilteredItems();

    filtered.forEach(item => {
      const slot = document.createElement('div');
      slot.className = `item-slot ${item.rarity || 'common'} ${this.selectedItem && this.selectedItem.id === item.id ? 'selected' : ''}`;
      slot.title = `${item.name} (${item.type || 'Предмет'})`;

      slot.innerHTML = `
        <div class="item-slot-icon">${getIconSvg(item.iconKey || 'sword')}</div>
        ${item.quantity && item.quantity > 1 ? `<span class="item-quantity">x${item.quantity}</span>` : ''}
        ${item.equipped ? `<span class="item-equipped-badge">E</span>` : ''}
      `;

      slot.addEventListener('click', () => {
        this.selectItem(item);
      });

      grid.appendChild(slot);
    });

    // Render empty slots (at least 32 cells)
    const totalSlots = Math.max(32, filtered.length + 8);
    for (let i = filtered.length; i < totalSlots; i++) {
      const emptySlot = document.createElement('div');
      emptySlot.className = 'item-slot empty';
      grid.appendChild(emptySlot);
    }
  }

  renderEquippedSlots() {
    const slotTypes = ['head', 'chest', 'gloves', 'boots', 'mainhand', 'offhand', 'amulet', 'ring'];
    let powerScore = 0;

    slotTypes.forEach(slotType => {
      const slotEl = document.getElementById(`slot-${slotType}`);
      const parentSlot = slotEl ? slotEl.closest('.equip-slot') : null;
      const equippedItem = this.items.find(i => i.equipped && i.slot === slotType);

      if (slotEl && parentSlot) {
        if (equippedItem) {
          slotEl.innerHTML = `<div class="equipped-svg-wrap">${getIconSvg(equippedItem.iconKey || 'sword')}</div>`;
          parentSlot.classList.add('filled');
          parentSlot.style.borderColor = `var(--rarity-${equippedItem.rarity || 'common'})`;
          powerScore += (equippedItem.lvl || 1) * 45 + (equippedItem.price || 0) * 0.2;
        } else {
          slotEl.innerHTML = '';
          parentSlot.classList.remove('filled');
          parentSlot.style.borderColor = '';
        }
      }
    });

    const scoreEl = document.getElementById('gear-power-score');
    if (scoreEl) scoreEl.textContent = Math.round(powerScore).toLocaleString('ru-RU');
  }

  selectItem(item) {
    this.selectedItem = item;
    this.renderGrid();

    const placeholder = document.getElementById('inspector-placeholder');
    const card = document.getElementById('inspector-card');
    if (placeholder) placeholder.classList.add('hidden');
    if (card) card.classList.remove('hidden');

    document.getElementById('insp-name').textContent = item.name;
    document.getElementById('insp-rarity').textContent = this.getRarityLabel(item.rarity || 'common');
    document.getElementById('insp-rarity').style.color = `var(--rarity-${item.rarity || 'common'})`;
    document.getElementById('insp-type').textContent = (item.category || 'Предмет').toUpperCase();
    
    const iconBox = document.getElementById('insp-icon-box');
    if (iconBox) {
      iconBox.innerHTML = getIconSvg(item.iconKey || 'sword');
      iconBox.style.borderColor = `var(--rarity-${item.rarity || 'common'})`;
    }

    const primaryStat = item.primaryStat || { label: 'Параметр', value: '0' };
    const primaryBox = document.getElementById('insp-primary-stat');
    if (primaryBox) {
      primaryBox.querySelector('.primary-num').textContent = primaryStat.value;
      primaryBox.querySelector('.primary-label').textContent = primaryStat.label;
    }

    const statsList = document.getElementById('insp-stats-list');
    if (statsList) {
      statsList.innerHTML = '';
      if (Array.isArray(item.stats)) {
        item.stats.forEach(st => {
          const row = document.createElement('div');
          row.className = 'item-stat-row';
          row.innerHTML = `<span>${st.name}</span> <span class="stat-plus">${st.value}</span>`;
          statsList.appendChild(row);
        });
      }
    }

    document.getElementById('insp-lore').textContent = item.lore || 'Описание отсутствует.';
    document.getElementById('insp-req-lvl').textContent = item.lvl || 1;
    document.getElementById('insp-weight').textContent = `${(item.weight || 0).toFixed(1)} кг`;
    document.getElementById('insp-price').textContent = `${item.price || 0} G`;

    const equipBtn = document.getElementById('btn-equip-item');
    const useBtn = document.getElementById('btn-use-item');

    if (item.category === 'consumable') {
      equipBtn?.classList.add('hidden');
      useBtn?.classList.remove('hidden');
    } else if (item.slot) {
      equipBtn?.classList.remove('hidden');
      useBtn?.classList.add('hidden');
      if (equipBtn) {
        equipBtn.textContent = item.equipped ? 'Снять снаряжение' : 'Экипировать';
        equipBtn.style.background = item.equipped ? '#64748b' : 'var(--accent-gold)';
      }
    } else {
      equipBtn?.classList.add('hidden');
      useBtn?.classList.add('hidden');
    }
  }

  toggleEquip(item) {
    if (!item.slot) return;

    if (item.equipped) {
      item.equipped = false;
      EngineBridge.send('unequip_item', { id: item.id, slot: item.slot });
    } else {
      this.items.forEach(i => {
        if (i.slot === item.slot && i.equipped) {
          i.equipped = false;
        }
      });
      item.equipped = true;
      EngineBridge.send('equip_item', { id: item.id, slot: item.slot });
    }

    this.renderEquippedSlots();
    this.renderGrid();
    this.selectItem(item);
  }

  useItem(item) {
    if (item.category !== 'consumable') return;

    if (item.quantity && item.quantity > 1) {
      item.quantity--;
    } else {
      const idx = this.items.indexOf(item);
      if (idx !== -1) this.items.splice(idx, 1);
      this.selectedItem = null;
      document.getElementById('inspector-placeholder')?.classList.remove('hidden');
      document.getElementById('inspector-card')?.classList.add('hidden');
    }

    EngineBridge.send('use_item', { id: item.id, name: item.name });
    this.renderGrid();
    this.updateWeightAndGold();
  }

  dropItem(item) {
    const idx = this.items.indexOf(item);
    if (idx !== -1) {
      this.items.splice(idx, 1);
      this.selectedItem = null;
      document.getElementById('inspector-placeholder')?.classList.remove('hidden');
      document.getElementById('inspector-card')?.classList.add('hidden');
      this.renderEquippedSlots();
      this.renderGrid();
      this.updateWeightAndGold();
      EngineBridge.send('drop_item', { id: item.id, name: item.name });
    }
  }

  updateWeightAndGold() {
    const totalWeight = this.items.reduce((sum, item) => sum + ((item.weight || 0) * (item.quantity || 1)), 0);
    const weightText = document.getElementById('weight-text');
    const weightBar = document.getElementById('weight-bar');
    if (weightText) weightText.textContent = `${totalWeight.toFixed(1)} / ${this.maxWeight.toFixed(1)} кг`;
    if (weightBar) weightBar.style.width = `${Math.min(100, (totalWeight / this.maxWeight) * 100)}%`;

    const goldInv = document.getElementById('inv-gold-amount');
    if (goldInv) goldInv.textContent = this.gold.toLocaleString('ru-RU');
  }

  getRarityLabel(rarity) {
    const map = {
      common: 'Обычный',
      rare: 'Редкий',
      epic: 'Эпический',
      legendary: 'Легендарный',
      mythic: 'Мифический'
    };
    return map[rarity] || 'Обычный';
  }
}

// =============================================================================
// 2. WORLD MAP MANAGER
// =============================================================================
class TabMapManager {
  constructor() {
    this.canvas = document.getElementById('world-map-canvas');
    if (!this.canvas) return;
    this.ctx = this.canvas.getContext('2d');

    // Baseline POIs for rich interactive exploration
    this.points = [
      { id: 'poi_01', name: 'Столица Эфирия', category: 'town', iconKey: 'town', x: 500, y: 300, fastTravelCost: 0, desc: 'Величественный белокаменный город, цитадель магов и правителей.' },
      { id: 'poi_02', name: 'Порт Нордхольм', category: 'town', iconKey: 'town', x: 680, y: 220, fastTravelCost: 25, desc: 'Северный торговый порт с выходом к морю Рассвета.' },
      { id: 'poi_03', name: 'Святилище Света', category: 'town', iconKey: 'town', x: 340, y: 380, fastTravelCost: 50, desc: 'Древний монастырь, хранящий Первичную Эфирную Сферу.' },
      { id: 'poi_04', name: 'Подземелье Древних', category: 'dungeon', iconKey: 'dungeon', x: 420, y: 180, fastTravelCost: 75, desc: 'Лабиринт руин с древними стражами и эпической добычей.' },
      { id: 'poi_05', name: 'Катакомбы Тьмы', category: 'dungeon', iconKey: 'dungeon', x: 620, y: 420, fastTravelCost: 100, desc: 'Заброшенные шахты, кишащие теневыми созданиями.' },
      { id: 'poi_06', name: 'Задание: Поиск Реликвии', category: 'quest', iconKey: 'quest', x: 560, y: 260, fastTravelCost: 40, desc: 'Разыскать фрагмент Солнечной короны для верховного жреца.' },
      { id: 'poi_07', name: 'Задание: Драконий Пик', category: 'quest', iconKey: 'quest', x: 300, y: 220, fastTravelCost: 60, desc: 'Очистить горный перевал от виверн и разбойников.' }
    ];

    this.selectedPOI = this.points[0];
    this.playerPos = { x: 500, y: 300 };

    this.scale = 1.0;
    this.offsetX = 0;
    this.offsetY = 0;
    this.isDragging = false;
    this.startX = 0;
    this.startY = 0;
    this.activeFilter = 'all';

    this.init();
  }

  init() {
    this.resize();
    window.addEventListener('resize', () => this.resize());
    this.bindControls();
    this.renderPOIList();
    this.selectPOI(this.points[0]);
    this.draw();
    this.bindEngineEvents();
  }

  bindEngineEvents() {
    EngineBridge.on('set_map_pois', (data) => {
      if (Array.isArray(data.pois)) this.points = data.pois;
      if (data.regionName) {
        const regEl = document.getElementById('current-region-name');
        if (regEl) regEl.textContent = data.regionName;
      }
      this.renderPOIList();
      this.draw();
    });

    EngineBridge.on('set_player_pos', (data) => {
      if (typeof data.x === 'number' && typeof data.y === 'number') {
        this.playerPos = { x: data.x, y: data.y };
        this.draw();
      }
    });
  }

  resize() {
    const container = document.getElementById('map-container');
    if (container && this.canvas) {
      this.canvas.width = container.clientWidth;
      this.canvas.height = container.clientHeight;
      this.draw();
    }
  }

  bindControls() {
    this.canvas.addEventListener('mousedown', (e) => {
      this.isDragging = true;
      this.startX = e.clientX - this.offsetX;
      this.startY = e.clientY - this.offsetY;
    });

    window.addEventListener('mousemove', (e) => {
      if (this.isDragging) {
        this.offsetX = e.clientX - this.startX;
        this.offsetY = e.clientY - this.startY;
        this.draw();
      }
    });

    window.addEventListener('mouseup', () => {
      this.isDragging = false;
    });

    document.getElementById('btn-map-zoom-in')?.addEventListener('click', () => {
      this.scale = Math.min(2.5, this.scale + 0.2);
      this.draw();
    });

    document.getElementById('btn-map-zoom-out')?.addEventListener('click', () => {
      this.scale = Math.max(0.5, this.scale - 0.2);
      this.draw();
    });

    document.getElementById('btn-map-recenter')?.addEventListener('click', () => {
      this.scale = 1.1;
      this.offsetX = -(this.playerPos.x - 500) * this.scale;
      this.offsetY = -(this.playerPos.y - 300) * this.scale;
      this.draw();
    });

    document.getElementById('btn-map-fullscreen')?.addEventListener('click', () => {
      this.scale = 1.0;
      this.offsetX = 0;
      this.offsetY = 0;
      this.draw();
    });

    this.canvas.addEventListener('click', (e) => {
      const rect = this.canvas.getBoundingClientRect();
      const mouseX = (e.clientX - rect.left - this.canvas.width / 2 - this.offsetX) / this.scale + 500;
      const mouseY = (e.clientY - rect.top - this.canvas.height / 2 - this.offsetY) / this.scale + 300;

      for (let poi of this.points) {
        const dx = mouseX - poi.x;
        const dy = mouseY - poi.y;
        if (Math.hypot(dx, dy) < 28) {
          this.selectPOI(poi);
          break;
        }
      }
    });

    const filterTabs = document.querySelectorAll('.poi-category-tabs .poi-tab');
    filterTabs.forEach(tab => {
      tab.addEventListener('click', () => {
        filterTabs.forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        this.activeFilter = tab.dataset.filter;
        this.renderPOIList();
      });
    });

    document.getElementById('btn-fast-travel')?.addEventListener('click', () => {
      if (!this.selectedPOI) return;
      this.playerPos = { x: this.selectedPOI.x, y: this.selectedPOI.y };
      this.draw();
      EngineBridge.send('fast_travel', { id: this.selectedPOI.id, pos: this.playerPos, name: this.selectedPOI.name });
    });
  }

  selectPOI(poi) {
    this.selectedPOI = poi;
    this.updatePOIDetails(poi);
    this.renderPOIList();
    this.draw();
  }

  updatePOIDetails(poi) {
    const card = document.getElementById('poi-detail-card');
    if (!card) return;

    if (!poi) {
      card.classList.add('hidden');
      return;
    }

    card.classList.remove('hidden');
    const cardIcon = document.getElementById('poi-card-icon');
    if (cardIcon) cardIcon.innerHTML = getIconSvg(poi.iconKey || 'town');
    document.getElementById('poi-card-title').textContent = poi.name;
    document.getElementById('poi-card-type').textContent = (poi.category || 'Локация').toUpperCase();
    document.getElementById('poi-card-desc').textContent = poi.desc || '';
    const costTag = document.getElementById('btn-fast-travel')?.querySelector('.cost-tag');
    if (costTag) costTag.textContent = `${poi.fastTravelCost || 0} G`;
  }

  renderPOIList() {
    const list = document.getElementById('poi-list-container');
    if (!list) return;
    list.innerHTML = '';

    const filtered = this.points.filter(p => this.activeFilter === 'all' || p.category === this.activeFilter);

    if (filtered.length === 0) {
      list.innerHTML = `
        <div class="empty-saves-box" style="padding: 30px 10px;">
          <svg viewBox="0 0 24 24" width="36" height="36" fill="none" stroke="currentColor" stroke-width="1.6"><circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/></svg>
          <h4>Нет обнаруженных локаций</h4>
          <p style="font-size:0.8rem;color:#64748b;">Локации появятся по мере исследования игрового мира.</p>
        </div>
      `;
      return;
    }

    filtered.forEach(poi => {
      const card = document.createElement('div');
      card.className = `poi-card ${this.selectedPOI && this.selectedPOI.id === poi.id ? 'active' : ''}`;
      card.innerHTML = `
        <div class="poi-card-svg">${getIconSvg(poi.iconKey || 'town')}</div>
        <div class="poi-card-info">
          <h4>${poi.name}</h4>
          <span>${(poi.category || '').toUpperCase()} • ${poi.fastTravelCost || 0} G</span>
        </div>
      `;

      card.addEventListener('click', () => {
        this.selectPOI(poi);
      });

      list.appendChild(card);
    });
  }

  draw() {
    if (!this.ctx) return;
    const ctx = this.ctx;
    const w = this.canvas.width;
    const h = this.canvas.height;

    ctx.clearRect(0, 0, w, h);
    ctx.save();
    ctx.translate(w / 2 + this.offsetX, h / 2 + this.offsetY);
    ctx.scale(this.scale, this.scale);
    ctx.translate(-500, -300);

    // Ocean / Map Base
    ctx.fillStyle = '#eaf2fa';
    ctx.fillRect(-300, -300, 1600, 1200);

    // Grid coordinates
    ctx.strokeStyle = 'rgba(203, 213, 225, 0.45)';
    ctx.lineWidth = 1;
    for (let x = 0; x <= 1000; x += 100) {
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, 600); ctx.stroke();
    }
    for (let y = 0; y <= 600; y += 100) {
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(1000, y); ctx.stroke();
    }

    // Main Continent Landmass
    ctx.fillStyle = '#ffffff';
    ctx.strokeStyle = '#cbd5e1';
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.ellipse(500, 300, 380, 220, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();

    // Mountains / Rivers Decoration
    ctx.strokeStyle = 'rgba(148, 163, 184, 0.4)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(350, 240); ctx.lineTo(400, 210); ctx.lineTo(460, 250); ctx.lineTo(520, 220); ctx.lineTo(580, 260);
    ctx.stroke();

    // POI Markers
    this.points.forEach(poi => {
      const isSelected = this.selectedPOI && this.selectedPOI.id === poi.id;

      if (isSelected) {
        ctx.beginPath();
        ctx.arc(poi.x, poi.y, 22, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(217, 119, 6, 0.25)';
        ctx.fill();
      }

      ctx.beginPath();
      ctx.arc(poi.x, poi.y, 12, 0, Math.PI * 2);
      ctx.fillStyle = '#ffffff';
      ctx.fill();
      ctx.strokeStyle = isSelected ? '#d97706' : '#64748b';
      ctx.lineWidth = 2.5;
      ctx.stroke();

      ctx.beginPath();
      ctx.arc(poi.x, poi.y, 5.5, 0, Math.PI * 2);
      ctx.fillStyle = isSelected ? '#d97706' : (poi.category === 'quest' ? '#eab308' : (poi.category === 'dungeon' ? '#ef4444' : '#10b981'));
      ctx.fill();

      ctx.font = 'bold 11px Plus Jakarta Sans, sans-serif';
      ctx.fillStyle = isSelected ? '#d97706' : '#0f172a';
      ctx.textAlign = 'center';
      ctx.fillText(poi.name, poi.x, poi.y + 24);
    });

    // Player Live Location Pin
    const px = this.playerPos.x;
    const py = this.playerPos.y;

    ctx.beginPath();
    ctx.arc(px, py, 18, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(2, 132, 199, 0.25)';
    ctx.fill();

    ctx.beginPath();
    ctx.arc(px, py, 9, 0, Math.PI * 2);
    ctx.fillStyle = '#0284c7';
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2.5;
    ctx.fill();
    ctx.stroke();

    ctx.font = 'bold 10px Plus Jakarta Sans, sans-serif';
    ctx.fillStyle = '#0284c7';
    ctx.textAlign = 'center';
    ctx.fillText('ИГРОК', px, py - 16);

    ctx.restore();
  }
}

// =============================================================================
// 3. CHARACTER STATS MANAGER
// =============================================================================
class TabCharacterManager {
  constructor() {
    this.name = 'Герой Эфирии';
    this.level = 5;
    this.xp = 3450;
    this.maxXP = 5000;
    this.stats = { str: 18, dex: 15, int: 14, vit: 16, lck: 12 };
    this.freePoints = 4;
    this.init();
  }

  init() {
    this.bindStatButtons();
    this.updateStatsDisplay();
    this.bindEngineEvents();
  }

  bindEngineEvents() {
    EngineBridge.on('set_character_stats', (data) => {
      if (data.name) this.name = data.name;
      if (typeof data.level === 'number') this.level = data.level;
      if (typeof data.xp === 'number') this.xp = data.xp;
      if (typeof data.maxXP === 'number') this.maxXP = data.maxXP;
      if (data.stats) Object.assign(this.stats, data.stats);
      if (typeof data.freePoints === 'number') this.freePoints = data.freePoints;

      const nameHeading = document.getElementById('char-name-heading');
      if (nameHeading) nameHeading.textContent = this.name;

      const lvlBadge = document.getElementById('char-level-badge');
      if (lvlBadge) lvlBadge.textContent = this.level;

      const xpPct = Math.min(100, Math.round((this.xp / (this.maxXP || 1)) * 100));
      const xpText = document.getElementById('char-xp-text');
      const xpBar = document.getElementById('char-xp-bar');
      if (xpText) xpText.textContent = `${this.xp.toLocaleString('ru-RU')} / ${this.maxXP.toLocaleString('ru-RU')} XP (${xpPct}%)`;
      if (xpBar) xpBar.style.width = `${xpPct}%`;

      this.updateStatsDisplay();
    });
  }

  bindStatButtons() {
    document.querySelectorAll('.add-stat-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const statKey = btn.dataset.stat;
        if (this.freePoints > 0 && this.stats[statKey] !== undefined) {
          this.stats[statKey]++;
          this.freePoints--;
          this.updateStatsDisplay();
          EngineBridge.send('attribute_allocated', { stat: statKey, value: this.stats[statKey] });
        }
      });
    });
  }

  updateStatsDisplay() {
    document.getElementById('stat-str').textContent = this.stats.str;
    document.getElementById('stat-dex').textContent = this.stats.dex;
    document.getElementById('stat-int').textContent = this.stats.int;
    document.getElementById('stat-vit').textContent = this.stats.vit;
    document.getElementById('stat-lck').textContent = this.stats.lck;

    const freePtsEl = document.getElementById('free-stat-points');
    if (freePtsEl) freePtsEl.textContent = this.freePoints;

    const hp = this.stats.vit * 25 + this.level * 40;
    const mp = this.stats.int * 15 + this.level * 20;
    const minAtk = this.stats.str * 3 + this.stats.dex;
    const maxAtk = Math.floor(minAtk * 1.45);
    const matk = this.stats.int * 4;
    const armor = this.stats.vit * 2 + 10;
    const mres = Math.min(75, Math.round(this.stats.int * 0.8));
    const crit = (this.stats.dex * 0.6 + this.stats.lck * 0.4).toFixed(1);
    const speed = (100 + this.stats.dex * 0.5).toFixed(0);

    document.getElementById('c-stat-hp').textContent = hp.toLocaleString('ru-RU');
    document.getElementById('c-stat-mp').textContent = mp.toLocaleString('ru-RU');
    document.getElementById('c-stat-atk').textContent = `${minAtk} - ${maxAtk}`;
    document.getElementById('c-stat-matk').textContent = matk;
    document.getElementById('c-stat-armor').textContent = armor;
    document.getElementById('c-stat-mres').textContent = `${mres}%`;
    document.getElementById('c-stat-crit').textContent = `${crit}%`;
    document.getElementById('c-stat-speed').textContent = `${speed}%`;
  }
}

// =============================================================================
// 4. TAB ULTRA AAA SETTINGS CONTROLLER
// =============================================================================
class TabSettingsManager {
  constructor() {
    this.init();
  }

  init() {
    // Subtabs switcher
    const subBtns = document.querySelectorAll('.settings-sub-btn');
    const subPanels = document.querySelectorAll('.settings-subpanel');

    subBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        subBtns.forEach(b => b.classList.remove('active'));
        subPanels.forEach(p => p.classList.remove('active'));

        btn.classList.add('active');
        const target = document.getElementById(`subpanel-${btn.dataset.subtab}`);
        if (target) target.classList.add('active');
      });
    });

    // Sliders & value displays helper
    const bindSlider = (id, valId, unit = '%') => {
      const el = document.getElementById(id);
      const valEl = document.getElementById(valId);
      if (el && valEl) {
        el.addEventListener('input', (e) => {
          valEl.textContent = `${e.target.value}${unit}`;
        });
      }
    };

    bindSlider('set-fov', 'val-set-fov', '°');
    bindSlider('set-hdr-nits', 'val-set-hdr-nits', ' nits');
    bindSlider('set-upscale-sharpness', 'val-set-sharpness', '%');
    bindSlider('set-lod-bias', 'val-set-lod-bias', '%');
    bindSlider('set-film-grain', 'val-set-film-grain', '%');
    bindSlider('set-mouse-sens', 'val-mouse-sens', '');

    // Volume sliders
    bindSlider('vol-master', 'val-vol-master', '%');
    bindSlider('vol-music', 'val-vol-music', '%');
    bindSlider('vol-sfx', 'val-vol-sfx', '%');
    bindSlider('vol-voice', 'val-vol-voice', '%');
    bindSlider('vol-ambience', 'val-vol-ambience', '%');

    // Populate actual monitor resolutions
    function populateTabResolutionDropdown() {
      const select = document.getElementById('set-resolution');
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

      const currentRes = window._currentEngineSettings?.resolution;
      if (currentRes && unique.includes(currentRes)) {
        select.value = currentRes;
      } else {
        select.value = unique[0];
      }
    }
    populateTabResolutionDropdown();

    // Sound test button (Explicit Web Audio playback)
    document.getElementById('btn-test-sound')?.addEventListener('click', () => {
      if (window.Audio && window.Audio.playTestSound) {
        window.Audio.playTestSound();
      }
    });

    // Save settings button
    document.getElementById('btn-save-settings')?.addEventListener('click', () => {
      const payload = {
        display: {
          mode: document.getElementById('set-display-mode')?.value,
          resolution: document.getElementById('set-resolution')?.value,
          refreshRate: document.getElementById('set-refresh-rate')?.value,
          vsync: document.getElementById('set-vsync')?.value,
          fpsLimit: document.getElementById('set-fps-limit')?.value,
          fov: document.getElementById('set-fov')?.value,
          upscaler: document.getElementById('set-upscaler-tech')?.value,
          upscaleMode: document.getElementById('set-upscaler-mode')?.value,
          mfg: document.getElementById('set-frame-gen-mode')?.value,
          reflex: document.getElementById('set-reflex')?.value,
          rayReconstruction: document.getElementById('set-dlss-ray-recon')?.value,
          sharpness: document.getElementById('set-upscale-sharpness')?.value,
          hdrMode: document.getElementById('set-hdr-mode')?.value,
          hdrNits: document.getElementById('set-hdr-nits')?.value
        },
        graphics: {
          preset: document.getElementById('set-graphics-preset')?.value,
          textures: document.getElementById('set-tex-quality')?.value,
          anisotropic: document.getElementById('set-aniso-filter')?.value,
          vrs: document.getElementById('set-vrs')?.value,
          meshNanite: document.getElementById('set-mesh-quality')?.value,
          lodBias: document.getElementById('set-lod-bias')?.value,
          raytracingMode: document.getElementById('set-raytracing-mode')?.value,
          gi: document.getElementById('set-rt-gi')?.value,
          reflections: document.getElementById('set-rt-reflections')?.value,
          shadows: document.getElementById('set-rt-shadows')?.value,
          bounces: document.getElementById('set-rt-bounces')?.value,
          denoiser: document.getElementById('set-rt-denoiser')?.value,
          shadowQuality: document.getElementById('set-shadow-quality')?.value,
          volumetricFog: document.getElementById('set-volumetrics')?.value,
          volumetricClouds: document.getElementById('set-clouds')?.value,
          waterSimulation: document.getElementById('set-water-sim')?.value,
          subsurfaceScattering: document.getElementById('set-sss')?.checked,
          foliage: document.getElementById('set-foliage')?.value,
          ssr: document.getElementById('set-ssr')?.value,
          motionBlur: document.getElementById('set-motion-blur')?.value,
          depthOfField: document.getElementById('set-dof-mode')?.value,
          filmGrain: document.getElementById('set-film-grain')?.value,
          chromaticAberration: document.getElementById('set-chromatic-aberration')?.checked,
          bloom: document.getElementById('set-bloom')?.checked
        },
        audio: {
          outputDevice: document.getElementById('set-audio-device')?.value,
          micDevice: document.getElementById('set-mic-device')?.value,
          spatialAudio: document.getElementById('set-spatial-audio')?.checked,
          master: document.getElementById('vol-master')?.value,
          music: document.getElementById('vol-music')?.value,
          sfx: document.getElementById('vol-sfx')?.value,
          voice: document.getElementById('vol-voice')?.value,
          ambience: document.getElementById('vol-ambience')?.value
        },
        gameplay: {
          difficulty: document.getElementById('set-difficulty')?.value,
          mouseSens: document.getElementById('set-mouse-sens')?.value,
          invertY: document.getElementById('set-invert-y')?.checked,
          autoloot: document.getElementById('set-autoloot')?.checked,
          subtitles: document.getElementById('set-subtitles')?.checked,
          fpsCounter: document.getElementById('set-fps-counter')?.checked
        }
      };

      const mode = document.getElementById('set-display-mode')?.value || 'windowed';
      const resolution = document.getElementById('set-resolution')?.value || '1920x1080';
      const vsync = document.getElementById('set-vsync')?.value || 'on';
      const fov = document.getElementById('set-fov')?.value || '95';
      const upscaler = document.getElementById('set-upscaler-tech')?.value || 'fsr';
      const upscaleMode = document.getElementById('set-upscaler-mode')?.value || 'quality';
      const rayReconstruction = document.getElementById('set-dlss-ray-recon')?.value || 'off';
      const raytracingMode = document.getElementById('set-raytracing-mode')?.value || 'off';
      const volumetricFog = document.getElementById('set-volumetrics')?.value || 'ultra';
      const master = document.getElementById('vol-master')?.value || '85';

      payload.action = 'save_settings';
      payload.event = 'save_settings';
      payload.mode = mode;
      payload.resolution = resolution;
      payload.vsync = vsync;
      payload.fov = parseFloat(fov);
      payload.upscaler = upscaler;
      payload.upscaleMode = upscaleMode;
      payload.rayReconstruction = rayReconstruction;
      payload.raytracingMode = raytracingMode;
      payload.volumetricFog = volumetricFog;
      payload.master = parseFloat(master);

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
    });

    // Reset settings button
    document.getElementById('btn-reset-settings')?.addEventListener('click', () => {
      EngineBridge.send('reset_settings_default');
    });
  }
}

// =============================================================================
// 5. MAIN TAB NAVIGATION & LIFECYCLE
// =============================================================================
document.addEventListener('DOMContentLoaded', () => {
  new BackgroundCanvas('bg-canvas');
  const inv = new TabInventoryManager();
  const map = new TabMapManager();
  const chr = new TabCharacterManager();
  const set = new TabSettingsManager();

  function switchTab(tabName) {
    document.querySelectorAll('.tab-nav-btn').forEach(b => {
      b.classList.toggle('active', b.dataset.tab === tabName);
    });
    document.querySelectorAll('.tab-panel').forEach(p => {
      p.classList.toggle('active', p.id === `tab-${tabName}`);
    });
    if (tabName === 'map') {
      setTimeout(() => map.resize(), 60);
    }
  }

  document.querySelectorAll('.tab-nav-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      switchTab(btn.dataset.tab);
    });
  });

  // Hotkeys: I (Inventory), M (Map), C (Character), O (Settings), ESC (Close), Alt+F4 (Exit)
  window.addEventListener('keydown', (e) => {
    if (e.altKey && (e.key === 'F4' || e.keyCode === 115)) {
      e.preventDefault();
      EngineBridge.send('REQUEST_EXIT');
      return;
    }

    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;

    const key = e.key.toUpperCase();
    if (key === 'I') switchTab('inventory');
    else if (key === 'M') switchTab('map');
    else if (key === 'C') switchTab('character');
    else if (key === 'O') switchTab('settings');
    else if (e.key === 'Escape') {
      EngineBridge.send('RESUME_GAME');
      EngineBridge.send('close_tab_menu');
    }
  });

  // Close / Resume Button
  document.getElementById('btn-close-tab-menu')?.addEventListener('click', () => {
    EngineBridge.send('RESUME_GAME');
    EngineBridge.send('close_tab_menu');
  });

  // Live Hardware Capability & Current Settings Synchronization
  EngineBridge.on('updateTelemetry', (data) => {
    if (!data) return;
    if (data.capabilities) {
      window._capabilities = data.capabilities;
      if (data.capabilities.supportedResolutions) {
        window._supportedResolutions = data.capabilities.supportedResolutions;
      }

      const upscaleSelect = document.getElementById('set-upscaler-tech');
      if (upscaleSelect) {
        const dlssOpt = upscaleSelect.querySelector('option[value="dlss"]');
        if (dlssOpt) {
          dlssOpt.style.display = data.capabilities.dlssSupported ? '' : 'none';
          if (!data.capabilities.dlssSupported && upscaleSelect.value === 'dlss') upscaleSelect.value = 'fsr';
        }
      }

      const rrItem = document.getElementById('set-dlss-ray-recon')?.closest('.setting-item');
      if (rrItem) {
        rrItem.style.display = (data.capabilities.dlssSupported && data.capabilities.dxrSupported) ? '' : 'none';
      }

      const rtSelect = document.getElementById('set-raytracing-mode');
      if (rtSelect) {
        const ptOpt = rtSelect.querySelector('option[value="path_tracing"]');
        const rtOpt = rtSelect.querySelector('option[value="ray_tracing"]');
        if (!data.capabilities.dxrSupported) {
          if (ptOpt) ptOpt.style.display = 'none';
          if (rtOpt) rtOpt.style.display = 'none';
          rtSelect.value = 'off';
        } else {
          if (ptOpt) ptOpt.style.display = '';
          if (rtOpt) rtOpt.style.display = '';
        }
      }
    }

    if (data.currentSettings) {
      window._currentEngineSettings = data.currentSettings;
      const s = data.currentSettings;
      if (document.getElementById('set-display-mode') && s.mode) document.getElementById('set-display-mode').value = s.mode;
      if (document.getElementById('set-resolution') && s.resolution) document.getElementById('set-resolution').value = s.resolution;
      if (document.getElementById('set-vsync')) document.getElementById('set-vsync').value = s.vsync ? 'on' : 'off';
      if (document.getElementById('set-fov') && s.fov) document.getElementById('set-fov').value = s.fov;
      if (document.getElementById('set-upscaler-tech') && s.upscaler) document.getElementById('set-upscaler-tech').value = s.upscaler;
      if (document.getElementById('set-dlss-ray-recon') && s.rayReconstruction !== undefined) {
        document.getElementById('set-dlss-ray-recon').value = (s.rayReconstruction === true || s.rayReconstruction === 'true' || s.rayReconstruction === 'on') ? 'v2' : s.rayReconstruction;
      }
      if (document.getElementById('set-raytracing-mode') && s.raytracingMode) document.getElementById('set-raytracing-mode').value = s.raytracingMode;
    }
  });
});
