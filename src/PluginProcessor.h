#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BeadsEngine.h"

class BeadsAudioProcessor : public juce::AudioProcessor
{
public:
    BeadsAudioProcessor();
    ~BeadsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    // Factory presets (starting points; they set the sound knobs and Quality, not Freeze/Mono/Grain Gate)
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    BeadsEngine::Params readParams() const;

    BeadsEngine engine;
    std::vector<uint8_t> gateFlags;
    std::vector<float> scratchL, scratchR;
    int heldNotes = 0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeadsAudioProcessor)
};
