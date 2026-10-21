#include "WavetableLoader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>

namespace WavetableLoader
{
namespace
{
    constexpr size_t kMaxFrames = 8u * 1024u * 1024u;   // about 3 minutes at 48 kHz; keeps memory sane on a bad file
    constexpr double kPi = 3.14159265358979323846;

    uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

    struct Wav
    {
        std::vector<float> mono;
        int clmCycle = 0;           // from the "clm " marker, 0 if none
    };

    bool decodeWav (const uint8_t* d, size_t n, Wav& out, std::string& err)
    {
        if (d == nullptr || n < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        {
            err = "This is not a WAV file.";
            return false;
        }

        int format = 0, channels = 0, bits = 0;
        bool haveFmt = false;
        const uint8_t* dataPtr = nullptr;
        size_t dataSize = 0;

        size_t pos = 12;
        while (pos + 8 <= n)
        {
            const uint8_t* id = d + pos;
            const size_t size = rd32 (d + pos + 4);
            const size_t bodyPos = pos + 8;
            const size_t avail = n - bodyPos;
            const size_t body = std::min (size, avail);       // a truncated last chunk is read as far as it goes

            if (std::memcmp (id, "fmt ", 4) == 0 && body >= 16)
            {
                format   = rd16 (d + bodyPos);
                channels = rd16 (d + bodyPos + 2);
                bits     = rd16 (d + bodyPos + 14);
                if (format == 0xFFFE && body >= 26) format = rd16 (d + bodyPos + 24);   // WAVE_FORMAT_EXTENSIBLE
                haveFmt = true;
            }
            else if (std::memcmp (id, "data", 4) == 0)
            {
                if (dataPtr == nullptr) { dataPtr = d + bodyPos; dataSize = body; }
            }
            else if (std::memcmp (id, "clm ", 4) == 0 && body >= 4 && std::memcmp (d + bodyPos, "<!>", 3) == 0)
            {
                long v = 0; size_t i = 3; int digits = 0;
                while (i < body && d[bodyPos + i] >= '0' && d[bodyPos + i] <= '9' && digits < 9)
                { v = v * 10 + (d[bodyPos + i] - '0'); ++i; ++digits; }
                out.clmCycle = (int) v;
            }

            if (size > avail) break;
            const size_t next = bodyPos + size + (size & 1);
            if (next <= pos) break;
            pos = next;
        }

        if (! haveFmt)  { err = "The WAV file has no format information."; return false; }
        if (dataPtr == nullptr) { err = "The WAV file has no audio data."; return false; }
        const bool isInt = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32));
        const bool isFloat = (format == 3 && (bits == 32 || bits == 64));
        if (! isInt && ! isFloat)
        {
            err = "This WAV format is not supported (use 16, 24 or 32-bit PCM, or 32-bit float).";
            return false;
        }
        if (channels < 1 || channels > 8) { err = "The WAV file has an unusable channel count."; return false; }

        const size_t bytes = (size_t) bits / 8;
        const size_t frameBytes = bytes * (size_t) channels;
        const size_t frames = dataSize / frameBytes;
        if (frames == 0) { err = "The WAV file has no audio data."; return false; }
        if (frames > kMaxFrames) { err = "The WAV file is too long for a wavetable."; return false; }

        out.mono.assign (frames, 0.0f);
        for (size_t f = 0; f < frames; ++f)
        {
            double sum = 0.0;
            for (int c = 0; c < channels; ++c)
            {
                const uint8_t* p = dataPtr + f * frameBytes + (size_t) c * bytes;
                double v = 0.0;
                if (isFloat)
                {
                    if (bits == 32) { float x; std::memcpy (&x, p, 4); v = x; }
                    else            { double x; std::memcpy (&x, p, 8); v = x; }
                    if (! std::isfinite (v)) v = 0.0;
                }
                else if (bits == 8)  v = ((int) p[0] - 128) / 128.0;
                else if (bits == 16) v = (int16_t) rd16 (p) / 32768.0;
                else if (bits == 24)
                {
                    int32_t x = (int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8;
                    v = x / 8388608.0;
                }
                else v = (int32_t) rd32 (p) / 2147483648.0;
                sum += v;
            }
            out.mono[f] = (float) (sum / channels);
        }
        return true;
    }

