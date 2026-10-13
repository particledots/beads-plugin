// JUCE-free wrapper around the Mutable Instruments Beads DSP (MIT licensed, Emilie Gillet).
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "Resampler.h"

namespace beads { class Processor; }

class BeadsEngine
{
public:
    enum Media { DirtyTape = 0, CleanTape = 1, VintageDigital = 2, Digital = 3 };
    enum SeedMode { FreeRun = 0, MidiGate = 1, Manual = 2 };

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
    };

    BeadsEngine();
    ~BeadsEngine();

    void prepare (double hostSampleRate);

    // `gate` may be null (no MIDI gate). If non-null it holds one flag per host sample.
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const Params& p, const uint8_t* gate);

    int latencyInHostSamples() const;
    double processorRate() const { return procRate; }

private:
    struct Frame { float l, r; uint8_t gate; };

    void configure (int media, bool mono);
    void runBlocks (const Params& p);

    std::unique_ptr<beads::Processor> proc;
    std::vector<uint8_t> delayBuf1, delayBuf2;
    std::vector<uint16_t> reverbBuf;

    double hostRate = 48000.0, procRate = 48000.0;
    int curMedia = -1; bool curMono = false;
    size_t blockSize = 12;

    SincResampler rsInL, rsInR, rsOutL, rsOutR;
    std::vector<float> tmpL, tmpR;
    std::vector<float> hostQL, hostQR;   // host-rate output queue
    size_t hostQHead = 0;
    std::vector<Frame> inQueue;          // processor-rate input, waiting for a full block
    size_t inQueueHead = 0;
    std::vector<float> procOutL, procOutR;   // processor-rate output, waiting for the output resampler
    int prevGateFlags = 0;
};
