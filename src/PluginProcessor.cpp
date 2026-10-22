#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

juce::AudioProcessorValueTreeState::ParameterLayout BeadsAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.f, 1.f, 0.0001f), def,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
    };
    auto bip = [] (const char* id, const char* name)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (-1.f, 1.f, 0.0001f), 0.f,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
    };

    p.push_back (unit ("time",  "Time",  0.5f));
    p.push_back (unit ("size",  "Size",  0.5f));
    p.push_back (unit ("shape", "Shape", 0.5f));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "pitch", 1 }, "Pitch",
                 juce::NormalisableRange<float> (-48.f, 48.f, 0.01f), 0.f, juce::AudioParameterFloatAttributes().withLabel ("st")));
    p.push_back (bip ("time_mod",  "Time Mod"));
    p.push_back (bip ("size_mod",  "Size Mod"));
    p.push_back (bip ("shape_mod", "Shape Mod"));
    p.push_back (bip ("pitch_mod", "Pitch Mod"));
    p.push_back (unit ("density",  "Density",  0.75f));
    p.push_back (unit ("feedback", "Feedback", 0.2f));
    p.push_back (unit ("dry_wet",  "Dry/Wet",  0.5f));
    p.push_back (unit ("reverb",   "Reverb",   0.2f));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "freeze", 1 }, "Freeze", false));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "seed_mode", 1 }, "Grain Gate",
                 juce::StringArray { "Free-run", "MIDI gate", "Manual" }, 0));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "seed", 1 }, "Seed (manual)", false));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "media", 1 }, "Quality",
                 juce::StringArray { "Dirty tape", "Clean tape", "Vintage digital", "Digital" }, 3));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "mono", 1 }, "Mono", false));
    // Added after 0.1.0, so they carry a higher version hint (keeps AU hosts happy with older sessions).
    p.push_back (std::make_unique<APB> (juce::ParameterID { "pitch_snap", 2 }, "Pitch Snap", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "soft_edges", 2 }, "Soft Grain Edges", false));
    // Synth mode (the module's idle wavetable oscillator + grains). Source defaults to the audio effect.
    p.push_back (std::make_unique<APC> (juce::ParameterID { "source", 2 }, "Source",
                 juce::StringArray { "Audio In", "Wavetable Synth" }, 0));
    juce::StringArray banks;
    for (int i = 1; i <= BeadsEngine::kNumBanks; ++i) banks.add (juce::String (i));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "bank", 2 }, "Wavetable Bank", banks, 0));
    // Synth mode: MIDI notes set the oscillator's pitch (on by default); Drone keeps the oscillator sounding with no note held (off by default).
    p.push_back (std::make_unique<APB> (juce::ParameterID { "midi_pitch", 3 }, "MIDI Pitch", true));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "drone", 3 }, "Drone", false));
    // Scale for the random pitch jumps set by the pitch Mod knob (Off = continuous, as the module does it).
    juce::StringArray scaleNames { "Off" };
    for (const auto& sc : Scales::kScales) scaleNames.add (sc.name);
    p.push_back (std::make_unique<APC> (juce::ParameterID { "pitch_scale", 4 }, "Pitch Scale", scaleNames, 0));
    return { p.begin(), p.end() };
}

namespace
{
    // Starting points, written from the knob meanings in the README. Not yet tuned by ear.
    struct Preset
    {
        const char* name;
        float time, size, shape, pitch;
        float timeMod, sizeMod, shapeMod, pitchMod;
        float density, feedback, dryWet, reverb;
        int media;   // 0 Dirty tape, 1 Clean tape, 2 Vintage digital, 3 Digital
    };

    const Preset kPresets[] =
    {
        //  name              time  size  shape pitch | tMod  sMod  shMod pMod | dens  fdbk  d/w   rev | media
        { "Init",             0.50f, 0.50f, 0.50f,   0.f,  0.00f, 0.00f, 0.00f, 0.00f,  0.75f, 0.20f, 0.50f, 0.20f, 3 },
        { "Slow Bloom",       0.60f, 0.75f, 0.80f,   0.f,  0.20f, 0.10f, 0.00f, 0.00f,  0.70f, 0.35f, 0.60f, 0.45f, 3 },
        { "Glass Dust",       0.30f, 0.25f, 0.50f,  12.f,  0.50f, 0.30f, 0.00f, 0.20f,  0.85f, 0.15f, 0.55f, 0.30f, 3 },
        { "Tape Smear",       0.55f, 0.60f, 0.65f,   0.f,  0.00f, 0.00f, 0.00f, 0.00f,  0.65f, 0.50f, 0.50f, 0.25f, 0 },
        { "Stutter Grid",     0.20f, 0.35f, 0.15f,   0.f,  0.00f, 0.00f, 0.00f, 0.00f,  0.35f, 0.25f, 0.60f, 0.10f, 3 },
        { "Octave Fog",       0.50f, 0.55f, 0.60f,  12.f,  0.00f, 0.00f, 0.00f, 0.15f,  0.80f, 0.40f, 0.55f, 0.50f, 2 },
        { "Long Haze",        0.80f, 0.90f, 0.90f,   0.f,  0.30f, 0.00f, 0.00f, 0.00f,  0.90f, 0.55f, 0.65f, 0.60f, 1 },
        { "Broken Cassette",  0.40f, 0.40f, 0.50f,  -7.f,  0.40f, 0.00f, 0.30f, 0.40f,  0.80f, 0.45f, 0.50f, 0.20f, 0 },
    };
    constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
}

