// Turns a WAV file into one Beads wavetable bank (16 waves), in the exact form the firmware stores them.
// JUCE-free, so it can be tested headless.
//
// How the stored form works (checked against the factory table in third_party/beads/resources.cc,
// and against a bank exported from GrainKontrol, which it reproduces bit for bit):
//   * a wave is 256 samples, peak-scaled to +-512
//   * the table holds the running total ("integrated" wave) of those samples, because the synth
//     differentiates again on playback (that is what keeps the oscillator band-limited)
//   * every wave is stored as 260 values: the 256 totals plus 4 wrap-around values for the interpolator
//   * the running total is centred on zero, and the exported WAV starts 3 samples later than the stored
//     data (the exporter lines the wave up so a sine starts at zero)
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace WavetableLoader
{
    constexpr int kWavesPerBank   = 16;
    constexpr int kWaveSize       = 256;
    constexpr int kStoredSize     = 260;                              // 256 + 4 wrap values
    constexpr int kBankValues     = kWavesPerBank * kStoredSize;      // 4160 int16 values per bank
    constexpr int kPeak           = 512;
    constexpr int kExportRotation = 3;

    struct Result
    {
        bool ok = false;
        std::string error;      // when !ok: a sentence a person can act on
        std::string summary;    // when ok: what was found, e.g. "16 cycles of 256 samples (cycle length from the file's marker)"
    };

    // Decode a WAV file held in memory and build one bank into `bank` (kBankValues values).
    // Reads 8/16/24/32-bit integer and 32/64-bit float WAVs, mono or multi-channel (channels are averaged),
    // and the Serum-style "clm " cycle-length marker that GrainKontrol writes.
    // `cycleLengthOverride` > 0 forces the cycle length; 0 means detect it.
    // Without a marker: exactly 4096 samples = 16 cycles of 256; a multiple of 2048 = cycles of 2048;
    // a multiple of 256 = cycles of 256.
    // Cycles of any other length are resampled to 256 samples without aliasing; if the file holds more or
    // fewer than 16 cycles, the 16 waves are spread evenly across it (cross-fading between neighbours).
    Result bankFromWav (const uint8_t* data, size_t size, std::vector<int16_t>& bank, int cycleLengthOverride = 0);

    // One wave: 256 samples in any range -> 260 stored values. DC is removed and the peak is set to +-512.
    void storeWave (const double* wave256, int16_t* out260);
}
