// Browser side of the web Console's audio (P7.2/P7.3): one WebRTC connection
// to the gateway sidecar on the same origin (`rtc/*`, see
// docker/web-console/gateway/README.md). Only exists where the origin serves
// `rtc/config`, i.e. in the web container, not in the Mac shell or the mock.
//
// The browser always sends its mic to the gateway; the engine decides who hears
// it (talk, solo, push-to-talk, mic on/off), so this module has no routing logic.

// Adds stereo=1 to the Opus fmtp line(s) of an SDP, once (see start()).
export function withStereoReceive(sdp) {
  return sdp.replace(/^(a=fmtp:\d+ [^\r\n]*useinbandfec=1[^\r\n]*)$/gm,
    line => /(^|;|\s)stereo=1/.test(line) ? line : `${line};stereo=1`);
}

export class RtcLink {
  constructor() {
    this.state = 'unavailable';  // unavailable | idle | starting | connected | failed
    this.reason = '';
    this.micBlocked = false;
    this.listeners = new Set();
    this.pc = null;
    this.audio = null;
    this.retry = 0;
    this.wanted = false;
  }

  on(fn) { this.listeners.add(fn); fn(this); return () => this.listeners.delete(fn); }
  set(state, reason = '') { this.state = state; this.reason = reason; for (const fn of this.listeners) fn(this); }

  /** Is there a gateway on this origin? Sets `idle` if so. */
  async probe() {
    try {
      const res = await fetch('rtc/config', { cache: 'no-store' });
      if (res.ok && (res.headers.get('content-type') ?? '').includes('json')) {
        this.config = await res.json();
        this.set('idle');
        return true;
      }
    } catch {}
    this.set('unavailable');
    return false;
  }

  /** Must run from a user gesture (browsers gate mic and autoplay). */
  async start() {
    if (this.state === 'unavailable') return;
    this.wanted = true;
    this.set('starting');
    // An <audio> element created and played inside the gesture unlocks playback.
    if (!this.audio) {
      this.audio = new Audio();
      this.audio.autoplay = true;
      this.audio.setAttribute('playsinline', '');
    }
    this.audio.play().catch(() => {});

    let mic = null;
    try {
      // Speech defaults: echo cancellation (phone speakers) + noise suppression.
      // ?rawmic=1 turns processing off (diagnostics; NS removes steady test tones).
      const raw = new URLSearchParams(location.search).has('rawmic');
      mic = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: !raw, noiseSuppression: !raw, autoGainControl: false, channelCount: 1 },
      });
      this.micBlocked = false;
    } catch (e) {
      // Listening still works without a mic.
      this.micBlocked = true;
    }

    try {
      this.pc?.close();
      const pc = new RTCPeerConnection({ iceServers: this.config?.iceServers ?? [] });
      this.pc = pc;
      if (mic) mic.getTracks().forEach(t => pc.addTrack(t, mic));
      else pc.addTransceiver('audio', { direction: 'recvonly' });
      pc.ontrack = ev => { this.audio.srcObject = ev.streams[0] ?? new MediaStream([ev.track]); this.audio.play().catch(() => {}); };
      pc.onconnectionstatechange = () => {
        if (pc !== this.pc) return;
        if (pc.connectionState === 'connected') { this.retry = 0; this.set('connected'); }
        if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') this.fail('Audio connection lost');
      };
      // Chrome decodes Opus to stereo only when its own SDP asks for it (stereo=1
      // is the receiver's preference, RFC 7587); without it the Console mix, pans
      // included, is played in mono. Our uplink (the mic) stays mono.
      const offer = await pc.createOffer();
      offer.sdp = withStereoReceive(offer.sdp);
      await pc.setLocalDescription(offer);
      await new Promise(r => {  // non-trickle: the gateway answers once, with all candidates
        if (pc.iceGatheringState === 'complete') return r();
        pc.onicegatheringstatechange = () => pc.iceGatheringState === 'complete' && r();
        setTimeout(r, 2000);
      });
      const res = await fetch('rtc/offer', { method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ type: 'offer', sdp: pc.localDescription.sdp }) });
      if (!res.ok) throw new Error(`gateway answered ${res.status}`);
      await pc.setRemoteDescription(await res.json());
    } catch (e) {
      this.fail(e.message || 'Could not start audio');
    }
  }

  fail(reason) {
    if (this.state === 'failed') return;
    this.set('failed', reason);
    // Keep trying while the user wants audio (Wi-Fi blips, gateway restarts).
    if (this.wanted && this.retry < 5) {
      const ms = Math.min(1000 * 2 ** this.retry++, 15000);
      setTimeout(() => { if (this.wanted && this.state === 'failed') this.start(); }, ms);
    }
  }

  stop() {
    this.wanted = false;
    this.pc?.close();
    this.pc = null;
    fetch('rtc/close', { method: 'POST' }).catch(() => {});
    this.set('idle');
  }
}
