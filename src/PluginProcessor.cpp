#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    return { p.begin(), p.end() };
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
    heldNotes = 0;
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

    // MIDI notes -> grain gate (high while any note is held), sample-accurate.
    const uint8_t* gate = nullptr;
    if (params.seedMode == BeadsEngine::MidiGate)
    {
        int pos = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int at = juce::jlimit (0, n, meta.samplePosition);
            for (; pos < at; ++pos) gateFlags[(size_t) pos] = heldNotes > 0 ? 1 : 0;
            if (msg.isNoteOn()) ++heldNotes;
            else if (msg.isNoteOff()) heldNotes = juce::jmax (0, heldNotes - 1);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) heldNotes = 0;
        }
        for (; pos < n; ++pos) gateFlags[(size_t) pos] = heldNotes > 0 ? 1 : 0;
        gate = gateFlags.data();
    }
    else heldNotes = 0;
    midi.clear();

    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (chans > 1 ? 1 : 0);
    // copy input first; engine writes to scratch, then back (works in-place)
    engine.process (inL, inR, scratchL.data(), scratchR.data(), n, params, gate);
    buffer.copyFrom (0, 0, scratchL.data(), n);
    if (chans > 1) buffer.copyFrom (1, 0, scratchR.data(), n);
}

juce::AudioProcessorEditor* BeadsAudioProcessor::createEditor()
{
    return new BeadsEditor (*this);
}

void BeadsAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void BeadsAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BeadsAudioProcessor(); }
