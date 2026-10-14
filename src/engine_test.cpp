// Offline sanity test of BeadsEngine at several host sample rates / media modes.
#include "BeadsEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
int main()
{
    int fails = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    for (int media = 0; media < 4; ++media)
    {
        BeadsEngine e; e.prepare (sr);
        BeadsEngine::Params p; p.media = media; p.density = 0.9f; p.dryWet = 0.5f; p.reverb = 0.1f;
        const int N = 512; std::vector<float> in (N), out (N), outR (N);
        double phase = 0, dryE = 0, allE = 0; bool bad = false; double maxAbs = 0;
        const int blocks = (int) (sr * 6 / N);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < N; ++i) in[i] = 0.4f * (float) std::sin (phase += 2 * M_PI * 220.0 / sr);
            e.process (in.data(), in.data(), out.data(), outR.data(), N, p, nullptr);
            for (int i = 0; i < N; ++i) { if (! std::isfinite (out[i])) bad = true; maxAbs = std::max (maxAbs, (double) std::fabs (out[i])); if (b > blocks / 2) allE += out[i] * out[i]; }
        }
        bool ok = ! bad && allE > 1.0 && maxAbs < 4.0;
        printf ("sr=%6.0f media=%d proc=%5.0f latency=%4d samples  energy=%9.2f max=%.2f  %s\n", sr, media, e.processorRate(), e.latencyInHostSamples(), allE, maxAbs, ok ? "OK" : "FAIL");
        fails += ! ok;
    }
    // --- Quality is held back while Freeze is on, and applies once Freeze is released ---
    {
        BeadsEngine e; e.prepare (48000.0);
        BeadsEngine::Params p; p.media = BeadsEngine::Digital; p.density = 0.9f; p.dryWet = 1.0f; p.reverb = 0.0f; p.feedback = 0.0f;
        const int N = 512; std::vector<float> in (N), zero (N, 0.f), out (N), outR (N);
        double phase = 0;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < N; ++i) in[i] = 0.4f * (float) std::sin (phase += 2 * M_PI * 220.0 / 48000.0);
            e.process (in.data(), in.data(), out.data(), outR.data(), N, p, nullptr);
        }
        p.freeze = true;
        e.process (zero.data(), zero.data(), out.data(), outR.data(), N, p, nullptr);
        p.media = BeadsEngine::DirtyTape;                       // would normally clear the buffer and switch to 24 kHz
        double frozenE = 0;
        for (int b = 0; b < 100; ++b)
        {
            e.process (zero.data(), zero.data(), out.data(), outR.data(), N, p, nullptr);
            for (int i = 0; i < N; ++i) frozenE += out[i] * out[i];
        }
        const bool heldBack = e.processorRate() == 48000.0 && frozenE > 1.0;
        p.freeze = false;
        e.process (zero.data(), zero.data(), out.data(), outR.data(), N, p, nullptr);
        const bool applied = e.processorRate() == 24000.0;
        printf ("freeze lock: held back while frozen (rate 48000, energy %.2f) %s; applied after release %s\n",
                frozenE, heldBack ? "OK" : "FAIL", applied ? "OK" : "FAIL");
        fails += ! heldBack; fails += ! applied;
    }

    // --- Soft grain edges and pitch snap: output stays finite and audible in every mode ---
    for (int media = 0; media < 4; ++media)
    {
        BeadsEngine e; e.prepare (48000.0);
        BeadsEngine::Params p; p.media = media; p.density = 0.9f; p.dryWet = 0.5f; p.size = 0.2f;
        p.softEdges = true; p.pitchSnap = true; p.pitchSemitones = 12.1f;
        const int N = 512; std::vector<float> in (N), out (N), outR (N);
        double phase = 0, energy = 0; bool bad = false;
        for (int b = 0; b < 600; ++b)
        {
            if (b == 300) p.softEdges = false;                   // switch off mid-run
            for (int i = 0; i < N; ++i) in[i] = 0.4f * (float) std::sin (phase += 2 * M_PI * 220.0 / 48000.0);
            e.process (in.data(), in.data(), out.data(), outR.data(), N, p, nullptr);
            for (int i = 0; i < N; ++i) { if (! std::isfinite (out[i])) bad = true; if (b > 300) energy += out[i] * out[i]; }
        }
        const bool ok = ! bad && energy > 1.0;
        printf ("soft edges + snap media=%d energy=%9.2f %s\n", media, energy, ok ? "OK" : "FAIL");
        fails += ! ok;
    }
    return fails;
}
