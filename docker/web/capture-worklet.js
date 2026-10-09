// Capture worklet: forwards the microphone to the page as interleaved s16le
// stereo PCM, matching what the bridge's `pacat` expects.
//
// The mic usually arrives mono at 48 kHz; it is duplicated to two channels so
// the byte layout is identical in both directions.

class CaptureProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.muted = false;
    this.port.onmessage = (event) => {
      if (event.data?.type === "mute") this.muted = !!event.data.value;
    };
  }

  process(inputs) {
    const input = inputs[0];
    if (!input || input.length === 0) return true;
    const frames = input[0].length;
    if (frames === 0) return true;

    const left = input[0];
    const right = input.length > 1 ? input[1] : input[0];

    const out = new Int16Array(frames * 2);
    for (let f = 0; f < frames; f += 1) {
      const l = this.muted ? 0 : Math.max(-1, Math.min(1, left[f]));
      const r = this.muted ? 0 : Math.max(-1, Math.min(1, right[f]));
      out[f * 2] = l < 0 ? l * 0x8000 : l * 0x7fff;
      out[f * 2 + 1] = r < 0 ? r * 0x8000 : r * 0x7fff;
    }

    this.port.postMessage(out.buffer, [out.buffer]);
    return true;
  }
}

registerProcessor("sonobus-capture", CaptureProcessor);
