// Holds the engine state received over the control API and notifies views.
// Views never mutate it; they send commands and wait for the patch.

import { applyPatch } from './patch.js';

export class Store {
  constructor() {
    /** @type {any} */ this.state = null;
    this.rev = -1;
    /** @type {Set<(state:any)=>void>} */ this.listeners = new Set();
    this.scheduled = false;
  }

  /** Replace the whole state (from a `state` message). */
  reset(state, rev) {
    this.state = state;
    this.rev = rev;
    this.notify();
  }

  /**
   * Apply a `patch` message. Returns false on a revision gap, in which case
   * the caller must ask for a resync.
   */
  patch(ops, rev) {
    if (this.state === null || rev !== this.rev + 1) return false;
    applyPatch(this.state, ops);
    this.rev = rev;
    this.notify();
    return true;
  }

  subscribe(fn) {
    this.listeners.add(fn);
    if (this.state) fn(this.state);
    return () => this.listeners.delete(fn);
  }

  // Coalesce bursts of patches into one render per frame.
  notify() {
    if (this.scheduled) return;
    this.scheduled = true;
    const run = () => {
      this.scheduled = false;
      for (const fn of this.listeners) fn(this.state);
    };
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(run);
    else queueMicrotask(run);
  }
}
