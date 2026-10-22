#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      (0xff1a1c20);
    const juce::Colour kPanel   (0xff23262c);
    const juce::Colour kTrack   (0xff3a3e47);
    const juce::Colour kAccent  (0xffe3a857);
    const juce::Colour kText    (0xffe8e6e1);
    const juce::Colour kDim     (0xff8d909a);
}

BeadsLookAndFeel::BeadsLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kTrack);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, kTrack);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, kText);
}

void BeadsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    const float thick = juce::jmax (3.0f, d * 0.09f);
    const auto arcR = r.reduced (thick * 0.5f);

    juce::Path track;
    track.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0, a0, a1, true);
    g.setColour (kTrack);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = a0 + pos * (a1 - a0);
    // bipolar controls fill from the centre, others from the start
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    juce::Path val;
    val.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0,
                       juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? kAccent : kDim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    const auto c = r.getCentre();
    const float len = d * 0.5f - thick * 2.2f;
    juce::Line<float> line (c.getPointOnCircumference (len * 0.35f, angle),
                            c.getPointOnCircumference (len, angle));
    g.setColour (kText);
    g.drawLine (line, juce::jmax (2.0f, d * 0.045f));
}

void BeadsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    const float a = b.isEnabled() ? 1.0f : 0.4f;   // dimmed when unavailable (e.g. MONO while frozen)
    g.setColour ((on ? kAccent : kPanel).withMultipliedAlpha (a));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour ((on ? kAccent : kTrack).withMultipliedAlpha (a));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour ((on ? juce::Colours::black : kText).withMultipliedAlpha (a));
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

