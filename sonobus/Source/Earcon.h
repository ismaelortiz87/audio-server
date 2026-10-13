// A14 (docs/design/android.md section 5): the local mic-on / mic-off cues.
//
// Two tones, 70 ms each, 20 ms apart: rising 660 -> 880 Hz for "mic on",
// falling 880 -> 660 Hz for "mic off", -18 dBFS, 5 ms linear fades on every
// tone edge. The cue is mixed into the *output* only: the processor adds
// render() to the main bus after the send tap, so it can never reach a
// station (SonobusPluginProcessor::processBlock, next to the API test tone).
//
// No JUCE types and no allocation: render() is called from the audio thread,
// and tests/android/a14-earcon.cpp compiles this header on its own.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace crosspoint {

class Earcon
{
public:
    static constexpr float kLowHz = 660.0f, kHighHz = 880.0f;
    static constexpr float kToneMs = 70.0f, kGapMs = 20.0f, kFadeMs = 5.0f;
    static constexpr float kLevel = 0.12589254f;           // -18 dBFS peak

    /** Any thread. The next render() starts the cue (a cue in progress restarts). */
    void request (bool micOn)               { pending.store (micOn ? 1 : 2, std::memory_order_release); }

    /** Audio thread. Adds the cue to `channels` (numChannels pointers, numSamples each). */
    void render (float* const* channels, int numChannels, int numSamples, double sampleRate)
    {
        if (const int p = pending.exchange (0, std::memory_order_acquire); p != 0)
        {
            rising = (p == 1);
            elapsed = 0;
            phase = 0.0;
            active = true;
        }
        if (! active || numChannels <= 0 || sampleRate <= 0.0) return;

        const int toneN = (int) std::lround (kToneMs * 0.001 * sampleRate);
        const int gapN = (int) std::lround (kGapMs * 0.001 * sampleRate);
        const int fadeN = std::max (1, (int) std::lround (kFadeMs * 0.001 * sampleRate));
        const int total = 2 * toneN + gapN;
        const float first = rising ? kLowHz : kHighHz, second = rising ? kHighHz : kLowHz;

        for (int i = 0; i < numSamples; ++i, ++elapsed)
        {
            if (elapsed >= total) { active = false; return; }
            int pos;
            float hz;
            if (elapsed < toneN)                  { pos = elapsed;                 hz = first; }
            else if (elapsed < toneN + gapN)      { continue; }
            else                                  { pos = elapsed - toneN - gapN;  hz = second; }
            if (pos == 0) phase = 0.0;

            const int fromEnd = toneN - 1 - pos;
            const float env = std::min (1.0f, (float) std::min (pos + 1, fromEnd + 1) / (float) fadeN);
            const float v = kLevel * env * (float) std::sin (phase);
            phase += 6.283185307179586 * hz / sampleRate;
            for (int ch = 0; ch < numChannels; ++ch) channels[ch][i] += v;
        }
    }

    bool isPlaying() const { return active || pending.load() != 0; }

private:
    std::atomic<int> pending { 0 };    // 0 none, 1 mic on, 2 mic off
    bool active = false, rising = true;
    int elapsed = 0;
    double phase = 0.0;
};

} // namespace crosspoint
