/*
  ==============================================================================

   This file is part of the JUCE tutorials.
   Copyright (c) 2020 - Raw Material Software Limited

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   To use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES,
   WHETHER EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR
   PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

 name:             SynthUsingMidiInputTutorial
 version:          1.0.0
 vendor:           JUCE
 website:          http://juce.com
 description:      Synthesiser with midi input.

 dependencies:     juce_audio_basics, juce_audio_devices, juce_audio_formats,
                   juce_audio_processors, juce_audio_utils, juce_core,
                   juce_data_structures, juce_events, juce_graphics,
                   juce_gui_basics, juce_gui_extra
 exporters:        xcode_mac, vs2019, linux_make

 type:             Component
 mainClass:        MainContentComponent

 useLocalCopy:     1

 END_JUCE_PIP_METADATA

*******************************************************************************/


#pragma once

//==============================================================================
struct SineWaveSound   : public juce::SynthesiserSound
{
    SineWaveSound() {}

    bool appliesToNote    (int) override        { return true; }
    bool appliesToChannel (int) override        { return true; }
};

//==============================================================================
struct SineWaveVoice   : public juce::SynthesiserVoice
{
    SineWaveVoice() : decay(0.999) {}

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SineWaveSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        currentAngle = 0.0;
        level = velocity * 0.15;
        tailOff = 0.0;

        auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        auto cyclesPerSample = cyclesPerSecond / getSampleRate();

        angleDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::pi;
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            if (tailOff == 0.0)
                tailOff = 1.0;
        }
        else
        {
            clearCurrentNote();
            angleDelta = 0.0;
        }
    }

    void pitchWheelMoved (int) override      {}
    void controllerMoved (int, int) override {}

    void setDecay(double newDecay)
    {
        decay = newDecay;
    }

    void renderNextBlock (juce::AudioSampleBuffer& outputBuffer, int startSample, int numSamples) override
    {
        if (angleDelta != 0.0)
        {
            if (tailOff > 0.0) // [7]
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float) (std::sin (currentAngle) * level * tailOff);

                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;

                    tailOff *= decay; // [8]

                    if (tailOff <= 0.005)
                    {
                        clearCurrentNote(); // [9]

                        angleDelta = 0.0;
                        break;
                    }
                }
            }
            else
            {
                while (--numSamples >= 0) // [6]
                {
                    auto currentSample = (float) (std::sin (currentAngle) * level);

                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;
                }
            }
        }
    }

private:
    double currentAngle = 0.0, angleDelta = 0.0, level = 0.0, tailOff = 0.0;
    double decay = 0.0;
};

//==============================================================================
class SynthAudioSource   : public juce::AudioSource
{
public:
    SynthAudioSource (juce::MidiKeyboardState& keyState)
        : keyboardState (keyState)
    {
        for (auto i = 0; i < 4; ++i)                // [1]
            synth.addVoice (new SineWaveVoice());

        synth.addSound (new SineWaveSound());       // [2]
    }

    juce::MidiMessageCollector* getMidiCollector()
    {
        return &midiCollector;
    }

    void setUsingSineWaveSound()
    {
        synth.clearSounds();
    }

    void prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate) override
    {
        synth.setCurrentPlaybackSampleRate (sampleRate); // [3]
        midiCollector.reset(sampleRate);
    }

    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        juce::MidiBuffer incomingMidi;
        midiCollector.removeNextBlockOfMessages(incomingMidi,
            bufferToFill.startSample);

        keyboardState.processNextMidiBuffer (incomingMidi, bufferToFill.startSample,
                                             bufferToFill.numSamples, true);       // [4]

        synth.renderNextBlock (*bufferToFill.buffer, incomingMidi,
                               bufferToFill.startSample, bufferToFill.numSamples); // [5]
    }

    void setDecay(double newDecay)
    {
        for (int i = 0; i < synth.getNumVoices(); ++i) {
            if (auto* sineWaveVoice = dynamic_cast<SineWaveVoice*>(synth.getVoice(i))) {
                sineWaveVoice->setDecay(newDecay);
            }
        }
    }

