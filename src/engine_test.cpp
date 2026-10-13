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
    return fails;
}