int BeadsAudioProcessor::getNumPrograms() { return kNumPresets; }

const juce::String BeadsAudioProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, kNumPresets) ? juce::String (kPresets[i].name) : juce::String();
}

void BeadsAudioProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumPresets)) return;
    currentProgram = i;
    const auto& p = kPresets[i];

    auto set = [this] (const char* id, float realValue)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
    };
    set ("time", p.time);   set ("size", p.size);   set ("shape", p.shape);   set ("pitch", p.pitch);
    set ("time_mod", p.timeMod);   set ("size_mod", p.sizeMod);   set ("shape_mod", p.shapeMod);   set ("pitch_mod", p.pitchMod);
    set ("density", p.density);   set ("feedback", p.feedback);   set ("dry_wet", p.dryWet);   set ("reverb", p.reverb);
    set ("media", (float) p.media);
}

BeadsAudioProcessor::BeadsAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool BeadsAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && l.getMainInputChannelSet() == out;
}

void BeadsAudioProcessor::prepareToPlay (double sr, int maxBlock)
{
    engine.prepare (sr);
    gateFlags.assign ((size_t) maxBlock, 0);
    scratchL.assign ((size_t) maxBlock, 0.f);
    scratchR.assign ((size_t) maxBlock, 0.f);
    notes.reset();
    setLatencySamples (engine.latencyInHostSamples());
}

BeadsEngine::Params BeadsAudioProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    BeadsEngine::Params p;
    p.time = v ("time"); p.size = v ("size"); p.shape = v ("shape"); p.pitchSemitones = v ("pitch");
    p.timeMod = v ("time_mod"); p.sizeMod = v ("size_mod"); p.shapeMod = v ("shape_mod"); p.pitchMod = v ("pitch_mod");
    p.density = v ("density"); p.feedback = v ("feedback"); p.dryWet = v ("dry_wet"); p.reverb = v ("reverb");
    p.freeze = v ("freeze") > 0.5f;
    p.seedMode = (int) v ("seed_mode");
    p.seedButton = v ("seed") > 0.5f;
    p.media = (int) v ("media");
    p.mono = v ("mono") > 0.5f;
    p.pitchSnap = v ("pitch_snap") > 0.5f;
    p.softEdges = v ("soft_edges") > 0.5f;
    p.source = (int) v ("source");
    p.bank = (int) v ("bank");
    p.droneOn = v ("drone") > 0.5f;
    p.pitchScale = (int) v ("pitch_scale");
    return p;
}

void BeadsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    if ((size_t) n > scratchL.size())
    {   // host exceeded its promised max block size
        scratchL.resize ((size_t) n); scratchR.resize ((size_t) n); gateFlags.resize ((size_t) n);
    }

    const auto params = readParams();

    // MIDI notes are always tracked: held notes give the gate (sample-accurate); in synth mode the most recent
    // held note also sets the oscillator pitch (applied per audio buffer) and, with Drone off, lets it sound.
    int pos = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit (0, n, meta.samplePosition);
        for (; pos < at; ++pos) gateFlags[(size_t) pos] = notes.held() ? 1 : 0;
        if (msg.isNoteOn()) notes.noteOn (msg.getNoteNumber());
        else if (msg.isNoteOff()) notes.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) notes.allOff();
    }
    for (; pos < n; ++pos) gateFlags[(size_t) pos] = notes.held() ? 1 : 0;
    midi.clear();

    // Grains follow the gate only in "MIDI gate" mode; the oscillator follows it when Drone is off.
    auto engineParams = params;
    const bool synthMode = params.source == BeadsEngine::WavetableSynth;
    if (synthMode && notes.current() >= 0 && apvts.getRawParameterValue ("midi_pitch")->load() > 0.5f)
        engineParams.midiPitchOffset = (float) (notes.current() - 48);   // note 48 (C3) = the oscillator's own pitch
    const uint8_t* gate = gateFlags.data();
    if (params.seedMode != BeadsEngine::MidiGate && ! (synthMode && ! params.droneOn))
        gate = nullptr;

    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (chans > 1 ? 1 : 0);
    // copy input first; engine writes to scratch, then back (works in-place)
    engine.process (inL, inR, scratchL.data(), scratchR.data(), n, engineParams, gate);
    buffer.copyFrom (0, 0, scratchL.data(), n);
    if (chans > 1) buffer.copyFrom (1, 0, scratchR.data(), n);
}

