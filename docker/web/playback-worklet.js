// Playback worklet: receives interleaved s16le stereo PCM over the main thread
// and renders it continuously.
//
// The engine pushes audio in realtime, so the browser must not simply play each
// packet as it arrives — network jitter would cause gaps. Incoming frames are
// written into a ring buffer and the worklet drains it at exactly the hardware
// sample rate, which converts jitter into a small constant latency.

const CHANNELS = 2;

class PlaybackProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const seconds = options?.processorOptions?.bufferSeconds ?? 0.75;
    this.capacity = Math.ceil(sampleRate * seconds);
    this.ring = new Float32Array(this.capacity * CHANNELS);
    this.writeIndex = 0; // in frames
    this.readIndex = 0; // in frames
    this.available = 0; // frames buffered

    // Counters surfaced to the page for a diagnostics readout.
    this.underruns = 0;
    this.dropped = 0;

    this.port.onmessage = (event) => {
      if (event.data instanceof ArrayBuffer) {
        this.push(event.data);
      } else if (event.data === "reset") {
        this.writeIndex = this.readIndex = this.available = 0;
      }
    };
  }

  push(buffer) {
    const samples = new Int16Array(buffer);
    const frames = Math.floor(samples.length / CHANNELS);

    for (let f = 0; f < frames; f += 1) {
      const base = this.writeIndex * CHANNELS;
      for (let c = 0; c < CHANNELS; c += 1) {
        // s16 -> [-1, 1)
        this.ring[base + c] = samples[f * CHANNELS + c] / 32768;
      }
      this.writeIndex = (this.writeIndex + 1) % this.capacity;
      if (this.available < this.capacity) {
        this.available += 1;
      } else {
        // Buffer full: advance the reader so the newest audio wins.
        this.readIndex = (this.readIndex + 1) % this.capacity;
        this.dropped += 1;
      }
    }
  }

  process(inputs, outputs) {
    const output = outputs[0];
    if (!output || output.length === 0) return true;
    const frames = output[0].length;

    // Prebuffer before playing so momentary jitter does not cause a dropout.
    const minFrames = Math.min(this.capacity, Math.ceil(sampleRate * 0.04));

    for (let f = 0; f < frames; f += 1) {
      if (this.available <= 0) {
        for (let c = 0; c < output.length; c += 1) output[c][f] = 0;
        if (this.started) this.underruns += 1;
        continue;
      }
      if (!this.started && this.available < minFrames) {
        for (let c = 0; c < output.length; c += 1) output[c][f] = 0;
        continue;
      }
      this.started = true;

      const base = this.readIndex * CHANNELS;
      for (let c = 0; c < output.length; c += 1) {
        output[c][f] = this.ring[base + (c % CHANNELS)];
      }
      this.readIndex = (this.readIndex + 1) % this.capacity;
      this.available -= 1;
    }

    // Report roughly 10x/second.
    this.reportCounter = (this.reportCounter ?? 0) + 1;
    if (this.reportCounter >= 40) {
      this.reportCounter = 0;
      this.port.postMessage({
        type: "stats",
        bufferedMs: (this.available / sampleRate) * 1000,
        underruns: this.underruns,
        dropped: this.dropped,
      });
    }

    return true;
  }
}

registerProcessor("sonobus-playback", PlaybackProcessor);
