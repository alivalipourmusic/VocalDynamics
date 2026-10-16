#pragma once
#include <JuceHeader.h>
#include <vector>

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
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    struct WordEvent
    {
        juce::String word;
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        float gainDb = 0.0f;
    };

    class EventGainSource final : public juce::AudioSource
    {
    public:
        explicit EventGainSource(juce::AudioTransportSource& source) : transport(source) {}
        void setEvents(const std::vector<WordEvent>* newEvents) { events = newEvents; }
        void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    private:
        juce::AudioTransportSource& transport;
        const std::vector<WordEvent>* events = nullptr;
        double currentSampleRate = 44100.0;
    };

    void chooseFile();
    void loadFile(const juce::File& file);
    void togglePlayback();
    void stopPlayback();
    void zoomBy(double factor);
    void seekFromMouse(int x);
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source);
    void createWordEventFromSelection();
    void selectEventAt(juce::Point<int> point);
    void updateSelectedGain();
    void exportProcessedAudio();
    juce::Rectangle<int> waveformBounds() const;
    double timeAtX(int x) const;
    void updateZoomLabel();

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    juce::AudioTransportSource transport;
    EventGainSource eventGainSource { transport };
    juce::TimeSliceThread readAheadThread { "Vocal Dynamics audio read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    std::vector<WordEvent> wordEvents;
    int selectedEvent = -1;
    bool isDraggingFile = false;
    bool isSelectingRange = false;
    bool mouseMovedOnWave = false;
    int selectionStartX = 0;
    int selectionCurrentX = 0;
    double viewStartSeconds = 0.0;
    double viewEndSeconds = 0.0;

    juce::Label title, status, timeLabel, selectedWordLabel, gainValueLabel, zoomLabel;
    juce::TextButton importButton { "IMPORT VOCAL" };
    juce::TextButton playButton { "PLAY / SPACE" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton exportButton { "EXPORT WAV" };
    juce::TextButton addWordButton { "ADD REGION" };
    juce::TextButton zoomInButton { "+" };
    juce::TextButton zoomOutButton { "−" };
    juce::Slider wordGainSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalDynamicsStandaloneEditor)
};
