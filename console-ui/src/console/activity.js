// Activity strips (spec §3.5): rolling bars of what each station sends,
// pre-fader, fed by the meters stream. Drawn on one rAF loop for all cards.

import { dbToUnit } from '../lib/format.js';
import { STATION_COLORS } from '../lib/tokens.js';

const BARS = 64;

export class ActivityHub {
  constructor(client) {
    /** @type {Map<string, {hist:number[], canvas:HTMLCanvasElement|null, look:{alpha:number, grey:boolean}}>} */
    this.strips = new Map();
    client.onMeters(m => {
      for (const [id, [peak]] of Object.entries(m.stations ?? {})) this.push(id, dbToUnit(peak));
      // stations missing from the frame (lost/offline) decay to silence
      for (const [id, s] of this.strips) if (!(id in (m.stations ?? {}))) s.hist.push(0), s.hist.shift();
    });
    const loop = () => { this.draw(); requestAnimationFrame(loop); };
    requestAnimationFrame(loop);
  }

  strip(id) {
    if (!this.strips.has(id)) this.strips.set(id, { hist: new Array(BARS).fill(0), canvas: null, look: { alpha: 0.9, grey: false, color: 0 } });
    return this.strips.get(id);
  }

  push(id, v) {
    const s = this.strip(id);
    s.hist.push(v);
    s.hist.shift();
  }

  attach(id, canvas) { this.strip(id).canvas = canvas; }
  detach(id) { this.strips.delete(id); }

  /** Emphasis per state: 0.9 normal, 0.4 dimmed, 0.18 muted, grey when down. */
  setLook(id, look) { Object.assign(this.strip(id).look, look); }

  draw() {
    if (document.hidden) return;
    const dpr = window.devicePixelRatio || 1;
    for (const { hist, canvas, look } of this.strips.values()) {
      if (!canvas || !canvas.isConnected) continue;
      const w = canvas.clientWidth, h = canvas.clientHeight;
      if (!w || !h) continue;
      if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
        canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr);
      }
      const g = canvas.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, h);
      g.fillStyle = look.grey ? '#4a4e4b' : STATION_COLORS[look.color % STATION_COLORS.length];
      g.globalAlpha = look.alpha;
      const bw = w / BARS;
      for (let i = 0; i < BARS; i++) {
        const bh = Math.max(0.02, hist[i]) * (h - 6);
        g.fillRect(i * bw + 1, (h - bh) / 2, Math.max(1, bw - 2), bh);
      }
    }
  }
}