juce::AudioProcessorEditor* BeadsAudioProcessor::createEditor()
{
    return new BeadsEditor (*this);
}

bool BeadsAudioProcessor::loadBankFromFile (int bank, const juce::File& file, juce::String& message)
{
    if (bank < 0 || bank >= BeadsEngine::kNumBanks) { message = "There is no such bank."; return false; }
    if (! file.existsAsFile())                      { message = "That file doesn't exist."; return false; }
    if (file.getSize() > 64 * 1024 * 1024)          { message = "That file is too large (over 64 MB)."; return false; }

    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes)) { message = "The file couldn't be read."; return false; }

    std::vector<int16_t> data;
    const auto r = WavetableLoader::bankFromWav (static_cast<const uint8_t*> (bytes.getData()), bytes.getSize(), data);
    if (! r.ok) { message = juce::String (r.error); return false; }

    {
        const juce::ScopedLock sl (bankLock);
        auto& u = userBanks[(size_t) bank];
        u.loaded = true; u.name = file.getFileName(); u.data = data;
    }
    engine.queueBank (bank, data.data());
    message = "Loaded " + file.getFileName() + ": " + juce::String (r.summary).upToFirstOccurrenceOf (" (", false, false);
    return true;
}

void BeadsAudioProcessor::resetBank (int bank)
{
    if (bank < 0 || bank >= BeadsEngine::kNumBanks) return;
    {
        const juce::ScopedLock sl (bankLock);
        userBanks[(size_t) bank] = UserBank();
    }
    engine.queueFactoryBank (bank);
}

bool BeadsAudioProcessor::isUserBank (int bank) const
{
    const juce::ScopedLock sl (bankLock);
    return bank >= 0 && bank < BeadsEngine::kNumBanks && userBanks[(size_t) bank].loaded;
}

juce::String BeadsAudioProcessor::describeBank (int bank) const
{
    const juce::ScopedLock sl (bankLock);
    if (bank >= 0 && bank < BeadsEngine::kNumBanks && userBanks[(size_t) bank].loaded)
        return "User: " + userBanks[(size_t) bank].name;
    return "Factory";
}

// The banks you load are saved inside the host project (as base64 inside the plugin state), so a Logic
// project keeps working if the original WAV files move or are deleted. Each bank is about 11 KB.
void BeadsAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild (i).hasType ("USERBANK")) state.removeChild (i, nullptr);

    {
        const juce::ScopedLock sl (bankLock);
        for (int b = 0; b < BeadsEngine::kNumBanks; ++b)
        {
            const auto& u = userBanks[(size_t) b];
            if (! u.loaded || u.data.size() != (size_t) WavetableLoader::kBankValues) continue;
            juce::ValueTree vt ("USERBANK");
            vt.setProperty ("bank", b, nullptr);
            vt.setProperty ("name", u.name, nullptr);
            vt.setProperty ("data", juce::Base64::toBase64 (u.data.data(), u.data.size() * sizeof (int16_t)), nullptr);
            state.appendChild (vt, nullptr);
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void BeadsAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto vt = juce::ValueTree::fromXml (*xml);
    std::array<UserBank, 12> incoming;
    for (int i = vt.getNumChildren(); --i >= 0;)
    {
        auto child = vt.getChild (i);
        if (! child.hasType ("USERBANK")) continue;
        const int b = child.getProperty ("bank", -1);
        juce::MemoryOutputStream decoded;
        if (b >= 0 && b < BeadsEngine::kNumBanks
            && juce::Base64::convertFromBase64 (decoded, child.getProperty ("data").toString())
            && decoded.getDataSize() == (size_t) WavetableLoader::kBankValues * sizeof (int16_t))
        {
            auto& u = incoming[(size_t) b];
            u.loaded = true;
            u.name = child.getProperty ("name").toString();
            u.data.resize ((size_t) WavetableLoader::kBankValues);
            std::memcpy (u.data.data(), decoded.getData(), decoded.getDataSize());
        }
        vt.removeChild (i, nullptr);       // keep the parameter state free of our extra nodes
    }
    apvts.replaceState (vt);

    // Banks that are not in the saved state go back to factory.
    {
        const juce::ScopedLock sl (bankLock);
        userBanks = std::move (incoming);
    }
    for (int b = 0; b < BeadsEngine::kNumBanks; ++b)
    {
        const juce::ScopedLock sl (bankLock);
        const auto& u = userBanks[(size_t) b];
        if (u.loaded) engine.queueBank (b, u.data.data());
        else          engine.queueFactoryBank (b);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BeadsAudioProcessor(); }
