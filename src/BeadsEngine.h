// JUCE-free wrapper around the Mutable Instruments Beads DSP (MIT licensed, Emilie Gillet).
#pragma once
#include <array>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>
#include "Resampler.h"
#include "WavetableLoader.h"

namespace beads { class Processor; class Synth; }

class BeadsEngine
{
public:
    enum Media { DirtyTape = 0, CleanTape = 1, VintageDigital = 2, Digital = 3 };
    enum SeedMode { FreeRun = 0, MidiGate = 1, Manual = 2 };
    // AudioIn = the granular effect on the input. WavetableSynth = the module's idle mode
    // (wavetable oscillator + wavetable grains); the audio input is ignored there.
    enum Source { AudioIn = 0, WavetableSynth = 1 };
    static constexpr int kNumBanks = 12;

    struct Params
    {
        // Same meaning as the module's knobs.
        float time = 0.5f, size = 0.5f, shape = 0.5f;   // 0..1
        float pitchSemitones = 0.0f;                    // semitones
        float timeMod = 0.f, sizeMod = 0.f, shapeMod = 0.f, pitchMod = 0.f;   // attenuverters, -1..1
        float density = 0.75f, feedback = 0.2f, dryWet = 0.5f, reverb = 0.2f;   // 0..1
        bool freeze = false;
        bool seedButton = false;        // used in Manual seed mode
        int seedMode = FreeRun;
        int media = Digital;
        bool mono = false;
        bool pitchSnap = false;         // our addition: Pitch settles on musical intervals (see snapPitch)
        bool softEdges = false;         // our addition: longer minimum grain fades (see GrainEnvelope::SetSoftEdges)
        int source = AudioIn;           // AudioIn or WavetableSynth
        int bank = 0;                   // wavetable bank 0..11 (synth mode only; the module uses the Feedback knob for this)
        // Synth mode only. The plugin fills these from MIDI; the module has a V/oct input for the same job.
        float midiPitchOffset = 0.0f;   // semitones added to Pitch (0 = the oscillator's own 130.81 Hz)
        bool droneOn = true;            // false: the oscillator only sounds while a note is held (the grains are not affected)
    };

    BeadsEngine();
    ~BeadsEngine();

    void prepare (double hostSampleRate);

    // `gate` may be null (no MIDI gate). If non-null it holds one flag per host sample.
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const Params& p, const uint8_t* gate);

    int latencyInHostSamples() const;
    double processorRate() const { return procRate; }

    // --- Wavetable banks (synth mode) ---
    // Replace one bank (0..11) with 16 waves in the stored form (WavetableLoader::kBankValues values).
    // Safe to call from the message thread while audio runs: the new data waits in a side buffer and is
    // copied in at the start of the next audio block (no allocation, no blocking on the audio thread).
    void queueBank (int bank, const int16_t* bankData);
    // Put one factory bank back.
    void queueFactoryBank (int bank);
    // The table the synth is playing right now (12 banks x kBankValues). For tests; call from the audio thread's side only.
    const int16_t* currentWavetables() const { return wavetables.data(); }
    static const int16_t* factoryBank (int bank);

private:
    struct Frame { float l, r; uint8_t gate; };

    void configure (int media, bool mono);
    void setupRates();        // processor rate, block size, resamplers and queues for the current media + source
    void runBlocks (const Params& p);

    std::unique_ptr<beads::Processor> proc;
    std::unique_ptr<beads::Synth> synth;
    std::vector<uint8_t> delayBuf1, delayBuf2;
    std::vector<uint16_t> reverbBuf, synthReverbBuf;

    // The synth's wavetables: its own copy of the 12 factory banks, never resized, so the pointers the
    // synth keeps stay valid. Banks replaced from the UI wait in `pending` until the audio thread picks them up.
    std::vector<int16_t> wavetables;
    struct PendingBank { bool dirty = false; std::array<int16_t, WavetableLoader::kBankValues> data {}; };
    std::array<PendingBank, 12> pending;
    std::mutex pendingMutex;
    void applyPendingBanks();

    double hostRate = 48000.0, procRate = 48000.0;
    int curMedia = -1; bool curMono = false;
    int curSource = AudioIn;
    bool curSoft = false;
    bool paramsApplied = false;   // false until the first block has applied the host's Quality/Mono
    size_t blockSize = 12;
    float oscLevel = 1.0f;        // smoothed oscillator level in synth mode (see Params::droneOn)

    SincResampler rsInL, rsInR, rsOutL, rsOutR;
    std::vector<float> tmpL, tmpR;
    std::vector<float> hostQL, hostQR;   // host-rate output queue
    size_t hostQHead = 0;
    std::vector<Frame> inQueue;          // processor-rate input, waiting for a full block
    size_t inQueueHead = 0;
    std::vector<float> procOutL, procOutR;   // processor-rate output, waiting for the output resampler
    int prevGateFlags = 0;
};