    // One cycle of any length -> 256 samples. Equal length is copied untouched (so a Beads-shaped export
    // round-trips exactly); otherwise the cycle is rebuilt from its harmonics up to the lower of the two
    // Nyquist limits, so shrinking never aliases and stretching stays smooth.
    void resampleCycle (const float* x, int N, double* y)
    {
        if (N == kWaveSize) { for (int i = 0; i < kWaveSize; ++i) y[i] = x[i]; return; }

        const int H = std::min (kWaveSize / 2 - 1, (N - 1) / 2);
        std::vector<double> ct ((size_t) N), st ((size_t) N);
        for (int k = 0; k < N; ++k) { ct[(size_t) k] = std::cos (2.0 * kPi * k / N); st[(size_t) k] = std::sin (2.0 * kPi * k / N); }

        double mean = 0.0;
        for (int n = 0; n < N; ++n) mean += x[n];
        mean /= N;

        std::array<double, kWaveSize> c256, s256;
        for (int k = 0; k < kWaveSize; ++k) { c256[(size_t) k] = std::cos (2.0 * kPi * k / kWaveSize); s256[(size_t) k] = std::sin (2.0 * kPi * k / kWaveSize); }

        for (int m = 0; m < kWaveSize; ++m) y[m] = mean;
        for (int h = 1; h <= H; ++h)
        {
            double a = 0.0, b = 0.0;
            size_t idx = 0;
            for (int n = 0; n < N; ++n)
            {
                a += x[n] * ct[idx]; b += x[n] * st[idx];
                idx += (size_t) h; if (idx >= (size_t) N) idx %= (size_t) N;
            }
            a *= 2.0 / N; b *= 2.0 / N;
            for (int m = 0; m < kWaveSize; ++m)
            {
                const size_t j = ((size_t) h * (size_t) m) % kWaveSize;
                y[m] += a * c256[j] + b * s256[j];
            }
        }
    }
}   // namespace

void storeWave (const double* w, int16_t* out)
{
    double mean = 0.0;
    for (int j = 0; j < kWaveSize; ++j) mean += w[j];
    mean /= kWaveSize;

    double peak = 0.0;
    for (int j = 0; j < kWaveSize; ++j) peak = std::max (peak, std::fabs (w[j] - mean));

    int dd[kWaveSize] = {};
    if (peak > 1e-12)
    {
        const double scale = kPeak / peak;
        for (int j = 0; j < kWaveSize; ++j) dd[j] = (int) std::lround ((w[j] - mean) * scale);
    }

    // Rounding can leave the wave a few units off zero-sum, which would leave a step where the wave wraps.
    // Spread the leftover over evenly spaced samples (one unit each) so the running total closes exactly.
    int residual = 0;
    for (int j = 0; j < kWaveSize; ++j) residual += dd[j];
    if (residual != 0)
    {
        const int count = std::abs (residual), sign = residual > 0 ? 1 : -1;
        for (int i = 0; i < count; ++i) dd[(int) (((i + 0.5) * kWaveSize) / count) % kWaveSize] -= sign;
    }

    int d[kWaveSize];
    for (int m = 0; m < kWaveSize; ++m) d[m] = dd[(m + kWaveSize - kExportRotation) % kWaveSize];

    int S[kStoredSize];
    S[0] = 0;
    for (int k = 0; k < kStoredSize - 1; ++k) S[k + 1] = S[k] + d[k % kWaveSize];

    double sum = 0.0;
    for (int i = 1; i <= kWaveSize; ++i) sum += S[i];
    const int offset = (int) std::floor (sum / kWaveSize + 0.5);
    for (int i = 0; i < kStoredSize; ++i) out[i] = (int16_t) std::clamp (S[i] - offset, -32768, 32767);
}

Result bankFromWav (const uint8_t* data, size_t size, std::vector<int16_t>& bank, int cycleOverride)
{
    Result r;
    Wav wav;
    if (! decodeWav (data, size, wav, r.error)) return r;

    const size_t total = wav.mono.size();
    int cycle = 0;
    const char* how = "";
    if (cycleOverride > 0)      { cycle = cycleOverride;  how = "cycle length chosen by the caller"; }
    else if (wav.clmCycle > 0)  { cycle = wav.clmCycle;   how = "cycle length from the file's marker"; }
    else
    {
        how = "cycle length guessed from the file length";
        if (total == (size_t) kWavesPerBank * kWaveSize) cycle = kWaveSize;
        else if (total % 2048 == 0) cycle = 2048;
        else if (total % kWaveSize == 0) cycle = kWaveSize;
        else if (total <= 65536) cycle = (int) total;                 // short file: one cycle
        else
        {
            r.error = "The file has no cycle-length marker and its length (" + std::to_string (total)
                      + " samples) is not a multiple of 256 or 2048, so I can't tell where one wave ends.";
            return r;
        }
    }
    if (cycle < 8 || cycle > 65536 || total < (size_t) cycle)
    {
        r.error = "A cycle length of " + std::to_string (cycle) + " samples doesn't fit this file ("
                  + std::to_string (total) + " samples). Cycles need to be 8 to 65536 samples.";
        return r;
    }

    const int numCycles = (int) (total / (size_t) cycle);
    std::map<int, std::vector<double>> cache;
    auto getCycle = [&] (int idx) -> const std::vector<double>&
    {
        auto it = cache.find (idx);
        if (it == cache.end())
        {
            std::vector<double> y ((size_t) kWaveSize);
            resampleCycle (wav.mono.data() + (size_t) idx * (size_t) cycle, cycle, y.data());
            it = cache.emplace (idx, std::move (y)).first;
        }
        return it->second;
    };

    bank.assign ((size_t) kBankValues, 0);
    bool anyAudible = false;
    for (int i = 0; i < kWavesPerBank; ++i)
    {
        const double pos = numCycles == 1 ? 0.0 : (double) i * (numCycles - 1) / (kWavesPerBank - 1);
        const int i0 = std::min ((int) std::floor (pos), numCycles - 1);
        const int i1 = std::min (i0 + 1, numCycles - 1);
        const double fr = pos - i0;

        std::array<double, kWaveSize> w;
        const auto& c0 = getCycle (i0);
        if (fr == 0.0 || i1 == i0) { for (int j = 0; j < kWaveSize; ++j) w[(size_t) j] = c0[(size_t) j]; }
        else
        {
            const auto& c1 = getCycle (i1);
            for (int j = 0; j < kWaveSize; ++j) w[(size_t) j] = (1.0 - fr) * c0[(size_t) j] + fr * c1[(size_t) j];
        }

        double mean = 0.0, peak = 0.0;
        for (double v : w) mean += v;
        mean /= kWaveSize;
        for (double v : w) peak = std::max (peak, std::fabs (v - mean));
        if (peak > 1e-9) anyAudible = true;

        storeWave (w.data(), bank.data() + (size_t) i * kStoredSize);
    }

    if (! anyAudible)
    {
        bank.clear();
        r.error = "The file is silent (or perfectly flat), so there is no wave to use.";
        return r;
    }

    r.ok = true;
    r.summary = std::to_string (numCycles) + (numCycles == 1 ? " cycle" : " cycles") + " of " + std::to_string (cycle)
                + " samples (" + how + ")";
    return r;
}
}   // namespace WavetableLoader
