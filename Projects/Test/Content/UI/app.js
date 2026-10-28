/**
 * Idontknow — CORE ENGINE & SHARED MODULES (app.js)
 * Features:
 * - C++ Engine Bridge (seamless two-way communication with WebView2 / C++ Host Engine)
 * - Pure Web Audio API Sound Synthesizer (for explicit "Тест звука" button)
 * - Vector SVG Icon Library (Zero Emojis, pure modern SVG graphics)
 * - Dynamic Background Canvas Atmosphere Particles
 * - Shared Game State & Telemetry Synchronization
 */

// =============================================================================
// 1. C++ ENGINE BRIDGE
// =============================================================================
window.EngineBridge = {
  // Send action or event to C++ Game Engine
  send: function(eventName, data = {}) {
    console.log('[C++ Bridge Send]', eventName, data);

    const payload = {
      action: eventName,
      event: eventName,
      data: data,
      ...data
    };
    const jsonStr = JSON.stringify(payload);

    // 1. Microsoft WebView2
    if (window.chrome && window.chrome.webview) {
      try {
        // Send string for maximum C++ compatibility
        window.chrome.webview.postMessage(jsonStr);
      } catch (e) {
        window.chrome.webview.postMessage(payload);
      }
    }
    // 2. CEF / CefSharp Query
    else if (window.cefQuery) {
      window.cefQuery({ request: jsonStr });
    }
    // 3. Unreal Engine / Coherent Gameface / WebKit
    else if (window.ue && window.ue.game) {
      if (typeof window.ue.game[eventName] === 'function') {
        window.ue.game[eventName](jsonStr);
      }
    }
    // 4. Custom Qt / Ultralight / WebKit bridge
    else if (window.external && typeof window.external.invoke === 'function') {
      window.external.invoke(jsonStr);
    }

    // Also dispatch as DOM custom event for JS listeners
    window.dispatchEvent(new CustomEvent('engine-event', { detail: payload }));
  },

  // Listen for calls coming from C++ into JS
  on: function(eventName, callback) {
    window.addEventListener(`engine-call-${eventName}`, (e) => callback(e.detail));
  },

  // Helper C++ can invoke directly: EngineBridge.receive('updateTelemetry', { fps: 60 })
  receive: function(eventName, data) {
    console.log('[C++ Bridge Receive]', eventName, data);
    window.dispatchEvent(new CustomEvent(`engine-call-${eventName}`, { detail: data }));
  }
};

// Global shorthand for sending actions
window.sendAction = function(actionName, data = {}) {
  window.EngineBridge.send(actionName, data);
};

// =============================================================================
// 2. VECTOR SVG ICONS LIBRARY (Pure CSS & SVG)
// =============================================================================
const SVG_ICONS = {
  sword: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="19" y1="5" x2="5" y2="19"/><polyline points="14 5 19 5 19 10"/><line x1="5" y1="15" x2="9" y2="19"/></svg>`,
  staff: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 18L18 6"/><circle cx="19" cy="5" r="3"/><line x1="3" y1="21" x2="6" y2="18"/></svg>`,
  bow: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 3a15 15 0 0 0-15 15"/><line x1="3" y1="18" x2="18" y2="3"/><line x1="18" y1="3" x2="21" y2="6"/><line x1="3" y1="18" x2="6" y2="21"/></svg>`,
  armor: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 4h4l4 4 4-4h4v6l-2 9H6L4 10V4z"/><line x1="12" y1="8" x2="12" y2="19"/></svg>`,
  helmet: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 2a9 9 0 0 0-9 9v7h4v-3h10v3h4v-7a9 9 0 0 0-9-9z"/><line x1="12" y1="2" x2="12" y2="12"/><line x1="7" y1="12" x2="17" y2="12"/></svg>`,
  shield: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>`,
  boots: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 3h6v12h4a3 3 0 0 1 3 3v3H5a2 2 0 0 1-2-2V7a4 4 0 0 1 3-4z"/></svg>`,
  gloves: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 14V8a2 2 0 0 1 4 0v-2a2 2 0 0 1 4 0v-1a2 2 0 0 1 4 0v9a5 5 0 0 1-5 5H9a5 5 0 0 1-5-5v-1a2 2 0 0 1 2-2h0"/></svg>`,
  ring: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="14" r="7"/><polygon points="12 2 15 6 9 6 12 2"/></svg>`,
  amulet: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 3c2 4 4 7 6 7s4-3 6-7"/><circle cx="12" cy="16" r="4"/></svg>`,
  potion_hp: `<svg class="item-svg potion-red" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 2h6"/><path d="M10 2v4l-5 9a3 3 0 0 0 3 5h8a3 3 0 0 0 3-5l-5-9V2"/><line x1="7" y1="15" x2="17" y2="15"/></svg>`,
  potion_mp: `<svg class="item-svg potion-blue" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 2h6"/><path d="M10 2v4l-5 9a3 3 0 0 0 3 5h8a3 3 0 0 0 3-5l-5-9V2"/><line x1="7" y1="15" x2="17" y2="15"/></svg>`,
  scroll: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/></svg>`,
  crystal: `<svg class="item-svg potion-cyan" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="6 3 18 3 22 9 12 22 2 9 6 3"/><line x1="2" y1="9" x2="22" y2="9"/><polyline points="12 22 7 9 12 3 17 9 12 22"/></svg>`,
  town: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 21h18M5 21V7l7-4 7 4v14M9 10a2 2 0 1 0 0-4 2 2 0 0 0 0 4zm6 0a2 2 0 1 0 0-4 2 2 0 0 0 0 4z"/></svg>`,
  dungeon: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/></svg>`,
  quest: `<svg class="item-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/></svg>`
};

