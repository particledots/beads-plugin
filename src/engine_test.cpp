// Offline sanity test of BeadsEngine at several host sample rates / media modes.
#include "BeadsEngine.h"
#include "NoteTracker.h"
#include "WavetableLoader.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Build an in-memory WAV. Every channel carries the same samples (-1..1). bits: 8/16/24/32 (int) or 32 (float).
static std::vector<uint8_t> makeWav (const std::vector<double>& samples, int channels, int bits, bool isFloat, int clm = 0)
{
    std::vector<uint8_t> b;
    auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) ((v >> (8 * i)) & 255)); };
    auto put16 = [&] (uint32_t v) { b.push_back ((uint8_t) (v & 255)); b.push_back ((uint8_t) ((v >> 8) & 255)); };
    auto tag   = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) t[i]); };
    const int bytes = bits / 8;
    const uint32_t dataBytes = (uint32_t) (samples.size() * (size_t) channels * (size_t) bytes);
    std::string clmBody;
    if (clm > 0) clmBody = "<!>" + std::to_string (clm) + " 01000000 wavetable (test)";
    const uint32_t clmSize = (uint32_t) clmBody.size();
    tag ("RIFF"); put32 (0); tag ("WAVE");
    tag ("fmt "); put32 (16); put16 (isFloat ? 3 : 1); put16 ((uint32_t) channels); put32 (48000);
    put32 (48000u * (uint32_t) channels * (uint32_t) bytes); put16 ((uint32_t) (channels * bytes)); put16 ((uint32_t) bits);
    if (clm > 0) { tag ("clm "); put32 (clmSize); for (char c : clmBody) b.push_back ((uint8_t) c); if (clmSize & 1) b.push_back (0); }
    tag ("data"); put32 (dataBytes);
    for (double v : samples)
        for (int c = 0; c < channels; ++c)
        {
            if (isFloat) { float f = (float) v; uint8_t t[4]; std::memcpy (t, &f, 4); for (int i = 0; i < 4; ++i) b.push_back (t[i]); }
            else if (bits == 8)  b.push_back ((uint8_t) (std::lround (v * 127.0) + 128));
            else if (bits == 16) put16 ((uint32_t) (int16_t) std::lround (v * 32767.0));
            else if (bits == 24) { int32_t x = (int32_t) std::lround (v * 8388607.0); b.push_back ((uint8_t) (x & 255)); b.push_back ((uint8_t) ((x >> 8) & 255)); b.push_back ((uint8_t) ((x >> 16) & 255)); }
            else put32 ((uint32_t) (int32_t) std::lround (v * 2147483647.0));
        }
    const uint32_t riff = (uint32_t) b.size() - 8;
    for (int i = 0; i < 4; ++i) b[(size_t) 4 + (size_t) i] = (uint8_t) ((riff >> (8 * i)) & 255);
    return b;
}

// A bank-shaped test signal: 16 cycles of 256 samples that morph from one harmonic mix to another.
static std::vector<double> morphingCycles (int numCycles, int cycleLen)
{
    std::vector<double> x;
    for (int c = 0; c < numCycles; ++c)
        for (int n = 0; n < cycleLen; ++n)
        {
            const double t = 2.0 * M_PI * n / cycleLen, k = numCycles > 1 ? (double) c / (numCycles - 1) : 0.0;
            x.push_back (0.6 * std::sin (t) + 0.3 * k * std::sin (3 * t + 0.4) + 0.2 * (1 - k) * std::sin (2 * t + 1.1));
        }
    return x;
}

// Properties every stored wave must have, however it was made.
static bool storedBankIsWellFormed (const std::vector<int16_t>& bank, std::string& why)
{
    if (bank.size() != (size_t) WavetableLoader::kBankValues) { why = "wrong size"; return false; }
    for (int w = 0; w < WavetableLoader::kWavesPerBank; ++w)
    {
        const int16_t* t = &bank[(size_t) w * WavetableLoader::kStoredSize];
        if (t[0] != t[256] || t[257] != t[1] || t[258] != t[2] || t[259] != t[3]) { why = "wrap values wrong in wave " + std::to_string (w); return false; }
        int peak = 0; long sum = 0; double mean = 0;
        for (int j = 0; j < 256; ++j) { const int d = t[j + 1] - t[j]; peak = std::max (peak, std::abs (d)); sum += d; mean += t[j + 1]; }
        mean /= 256;
        if (sum != 0) { why = "wave " + std::to_string (w) + " does not close"; return false; }
        if (peak < 511 || peak > 514) { why = "wave " + std::to_string (w) + " peak " + std::to_string (peak); return false; }
        if (std::fabs (mean) > 1.0) { why = "wave " + std::to_string (w) + " not centred"; return false; }
    }
    return true;
}

