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

    // --- Synth mode: makes its own sound from silent input, finite and bounded, at every host rate / quality ---
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    for (int media = 0; media < 4; ++media)
    {
        BeadsEngine e; e.prepare (sr);
        BeadsEngine::Params p; p.source = BeadsEngine::WavetableSynth; p.media = media; p.bank = 3;
        p.density = 0.9f; p.dryWet = 0.5f; p.reverb = 0.1f;
        const int N = 512; std::vector<float> silence (N, 0.f), outL (N), outR (N);
        double energy = 0, maxAbs = 0; bool bad = false;
        const int blocks = (int) (sr * 4 / N);
        for (int b = 0; b < blocks; ++b)
        {
            e.process (silence.data(), silence.data(), outL.data(), outR.data(), N, p, nullptr);
            for (int i = 0; i < N; ++i)
            {
                if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i])) bad = true;
                maxAbs = std::max (maxAbs, (double) std::fabs (outL[i]));
                if (b > blocks / 2) energy += outL[i] * outL[i];
            }
        }
        const bool ok = ! bad && energy > 1.0 && maxAbs < 4.0 && e.processorRate() == 48000.0;
        printf ("synth sr=%6.0f quality=%d proc=%5.0f energy=%9.2f max=%.2f  %s\n", sr, media, e.processorRate(), energy, maxAbs, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // --- Synth mode: each of the 12 banks sounds different (oscillator only, so the result is deterministic) ---
    {
        const int N = 512, blocks = 150;
        std::vector<std::vector<float>> render (BeadsEngine::kNumBanks);
        std::vector<float> silence (N, 0.f), outL (N), outR (N);
        for (int bank = 0; bank < BeadsEngine::kNumBanks; ++bank)
        {
            BeadsEngine e; e.prepare (48000.0);
            BeadsEngine::Params p; p.source = BeadsEngine::WavetableSynth; p.bank = bank;
            p.dryWet = 0.0f; p.reverb = 0.0f; p.time = 0.5f; p.density = 0.5f;
            for (int b = 0; b < blocks; ++b)
            {
                e.process (silence.data(), silence.data(), outL.data(), outR.data(), N, p, nullptr);
                if (b >= blocks - 20) render[(size_t) bank].insert (render[(size_t) bank].end(), outL.begin(), outL.end());
            }
        }
        double minDiff = 1e9; int worstA = 0, worstB = 0; bool silent = false;
        for (int a = 0; a < BeadsEngine::kNumBanks; ++a)
        {
            double ea = 0; for (float v : render[(size_t) a]) ea += (double) v * v;
            if (ea < 1e-6) silent = true;
            for (int c = a + 1; c < BeadsEngine::kNumBanks; ++c)
            {
                double d = 0, ec = 0;
                for (size_t i = 0; i < render[(size_t) a].size(); ++i)
                {
                    const double x = render[(size_t) a][i], y = render[(size_t) c][i];
                    d += (x - y) * (x - y); ec += y * y;
                }
                const double rel = d / std::max (1e-12, 0.5 * (ea + ec));
                if (rel < minDiff) { minDiff = rel; worstA = a; worstB = c; }
            }
        }
        const bool ok = ! silent && minDiff > 0.01;
        printf ("synth banks: all 12 audible: %s; most similar pair is bank %d vs %d (relative difference %.3f)  %s\n",
                silent ? "NO" : "yes", worstA + 1, worstB + 1, minDiff, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // --- Source switch: audio -> synth -> audio keeps working; held back while Freeze is on ---
    {
        BeadsEngine e; e.prepare (48000.0);
        BeadsEngine::Params p; p.media = BeadsEngine::VintageDigital; p.density = 0.9f; p.dryWet = 0.5f; p.reverb = 0.0f;
        const int N = 512; std::vector<float> in (N), silence (N, 0.f), outL (N), outR (N);
        double phase = 0; bool bad = false;
        auto run = [&] (int blocks, bool useSine, double* energy)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < N; ++i) in[i] = useSine ? 0.4f * (float) std::sin (phase += 2 * M_PI * 220.0 / 48000.0) : 0.f;
                e.process (in.data(), in.data(), outL.data(), outR.data(), N, p, nullptr);
                for (int i = 0; i < N; ++i) { if (! std::isfinite (outL[i])) bad = true; if (energy && b >= blocks / 2) *energy += outL[i] * outL[i]; }
            }
        };
        double e1 = 0, e2 = 0, e3 = 0;
        run (200, true, &e1);
        const bool audioRate = e.processorRate() == 32000.0;           // vintage digital effect runs at 32 kHz

        p.freeze = true;
        run (2, true, nullptr);
        p.source = BeadsEngine::WavetableSynth;                         // asked for while frozen
        run (4, false, nullptr);
        const bool heldBack = e.processorRate() == 32000.0;
        p.freeze = false;
        run (4, false, nullptr);
        const bool applied = e.processorRate() == 48000.0;              // synth runs at 48 kHz
        run (200, false, &e2);                                          // silent input, so any energy is the synth

        p.source = BeadsEngine::AudioIn;
        run (200, true, &e3);
        const bool backToAudio = e.processorRate() == 32000.0;

        const bool ok = ! bad && audioRate && heldBack && applied && backToAudio && e1 > 1.0 && e2 > 1.0 && e3 > 1.0;
        printf ("source switch: audio rate %s, held back while frozen %s, applied after release %s, back to audio %s; energy %.2f / %.2f / %.2f  %s\n",
                audioRate ? "OK" : "FAIL", heldBack ? "OK" : "FAIL", applied ? "OK" : "FAIL", backToAudio ? "OK" : "FAIL", e1, e2, e3, ok ? "OK" : "FAIL");
        fails += ! ok;
    }
    return fails;
}
