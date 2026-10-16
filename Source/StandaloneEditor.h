#pragma once
#include <JuceHeader.h>
#include <vector>
#include <functional>

class VocalDynamicsStandaloneEditor final : public juce::Component,
                                             private juce::Timer,
                                             private juce::ChangeListener,
                                             private juce::AudioIODeviceCallback,
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
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    struct WordEvent
    {
        juce::String word;
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        float gainDb = 0.0f;
        float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;
    };

    class EventGainSource final : public juce::AudioSource
    {
    public:
        explicit EventGainSource(juce::AudioTransportSource& source) : transport(source) {}
        void setEvents(const std::vector<WordEvent>* newEvents, const float* globalLow, const float* globalMid, const float* globalHigh) { events = newEvents; lowGlobal = globalLow; midGlobal = globalMid; highGlobal = globalHigh; }
        void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    private:
        juce::AudioTransportSource& transport;
        const std::vector<WordEvent>* events = nullptr;
        double currentSampleRate = 44100.0;
        const float* lowGlobal = nullptr; const float* midGlobal = nullptr; const float* highGlobal = nullptr;
        float lowState[2] = {}, midState[2] = {};
    };

    class TranscriptionWorker final : public juce::Thread
    {
    public:
        TranscriptionWorker(juce::File executable, juce::File model, juce::File audio,
                            juce::File outputSrt, std::function<void(bool, juce::String)> completion);
        void run() override;
    private:
        juce::File executableFile, modelFile, audioFile, srtFile;
        std::function<void(bool, juce::String)> onFinished;
    };

    void chooseFile();
    void openAudioSettings();
    void refreshAudioStatus();
    void chooseWhisperExecutable();
    void chooseWhisperModel();
    void startTranscription();
    void applyTranscriptionResult(bool success, const juce::String& message);
    bool parseSrt(const juce::String& content, std::vector<WordEvent>& result);
    void loadFile(const juce::File& file);
    void togglePlayback();
    void stopPlayback();
    void zoomBy(double factor);
    void seekFromMouse(int x);
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source);
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels, int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void toggleRecording();
    void stopRecording();
    void createWordEventFromSelection();
    void selectEventAt(juce::Point<int> point);
    void updateSelectedGain();
    void updateSelectedEQ();
    void exportProcessedAudio();
    void analyzeTempoAndKey();
    void analyzePitchNotes();
    bool pitchClassAllowed(int midiNote) const;
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
    std::unique_ptr<TranscriptionWorker> transcriptionWorker;
    juce::TimeSliceThread recordingThread { "Vocal Editor recording writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recordingWriter;
    std::unique_ptr<juce::FileChooser> recordingChooser;
    juce::CriticalSection recordingLock;
    juce::File lastRecordingFile;
    double recordingSampleRate = 44100.0;
    bool recordingActive = false;
    juce::File currentFile, whisperExecutable, whisperModel;
    std::vector<WordEvent> wordEvents;
    struct PitchNote { double startSeconds = 0.0; double endSeconds = 0.0; int midiNote = 0; float confidence = 0.0f; };
    std::vector<PitchNote> pitchNotes;
    int selectedRootNote = 0;
    int selectedScale = 0;
    int selectedEvent = -1;
    bool isDraggingFile = false;
    bool isSelectingRange = false;
    bool mouseMovedOnWave = false;
    int selectionStartX = 0;
    int selectionCurrentX = 0;
    double viewStartSeconds = 0.0;
    double viewEndSeconds = 0.0;

    juce::Label title, status, timeLabel, selectedWordLabel, gainValueLabel, zoomLabel, analysisLabel;
    juce::TextButton importButton { "IMPORT VOCAL" };
    juce::TextButton recordButton { "● RECORD" };
    juce::TextButton settingsButton { "AUDIO SETTINGS" };
    juce::TextButton playButton { "PLAY / SPACE" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton exportButton { "EXPORT WAV" };
    juce::TextButton addWordButton { "ADD REGION" };
    juce::TextButton zoomInButton { "+" };
    juce::TextButton zoomOutButton { "−" };
    juce::TextButton whisperExeButton { "SET WHISPER EXE" };
    juce::TextButton whisperModelButton { "SET MODEL" };
    juce::TextButton transcribeButton { "DETECT WORDS" };
    juce::TextButton analysisButton { "ANALYZE BPM + KEY" };
    juce::TextButton pitchAnalysisButton { "ANALYZE NOTES" };
    juce::ComboBox rootNoteBox;
    juce::ComboBox scaleBox;
    juce::Label rootNoteLabel, scaleLabel;
    juce::Slider wordGainSlider;
    juce::Slider lowEqSlider, midEqSlider, highEqSlider;
    juce::Label eqTitle, lowEqLabel, midEqLabel, highEqLabel;
    float globalLowDb = 0.0f, globalMidDb = 0.0f, globalHighDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalDynamicsStandaloneEditor)
};