// Number of sign changes of a stored wave's differentiated shape (a pure harmonic h has 2h).
static int signChanges (const int16_t* t)
{
    int n = 0, prev = 0;
    for (int j = 0; j < 256; ++j)
    {
        const int d = t[j + 1] - t[j];
        const int s = d > 8 ? 1 : (d < -8 ? -1 : 0);
        if (s != 0) { if (prev != 0 && s != prev) ++n; prev = s; }
    }
    return n;
}

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
    // =========================== Loading wavetable banks from WAV files ===========================

    // --- The key check: a bank exported by GrainKontrol (factory bank 12) must rebuild the factory table exactly ---
    {
        const std::string path = std::string (BEADS_TEST_DATA_DIR) + "/bank-12-fm-8-1.wav";
        std::ifstream f (path, std::ios::binary);
        std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        std::vector<int16_t> bank;
        const auto r = WavetableLoader::bankFromWav (bytes.data(), bytes.size(), bank);
        int mismatches = -1;
        if (r.ok && bank.size() == (size_t) WavetableLoader::kBankValues)
        {
            mismatches = 0;
            const int16_t* fac = BeadsEngine::factoryBank (11);
            for (int i = 0; i < WavetableLoader::kBankValues; ++i) mismatches += bank[(size_t) i] != fac[i];
        }
        const bool ok = ! bytes.empty() && r.ok && mismatches == 0;
        printf ("loader: GrainKontrol export (%zu bytes) -> %s; values differing from factory bank 12: %d of %d  %s\n",
                bytes.size(), r.ok ? r.summary.c_str() : r.error.c_str(), mismatches, WavetableLoader::kBankValues, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // --- A second real export: a morph between different waves (not a factory bank). The last wave is factory bank 7's
    //     wave 16, which must come out bit-exact; every wave must survive the round trip to within rounding. ---
    {
        const std::string path = std::string (BEADS_TEST_DATA_DIR) + "/bank-07-braids-digital-and-metallic.wav";
        std::ifstream f (path, std::ios::binary);
        std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        std::vector<int16_t> bank, pcm; std::string why;
        const auto r = WavetableLoader::bankFromWav (bytes.data(), bytes.size(), bank);
        // the file's own samples (16-bit mono PCM after the "data" chunk header)
        for (size_t i = 12; i + 8 <= bytes.size();)
        {
            const uint32_t sz = (uint32_t) bytes[i + 4] | ((uint32_t) bytes[i + 5] << 8) | ((uint32_t) bytes[i + 6] << 16) | ((uint32_t) bytes[i + 7] << 24);
            if (std::memcmp (&bytes[i], "data", 4) == 0)
            {
                for (size_t k = 0; k + 1 < sz && i + 9 + k < bytes.size(); k += 2) pcm.push_back ((int16_t) (bytes[i + 8 + k] | (bytes[i + 9 + k] << 8)));
                break;
            }
            i += 8 + sz + (sz & 1);
        }
        const bool wellFormed = r.ok && storedBankIsWellFormed (bank, why) && pcm.size() == 4096;
        int lastWaveMismatches = -1; double maxErr = 1e9;
        if (wellFormed)
        {
            lastWaveMismatches = 0;
            const int16_t* fac = BeadsEngine::factoryBank (6) + 15 * WavetableLoader::kStoredSize;
            for (int i = 0; i < WavetableLoader::kStoredSize; ++i) lastWaveMismatches += bank[(size_t) 15 * WavetableLoader::kStoredSize + (size_t) i] != fac[i];
            // stored waves back into file samples: slope of the stored table, rotated back by 3, scaled to the file's peak
            maxErr = 0;
            for (int k = 0; k < 16; ++k)
                for (int j = 0; j < 256; ++j)
                {
                    const int m = (j + WavetableLoader::kExportRotation) % 256;
                    const int16_t* t = &bank[(size_t) k * WavetableLoader::kStoredSize];
                    const double back = (double) (t[m + 1] - t[m]) * 32112.0 / 512.0;
                    maxErr = std::max (maxErr, std::fabs (back - (double) pcm[(size_t) k * 256 + (size_t) j]));
                }
        }
        // one stored step is 62.7 file units, so rounding alone can be off by half of that
        const bool ok = wellFormed && lastWaveMismatches == 0 && maxErr <= 32.0;
        printf ("loader: custom morph export -> %s; last wave vs factory bank 7 wave 16: %d of %d values differ; largest difference after the round trip: %.1f of 32768 (rounding limit 31.4)  %s\n",
                r.ok ? r.summary.c_str() : r.error.c_str(), lastWaveMismatches, WavetableLoader::kStoredSize, maxErr, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // --- Generated test files: stored waves are well formed, and the wave positions land where they should ---
    {
        // one 2048-sample sine cycle with a Serum-style marker
        std::vector<double> sine;
        for (int n = 0; n < 2048; ++n) sine.push_back (0.8 * std::sin (2.0 * M_PI * n / 2048));
        auto wav = makeWav (sine, 1, 16, false, 2048);
        std::vector<int16_t> bank; std::string why;
        auto r = WavetableLoader::bankFromWav (wav.data(), wav.size(), bank);
        bool ok = r.ok && storedBankIsWellFormed (bank, why);
        // a sine starts at zero in the export, so after the 3-sample rotation the stored slope is zero at index 3
        const int16_t* t = ok ? &bank[0] : nullptr;
        const bool startsAtZero = ok && std::abs (t[4] - t[3]) <= 1;
        printf ("loader: single 2048-sample sine: %s%s; sine starts at zero: %s  %s\n", ok ? "well formed" : why.c_str(),
                r.ok ? (" (" + r.summary + ")").c_str() : (" " + r.error).c_str(), startsAtZero ? "yes" : "NO", (ok && startsAtZero) ? "OK" : "FAIL");
        fails += ! (ok && startsAtZero);

        // eight 2048-sample cycles; cycle c holds harmonic c+1 -> wave 0 has 1 harmonic, wave 15 has 8
        std::vector<double> harm;
        for (int c = 0; c < 8; ++c) for (int n = 0; n < 2048; ++n) harm.push_back (0.7 * std::sin (2.0 * M_PI * (c + 1) * n / 2048));
        wav = makeWav (harm, 1, 16, false, 2048);
        r = WavetableLoader::bankFromWav (wav.data(), wav.size(), bank);
        ok = r.ok && storedBankIsWellFormed (bank, why);
        const int first = ok ? signChanges (&bank[0]) : -1;
        const int last  = ok ? signChanges (&bank[(size_t) 15 * WavetableLoader::kStoredSize]) : -1;
        const bool morph = ok && first == 2 && last == 16;
        printf ("loader: 8 cycles of 2048 spread over 16 waves: wave 1 has %d sign changes (want 2), wave 16 has %d (want 16)  %s\n",
                first, last, morph ? "OK" : "FAIL");
        fails += ! morph;
    }

    // --- Sample formats and channel layouts all give the same bank ---
    {
        const auto src = morphingCycles (16, 256);
        std::vector<int16_t> ref; std::string why;
        auto base = makeWav (src, 1, 16, false, 256);
        const bool refOk = WavetableLoader::bankFromWav (base.data(), base.size(), ref).ok && storedBankIsWellFormed (ref, why);
        struct Fmt { const char* name; int ch, bits; bool fl; };
        for (const Fmt& f : { Fmt { "24-bit mono", 1, 24, false }, Fmt { "16-bit stereo", 2, 16, false }, Fmt { "32-bit int mono", 1, 32, false },
                              Fmt { "32-bit float mono", 1, 32, true }, Fmt { "32-bit float stereo", 2, 32, true } })
        {
            auto wav = makeWav (src, f.ch, f.bits, f.fl, 256);
            std::vector<int16_t> bank;
            const auto r = WavetableLoader::bankFromWav (wav.data(), wav.size(), bank);
            int maxDiff = 100000;
            if (r.ok && bank.size() == ref.size()) { maxDiff = 0; for (size_t i = 0; i < ref.size(); ++i) maxDiff = std::max (maxDiff, std::abs ((int) bank[i] - (int) ref[i])); }
            const bool ok = refOk && r.ok && maxDiff <= 330;   // 1% of full scale: only rounding differs between formats
            printf ("loader: %-20s largest difference from the 16-bit mono result: %d  %s\n", f.name, maxDiff, ok ? "OK" : "FAIL");
            fails += ! ok;
        }
    }

    // --- Cycle length detection without a marker ---
    {
        struct Case { int total, cycle; const char* want; };
        for (const Case& c : { Case { 4096, 256, "16 cycles of 256" }, Case { 2048, 2048, "1 cycle of 2048" }, Case { 8192, 2048, "4 cycles of 2048" },
                               Case { 3 * 256, 256, "3 cycles of 256" } })
        {
            std::vector<double> x;
            for (int n = 0; n < c.total; ++n) x.push_back (0.5 * std::sin (2.0 * M_PI * n / c.cycle));
            auto wav = makeWav (x, 1, 16, false, 0);
            std::vector<int16_t> bank;
            const auto r = WavetableLoader::bankFromWav (wav.data(), wav.size(), bank);
            const bool ok = r.ok && r.summary.find (c.want) != std::string::npos;
            printf ("loader: %5d samples, no marker -> %s  %s\n", c.total, r.ok ? r.summary.c_str() : r.error.c_str(), ok ? "OK" : "FAIL");
            fails += ! ok;
        }
    }

    // --- Bad files fail with a message and never crash ---
    {
        std::vector<std::pair<const char*, std::vector<uint8_t>>> bad;
        bad.push_back ({ "empty", {} });
        bad.push_back ({ "3 bytes", { 'R', 'I', 'F' } });
        bad.push_back ({ "RIFF/WAVE header only", { 'R','I','F','F', 4,0,0,0, 'W','A','V','E' } });
        bad.push_back ({ "text file", std::vector<uint8_t> { 'h','e','l','l','o',' ','w','o','r','l','d','!','!','!','!','!' } });
        auto noData = makeWav ({}, 1, 16, false, 256);
        bad.push_back ({ "WAV with no samples", noData });
        auto silent = makeWav (std::vector<double> (4096, 0.0), 1, 16, false, 256);
        bad.push_back ({ "silent WAV", silent });
        auto flat = makeWav (std::vector<double> (4096, 0.5), 1, 16, false, 256);
        bad.push_back ({ "flat (DC only) WAV", flat });
        auto adpcm = makeWav (morphingCycles (16, 256), 1, 16, false, 256);
        adpcm[20] = 2;   // format tag 2 (ADPCM) is not supported
        bad.push_back ({ "unsupported format tag", adpcm });
        auto oddLen = makeWav (std::vector<double> (70001, 0.3), 1, 16, false, 0);
        bad.push_back ({ "long file, odd length, no marker", oddLen });
        auto tooBig = makeWav (morphingCycles (16, 256), 1, 16, false, 100000);
        bad.push_back ({ "cycle length larger than the file", tooBig });
        std::vector<uint8_t> noise (5000);
        unsigned seed = 12345; for (auto& v : noise) { seed = seed * 1664525u + 1013904223u; v = (uint8_t) (seed >> 24); }
        bad.push_back ({ "random bytes", noise });
        auto head = noise; std::memcpy (head.data(), "RIFF", 4); std::memcpy (head.data() + 8, "WAVE", 4);
        bad.push_back ({ "random bytes behind a RIFF header", head });
        for (auto& [name, bytes] : bad)
        {
            std::vector<int16_t> bank;
            const auto r = WavetableLoader::bankFromWav (bytes.empty() ? nullptr : bytes.data(), bytes.size(), bank);
            const bool ok = ! r.ok && ! r.error.empty();
            printf ("loader: bad file \"%s\" -> %s  %s\n", name, r.ok ? "accepted" : r.error.c_str(), ok ? "OK" : "FAIL");
            fails += ! ok;
        }
        // a valid file cut off in the middle of its audio loads what is there instead of failing
        auto cut = makeWav (morphingCycles (16, 256), 1, 16, false, 256);
        cut.resize (cut.size() - 3000);
        std::vector<int16_t> bank; std::string why;
        const auto r = WavetableLoader::bankFromWav (cut.data(), cut.size(), bank);
        const bool ok = r.ok && storedBankIsWellFormed (bank, why);
        printf ("loader: truncated file -> %s  %s\n", r.ok ? r.summary.c_str() : r.error.c_str(), ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // =========================== Swapping banks in the engine ===========================
    {
        // oscillator-only render of synth bank 12 (index 11): deterministic, so two runs can be compared exactly
        auto render = [] (BeadsEngine& e)
        {
            const int N = 512, blocks = 120;
            BeadsEngine::Params p; p.source = BeadsEngine::WavetableSynth; p.bank = 11; p.dryWet = 0.0f; p.reverb = 0.0f; p.time = 0.5f; p.density = 0.5f;
            std::vector<float> silence (N, 0.f), l (N), r (N), all;
            for (int b = 0; b < blocks; ++b)
            {
                e.process (silence.data(), silence.data(), l.data(), r.data(), N, p, nullptr);
                if (b >= blocks - 20) all.insert (all.end(), l.begin(), l.end());
            }
            return all;
        };
        auto relDiff = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            double d = 0, e = 0;
            for (size_t i = 0; i < a.size(); ++i) { d += (double) (a[i] - b[i]) * (a[i] - b[i]); e += (double) a[i] * a[i] + (double) b[i] * b[i]; }
            return d / std::max (1e-12, 0.5 * e);
        };

        BeadsEngine e0; e0.prepare (48000.0);
        const auto factoryA = render (e0);
        BeadsEngine e1; e1.prepare (48000.0);
        const auto factoryB = render (e1);
        const bool deterministic = relDiff (factoryA, factoryB) == 0.0;

        // the exported file loaded into slot 12 plays exactly like the factory bank it came from
        std::ifstream f (std::string (BEADS_TEST_DATA_DIR) + "/bank-12-fm-8-1.wav", std::ios::binary);
        std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        std::vector<int16_t> fixture;
        const bool loaded = WavetableLoader::bankFromWav (bytes.data(), bytes.size(), fixture).ok;
        BeadsEngine e2; e2.prepare (48000.0);
        if (loaded) e2.queueBank (11, fixture.data());
        const auto viaFile = render (e2);
        const bool sameAsFactory = loaded && relDiff (factoryA, viaFile) == 0.0;

        // a different bank in the same slot sounds different
        std::vector<double> harm;
        for (int c = 0; c < 8; ++c) for (int n = 0; n < 2048; ++n) harm.push_back (0.7 * std::sin (2.0 * M_PI * (c + 1) * n / 2048));
        auto harmWav = makeWav (harm, 1, 16, false, 2048);
        std::vector<int16_t> other;
        const bool otherOk = WavetableLoader::bankFromWav (harmWav.data(), harmWav.size(), other).ok;
        BeadsEngine e3; e3.prepare (48000.0);
        if (otherOk) e3.queueBank (11, other.data());
        const auto replaced = render (e3);
        const double replacedDiff = relDiff (factoryA, replaced);

        // only the chosen slot changes, and restoring the factory bank puts every value back
        const int16_t* tab = e3.currentWavetables();
        int othersChanged = 0, slotChanged = 0;
        for (int b = 0; b < 12; ++b)
            for (int i = 0; i < WavetableLoader::kBankValues; ++i)
                if (tab[(size_t) b * WavetableLoader::kBankValues + (size_t) i] != BeadsEngine::factoryBank (b)[i]) (b == 11 ? slotChanged : othersChanged)++;
        e3.queueFactoryBank (11);
        e3.queueBank (-1, other.data()); e3.queueBank (12, other.data()); e3.queueBank (3, nullptr);   // out-of-range requests are ignored
        std::vector<float> in (64, 0.f), l (64), r (64);
        BeadsEngine::Params p; p.source = BeadsEngine::WavetableSynth;
        e3.process (in.data(), in.data(), l.data(), r.data(), 64, p, nullptr);
        int restoredWrong = 0;
        for (int i = 0; i < 12 * WavetableLoader::kBankValues; ++i) restoredWrong += e3.currentWavetables()[i] != BeadsEngine::factoryBank (0)[i];

        const bool ok = deterministic && sameAsFactory && otherOk && replacedDiff > 0.01 && othersChanged == 0 && slotChanged > 0 && restoredWrong == 0;
        printf ("engine banks: runs repeat exactly %s; export loaded into slot 12 plays identically to factory %s; a different bank differs by %.3f; "
                "other slots touched: %d; after restore wrong values: %d  %s\n",
                deterministic ? "yes" : "NO", sameAsFactory ? "yes" : "NO", replacedDiff, othersChanged, restoredWrong, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // =========================== MIDI pitch + Drone ===========================
    {
        // One engine, oscillator only (Dry/Wet 0, no reverb). gatePattern gives the gate per 480-sample block.
        struct Run { std::vector<float> out; std::vector<int> blockStart; };
        auto run = [] (BeadsEngine& e, BeadsEngine::Params p, const std::vector<uint8_t>& gatePattern, std::vector<float>& out)
        {
            const int N = 480;
            std::vector<float> silence ((size_t) N, 0.f), l ((size_t) N), r ((size_t) N);
            std::vector<uint8_t> g ((size_t) N, 0);
            for (uint8_t gv : gatePattern)
            {
                std::fill (g.begin(), g.end(), gv);
                e.process (silence.data(), silence.data(), l.data(), r.data(), N, p, g.data());
                out.insert (out.end(), l.begin(), l.end());
            }
        };
        auto rms = [] (const std::vector<float>& v, size_t a, size_t b)
        {
            double e = 0; for (size_t i = a; i < b; ++i) e += (double) v[i] * v[i];
            return std::sqrt (e / (double) std::max<size_t> (1, b - a));
        };
        auto freqOf = [] (const std::vector<float>& v, size_t a, size_t b)   // upward zero crossings per second at 48 kHz
        {
            int c = 0; for (size_t i = a + 1; i < b; ++i) c += (v[i - 1] < 0.f && v[i] >= 0.f);
            return c * 48000.0 / (double) (b - a);
        };

        BeadsEngine::Params base; base.source = BeadsEngine::WavetableSynth; base.bank = 0; base.dryWet = 0.0f; base.reverb = 0.0f; base.droneOn = true;
        const std::vector<uint8_t> always (60, 1);

        // pitch: +12 semitones doubles the oscillator frequency, +7 gives 2^(7/12)
        double f0 = 0, f12 = 0, f7 = 0;
        for (int k = 0; k < 3; ++k)
        {
            BeadsEngine e; e.prepare (48000.0);
            auto p = base; p.midiPitchOffset = k == 0 ? 0.f : (k == 1 ? 12.f : 7.f);
            std::vector<float> out; run (e, p, always, out);
            const double f = freqOf (out, out.size() / 2, out.size());
            (k == 0 ? f0 : (k == 1 ? f12 : f7)) = f;
        }
        const bool pitchOk = f0 > 20 && std::fabs (f12 / f0 - 2.0) < 0.03 && std::fabs (f7 / f0 - std::pow (2.0, 7.0 / 12.0)) < 0.03;
        printf ("midi pitch: 0 st %.1f Hz, +12 st %.1f Hz (ratio %.3f), +7 st %.1f Hz (ratio %.3f)  %s\n", f0, f12, f12 / f0, f7, f7 / f0, pitchOk ? "OK" : "FAIL");
        fails += ! pitchOk;

        // extreme offsets stay finite
        bool finiteOk = true;
        for (float off : { -127.f, -60.f, 60.f, 127.f })
        {
            BeadsEngine e; e.prepare (48000.0);
            auto p = base; p.midiPitchOffset = off; p.pitchSemitones = 48.f;
            std::vector<float> out; run (e, p, always, out);
            for (float x : out) finiteOk = finiteOk && std::isfinite (x) && std::fabs (x) < 8.f;
        }
        printf ("midi pitch: extreme offsets finite and bounded  %s\n", finiteOk ? "OK" : "FAIL");
        fails += ! finiteOk;

        // drone off: silent without a note; sounds while held; fades out after release
        {
            BeadsEngine e; e.prepare (48000.0);
            auto p = base; p.droneOn = false;
            std::vector<uint8_t> pat (20, 0);                 // 20 blocks no note
            pat.insert (pat.end (), 40, 1);                   // 40 blocks note held
            pat.insert (pat.end (), 40, 0);                   // 40 blocks released
            std::vector<float> out; run (e, p, pat, out);
            const size_t B = 480;
            const double before = rms (out, 5 * B, 20 * B);
            const double held = rms (out, 40 * B, 60 * B);
            const double after = rms (out, 90 * B, 100 * B);
            // click check: largest sample-to-sample step across the note-on and note-off transitions vs the steady-state step
            auto maxStep = [&] (size_t a, size_t b) { double m = 0; for (size_t i = a + 1; i < b; ++i) m = std::max (m, (double) std::fabs (out[i] - out[i - 1])); return m; };
            const double steadyStep = maxStep (40 * B, 60 * B);
            const double onStep = maxStep (19 * B, 24 * B), offStep = maxStep (59 * B, 80 * B);
            const bool ok = before < 1e-4 && held > 1e-3 && after < 1e-3 && onStep < 2.0 * steadyStep + 1e-3 && offStep < 2.0 * steadyStep + 1e-3;
            printf ("drone off: rms before note %.5f, held %.4f, after release %.5f; max step on %.4f off %.4f (steady %.4f)  %s\n",
                    before, held, after, onStep, offStep, steadyStep, ok ? "OK" : "FAIL");
            fails += ! ok;
        }

        // drone on: sounds with no note held at all
        {
            BeadsEngine e; e.prepare (48000.0);
            std::vector<float> out; run (e, base, std::vector<uint8_t> (40, 0), out);
            const double level = rms (out, 20 * 480, out.size());
            const bool ok = level > 1e-3;
            printf ("drone on: rms with no note %.4f  %s\n", level, ok ? "OK" : "FAIL");
            fails += ! ok;
        }

        // drone off leaves the grains alone: fully wet, seed locked (free-run), still sounds with no note
        {
            BeadsEngine e; e.prepare (48000.0);
            auto p = base; p.droneOn = false; p.dryWet = 1.0f; p.seedMode = BeadsEngine::FreeRun; p.density = 1.0f; p.time = 0.5f;
            std::vector<float> out; run (e, p, std::vector<uint8_t> (100, 0), out);
            const double level = rms (out, 40 * 480, out.size());
            const bool ok = level > 1e-3;
            printf ("drone off, fully wet free-run grains: rms %.4f  %s\n", level, ok ? "OK" : "FAIL");
            fails += ! ok;
        }
    }

    // =========================== NoteTracker ===========================
    {
        NoteTracker t;
        bool ok = ! t.held() && t.current() == -1;
        t.noteOn (60); t.noteOn (64);
        ok = ok && t.held() && t.current() == 64;
        t.noteOff (64);
        ok = ok && t.held() && t.current() == 60;          // falls back to the key still down
        t.noteOff (60);
        ok = ok && ! t.held() && t.current() == 60;        // all up: keeps the last pitch
        t.noteOn (72); t.allOff();
        ok = ok && ! t.held() && t.current() == 72;
        t.noteOn (50); t.noteOn (50); t.noteOff (50);
        ok = ok && ! t.held();                              // a repeated note-on does not need two note-offs
        for (int n = 0; n < 40; ++n) t.noteOn (n);          // more than the stack holds: oldest drop out, nothing breaks
        for (int n = 0; n < 40; ++n) t.noteOff (n);
        ok = ok && ! t.held();
        t.noteOn (-1); t.noteOn (128);
        ok = ok && ! t.held();
        printf ("note tracker: last-note priority, fall-back, overflow  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }
    return fails;
}