private:
    juce::MidiKeyboardState& keyboardState;
    juce::Synthesiser synth;
    juce::MidiMessageCollector midiCollector;
};

//==============================================================================
class MainContentComponent   : public juce::AudioAppComponent,
                               private juce::Timer,
                               public juce::Slider::Listener
{
public:
    MainContentComponent()
        : synthAudioSource  (keyboardState),
          keyboardComponent (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        addAndMakeVisible(decaySlider);
        decaySlider.setRange(0.999, 0.99999);
        decaySlider.addListener(this);

        addAndMakeVisible(decayLabel);
        decayLabel.setText("Decay", juce::dontSendNotification);
        decayLabel.attachToComponent(&decaySlider, true);

        addAndMakeVisible(midiInputListLabel);
        midiInputListLabel.setText("MIDI Input:", juce::dontSendNotification);
        midiInputListLabel.attachToComponent(&midiInputList, true);

        auto midiInputs = juce::MidiInput::getAvailableDevices();
        addAndMakeVisible(midiInputList);
        midiInputList.setTextWhenNoChoicesAvailable("No MIDI Inputs Enabled");

        juce::StringArray midiInputNames;
        for (auto input : midiInputs) {
            midiInputNames.add(input.name);
        }

        midiInputList.addItemList(midiInputNames, 1);
        midiInputList.onChange = [this] {setMidiInput(midiInputList.getSelectedItemIndex()); };

        for (auto input : midiInputs) {
            if (deviceManager.isMidiInputDeviceEnabled(input.identifier)) {
                setMidiInput(midiInputs.indexOf(input));
                break;
            }
        }

        if (midiInputList.getSelectedId() == 0) {
            setMidiInput(0);
        }

        addAndMakeVisible (keyboardComponent);
        setAudioChannels (0, 2);

        setSize (600, 160);
        startTimer (400);
    }

    ~MainContentComponent() override
    {
        shutdownAudio();
    }

    void resized() override
    {
        midiInputList.setBounds(200, 10, getWidth() - 210, 20);
        decaySlider.setBounds(120, 40, getWidth() - 130, 20);
        keyboardComponent.setBounds (10, 70, getWidth() - 20, getHeight() - 20);
    }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        synthAudioSource.prepareToPlay (samplesPerBlockExpected, sampleRate);
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        synthAudioSource.getNextAudioBlock (bufferToFill);
    }

    void releaseResources() override
    {
        synthAudioSource.releaseResources();
    }

    void sliderValueChanged(juce::Slider* slider) override
    {
        if (slider == &decaySlider) {
            synthAudioSource.setDecay(decaySlider.getValue());
        }
    }

private:
    void timerCallback() override
    {
        keyboardComponent.grabKeyboardFocus();
        stopTimer();
    }

    void setMidiInput(int index)
    {
        auto list = juce::MidiInput::getAvailableDevices();

        deviceManager.removeMidiInputDeviceCallback(list[lastInputIndex].identifier,
            synthAudioSource.getMidiCollector());

        auto newInput = list[index];

        if (! deviceManager.isMidiInputDeviceEnabled(newInput.identifier)) {
            deviceManager.setMidiInputDeviceEnabled(newInput.identifier, true);
        }

        deviceManager.addMidiInputDeviceCallback(newInput.identifier,
            synthAudioSource.getMidiCollector());
        midiInputList.setSelectedId(index + 1, juce::dontSendNotification);

        lastInputIndex = index;
    }

    //==========================================================================
    juce::MidiKeyboardState keyboardState;
    SynthAudioSource synthAudioSource;
    juce::MidiKeyboardComponent keyboardComponent;

    juce::ComboBox midiInputList;
    juce::Label midiInputListLabel;
    int lastInputIndex = 0;

    juce::Slider decaySlider;
    juce::Label decayLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContentComponent)
};
