
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class VocalDynamicsAudioProcessorEditor
    : public juce::AudioProcessorEditor
{
public:
    explicit VocalDynamicsAudioProcessorEditor(
        VocalDynamicsAudioProcessor&);

    ~VocalDynamicsAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    VocalDynamicsAudioProcessor& audioProcessor;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label gainLabel;
    juce::Label thresholdLabel;

    juce::Slider gainSlider;
    juce::Slider thresholdSlider;
    juce::ToggleButton gateButton;

    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment;
    std::unique_ptr<ButtonAttachment> gateAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        VocalDynamicsAudioProcessorEditor)
};
