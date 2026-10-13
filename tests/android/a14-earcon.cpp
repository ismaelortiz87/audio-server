// A14: checks the earcon generator on the host (it has no JUCE dependency).
//   c++ -std=c++17 -O1 -I sonobus/Source tests/android/a14-earcon.cpp -o a14-earcon && ./a14-earcon
#include "Earcon.h"
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL line %d  ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// Goertzel amplitude of `hz` over [from, to)
static double tone (const std::vector<float>& x, int from, int to, double hz, double sr)
{
    double w = 2.0 * M_PI * hz / sr, c = 2.0 * std::cos (w), s1 = 0, s2 = 0;
    for (int i = from; i < to; ++i) { double s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2.0 * std::sqrt (std::max (0.0, p)) / (to - from);
}

static std::vector<float> run (bool up, double sr, int block)
{
    crosspoint::Earcon e;
    std::vector<float> out ((size_t) (sr * 0.4), 0.0f);
    e.request (up);
    for (size_t i = 0; i < out.size(); i += (size_t) block)
    {
        float* ch[1] = { out.data() + i };
        e.render (ch, 1, (int) std::min<size_t> ((size_t) block, out.size() - i), sr);
    }
    CHECK (! e.isPlaying(), "cue finished");
    return out;
}

int main()
{
    for (double sr : { 48000.0, 44100.0 })
    for (int block : { 64, 480, 1000 })
    for (bool up : { true, false })
    {
        const auto x = run (up, sr, block);
        const int ms = (int) (sr / 1000);
        const double f1 = up ? 660 : 880, f2 = up ? 880 : 660;
        // tone 1: 0..70 ms, gap 70..90, tone 2: 90..160, then silence
        const double a1 = tone (x, 10 * ms, 65 * ms, f1, sr), o1 = tone (x, 10 * ms, 65 * ms, f2, sr);
        const double a2 = tone (x, 100 * ms, 155 * ms, f2, sr), o2 = tone (x, 100 * ms, 155 * ms, f1, sr);
        CHECK (a1 > 0.11 && a1 < 0.14, "tone 1 amplitude %.4f (-18 dBFS = 0.1259) sr=%.0f", a1, sr);
        CHECK (a2 > 0.11 && a2 < 0.14, "tone 2 amplitude %.4f", a2);
        CHECK (o1 < 0.02 && o2 < 0.02, "wrong frequency leaks: %.4f %.4f", o1, o2);
        float peak = 0, gapPeak = 0, tail = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            peak = std::max (peak, std::fabs (x[i]));
            if (i >= (size_t) (72 * ms) && i < (size_t) (88 * ms)) gapPeak = std::max (gapPeak, std::fabs (x[i]));
            if (i >= (size_t) (162 * ms)) tail = std::max (tail, std::fabs (x[i]));
        }
        CHECK (peak <= 0.1260f, "peak %.4f never above -18 dBFS", peak);
        CHECK (gapPeak == 0.0f, "20 ms gap is silent (%.5f)", gapPeak);
        CHECK (tail == 0.0f, "silent after 160 ms (%.5f)", tail);
        float head = 0; for (int i = 0; i < ms; ++i) head = std::max (head, std::fabs (x[(size_t) i]));
        CHECK (std::fabs (x[0]) < 0.01f && head < 0.5f * 0.1259f, "fade in (first ms peak %.4f)", head);
        float fin = 0; for (int i = 69 * ms; i < 70 * ms; ++i) fin = std::max (fin, std::fabs (x[(size_t) i]));
        CHECK (fin < 0.5f * 0.1259f, "fade out (last ms peak %.4f)", fin);
        if (block != 64)
        {
            const auto ref = run (up, sr, 64);
            for (size_t i = 0; i < x.size(); ++i) if (x[i] != ref[i]) { CHECK (false, "block-size dependent at %zu", i); break; }
        }
    }
    { crosspoint::Earcon e; float a[8] = { 1,1,1,1,1,1,1,1 }; float* ch[1] = { a }; e.render (ch, 1, 8, 48000); CHECK (a[3] == 1.0f, "idle render adds nothing"); }
    { crosspoint::Earcon e; e.request (true); std::vector<float> a (4800, 0.5f); float* ch[1] = { a.data() }; e.render (ch, 1, 4800, 48000);
      CHECK (std::fabs (a[0] - 0.5f) < 0.01f && std::fabs (a[1000] - 0.5f) > 0.001f, "adds on top of the existing signal"); }
    std::printf (failures ? "%d FAILED\n" : "earcon: all checks passed\n", failures);
    return failures != 0;
}
