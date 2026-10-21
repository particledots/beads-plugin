#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Simple flat look for rotary knobs.
class BeadsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BeadsLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class BeadsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeadsEditor (BeadsAudioProcessor&);
    ~BeadsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    int selectedBank() const;
    void chooseWavetableFile();
    void setStatus (int forBank, const juce::String& text);
    void refreshBankInfo();

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramId, const juce::String& name, bool small);

    BeadsAudioProcessor& proc;
    BeadsLookAndFeel laf;

    Knob time, size, shape, pitch;
    Knob timeMod, sizeMod, shapeMod, pitchMod;
    Knob density, feedback, dryWet, reverb;

    juce::ToggleButton freeze { "FREEZE" }, seed { "SEED" }, mono { "MONO" };
    juce::ToggleButton snap { "PITCH SNAP" }, softEdges { "SOFT GRAIN EDGES" };
    juce::ComboBox gateMode, quality, source, bank;
    juce::Label gateLabel { {}, "GRAIN GATE" }, qualityLabel { {}, "QUALITY" },
                sourceLabel { {}, "SOURCE" }, bankLabel { {}, "BANK" };
    juce::TextButton loadBank { "LOAD WAVETABLE..." }, resetBank { "RESET BANK" };
    juce::Label bankInfo;
    juce::String statusText;            // result of the last Load, shown until the Bank selection changes
    int statusBank = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ButtonAtt> freezeAtt, seedAtt, monoAtt, snapAtt, softEdgesAtt;
    std::unique_ptr<ComboAtt> gateAtt, qualityAtt, sourceAtt, bankAtt;
    std::unique_ptr<juce::ParameterAttachment> sourceModeAtt;   // Feedback is unused and Bank is used only in synth mode
    std::unique_ptr<juce::ParameterAttachment> freezeLockAtt;   // greys out Quality/Mono/Source while frozen (declared last)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeadsEditor)
};