BeadsEditor::BeadsEditor (BeadsAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    addKnob (time,  "time",  "TIME",  false);
    addKnob (size,  "size",  "SIZE",  false);
    addKnob (shape, "shape", "SHAPE", false);
    addKnob (pitch, "pitch", "PITCH", false);
    addKnob (timeMod,  "time_mod",  "mod", true);
    addKnob (sizeMod,  "size_mod",  "mod", true);
    addKnob (shapeMod, "shape_mod", "mod", true);
    addKnob (pitchMod, "pitch_mod", "mod", true);
    addKnob (density,  "density",  "DENSITY",  false);
    addKnob (feedback, "feedback", "FEEDBACK", false);
    addKnob (dryWet,   "dry_wet",  "DRY/WET",  false);
    addKnob (reverb,   "reverb",   "REVERB",   false);

    for (auto* b : { &freeze, &seed, &mono, &softEdges, &midiPitch, &drone }) addAndMakeVisible (*b);
    freezeAtt = std::make_unique<ButtonAtt> (proc.apvts, "freeze", freeze);
    seedAtt   = std::make_unique<ButtonAtt> (proc.apvts, "seed", seed);
    monoAtt   = std::make_unique<ButtonAtt> (proc.apvts, "mono", mono);
    softEdgesAtt = std::make_unique<ButtonAtt> (proc.apvts, "soft_edges", softEdges);
    midiPitchAtt = std::make_unique<ButtonAtt> (proc.apvts, "midi_pitch", midiPitch);
    droneAtt     = std::make_unique<ButtonAtt> (proc.apvts, "drone", drone);
    midiPitch.setTooltip ("Synth mode: MIDI notes set the oscillator pitch (C3 = the Pitch knob's own pitch).");
    drone.setTooltip ("Synth mode: on = the oscillator always sounds; off = only while a MIDI note is held.");

    gateMode.addItemList ({ "Free-run", "MIDI gate", "Manual" }, 1);
    quality.addItemList ({ "Dirty tape", "Clean tape", "Vintage digital", "Digital" }, 1);
    addAndMakeVisible (gateMode); addAndMakeVisible (quality);
    gateAtt    = std::make_unique<ComboAtt> (proc.apvts, "seed_mode", gateMode);
    qualityAtt = std::make_unique<ComboAtt> (proc.apvts, "media", quality);

    source.addItemList ({ "Audio In", "Wavetable Synth" }, 1);
    for (int i = 1; i <= BeadsEngine::kNumBanks; ++i) bank.addItem (juce::String (i), i);
    addAndMakeVisible (source); addAndMakeVisible (bank);
    sourceAtt = std::make_unique<ComboAtt> (proc.apvts, "source", source);
    bankAtt   = std::make_unique<ComboAtt> (proc.apvts, "bank", bank);

    snapMode.addItemList ({ "Off", "Steps", "Semitones" }, 1);
    addAndMakeVisible (snapMode);
    snapAtt = std::make_unique<ComboAtt> (proc.apvts, "pitch_snap_mode", snapMode);
    snapMode.setTooltip ("What the Pitch knob settles on. Steps = octaves and fifths (0, 7, 12, 19, 24 ... up to 48, both directions); Semitones = whole semitones.");
    pitchScale.addItem ("Off", 1);
    for (int i = 0; i < Scales::kNumScales; ++i) pitchScale.addItem (Scales::kScales[i].name, i + 2);
    addAndMakeVisible (pitchScale);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, "pitch_scale", pitchScale);
    pitchScale.setTooltip ("The random pitch jumps from the Pitch mod knob land only on notes of this scale, built on the note being played (or the Pitch knob if no note).");

    for (auto* l : { &gateLabel, &qualityLabel, &sourceLabel, &bankLabel, &scaleLabel, &snapLabel })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        addAndMakeVisible (*l);
    }

    // In synth mode the Feedback knob does nothing (the module uses it to pick the bank, which has its own
    // control here) and Bank only matters there.
    sourceModeAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("source"),
        [this] (float v)
        {
            const bool synthMode = v > 0.5f;
            feedback.slider.setEnabled (! synthMode);
            bank.setEnabled (synthMode);
            midiPitch.setEnabled (synthMode);
            drone.setEnabled (synthMode);
        });
    sourceModeAtt->sendInitialUpdate();

    // Quality, Mono and Source changes are held back while Freeze is on (they would clear or change the frozen
    // sound), so show those controls as unavailable meanwhile.
    freezeLockAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("freeze"),
        [this] (float v)
        {
            const bool frozen = v > 0.5f;
            quality.setEnabled (! frozen);
            mono.setEnabled (! frozen);
            source.setEnabled (! frozen);
        });
    freezeLockAtt->sendInitialUpdate();

    // Wavetable banks: load a WAV into the selected bank, or put the factory bank back.
    for (auto* b : { &loadBank, &resetBank }) addAndMakeVisible (*b);
    loadBank.onClick  = [this] { chooseWavetableFile(); };
    resetBank.onClick = [this]
    {
        const int b = selectedBank();
        proc.resetBank (b);
        setStatus (b, "Bank " + juce::String (b + 1) + " is back to the factory wavetable.");
    };
    bankInfo.setJustificationType (juce::Justification::centredLeft);
    bankInfo.setFont (juce::Font (juce::FontOptions (11.0f)));
    bankInfo.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (bankInfo);
    refreshBankInfo();
    startTimerHz (4);

    setSize (640, 656);
}

BeadsEditor::~BeadsEditor() { stopTimer(); setLookAndFeel (nullptr); }

int BeadsEditor::selectedBank() const
{
    return juce::jlimit (0, BeadsEngine::kNumBanks - 1, (int) proc.apvts.getRawParameterValue ("bank")->load());
}

void BeadsEditor::setStatus (int forBank, const juce::String& text)
{
    statusBank = forBank;
    statusText = text;
    refreshBankInfo();
}

void BeadsEditor::refreshBankInfo()
{
    const int b = selectedBank();
    if (statusText.isNotEmpty() && statusBank != b) statusText = {};      // the message belonged to another bank
    const juce::String text = statusText.isNotEmpty() ? statusText
                                                      : "Bank " + juce::String (b + 1) + ": " + proc.describeBank (b);
    if (bankInfo.getText() != text) bankInfo.setText (text, juce::dontSendNotification);
    resetBank.setEnabled (proc.isUserBank (b));
}

void BeadsEditor::timerCallback() { refreshBankInfo(); }

void BeadsEditor::chooseWavetableFile()
{
    static juce::File lastFolder;
    const int b = selectedBank();
    chooser = std::make_unique<juce::FileChooser> ("Choose a wavetable (.wav) for bank " + juce::String (b + 1), lastFolder, "*.wav", true);
    juce::Component::SafePointer<BeadsEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, b] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;                         // cancelled
            lastFolder = f.getParentDirectory();
            if (safe == nullptr) return;
            juce::String message;
            const bool ok = safe->proc.loadBankFromFile (b, f, message);
            safe->setStatus (b, ok ? "Bank " + juce::String (b + 1) + " - " + message : "Couldn't load: " + message);
        });
}

void BeadsEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name, bool small)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, small ? 56 : 70, small ? 14 : 16);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.slider.setNumDecimalPlacesToDisplay (2);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (small ? 10.0f : 12.0f, small ? juce::Font::plain : juce::Font::bold)));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

void BeadsEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("UNOFFICIAL BEADS", 20, 10, 400, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("granular texture - community port, not a Mutable Instruments product", 20, 34, 500, 16, juce::Justification::centredLeft);

    g.setColour (kPanel);
    g.fillRoundedRectangle (12.0f, 58.0f, (float) getWidth() - 24.0f, 232.0f, 10.0f);
    g.fillRoundedRectangle (12.0f, 298.0f, (float) getWidth() - 24.0f, 132.0f, 10.0f);
    g.fillRoundedRectangle (12.0f, 438.0f, (float) getWidth() - 24.0f, 206.0f, 10.0f);
}

void BeadsEditor::resized()
{
    const int W = getWidth();
    const int colW = (W - 24) / 4;

    auto place = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };

    // Row 1: main knobs with their mod knobs underneath
    Knob* mainRow[4] = { &time, &size, &shape, &pitch };
    Knob* modRow[4]  = { &timeMod, &sizeMod, &shapeMod, &pitchMod };
    for (int i = 0; i < 4; ++i)
    {
        juce::Rectangle<int> col (12 + i * colW, 62, colW, 224);
        place (*mainRow[i], col.removeFromTop (132).reduced (10, 0));
        place (*modRow[i], col.removeFromTop (88).reduced (30, 0));
    }

    // Row 2
    Knob* row2[4] = { &density, &feedback, &dryWet, &reverb };
    for (int i = 0; i < 4; ++i)
        place (*row2[i], juce::Rectangle<int> (12 + i * colW, 302, colW, 124).reduced (10, 0));

    // Row 3: switches and menus
    auto row3 = juce::Rectangle<int> (24, 444, W - 48, 40);
    freeze.setBounds (row3.removeFromLeft (84)); row3.removeFromLeft (8);
    seed.setBounds   (row3.removeFromLeft (70)); row3.removeFromLeft (8);
    mono.setBounds   (row3.removeFromLeft (70)); row3.removeFromLeft (16);
    gateLabel.setBounds (row3.removeFromLeft (70));
    gateMode.setBounds  (row3.removeFromLeft (100).reduced (0, 6)); row3.removeFromLeft (14);
    qualityLabel.setBounds (row3.removeFromLeft (52));
    quality.setBounds   (row3.reduced (0, 6));

    // Row 4: extra options
    auto row4 = juce::Rectangle<int> (24, 490, W - 48, 40);
    snapLabel.setBounds (row4.removeFromLeft (72));
    snapMode.setBounds (row4.removeFromLeft (100).reduced (0, 6)); row4.removeFromLeft (6);
    softEdges.setBounds (row4.removeFromLeft (150)); row4.removeFromLeft (10);
    sourceLabel.setBounds (row4.removeFromLeft (48));
    source.setBounds (row4.removeFromLeft (110).reduced (0, 6)); row4.removeFromLeft (6);
    bankLabel.setBounds (row4.removeFromLeft (34));
    bank.setBounds (row4.removeFromLeft (54).reduced (0, 6));

    // Row 5: wavetable banks (synth mode)
    auto row5 = juce::Rectangle<int> (24, 536, W - 48, 44);
    loadBank.setBounds (row5.removeFromLeft (150).withSizeKeepingCentre (150, 30)); row5.removeFromLeft (8);
    resetBank.setBounds (row5.removeFromLeft (110).withSizeKeepingCentre (110, 30)); row5.removeFromLeft (12);
    bankInfo.setBounds (row5);

    // Row 6: synth-mode playing options
    auto row6 = juce::Rectangle<int> (24, 590, W - 48, 40);
    midiPitch.setBounds (row6.removeFromLeft (130)); row6.removeFromLeft (8);
    drone.setBounds (row6.removeFromLeft (100)); row6.removeFromLeft (16);
    scaleLabel.setBounds (row6.removeFromLeft (82));
    pitchScale.setBounds (row6.removeFromLeft (210).reduced (0, 6));
}