function getIconSvg(key) {
  return SVG_ICONS[key] || SVG_ICONS.sword;
}

// =============================================================================
// 3. WEB AUDIO SYNTHESIZER (Explicit Sound Test & C++ Feedback)
// =============================================================================
class SoundEngine {
  constructor() {
    this.ctx = null;
    this.masterGain = null;
    this.sfxVolume = 0.85;
  }

  init() {
    if (!this.ctx) {
      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      if (AudioCtx) {
        this.ctx = new AudioCtx();
        this.masterGain = this.ctx.createGain();
        this.masterGain.gain.setValueAtTime(this.sfxVolume, this.ctx.currentTime);
        this.masterGain.connect(this.ctx.destination);
      }
    }
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume();
    }
  }

  setVolume(vol) {
    this.sfxVolume = Math.max(0, Math.min(1, vol));
    if (this.masterGain && this.ctx) {
      this.masterGain.gain.setValueAtTime(this.sfxVolume, this.ctx.currentTime);
    }
  }

  playTestSound() {
    try {
      this.init();
      if (!this.ctx) return;
      const now = this.ctx.currentTime;
      const chord = [440, 554.37, 659.25, 880]; // A Major Chord Harmonic

      chord.forEach((freq, idx) => {
        const osc = this.ctx.createOscillator();
        const gain = this.ctx.createGain();
        osc.type = 'sine';
        osc.frequency.setValueAtTime(freq, now);

        gain.gain.setValueAtTime(0, now);
        gain.gain.linearRampToValueAtTime(0.12 / chord.length, now + 0.04 + idx * 0.02);
        gain.gain.exponentialRampToValueAtTime(0.0001, now + 0.7);

        osc.connect(gain);
        gain.connect(this.masterGain);

        osc.start(now);
        osc.stop(now + 0.75);
      });
    } catch (e) {
      console.warn('Audio test playback failed:', e);
    }
  }
}

window.Audio = new SoundEngine();

// =============================================================================
// 4. BACKGROUND CANVAS PARTICLES (Atmosphere)
// =============================================================================
class BackgroundCanvas {
  constructor(canvasId) {
    this.canvas = document.getElementById(canvasId);
    if (!this.canvas) return;
    this.ctx = this.canvas.getContext('2d');
    this.particles = [];
    this.numParticles = 40;
    this.init();
  }

  init() {
    this.resize();
    window.addEventListener('resize', () => this.resize());
    this.createParticles();
    this.animate();
  }

  resize() {
    if (!this.canvas) return;
    this.width = this.canvas.width = window.innerWidth;
    this.height = this.canvas.height = window.innerHeight;
  }

  createParticles() {
    this.particles = [];
    for (let i = 0; i < this.numParticles; i++) {
      this.particles.push({
        x: Math.random() * this.width,
        y: Math.random() * this.height,
        radius: Math.random() * 2.2 + 0.8,
        speedX: (Math.random() - 0.5) * 0.35,
        speedY: (Math.random() - 0.5) * 0.35 - 0.1,
        opacity: Math.random() * 0.45 + 0.15,
        pulse: Math.random() * 0.02 + 0.005
      });
    }
  }

  animate() {
    if (!this.ctx) return;
    this.ctx.clearRect(0, 0, this.width, this.height);

    for (let p of this.particles) {
      p.x += p.speedX;
      p.y += p.speedY;

      p.opacity += p.pulse;
      if (p.opacity > 0.6 || p.opacity < 0.15) {
        p.pulse = -p.pulse;
      }

      if (p.x < 0) p.x = this.width;
      if (p.x > this.width) p.x = 0;
      if (p.y < 0) p.y = this.height;
      if (p.y > this.height) p.y = 0;

      this.ctx.beginPath();
      this.ctx.arc(p.x, p.y, p.radius, 0, Math.PI * 2);
      this.ctx.fillStyle = `rgba(217, 119, 6, ${p.opacity * 0.6})`;
      this.ctx.fill();
    }

    requestAnimationFrame(() => this.animate());
  }
}

// Global no-op toast
window.showToast = function() {};
