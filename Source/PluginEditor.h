
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class VocalDynamicsAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      private juce::ChangeListener
{
public:
    explicit VocalDynamicsAudioProcessorEditor(
        VocalDynamicsAudioProcessor& processor);

    ~VocalDynamicsAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    void importAudio();
    void loadAudioFile(const juce::File& file);

    void changeListenerCallback(
        juce::ChangeBroadcaster* source) override;

    VocalDynamicsAudioProcessor& audioProcessor;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 5 };
    juce::AudioThumbnail thumbnail {
        512, formatManager, thumbnailCache
    };

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label durationLabel;

    juce::TextButton importButton { "IMPORT VOCAL" };
    juce::TextButton analyzeButton { "ANALYZE WORDS" };
    juce::TextButton playButton { "PLAY" };

    juce::Slider gainSlider;

    std::unique_ptr<SliderAttachment> gainAttachment;

    juce::File currentAudioFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        VocalDynamicsAudioProcessorEditor)
};
