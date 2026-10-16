#pragma once
#include <JuceHeader.h>

class VocalDynamicsStandaloneEditor final : public juce::Component,
                                             private juce::Timer,
                                             private juce::ChangeListener,
                                             public juce::FileDragAndDropTarget
{
public:
    VocalDynamicsStandaloneEditor();
    ~VocalDynamicsStandaloneEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void chooseFile();
    void loadFile(const juce::File& file);
    void togglePlayback();
    void stopPlayback();
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    juce::AudioTransportSource transport;
    juce::TimeSliceThread readAheadThread { "Vocal Dynamics audio read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    bool isDraggingFile = false;

    juce::Label title, status, timeLabel;
    juce::TextButton importButton { "IMPORT VOCAL" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalDynamicsStandaloneEditor)
};
