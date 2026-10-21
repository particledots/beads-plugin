#include "BeadsEngine.h"
#include <algorithm>
#include <cstring>
#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "stmlib/utils/gate_flags.h"

BeadsEngine::BeadsEngine() : proc (new beads::Processor()), synth (new beads::Synth())
{
    delayBuf1.assign (512 * 1024, 0);
    delayBuf2.assign (256 * 1024, 0);
    reverbBuf.assign (32768, 0);
    uint8_t* bufs[3] = { delayBuf1.data(), delayBuf1.data() + 256 * 1024, delayBuf2.data() };
    proc->Init (bufs, reverbBuf.data());

    // The synth has its own reverb (same size as the effect's), so the two never share memory.
    synthReverbBuf.assign (32768, 0);
    synth->Init (synthReverbBuf.data());
}

BeadsEngine::~BeadsEngine() = default;

void BeadsEngine::prepare (double sr)
{
    hostRate = sr;
    curMedia = -1;                // force reconfigure
    curSource = AudioIn;          // the first process() call applies the host's Source choice
    paramsApplied = false;        // the first process() call applies the host's Quality/Mono/Source even if frozen
    configure (beads::Processor::MEDIA_DIGITAL, false);
}

void BeadsEngine::configure (int media, bool mono)
{
    curMedia = media; curMono = mono;
    proc->Configure ((beads::Processor::Media) media, mono);   // also clears delay memory, as on the module
    proc->SetSoftEdges (curSoft);                              // Configure resets the grains to the default fades
    setupRates();
}

void BeadsEngine::setupRates()
{
    // The effect runs at the rate of the chosen Quality. The synth is fixed at 48 kHz (Synth::Init hard-codes it).
    procRate = (curSource == WavetableSynth) ? 48000.0 : (double) proc->sample_rate();
    blockSize = (size_t) std::max (1.0, std::round (procRate / 4000.0));   // module's block rate is 4 kHz
    rsInL.init (hostRate, procRate);  rsInR.init (hostRate, procRate);
    rsOutL.init (procRate, hostRate); rsOutR.init (procRate, hostRate);
    inQueue.clear(); inQueueHead = 0;
    procOutL.clear(); procOutR.clear();
    hostQL.clear(); hostQR.clear(); hostQHead = 0;
    // prime the output with a few blocks of silence so the output resampler never starves
    const size_t prime = blockSize * 2 + SincResampler::kTaps;
    procOutL.assign (prime, 0.0f); procOutR.assign (prime, 0.0f);
    prevGateFlags = 0;
}

int BeadsEngine::latencyInHostSamples() const
{
    const double procFrames = (double) blockSize * 2 + SincResampler::kTaps;   // block + priming
    if (std::abs (hostRate - procRate) < 1e-6) return (int) procFrames;       // resamplers bypassed
    return (int) std::ceil (procFrames * hostRate / procRate);
}

// Our own addition (not from the Beads firmware): when on, the Pitch value settles on a musical interval
// if it is within a quarter of a semitone of one, and is left alone everywhere else.
static float snapPitch (float semitones, bool enabled)
{
    if (! enabled) return semitones;
    static const float kDetents[] = { 0.f, 1.f, 3.f, 4.f, 7.f, 12.f, 24.f, 36.f, 48.f };   // unison, semitone, minor/major third, fifth, octaves
    const float a = std::fabs (semitones);
    for (float d : kDetents)
        if (std::fabs (a - d) < 0.25f)
            return semitones < 0.f ? -d : d;
    return semitones;
}

