#pragma once
#include <array>
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

    // --- Wavetable banks for synth mode ---
    // Replace wavetable bank `bank` (0..11) with the contents of a WAV file. `message` says what happened either way.
    bool loadBankFromFile (int bank, const juce::File& file, juce::String& message);
    void resetBank (int bank);                    // back to the factory bank
    bool isUserBank (int bank) const;
    juce::String describeBank (int bank) const;   // "Factory" or "User: file name"

private:
    struct UserBank { bool loaded = false; juce::String name; std::vector<int16_t> data; };
    mutable juce::CriticalSection bankLock;       // guards userBanks (message thread only touches them; the audio thread never does)
    std::array<UserBank, 12> userBanks;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    BeadsEngine::Params readParams() const;

    BeadsEngine engine;
    std::vector<uint8_t> gateFlags;
    std::vector<float> scratchL, scratchR;
    int heldNotes = 0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeadsAudioProcessor)
};