void BeadsEngine::runBlocks (const Params& p)
{
    using namespace beads;
    const size_t B = blockSize;

    Parameters prm;
    auto setp = [] (RandomizedParameter& r, float base, float att) {
        r.base = base; r.attenuverter = att; r.cv = 0.0f; r.has_cv = false;
    };
    setp (prm.time,  p.time,  p.timeMod);
    setp (prm.size,  p.size,  p.sizeMod);
    setp (prm.shape, p.shape, p.shapeMod);
    setp (prm.pitch, snapPitch (p.pitchSemitones, p.pitchSnap), p.pitchMod);
    prm.monophonic_mode = false;

    ModulatedParameter d; d.base = p.density; d.cv = 0.0f;
    Processor::MixParameters m;
    m.feedback = p.feedback; m.dry_wet = p.dryWet; m.reverb_amount = p.reverb; m.output_gate = false;

    SeedButtonState seedState = SEED_BUTTON_STATE_OFF;
    bool useGateInput = false;
    switch (p.seedMode)
    {
        case FreeRun:  seedState = SEED_BUTTON_STATE_LOCKED; break;
        case MidiGate: seedState = SEED_BUTTON_STATE_OFF; useGateInput = true; break;
        case Manual:   seedState = p.seedButton ? SEED_BUTTON_STATE_ON : SEED_BUTTON_STATE_OFF; break;
    }

    std::vector<FloatFrame> in (B), out (B);
    std::unique_ptr<bool[]> freeze (new bool[B]);
    std::vector<stmlib::GateFlags> seed (B);

    while (inQueue.size() - inQueueHead >= B)
    {
        for (size_t i = 0; i < B; ++i)
        {
            const Frame& f = inQueue[inQueueHead + i];
            in[i].l = f.l; in[i].r = f.r;
            freeze[i] = p.freeze;
            prevGateFlags = seed[i] = stmlib::ExtractGateFlags ((stmlib::GateFlags) prevGateFlags, f.gate != 0);
        }
        inQueueHead += B;

        if (curSource == WavetableSynth)
        {
            // In synth mode the module's Feedback knob picks the bank (quantised to 12 steps). We have a
            // dedicated Bank parameter instead, so aim at the middle of that bank's step. Quality sets the bit depth.
            m.feedback = ((float) std::clamp (p.bank, 0, kNumBanks - 1) + 0.5f) / (float) kNumBanks;
            synth->Render (seedState, useGateInput ? seed.data() : nullptr, freeze.get(),
                           (beads::Synth::Resolution) std::clamp (curMedia, 0, 3),
                           d, prm, m, out.data(), B);   // audio input is not used in synth mode
        }
        else
            proc->Process (seedState, useGateInput ? seed.data() : nullptr, freeze.get(),
                           d, prm, m, in.data(), out.data(), B);

        for (size_t i = 0; i < B; ++i)
        {
            float l = out[i].l, r = out[i].r;
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;
            procOutL.push_back (std::clamp (l, -4.0f, 4.0f));
            procOutR.push_back (std::clamp (r, -4.0f, 4.0f));
        }
    }
    if (inQueueHead > 4096)   // compact
    {
        inQueue.erase (inQueue.begin(), inQueue.begin() + (long) inQueueHead);
        inQueueHead = 0;
    }
}

void BeadsEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                           int n, const Params& p, const uint8_t* gate)
{
    if (p.softEdges != curSoft) { curSoft = p.softEdges; proc->SetSoftEdges (curSoft); }

    // Changing Quality or Mono clears the buffer, so while Freeze is on the change is held back
    // (it applies once Freeze is released). Changing Source is held back the same way, so a frozen
    // sound never changes underneath you. The very first block always applies the saved settings.
    const bool mediaChanged  = p.media != curMedia || p.mono != curMono;
    const bool sourceChanged = p.source != curSource;
    if ((mediaChanged || sourceChanged) && (! p.freeze || ! paramsApplied))
    {
        curSource = p.source;
        if (mediaChanged) configure (p.media, p.mono);   // also sets up the rates for the new Source
        else              setupRates();                  // Source only: the effect's buffer is left untouched
    }
    paramsApplied = true;

    // 1. host rate -> processor rate (gate flag is carried along by nearest-sample mapping)
    tmpL.clear(); tmpR.clear();
    rsInL.process (inL, (size_t) n, tmpL);
    rsInR.process (inR, (size_t) n, tmpR);
    const size_t produced = std::min (tmpL.size(), tmpR.size());
    for (size_t i = 0; i < produced; ++i)
    {
        size_t src = produced > 0 ? std::min ((size_t) n - 1, i * (size_t) n / produced) : 0;
        inQueue.push_back ({ tmpL[i], tmpR[i], (uint8_t) (gate ? gate[src] : 0) });
    }

    // 2. run the module DSP
    runBlocks (p);

    // 3. processor rate -> host rate, queued so block-size mismatches never drop samples
    rsOutL.process (procOutL.data(), procOutL.size(), hostQL);
    rsOutR.process (procOutR.data(), procOutR.size(), hostQR);
    procOutL.clear(); procOutR.clear();

    const size_t avail = std::min (hostQL.size(), hostQR.size()) - hostQHead;
    const size_t take = std::min (avail, (size_t) n);
    for (size_t i = 0; i < take; ++i) { outL[i] = hostQL[hostQHead + i]; outR[i] = hostQR[hostQHead + i]; }
    for (size_t i = take; i < (size_t) n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }   // underrun guard
    hostQHead += take;
    if (hostQHead > 8192)
    {
        hostQL.erase (hostQL.begin(), hostQL.begin() + (long) hostQHead);
        hostQR.erase (hostQR.begin(), hostQR.begin() + (long) hostQHead);
        hostQHead = 0;
    }
}
