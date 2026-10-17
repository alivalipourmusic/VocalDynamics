#include <array>
#include <algorithm>
#include <cmath>
#include "StandaloneEditor.h"

namespace
{
juce::String formatTimelineTime(double seconds)
{
    seconds = juce::jmax(0.0, seconds);
    const auto totalMilliseconds = static_cast<juce::int64>(std::llround(seconds * 1000.0));
    const auto minutes = totalMilliseconds / 60000;
    const auto secs = (totalMilliseconds / 1000) % 60;
    const auto millis = totalMilliseconds % 1000;
    return juce::String::formatted("%02lld:%02lld.%03lld",
        static_cast<long long>(minutes),
        static_cast<long long>(secs),
        static_cast<long long>(millis));
}
}

VocalDynamicsStandaloneEditor::TranscriptionWorker::TranscriptionWorker(
    juce::File executable, juce::File model, juce::File audio, juce::File outputSrt,
    std::function<void(bool, juce::String)> completion)
    : juce::Thread("Offline word transcription"),
      executableFile(std::move(executable)), modelFile(std::move(model)),
      audioFile(std::move(audio)), srtFile(std::move(outputSrt)),
      onFinished(std::move(completion))
{
}

void VocalDynamicsStandaloneEditor::TranscriptionWorker::run()
{
    juce::StringArray args;
    args.add(executableFile.getFullPathName());
    args.add("-m"); args.add(modelFile.getFullPathName());
    args.add("-f"); args.add(audioFile.getFullPathName());
    args.add("-osrt");
    args.add("-of"); args.add(srtFile.withFileExtension("").getFullPathName());
    // -ml limits segment length in characters; 1 fragments output and is not word alignment.
    args.add("-sow");
    args.add("-l"); args.add("auto");
    args.add("-np");

    juce::ChildProcess process;
    if (!process.start(args, 0))
    {
        juce::MessageManager::callAsync([callback = onFinished]
        {
            if (callback) callback(false, "Could not launch whisper-cli.exe. Check the executable path.");
        });
        return;
    }

    while (!threadShouldExit())
    {
        if (process.waitForProcessToFinish(150))
            break;
    }

    if (threadShouldExit())
    {
        process.kill();
        return;
    }

    const bool outputExists = srtFile.existsAsFile() && srtFile.getSize() > 0;
    const int exitCode = process.getExitCode();
    const juce::String message = outputExists
        ? "SRT_READY"
        : "Word detection could not create timed words (error " + juce::String(exitCode) + "). Check the speech app and language pack.";

    juce::MessageManager::callAsync([callback = onFinished, outputExists, message]
    {
        if (callback) callback(outputExists, message);
    });
}

void VocalDynamicsStandaloneEditor::EventGainSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    transport.prepareToPlay(samplesPerBlockExpected, currentSampleRate);
}

void VocalDynamicsStandaloneEditor::EventGainSource::releaseResources()
{
    transport.releaseResources();
}

void VocalDynamicsStandaloneEditor::EventGainSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    const double blockStart = transport.getCurrentPosition();
    transport.getNextAudioBlock(info);

    if (events == nullptr || info.buffer == nullptr || currentSampleRate <= 0.0)
        return;

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        const double time = blockStart + static_cast<double>(sample) / currentSampleRate;
        float gain = 1.0f;
        float lowDb = lowGlobal != nullptr ? *lowGlobal : 0.0f;
        float midDb = midGlobal != nullptr ? *midGlobal : 0.0f;
        float highDb = highGlobal != nullptr ? *highGlobal : 0.0f;
        for (const auto& event : *events)
        {
            if (time >= event.startSeconds && time < event.endSeconds)
            {
                gain = juce::Decibels::decibelsToGain(event.gainDb);
                lowDb = event.lowDb; midDb = event.midDb; highDb = event.highDb;
            }
        }
        for (int channel = 0; channel < juce::jmin(2, info.buffer->getNumChannels()); ++channel)
        {
            auto* data = info.buffer->getWritePointer(channel, info.startSample);
            const float input = data[sample];
            lowState[channel] += 0.055f * (input - lowState[channel]);
            midState[channel] += 0.22f * (input - midState[channel]);
            const float low = lowState[channel];
            const float mid = midState[channel] - lowState[channel];
            const float high = input - midState[channel];
            data[sample] = (low * juce::Decibels::decibelsToGain(lowDb)
                          + mid * juce::Decibels::decibelsToGain(midDb)
                          + high * juce::Decibels::decibelsToGain(highDb)) * gain;
        }
    }
}

VocalDynamicsStandaloneEditor::VocalDynamicsStandaloneEditor()
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);
    readAheadThread.startThread();
    recordingThread.startThread();
    eventGainSource.setEvents(&wordEvents, &globalLowDb, &globalMidDb, &globalHighDb);

    const auto white = juce::Colour(0xffedf7ff);
    const auto cyan = juce::Colour(0xff54f2c2);
    const auto muted = juce::Colour(0xff91a3bd);

    title.setText("VOCAL EDITOR", juce::dontSendNotification);
    title.setFont(juce::Font(27.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, white);
    addAndMakeVisible(title);

    status.setText("Load a vocal, edit individual regions, or analyze tempo and key.", juce::dontSendNotification);
    status.setFont(juce::Font(12.0f));
    status.setColour(juce::Label::textColourId, cyan);
    addAndMakeVisible(status);

    timeLabel.setText("00:00.000  /  00:00.000", juce::dontSendNotification);
    timeLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    timeLabel.setColour(juce::Label::textColourId, muted);
    addAndMakeVisible(timeLabel);

    selectedWordLabel.setText("No region selected", juce::dontSendNotification);
    selectedWordLabel.setColour(juce::Label::textColourId, white);
    selectedWordLabel.setFont(juce::Font(15.0f, juce::Font::bold));
    selectedWordLabel.setEditable(true, true, false);
    selectedWordLabel.onTextChange = [this]
    {
        if (selectedEvent >= 0 && selectedEvent < static_cast<int>(wordEvents.size()))
        {
            auto name = selectedWordLabel.getText().trim();
            if (name.isNotEmpty())
                wordEvents[static_cast<size_t>(selectedEvent)].word = name;
            repaint();
        }
    };
    addAndMakeVisible(selectedWordLabel);

    gainValueLabel.setText("0.0 dB", juce::dontSendNotification);
    gainValueLabel.setColour(juce::Label::textColourId, cyan);
    addAndMakeVisible(gainValueLabel);

    zoomLabel.setText("0%", juce::dontSendNotification);
    zoomLabel.setColour(juce::Label::textColourId, muted);
    zoomLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(zoomLabel);

    wordGainSlider.setRange(-24.0, 12.0, 0.1);
    wordGainSlider.setValue(0.0);
    wordGainSlider.setEnabled(false);
    wordGainSlider.setTooltip("Select a word/region first, then adjust its gain.");
    wordGainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    wordGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
    wordGainSlider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                       juce::MathConstants<float>::pi * 2.75f, true);
    wordGainSlider.setColour(juce::Slider::trackColourId, cyan);
    wordGainSlider.setColour(juce::Slider::thumbColourId, white);
    wordGainSlider.onValueChange = [this] { updateSelectedGain(); };
    addAndMakeVisible(wordGainSlider);

    const auto eqCyan = juce::Colour(0xff54f2c2);
    const auto eqWhite = juce::Colour(0xffedf7ff);
    eqTitle.setText("3-BAND EQ", juce::dontSendNotification);
    eqTitle.setColour(juce::Label::textColourId, eqCyan);
    eqTitle.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(eqTitle);
    auto setupEqSlider = [this, eqCyan, eqWhite](juce::Slider& slider, juce::Label& label, const juce::String& name)
    {
        label.setText(name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, eqWhite);
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
        slider.setRange(-12.0, 12.0, 0.1);
        slider.setValue(0.0);
        slider.setColour(juce::Slider::trackColourId, eqCyan);
        slider.setColour(juce::Slider::thumbColourId, eqWhite);
        slider.onValueChange = [this] { updateSelectedEQ(); };
        addAndMakeVisible(slider);
    };
    setupEqSlider(lowEqSlider, lowEqLabel, "LOW");
    setupEqSlider(midEqSlider, midEqLabel, "MID");
    setupEqSlider(highEqSlider, highEqLabel, "HIGH");

    for (auto* button : { &importButton, &recordButton, &settingsButton, &playButton, &stopButton, &exportButton, &addWordButton, &zoomInButton, &zoomOutButton, &whisperExeButton, &whisperModelButton, &transcribeButton })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2940));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff245e5a));
        button->setColour(juce::TextButton::textColourOffId, white);
        button->setColour(juce::TextButton::textColourOnId, white);
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff18263b));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1c625a));
        button->setColour(juce::TextButton::textColourOffId, white);
        button->setColour(juce::TextButton::textColourOnId, white);
        button->setLookAndFeel(nullptr);
        addAndMakeVisible(*button);
    }
    importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b665b));
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff9e3347));
    recordButton.setTooltip("Record from the selected audio-device input and save a WAV take.");
    transcribeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d4ed8));
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253751));
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff174e48));

    importButton.onClick = [this] { chooseFile(); };
    recordButton.onClick = [this] { toggleRecording(); };
    analysisButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d4ed8));
    analysisButton.setColour(juce::TextButton::textColourOffId, white);
    analysisButton.onClick = [this] { analyzeTempoAndKey(); };
    analysisButton.setTooltip("Estimate tempo (BPM) and musical key from the loaded audio.");
    addAndMakeVisible(analysisButton);
    const char* pitchRoots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i) rootNoteBox.addItem(pitchRoots[i], i + 1);
    rootNoteBox.setSelectedId(1, juce::dontSendNotification);
    scaleBox.addItem("Major", 1);
    scaleBox.addItem("Natural Minor", 2);
    scaleBox.addItem("Harmonic Minor", 3);
    scaleBox.addItem("Melodic Minor", 4);
    scaleBox.addItem("Dorian", 5);
    scaleBox.addItem("Phrygian", 6);
    scaleBox.addItem("Mixolydian", 7);
    scaleBox.addItem("Major Pentatonic", 8);
    scaleBox.addItem("Minor Pentatonic", 9);
    scaleBox.setSelectedId(2, juce::dontSendNotification);
    selectedScale = 1; // Natural minor is the visible default.
    rootNoteBox.onChange = [this] { selectedRootNote = rootNoteBox.getSelectedId() - 1; repaint(); };
    scaleBox.onChange = [this] { selectedScale = scaleBox.getSelectedId() - 1; repaint(); };
    for (auto* combo : { &rootNoteBox, &scaleBox })
    {
        combo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff182338));
        combo->setColour(juce::ComboBox::textColourId, white);
        combo->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff26374f));
        addAndMakeVisible(*combo);
    }
    pitchAnalysisButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d4ed8));
    pitchAnalysisButton.setColour(juce::TextButton::textColourOffId, white);
    pitchAnalysisButton.setTooltip("Analyze monophonic pitch with autocorrelation and classify notes against the selected scale.");
    pitchAnalysisButton.onClick = [this] { analyzePitchNotes(); };
    addAndMakeVisible(pitchAnalysisButton);
    analysisLabel.setText("BPM: --   |   KEY: --", juce::dontSendNotification);
    analysisLabel.setColour(juce::Label::textColourId, cyan);
    analysisLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    analysisLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(analysisLabel);
    settingsButton.onClick = [this] { openAudioSettings(); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { stopPlayback(); };
    exportButton.onClick = [this] { exportProcessedAudio(); };
    // Manual placeholder-region creation is intentionally hidden. Word events
    // must be created by the transcription engine, not by drawing arbitrary ranges.
    addWordButton.setVisible(false);
    zoomInButton.setButtonText("+");
    zoomOutButton.setButtonText("-"); // ASCII hyphen avoids unsupported-glyph boxes on Windows.
    zoomInButton.setTooltip("Zoom in");
    zoomOutButton.setTooltip("Zoom out");
    zoomInButton.onClick = [this] { zoomBy(0.80); };
    zoomOutButton.onClick = [this] { zoomBy(1.25); };
    whisperExeButton.setButtonText("CHOOSE SPEECH APP");
    whisperModelButton.setButtonText("CHOOSE LANGUAGE PACK");
    transcribeButton.setButtonText("DETECT WORDS");
    whisperExeButton.setTooltip("Choose whisper-cli.exe, the offline speech-recognition program.");
    whisperModelButton.setTooltip("Choose a multilingual Whisper model file (.bin).");
    transcribeButton.setTooltip("Create timed word segments from the loaded vocal.");
    whisperExeButton.onClick = [this] { chooseWhisperExecutable(); };
    whisperModelButton.onClick = [this] { chooseWhisperModel(); };
    transcribeButton.onClick = [this] { startTranscription(); };
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);

    const auto setupResult = deviceManager.initialiseWithDefaultDevices(2, 2);
    if (setupResult.isNotEmpty())
        status.setText("Audio device error: " + setupResult, juce::dontSendNotification);

    audioSourcePlayer.setSource(&eventGainSource);
    deviceManager.addAudioCallback(&audioSourcePlayer);
    deviceManager.addAudioCallback(this);
    refreshAudioStatus();
    startTimerHz(20);
    setSize(1440, 900);
}

VocalDynamicsStandaloneEditor::~VocalDynamicsStandaloneEditor()
{
    if (transcriptionWorker != nullptr)
        transcriptionWorker->stopThread(3000);
    stopTimer();
    thumbnail.removeChangeListener(this);
    stopRecording();
    deviceManager.removeAudioCallback(this);
    deviceManager.removeAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(nullptr);
    transport.stop();
    transport.setSource(nullptr);
    readerSource.reset();
    readAheadThread.stopThread(2000);
    recordingThread.stopThread(2000);
}

bool VocalDynamicsStandaloneEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (files.isEmpty())
        return false;

    // Accept only real audio files supported by the registered JUCE formats.
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.existsAsFile() && formatManager.findFormatForFileExtension(file.getFileExtension()) != nullptr)
            return true;
    }
    return false;
}

void VocalDynamicsStandaloneEditor::fileDragEnter(const juce::StringArray&, int, int)
{
    isDraggingFile = true;
    repaint();
}

void VocalDynamicsStandaloneEditor::fileDragExit(const juce::StringArray&)
{
    isDraggingFile = false;
    repaint();
}

void VocalDynamicsStandaloneEditor::filesDropped(const juce::StringArray& files, int, int)
{
    isDraggingFile = false;
    if (!files.isEmpty())
    {
        const juce::File file(files[0]);
        if (file.existsAsFile())
            loadFile(file);
    }
    repaint();
}

void VocalDynamicsStandaloneEditor::openAudioSettings()
{
    // JUCE's device selector exposes available audio interfaces, output channels,
    // and supported sample rates. Changes are applied to this AudioDeviceManager.
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager, 0, 0, 1, 2, true, true, true, false);
    selector->setSize(620, 460);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Vocal Editor - Audio Settings";
    options.dialogBackgroundColour = juce::Colour(0xff111f2d);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();

    juce::Timer::callAfterDelay(400, [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this)]
    {
        if (safeThis != nullptr) safeThis->refreshAudioStatus();
    });
}

void VocalDynamicsStandaloneEditor::refreshAudioStatus()
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        status.setText("Audio: " + device->getName() + " | "
                       + juce::String(device->getCurrentSampleRate(), 0) + " Hz | "
                       + juce::String(device->getCurrentBufferSizeSamples()) + " samples",
                       juce::dontSendNotification);
    }
    else
    {
        status.setText("No audio output device selected. Open AUDIO SETTINGS and choose your interface.",
                       juce::dontSendNotification);
    }
}

void VocalDynamicsStandaloneEditor::chooseFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose a vocal audio file", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr)
            {
                const auto selected = chooser.getResult();
                if (selected != juce::File{})
                    safeThis->loadFile(selected);
            }
        });
}

void VocalDynamicsStandaloneEditor::chooseWhisperExecutable()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose the offline speech-recognition app (whisper-cli.exe)", juce::File{}, "*.exe");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr)
            {
                const auto f = chooser.getResult();
                if (f.existsAsFile())
                {
                    safeThis->whisperExecutable = f;
                    safeThis->status.setText("Speech app selected: " + f.getFileName(), juce::dontSendNotification);
                }
            }
        });
}

void VocalDynamicsStandaloneEditor::chooseWhisperModel()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose a multilingual speech model (.bin)", juce::File{}, "*.bin");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr)
            {
                const auto f = chooser.getResult();
                if (f.existsAsFile())
                {
                    safeThis->whisperModel = f;
                    safeThis->status.setText("Language pack selected: " + f.getFileName(), juce::dontSendNotification);
                }
            }
        });
}

void VocalDynamicsStandaloneEditor::startTranscription()
{
    if (!currentFile.existsAsFile())
    {
        status.setText("Import a vocal file before transcription.", juce::dontSendNotification);
        return;
    }
    if (!whisperExecutable.existsAsFile() || !whisperModel.existsAsFile())
    {
        status.setText("Choose the speech-recognition app and language pack first.", juce::dontSendNotification);
        return;
    }
    if (transcriptionWorker != nullptr && transcriptionWorker->isThreadRunning())
    {
        status.setText("Word detection is already running.", juce::dontSendNotification);
        return;
    }

    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto stem = "VocalDynamics_words_" + juce::String(juce::Time::getMillisecondCounter());
    const auto srt = temp.getChildFile(stem + ".srt");
    srt.deleteFile();

    status.setText("Detecting words offline. Longer recordings may take a few minutes...", juce::dontSendNotification);
    transcribeButton.setEnabled(false);

    transcriptionWorker = std::make_unique<TranscriptionWorker>(
        whisperExecutable, whisperModel, currentFile, srt,
        [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this), srt]
        (bool success, juce::String message)
        {
            if (safeThis == nullptr) return;
            safeThis->transcribeButton.setEnabled(true);
            if (!success)
            {
                safeThis->status.setText(message, juce::dontSendNotification);
                return;
            }
            const auto content = srt.loadFileAsString();
            safeThis->applyTranscriptionResult(content.isNotEmpty(), content.isNotEmpty()
                ? content : "Transcription output file was empty.");
        });
    transcriptionWorker->startThread();
}

static double parseWhisperTimestamp(juce::String value)
{
    value = value.trim().replaceCharacter(',', '.');
    juce::StringArray hms;
    hms.addTokens(value, ":", "");
    if (hms.size() != 3) return -1.0;
    juce::StringArray secParts;
    secParts.addTokens(hms[2], ".", "");
    if (secParts.isEmpty()) return -1.0;
    const double hours = hms[0].getDoubleValue();
    const double minutes = hms[1].getDoubleValue();
    const double seconds = secParts[0].getDoubleValue();
    double fraction = 0.0;
    if (secParts.size() > 1)
    {
        auto digits = secParts[1];
        while (digits.length() < 3) digits += "0";
        fraction = digits.substring(0, 3).getDoubleValue() / 1000.0;
    }
    return hours * 3600.0 + minutes * 60.0 + seconds + fraction;
}

bool VocalDynamicsStandaloneEditor::parseSrt(const juce::String& content, std::vector<WordEvent>& result)
{
    juce::StringArray lines;
    lines.addLines(content);
    for (int i = 0; i < lines.size(); ++i)
    {
        auto line = lines[i].trim();
        if (!line.contains("-->")) continue;
        const int arrow = line.indexOf("-->");
        const double start = parseWhisperTimestamp(line.substring(0, arrow));
        const double end = parseWhisperTimestamp(line.substring(arrow + 3));
        if (start < 0.0 || end <= start) continue;

        juce::String text;
        for (int j = i + 1; j < lines.size() && lines[j].trim().isNotEmpty(); ++j)
        {
            if (text.isNotEmpty()) text += " ";
            text += lines[j].trim();
        }
        text = text.trim();
        if (text.isEmpty()) continue;

        WordEvent event;
        event.word = text;
        event.startSeconds = start;
        event.endSeconds = end;
        event.gainDb = 0.0f;
        result.push_back(event);
    }
    return !result.empty();
}

void VocalDynamicsStandaloneEditor::applyTranscriptionResult(bool success, const juce::String& message)
{
    if (!success)
    {
        status.setText(message, juce::dontSendNotification);
        return;
    }
    std::vector<WordEvent> detected;
    if (!parseSrt(message, detected))
    {
        status.setText("No timed words were found. Check Whisper model and input format.", juce::dontSendNotification);
        return;
    }

    wordEvents = std::move(detected);
    selectedEvent = wordEvents.empty() ? -1 : 0;
    wordGainSlider.setEnabled(selectedEvent >= 0);
    if (selectedEvent >= 0)
    {
        selectedWordLabel.setText(wordEvents[0].word, juce::dontSendNotification);
        wordGainSlider.setValue(wordEvents[0].gainDb, juce::dontSendNotification);
    }
    status.setText("Detected " + juce::String(static_cast<int>(wordEvents.size()))
                   + " timed word segments. Review boundaries and correct any mistakes.",
                   juce::dontSendNotification);
    repaint();
}

void VocalDynamicsStandaloneEditor::loadFile(const juce::File& file)
{
    pitchNotes.clear();
    if (!file.existsAsFile())
        return;

    status.setText("Opening audio file...", juce::dontSendNotification);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
    {
        status.setText("Unsupported or unreadable audio file: " + file.getFileName(), juce::dontSendNotification);
        return;
    }
    if (reader->numChannels == 0 || reader->numChannels > 64 || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        status.setText("Invalid audio file information.", juce::dontSendNotification);
        return;
    }

    const double sampleRate = reader->sampleRate;
    transport.stop();
    transport.setSource(nullptr);
    readerSource.reset();

    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    transport.setSource(newSource.get(), 32768, &readAheadThread, sampleRate, 2);
    readerSource = std::move(newSource);

    currentFile = file;
    viewStartSeconds = 0.0;
    viewEndSeconds = readerSource->getTotalLength();
    updateZoomLabel();
    wordEvents.clear();
    selectedEvent = -1;
    selectedWordLabel.setText("No region selected", juce::dontSendNotification);
    wordGainSlider.setValue(0.0, juce::dontSendNotification);
    wordGainSlider.setEnabled(false);
    thumbnail.clear();
    // Use an explicit change notification after setting the source so the first
    // waveform is drawn immediately rather than only after a later zoom repaint.
    const bool waveformStarted = thumbnail.setSource(new juce::FileInputSource(file));
    playButton.setButtonText("PLAY");
    if (waveformStarted)
    {
        thumbnail.sendChangeMessage();
        status.setText("Loaded: " + file.getFileName(), juce::dontSendNotification);
    }
    else
    {
        status.setText("Audio loaded; waveform preview is still loading or unavailable.", juce::dontSendNotification);
    }
    refreshAudioStatus();
    repaint();
}

void VocalDynamicsStandaloneEditor::togglePlayback()
{
    if (readerSource == nullptr)
    {
        status.setText("Import a vocal file first.", juce::dontSendNotification);
        return;
    }
    if (transport.isPlaying())
    {
        transport.stop();
        playButton.setButtonText("PLAY");
    }
    else
    {
        if (transport.getCurrentPosition() >= transport.getLengthInSeconds())
            transport.setPosition(0.0);
        transport.start();
        playButton.setButtonText("PAUSE");
    }
}

void VocalDynamicsStandaloneEditor::stopPlayback()
{
    transport.stop();
    transport.setPosition(0.0);
    playButton.setButtonText("PLAY");
}

juce::Rectangle<int> VocalDynamicsStandaloneEditor::waveformBounds() const
{
    return { 24, 142, juce::jmax(520, getWidth() - 380), 286 };
}

double VocalDynamicsStandaloneEditor::timeAtX(int x) const
{
    // This is the exact horizontal area used by drawChannels(). Keep seeking,
    // playhead drawing, and mouse scrubbing on one shared time-to-pixel mapping.
    const auto area = waveformBounds().reduced(12, 28);
    const double duration = transport.getLengthInSeconds();
    if (duration <= 0.0 || area.getWidth() <= 0)
        return 0.0;
    const double start = juce::jlimit(0.0, duration, viewStartSeconds);
    const double end = viewEndSeconds > start ? juce::jlimit(start, duration, viewEndSeconds) : duration;
    const double ratio = juce::jlimit(0.0, 1.0,
        static_cast<double>(x - area.getX()) / static_cast<double>(area.getWidth()));
    return juce::jlimit(start, end, start + ratio * (end - start));
}

void VocalDynamicsStandaloneEditor::seekFromMouse(int x)
{
    if (transport.getLengthInSeconds() <= 0.0 || readerSource == nullptr)
        return;
    const double target = timeAtX(x);
    transport.setPosition(target);
    timeLabel.setText(formatTimelineTime(target) + "  /  "
                      + formatTimelineTime(transport.getLengthInSeconds()),
                      juce::dontSendNotification);
    repaint();
    if (!transport.isPlaying())
        playButton.setButtonText("PLAY");
}

void VocalDynamicsStandaloneEditor::zoomBy(double factor)
{
    const double total = transport.getLengthInSeconds();
    if (total <= 0.0 || factor <= 0.0)
        return;
    const double oldStart = juce::jlimit(0.0, total, viewStartSeconds);
    const double oldEnd = viewEndSeconds > oldStart ? juce::jlimit(oldStart, total, viewEndSeconds) : total;
    const double oldSpan = juce::jmax(0.001, oldEnd - oldStart);
    const double minSpan = juce::jmax(0.10, total / 100.0);
    const double newSpan = juce::jlimit(minSpan, total, oldSpan * factor);
    // Keep the playhead at the same horizontal position while magnification changes.
    const double playhead = juce::jlimit(oldStart, oldEnd, transport.getCurrentPosition());
    const double ratio = oldSpan > 0.0 ? (playhead - oldStart) / oldSpan : 0.5;
    double newStart = playhead - ratio * newSpan;
    newStart = juce::jlimit(0.0, juce::jmax(0.0, total - newSpan), newStart);
    viewStartSeconds = newStart;
    viewEndSeconds = juce::jmin(total, newStart + newSpan);
    updateZoomLabel();
    repaint();
}

void VocalDynamicsStandaloneEditor::updateZoomLabel()
{
    const double total = transport.getLengthInSeconds();
    const double span = viewEndSeconds > viewStartSeconds ? viewEndSeconds - viewStartSeconds : total;
    const int percent = total > 0.0
        ? juce::jlimit(0, 100, static_cast<int>(std::round((1.0 - span / total) * 100.0))) : 0;
    zoomLabel.setText(juce::String(percent) + "%", juce::dontSendNotification);
}

bool VocalDynamicsStandaloneEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        togglePlayback();
        return true;
    }
    if (key.getTextCharacter() == '+' || key.getKeyCode() == juce::KeyPress::numberPadAdd)
    {
        zoomBy(0.85);
        return true;
    }
    if (key.getTextCharacter() == '-' || key.getTextCharacter() == '_' ||
        key.getKeyCode() == juce::KeyPress::numberPadSubtract)
    {
        zoomBy(1.1764705882);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        stopPlayback();
        return true;
    }
    return false;
}

void VocalDynamicsStandaloneEditor::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto area = waveformBounds();
    if (thumbnail.getTotalLength() > 0.0 && area.reduced(12, 28).contains(event.getPosition()))
    {
        // Follow the clicked position immediately. A drag scrubs the playhead;
        // it must not create a fake word region.
        seekFromMouse(event.x);
        isSelectingRange = true;
        mouseMovedOnWave = false;
        const auto drawable = area.reduced(12, 28);
        selectionStartX = juce::jlimit(drawable.getX(), drawable.getRight(), event.x);
        selectionCurrentX = selectionStartX;
        return;
    }

    if (event.y >= 446 && event.y <= 558 && event.x >= waveformBounds().getX() && event.x <= waveformBounds().getRight())
        selectEventAt(event.getPosition());
}

void VocalDynamicsStandaloneEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (isSelectingRange)
    {
        if (std::abs(event.x - selectionStartX) > 3)
            mouseMovedOnWave = true;
        const auto drawable = waveformBounds().reduced(12, 28);
        selectionCurrentX = juce::jlimit(drawable.getX(), drawable.getRight(), event.x);
        repaint();
    }
}

void VocalDynamicsStandaloneEditor::mouseUp(const juce::MouseEvent& event)
{
    if (!isSelectingRange)
        return;
    const auto drawable = waveformBounds().reduced(12, 14);
    selectionCurrentX = juce::jlimit(drawable.getX(), drawable.getRight(), event.x);
    isSelectingRange = false;
    // Word events must come from transcription, not arbitrary mouse-drawn regions.
    if (mouseMovedOnWave)
        seekFromMouse(selectionCurrentX);
    else
        seekFromMouse(event.x);
    repaint();
}

void VocalDynamicsStandaloneEditor::createWordEventFromSelection()
{
    if (thumbnail.getTotalLength() <= 0.0)
    {
        status.setText("Import audio before creating a word region.", juce::dontSendNotification);
        return;
    }

    const int left = juce::jmin(selectionStartX, selectionCurrentX);
    const int right = juce::jmax(selectionStartX, selectionCurrentX);
    const double start = timeAtX(left);
    const double end = timeAtX(right);
    if (end - start < 0.04)
    {
        status.setText("Select a longer time range.", juce::dontSendNotification);
        return;
    }

    WordEvent event;
    event.word = "Word " + juce::String(wordEvents.size() + 1);
    event.startSeconds = start;
    event.endSeconds = end;
    event.gainDb = 0.0f;
    wordEvents.push_back(event);
    selectedEvent = static_cast<int>(wordEvents.size()) - 1;
    selectedWordLabel.setText(event.word, juce::dontSendNotification);
    wordGainSlider.setEnabled(true);
    wordGainSlider.setValue(0.0, juce::dontSendNotification);
    status.setText("Region created. Rename it by clicking its name; adjust its gain below.", juce::dontSendNotification);
    repaint();
}

void VocalDynamicsStandaloneEditor::selectEventAt(juce::Point<int> point)
{
    if (wordEvents.empty() || thumbnail.getTotalLength() <= 0.0)
        return;
    const double duration = thumbnail.getTotalLength();
    const double visibleStart = viewStartSeconds;
    const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : duration;
    const double visibleSpan = juce::jmax(0.001, visibleEnd - visibleStart);
    const int laneX = waveformBounds().getX() + 12;
    const int laneWidth = waveformBounds().getWidth() - 24;
    for (int i = static_cast<int>(wordEvents.size()) - 1; i >= 0; --i)
    {
        const auto& e = wordEvents[static_cast<size_t>(i)];
        const int x1 = laneX + static_cast<int>((e.startSeconds - visibleStart) / visibleSpan * laneWidth);
        const int x2 = laneX + static_cast<int>((e.endSeconds - visibleStart) / visibleSpan * laneWidth);
        if (point.x >= x1 && point.x <= x2)
        {
            selectedEvent = i;
            wordGainSlider.setEnabled(true);
            selectedWordLabel.setText(e.word, juce::dontSendNotification);
            wordGainSlider.setValue(e.gainDb, juce::dontSendNotification);
            lowEqSlider.setValue(e.lowDb, juce::dontSendNotification);
            midEqSlider.setValue(e.midDb, juce::dontSendNotification);
            highEqSlider.setValue(e.highDb, juce::dontSendNotification);
            gainValueLabel.setText(juce::String(e.gainDb, 1) + " dB", juce::dontSendNotification);
            // Selecting a detected word also places the playhead at its exact start time.
            if (readerSource != nullptr)
                transport.setPosition(juce::jlimit(0.0, transport.getLengthInSeconds(), e.startSeconds));
            repaint();
            return;
        }
    }
}

void VocalDynamicsStandaloneEditor::updateSelectedEQ()
{
    const float low = static_cast<float>(lowEqSlider.getValue());
    const float mid = static_cast<float>(midEqSlider.getValue());
    const float high = static_cast<float>(highEqSlider.getValue());
    if (selectedEvent >= 0 && selectedEvent < static_cast<int>(wordEvents.size()))
    {
        auto& event = wordEvents[static_cast<size_t>(selectedEvent)];
        event.lowDb = low; event.midDb = mid; event.highDb = high;
        status.setText("EQ set for selected region.", juce::dontSendNotification);
    }
    else
    {
        globalLowDb = low; globalMidDb = mid; globalHighDb = high;
        status.setText("Global vocal EQ updated. Select a region for separate EQ.", juce::dontSendNotification);
    }
    repaint();
}

void VocalDynamicsStandaloneEditor::updateSelectedGain()
{
    const auto value = static_cast<float>(wordGainSlider.getValue());
    gainValueLabel.setText(juce::String(value, 1) + " dB", juce::dontSendNotification);
    if (selectedEvent >= 0 && selectedEvent < static_cast<int>(wordEvents.size()))
    {
        wordEvents[static_cast<size_t>(selectedEvent)].gainDb = value;
        status.setText("Gain for \"" + wordEvents[static_cast<size_t>(selectedEvent)].word
                       + "\" set to " + juce::String(value, 1) + " dB.", juce::dontSendNotification);
        repaint();
    }
    else
    {
        status.setText("Select a word/region in the event lane before changing its gain.", juce::dontSendNotification);
    }
}

void VocalDynamicsStandaloneEditor::analyzeTempoAndKey()
{
    if (!currentFile.existsAsFile())
    {
        status.setText("Load an audio file before running music analysis.", juce::dontSendNotification);
        return;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(currentFile));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        status.setText("Could not read this audio file for analysis.", juce::dontSendNotification);
        return;
    }

    // Analyze up to 45 seconds, mono, then downsample for fast offline estimation.
    const auto maxSamples = static_cast<juce::int64>(reader->sampleRate * 45.0);
    const int sourceCount = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, maxSamples));
    juce::AudioBuffer<float> source(1, sourceCount);
    if (!reader->read(&source, 0, sourceCount, 0, true, false))
    {
        status.setText("Could not read audio samples for analysis.", juce::dontSendNotification);
        return;
    }

    const int stride = juce::jmax(1, static_cast<int>(std::floor(reader->sampleRate / 11025.0)));
    const double analysisRate = reader->sampleRate / static_cast<double>(stride);
    std::vector<float> mono;
    mono.reserve(static_cast<size_t>(sourceCount / stride + 1));
    const auto* input = source.getReadPointer(0);
    for (int i = 0; i < sourceCount; i += stride)
        mono.push_back(input[i]);

    if (mono.size() < 8192)
    {
        status.setText("Audio is too short for reliable BPM and key estimation.", juce::dontSendNotification);
        return;
    }

    // Tempo: frame RMS envelope followed by autocorrelation in the 60–180 BPM range.
    constexpr int envelopeHop = 256;
    std::vector<float> envelope;
    for (size_t start = 0; start + envelopeHop <= mono.size(); start += envelopeHop)
    {
        double energy = 0.0;
        for (int i = 0; i < envelopeHop; ++i)
        {
            const double v = mono[start + static_cast<size_t>(i)];
            energy += v * v;
        }
        envelope.push_back(static_cast<float>(std::sqrt(energy / envelopeHop)));
    }
    std::vector<float> onset(envelope.size(), 0.0f);
    for (size_t i = 1; i < envelope.size(); ++i)
        onset[i] = juce::jmax(0.0f, envelope[i] - envelope[i - 1]);

    double bestCorrelation = -1.0;
    int bestLag = 0;
    const int minLag = juce::jmax(1, static_cast<int>(analysisRate * 60.0 / (180.0 * envelopeHop)));
    const int maxLag = juce::jmax(minLag + 1, static_cast<int>(analysisRate * 60.0 / (60.0 * envelopeHop)));
    for (int lag = minLag; lag <= maxLag && lag < static_cast<int>(onset.size() / 2); ++lag)
    {
        double corr = 0.0;
        for (size_t i = static_cast<size_t>(lag); i < onset.size(); ++i)
            corr += static_cast<double>(onset[i]) * onset[i - static_cast<size_t>(lag)];
        if (corr > bestCorrelation)
        {
            bestCorrelation = corr;
            bestLag = lag;
        }
    }
    double bpm = bestLag > 0 ? 60.0 * analysisRate / (bestLag * envelopeHop) : 0.0;
    if (bpm > 0.0)
    {
        while (bpm < 70.0) bpm *= 2.0;
        while (bpm > 160.0) bpm *= 0.5;
    }

    // Key estimate: aggregate spectral energy into pitch classes and compare
    // against standard major/minor tonal profiles. Treat the result as a guide.
    constexpr int fftOrder = 13;
    constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::array<double, 12> chroma {};
    std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);
    int frameCount = 0;
    const size_t hop = fftSize / 2;
    for (size_t start = 0; start + fftSize <= mono.size(); start += hop)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftData[static_cast<size_t>(i)] = mono[start + static_cast<size_t>(i)];
        window.multiplyWithWindowingTable(fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        for (int bin = 2; bin < fftSize / 2; ++bin)
        {
            const double frequency = static_cast<double>(bin) * analysisRate / fftSize;
            if (frequency < 55.0 || frequency > 1500.0) continue;
            const int midi = static_cast<int>(std::round(69.0 + 12.0 * std::log2(frequency / 440.0)));
            const int pitchClass = (midi % 12 + 12) % 12;
            chroma[static_cast<size_t>(pitchClass)] += fftData[static_cast<size_t>(bin)];
        }
        ++frameCount;
    }

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double majorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    const double minorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double mean = 0.0;
    for (double value : chroma) mean += value;
    mean /= 12.0;
    double bestKeyScore = -1.0e30;
    int bestRoot = 0;
    bool bestMinor = false;
    for (int root = 0; root < 12; ++root)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            double score = 0.0;
            for (int i = 0; i < 12; ++i)
            {
                const double profile = mode == 0 ? majorProfile[i] : minorProfile[i];
                score += (chroma[static_cast<size_t>((root + i) % 12)] - mean) * profile;
            }
            if (score > bestKeyScore)
            {
                bestKeyScore = score;
                bestRoot = root;
                bestMinor = mode == 1;
            }
        }
    }

    const auto bpmText = bpm > 0.0 ? juce::String(static_cast<int>(std::round(bpm))) : juce::String("--");
    const auto keyText = frameCount > 0 ? juce::String(noteNames[bestRoot]) + (bestMinor ? " minor" : " major")
                                         : juce::String("unclear");
    analysisLabel.setText("BPM: " + bpmText + "   |   KEY: " + keyText + " (est.)", juce::dontSendNotification);
    status.setText("Analysis complete. BPM and key are estimates; confirm by ear before using them.", juce::dontSendNotification);
    repaint();
}

bool VocalDynamicsStandaloneEditor::pitchClassAllowed(int midiNote) const
{
    static const int intervals[9][7] = {
        {0,2,4,5,7,9,11}, {0,2,3,5,7,8,10}, {0,2,3,5,7,8,11},
        {0,2,3,5,7,9,11}, {0,2,3,5,7,9,10}, {0,1,3,5,7,8,10},
        {0,2,4,5,7,9,10}, {0,2,4,7,9,-1,-1}, {0,3,5,7,10,-1,-1}
    };
    const int pc = (midiNote % 12 + 12) % 12;
    const int relative = (pc - selectedRootNote + 12) % 12;
    for (int i = 0; i < 7; ++i) if (intervals[juce::jlimit(0, 8, selectedScale)][i] == relative) return true;
    return false;
}

void VocalDynamicsStandaloneEditor::analyzePitchNotes()
{
    if (!currentFile.existsAsFile()) { status.setText("Load an audio file before analyzing pitch notes.", juce::dontSendNotification); return; }
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(currentFile));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) { status.setText("Could not read the audio file for pitch analysis.", juce::dontSendNotification); return; }
    const auto limit = static_cast<juce::int64>(reader->sampleRate * 30.0);
    const int count = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, limit));
    juce::AudioBuffer<float> source(1, count);
    if (!reader->read(&source, 0, count, 0, true, false)) { status.setText("Could not read audio samples for pitch analysis.", juce::dontSendNotification); return; }
    const int stride = juce::jmax(1, static_cast<int>(std::ceil(reader->sampleRate / 11025.0)));
    const double rate = reader->sampleRate / static_cast<double>(stride);
    std::vector<float> mono; mono.reserve(static_cast<size_t>(count / stride + 1));
    const auto* in = source.getReadPointer(0);
    for (int i = 0; i < count; i += stride) mono.push_back(in[i]);
    constexpr int frame = 1024, hop = 256;
    const int minLag = static_cast<int>(rate / 1000.0);
    const int maxLag = juce::jmin(frame / 2, static_cast<int>(rate / 75.0));
    if (mono.size() < frame || minLag < 2 || maxLag <= minLag) { status.setText("Audio is too short for pitch analysis.", juce::dontSendNotification); return; }
    std::vector<PitchNote> found;
    for (size_t start = 0; start + frame < mono.size(); start += hop)
    {
        double energy = 0.0;
        for (int i = 0; i < frame; ++i) { const double v = mono[start + static_cast<size_t>(i)]; energy += v * v; }
        if (std::sqrt(energy / frame) < 0.008) continue;
        double best = 0.0; int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double cross = 0.0, e1 = 0.0, e2 = 0.0;
            for (int i = 0; i < frame - lag; ++i)
            {
                const double a = mono[start + static_cast<size_t>(i)], b = mono[start + static_cast<size_t>(i + lag)];
                cross += a * b; e1 += a * a; e2 += b * b;
            }
            const double corr = cross / std::sqrt(juce::jmax(1.0e-15, e1 * e2));
            if (corr > best) { best = corr; bestLag = lag; }
        }
        if (bestLag == 0 || best < 0.72) continue;
        const double hz = rate / bestLag;
        if (hz < 75.0 || hz > 1000.0) continue;
        const int midi = static_cast<int>(std::round(69.0 + 12.0 * std::log2(hz / 440.0)));
        const double t0 = static_cast<double>(start) / rate, t1 = static_cast<double>(start + frame) / rate;
        if (!found.empty() && found.back().midiNote == midi && t0 - found.back().endSeconds <= static_cast<double>(hop * 2) / rate)
        { found.back().endSeconds = t1; found.back().confidence = juce::jmax(found.back().confidence, static_cast<float>(best)); }
        else found.push_back({t0, t1, midi, static_cast<float>(best)});
    }
    pitchNotes = std::move(found);
    status.setText("Pitch analysis finished: " + juce::String(static_cast<int>(pitchNotes.size())) + " note regions. Monophonic estimate only; verify by ear.", juce::dontSendNotification);
    repaint();
}

void VocalDynamicsStandaloneEditor::exportProcessedAudio()
{
    if (!currentFile.existsAsFile())
    {
        status.setText("Import a vocal file before exporting.", juce::dontSendNotification);
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>(
        "Export processed vocal", currentFile.getSiblingFile(currentFile.getFileNameWithoutExtension() + "_processed.wav"),
        "*.wav");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            auto destination = chooser.getResult();
            if (destination == juce::File{})
                return;
            if (destination.getFileExtension().isEmpty())
                destination = destination.withFileExtension(".wav");

            std::unique_ptr<juce::AudioFormatReader> reader(safeThis->formatManager.createReaderFor(safeThis->currentFile));
            if (reader == nullptr)
            {
                safeThis->status.setText("Could not reopen the source file for export.", juce::dontSendNotification);
                return;
            }

            juce::WavAudioFormat wav;
            auto output = destination.createOutputStream();
            if (output == nullptr)
            {
                safeThis->status.setText("Could not create the output file.", juce::dontSendNotification);
                return;
            }
            output->setPosition(0);
            output->truncate();
            auto writer = std::unique_ptr<juce::AudioFormatWriter>(
                wav.createWriterFor(output.release(), reader->sampleRate, reader->numChannels, 24, {}, 0));
            if (writer == nullptr)
            {
                safeThis->status.setText("Could not create WAV writer.", juce::dontSendNotification);
                return;
            }

            constexpr int blockSize = 8192;
            juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), blockSize);
            juce::int64 position = 0;
            bool okay = true;
            while (position < reader->lengthInSamples)
            {
                const int count = static_cast<int>(juce::jmin<juce::int64>(blockSize, reader->lengthInSamples - position));
                buffer.clear();
                if (!reader->read(&buffer, 0, count, position, true, true))
                {
                    okay = false;
                    break;
                }

                static float lowState[64] = {}, midState[64] = {};
                for (int sample = 0; sample < count; ++sample)
                {
                    const double time = static_cast<double>(position + sample) / reader->sampleRate;
                    float gain = 1.0f;
                    float lowDb = safeThis->globalLowDb, midDb = safeThis->globalMidDb, highDb = safeThis->globalHighDb;
                    for (const auto& e : safeThis->wordEvents)
                        if (time >= e.startSeconds && time < e.endSeconds)
                        {
                            gain = juce::Decibels::decibelsToGain(e.gainDb);
                            lowDb = e.lowDb; midDb = e.midDb; highDb = e.highDb;
                        }
                    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    {
                        auto* data = buffer.getWritePointer(channel);
                        const float input = data[sample];
                        const int stateChannel = channel % 64;
                        lowState[stateChannel] += 0.055f * (input - lowState[stateChannel]);
                        midState[stateChannel] += 0.22f * (input - midState[stateChannel]);
                        const float low = lowState[stateChannel];
                        const float mid = midState[stateChannel] - lowState[stateChannel];
                        const float high = input - midState[stateChannel];
                        data[sample] = (low * juce::Decibels::decibelsToGain(lowDb)
                                      + mid * juce::Decibels::decibelsToGain(midDb)
                                      + high * juce::Decibels::decibelsToGain(highDb)) * gain;
                    }
                }

                if (!writer->writeFromAudioSampleBuffer(buffer, 0, count))
                {
                    okay = false;
                    break;
                }
                position += count;
            }
            writer.reset();
            safeThis->status.setText(okay ? "Exported processed WAV: " + destination.getFileName()
                                          : "Export failed while writing audio.",
                                     juce::dontSendNotification);
        });
}

void VocalDynamicsStandaloneEditor::timerCallback()
{
    const auto position = transport.getCurrentPosition();
    const auto length = transport.getLengthInSeconds();
    timeLabel.setText(formatTimelineTime(position) + "  /  " + formatTimelineTime(length),
                      juce::dontSendNotification);
    repaint();
}

void VocalDynamicsStandaloneEditor::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        recordingSampleRate = device->getCurrentSampleRate();
}

void VocalDynamicsStandaloneEditor::audioDeviceStopped()
{
    juce::MessageManager::callAsync([this] { stopRecording(); });
}

void VocalDynamicsStandaloneEditor::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    juce::ignoreUnused(outputChannelData, numOutputChannels);
    const juce::ScopedLock lock(recordingLock);
    if (!recordingActive || recordingWriter == nullptr || inputChannelData == nullptr || numInputChannels <= 0)
        return;
    int channel = 0;
    while (channel < numInputChannels && inputChannelData[channel] == nullptr)
        ++channel;
    if (channel >= numInputChannels)
        return;
    const float* channels[] = { inputChannelData[channel] };
    recordingWriter->write(channels, numSamples);
}

void VocalDynamicsStandaloneEditor::toggleRecording()
{
    if (recordingActive)
    {
        stopRecording();
        return;
    }
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr || device->getActiveInputChannels().isZero())
    {
        status.setText("Recording unavailable: enable an input in AUDIO DEVICE settings.", juce::dontSendNotification);
        openAudioSettings();
        return;
    }

    recordingChooser = std::make_unique<juce::FileChooser>(
        "Save vocal recording", juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile("Vocal Editor Take.wav"), "*.wav");
    const auto flags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;
    recordingChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto chosen = chooser.getResult();
        if (chosen == juce::File())
            return;
        const auto output = chosen.withFileExtension(".wav");
        if (output.existsAsFile() && !output.deleteFile())
        {
            status.setText("Could not replace the selected recording file.", juce::dontSendNotification);
            return;
        }
        const double rate = recordingSampleRate > 0.0 ? recordingSampleRate : 44100.0;
        auto stream = std::unique_ptr<juce::FileOutputStream>(output.createOutputStream());
        if (stream == nullptr)
        {
            status.setText("Could not create WAV file. Choose another location.", juce::dontSendNotification);
            return;
        }
        auto* format = formatManager.findFormatForFileExtension(".wav");
        auto* writer = format != nullptr ? format->createWriterFor(stream.release(), rate, 1, 24, {}, 0) : nullptr;
        if (writer == nullptr)
        {
            status.setText("Could not initialize WAV recording.", juce::dontSendNotification);
            return;
        }
        lastRecordingFile = output;
        {
            const juce::ScopedLock lock(recordingLock);
            recordingWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
                writer, recordingThread, 32768);
            recordingActive = true;
        }
        recordButton.setButtonText("■ STOP REC");
        status.setText("Recording input at " + juce::String(rate, 0) + " Hz — press STOP REC to finish.", juce::dontSendNotification);
        repaint();
    });
}

void VocalDynamicsStandaloneEditor::stopRecording()
{
    {
        const juce::ScopedLock lock(recordingLock);
        if (!recordingActive && recordingWriter == nullptr)
            return;
        recordingActive = false;
        recordingWriter.reset();
    }
    if (lastRecordingFile.existsAsFile() && lastRecordingFile.getSize() > 44)
    {
        status.setText("Recording saved: " + lastRecordingFile.getFileName(), juce::dontSendNotification);
        loadFile(lastRecordingFile);
    }
    recordButton.setButtonText("● RECORD");
    repaint();
}

void VocalDynamicsStandaloneEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &thumbnail)
        repaint();
}

void VocalDynamicsStandaloneEditor::paint(juce::Graphics& g)
{
    const auto bg = juce::Colour(0xff070b14);
    const auto surface = juce::Colour(0xff101827);
    const auto raised = juce::Colour(0xff182338);
    const auto edge = juce::Colour(0xff26374f);
    const auto cyan = juce::Colour(0xff54f2c2);
    const auto violet = juce::Colour(0xffa78bfa);
    const auto text = juce::Colour(0xfff3f6ff);
    const auto muted = juce::Colour(0xff91a3bd);
    const auto main = waveformBounds();

    g.fillAll(bg);
    // A purpose-built editorial layout: narrow utility rail, large waveform canvas,
    // event strip and a contextual inspector. Avoid the generic rounded-card look.
    g.setColour(juce::Colour(0xff0b1220));
    g.fillRect(0, 0, getWidth(), 112);
    g.setColour(juce::Colour(0xff111e31));
    g.fillRect(0, 108, getWidth(), 4);
    g.setColour(cyan);
    g.fillRect(0, 0, 4, 112);
    g.setColour(juce::Colour(0x3354f2c2));
    g.fillRect(4, 0, 18, 112);
    g.setColour(text);
    g.setFont(juce::Font("Aptos Display", 24.0f, juce::Font::bold));
    g.drawText("VOCAL", 28, 14, 104, 29, juce::Justification::centredLeft);
    g.setColour(cyan);
    g.setFont(juce::Font("Aptos Display", 20.0f, juce::Font::bold));
    g.drawText("EDITOR", 28, 43, 140, 24, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(10.5f);
    g.drawText("PRECISION VOCAL WORKSTATION", 28, 75, 250, 16, juce::Justification::centredLeft);

    g.setColour(edge);
    g.drawHorizontalLine(111, 0.0f, static_cast<float>(getWidth()));
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("TRANSPORT", 285, 17, 90, 14, juce::Justification::centredLeft);
    g.setColour(text);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText(timeLabel.getText(), 285, 34, 220, 27, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("SPACE  PLAY/PAUSE     +/-  ZOOM     ESC  STOP", 285, 68, 300, 16, juce::Justification::centredLeft);

    // Main waveform / timeline canvas.
    g.setColour(surface);
    g.fillRoundedRectangle(main.toFloat(), 5.0f);
    g.setColour(isDraggingFile ? cyan : edge);
    g.drawRoundedRectangle(main.toFloat(), 5.0f, isDraggingFile ? 2.0f : 1.0f);
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("WAVEFORM  /  CLICK TO SEEK  /  DRAG TO SCRUB", main.getX() + 14, main.getY() + 8,
               main.getWidth() - 28, 14, juce::Justification::centredLeft);

    const auto drawable = main.reduced(12, 28);
    if (thumbnail.getTotalLength() > 0.0)
    {
        const double duration = transport.getLengthInSeconds();
        const double visibleStart = viewStartSeconds;
        const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : duration;
        const double span = juce::jmax(0.001, visibleEnd - visibleStart);

        // Quiet grid lines make timing readable without competing with the signal.
        for (int i = 0; i <= 8; ++i)
        {
            const int x = drawable.getX() + i * drawable.getWidth() / 8;
            g.setColour(juce::Colour(0xff243149));
            g.drawVerticalLine(x, static_cast<float>(drawable.getY()), static_cast<float>(drawable.getBottom()));
            const double t = visibleStart + span * static_cast<double>(i) / 8.0;
            g.setColour(muted);
            g.setFont(9.0f);
            const auto tickText = juce::String::formatted("%02d:%02d",
                static_cast<int>(t) / 60, static_cast<int>(t) % 60);
            const int tickWidth = juce::jmin(54, drawable.getWidth() / 8);
            g.drawText(tickText, juce::Rectangle<int>(x - tickWidth / 2, main.getY() + 24, tickWidth, 13),
                       juce::Justification::centred, false);
        }

        g.setColour(cyan.withAlpha(0.96f));
        thumbnail.drawChannels(g, drawable, visibleStart, visibleEnd, 1.0f);

        // A center reference line improves visual reading of low-level consonants.
        g.setColour(juce::Colour(0x334bded0));
        g.drawHorizontalLine(drawable.getCentreY(), static_cast<float>(drawable.getX()), static_cast<float>(drawable.getRight()));

        if (isSelectingRange)
        {
            const int x = juce::jmin(selectionStartX, selectionCurrentX);
            const int w = std::abs(selectionCurrentX - selectionStartX);
            g.setColour(juce::Colour(0x3351e0d0));
            g.fillRect(x, drawable.getY(), w, drawable.getHeight());
            g.setColour(cyan);
            g.drawRect(static_cast<float>(x), static_cast<float>(drawable.getY()), static_cast<float>(w), static_cast<float>(drawable.getHeight()), 1.0f);
        }

        if (duration > 0.0)
        {
            const double playheadTime = juce::jlimit(visibleStart, visibleEnd, transport.getCurrentPosition());
            const int x = drawable.getX() + static_cast<int>((playheadTime - visibleStart) / span * drawable.getWidth());
            g.setColour(juce::Colour(0xfff6fcff));
            g.setColour(juce::Colour(0xfff6fcff));
            g.drawVerticalLine(x, static_cast<float>(drawable.getY() - 2), static_cast<float>(drawable.getBottom()));
            g.setColour(cyan);
            g.fillEllipse(static_cast<float>(x - 3), static_cast<float>(drawable.getY() - 4), 6.0f, 6.0f);
        }
    }
    else
    {
        g.setColour(juce::Colour(0x1451e0d0));
        g.fillRect(drawable);
        g.setColour(cyan);
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("DROP A VOCAL FILE", drawable, juce::Justification::centred);
        g.setColour(muted);
        g.setFont(11.0f);
        g.drawText("WAV  /  AIFF  /  FLAC  /  MP3  /  OGG", drawable.getX(), drawable.getCentreY() + 24,
                   drawable.getWidth(), 20, juce::Justification::centred);
    }
    if (isDraggingFile)
    {
        g.setColour(juce::Colour(0x2251e0d0));
        g.fillRect(main.reduced(4));
        g.setColour(cyan);
        g.drawRect(main.reduced(4), 2.0f);
    }

    // Event lane: only display real transcription segments. No fake detector events.
    juce::Rectangle<int> lane(main.getX(), 446, main.getWidth(), 112);
    g.setColour(surface);
    g.fillRoundedRectangle(lane.toFloat(), 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(lane.toFloat(), 5.0f, 1.0f);
    g.setColour(violet);
    g.fillRect(lane.getX(), lane.getY(), 3, lane.getHeight());
    g.setColour(text);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("WORD EVENTS", lane.getX() + 14, lane.getY() + 8, 140, 18, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText(juce::String(static_cast<int>(wordEvents.size())) + " SEGMENTS",
               lane.getRight() - 110, lane.getY() + 9, 94, 16, juce::Justification::centredRight);

    if (wordEvents.empty() || thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour(muted);
        g.setFont(11.0f);
        g.drawText("No words detected yet. Load audio, choose the speech app and language pack, then press Detect Words.",
                   lane.getX() + 14, lane.getY() + 42, lane.getWidth() - 28, 28, juce::Justification::centredLeft);
    }
    else
    {
        const double duration = thumbnail.getTotalLength();
        const double visibleStart = viewStartSeconds;
        const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : duration;
        const double span = juce::jmax(0.001, visibleEnd - visibleStart);
        const int laneX = main.getX() + 12;
        const int laneWidth = main.getWidth() - 24;
        for (int i = 0; i < static_cast<int>(wordEvents.size()); ++i)
        {
            const auto& e = wordEvents[static_cast<size_t>(i)];
            if (e.endSeconds < visibleStart || e.startSeconds > visibleEnd) continue;
            const int x1 = laneX + static_cast<int>((e.startSeconds - visibleStart) / span * laneWidth);
            const int x2 = laneX + static_cast<int>((e.endSeconds - visibleStart) / span * laneWidth);
            juce::Rectangle<float> block(static_cast<float>(x1), 482.0f,
                static_cast<float>(juce::jmax(10, x2 - x1)), 31.0f);
            g.setColour(i == selectedEvent ? juce::Colour(0xff1f625f) : juce::Colour(0xff263342));
            g.fillRoundedRectangle(block, 3.0f);
            g.setColour(i == selectedEvent ? cyan : edge);
            g.drawRoundedRectangle(block, 3.0f, i == selectedEvent ? 1.5f : 1.0f);
            g.setColour(text);
            g.setFont(10.0f);
            g.drawText(e.word + "  " + juce::String(e.gainDb, 1) + " dB", block.reduced(3.0f),
                       juce::Justification::centred);
        }
    }

    // Inspector with aligned labels and controls; no text is drawn beneath another control.
    const int sideX = main.getRight() + 14;
    const int sideW = juce::jmax(220, getWidth() - sideX - 24);
    juce::Rectangle<int> inspector(sideX, 142, sideW, 530);
    g.setColour(surface);
    g.fillRoundedRectangle(inspector.toFloat(), 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(inspector.toFloat(), 5.0f, 1.0f);
    g.setColour(cyan);
    g.fillRect(inspector.getX(), inspector.getY(), 3, 36);
    g.setColour(text);
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText("REGION INSPECTOR", inspector.getX() + 14, inspector.getY() + 8,
               inspector.getWidth() - 26, 20, juce::Justification::centredLeft);
    g.setColour(edge);
    g.drawHorizontalLine(inspector.getY() + 40, static_cast<float>(inspector.getX() + 12),
                         static_cast<float>(inspector.getRight() - 12));
    g.setColour(muted);
    g.setFont(10.5f);
    g.drawText("SELECTED WORD / REGION", inspector.getX() + 14, inspector.getY() + 47,
               inspector.getWidth() - 26, 16, juce::Justification::centredLeft);
    g.drawText("GAIN", inspector.getX() + 14, inspector.getY() + 105,
               inspector.getWidth() - 26, 16, juce::Justification::centredLeft);
    g.setColour(cyan);
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.drawText(gainValueLabel.getText(), inspector.getX() + 14, inspector.getY() + 121,
               inspector.getWidth() - 26, 28, juce::Justification::centredLeft);
    g.setColour(edge);
    g.drawHorizontalLine(inspector.getY() + 300, static_cast<float>(inspector.getX() + 12),
                         static_cast<float>(inspector.getRight() - 12));
    g.setColour(text);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("WORD DETECTION  /  SET UP ONCE", inspector.getX() + 14, inspector.getY() + 474,
               inspector.getWidth() - 26, 14, juce::Justification::centredLeft);

    juce::Rectangle<int> analysis(main.getX(), 584, main.getWidth(), 76);
    g.setColour(surface);
    g.fillRoundedRectangle(analysis.toFloat(), 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(analysis.toFloat(), 5.0f, 1.0f);
    g.setColour(violet);
    g.fillRect(analysis.getX(), analysis.getY(), 3, analysis.getHeight());
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("MUSIC ANALYSIS", analysis.getX() + 14, analysis.getY() + 9, 130, 15,
               juce::Justification::centredLeft);
    g.setColour(text);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    const int analysisTextWidth = juce::jmax(80, analysisButton.getX() - analysis.getX() - 30);
    g.drawFittedText(analysisLabel.getText(), analysis.getX() + 14, analysis.getY() + 29,
                     analysisTextWidth, 25, juce::Justification::centredLeft, 1, 0.75f);

    // Scale-aware pitch lane. Green notes belong to the selected scale; red notes do not.
    juce::Rectangle<int> pitchLane(main.getX(), 670, main.getWidth(), 150);
    g.setColour(surface);
    g.fillRoundedRectangle(pitchLane.toFloat(), 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(pitchLane.toFloat(), 5.0f, 1.0f);
    g.setColour(violet);
    g.fillRect(pitchLane.getX(), pitchLane.getY(), 3, pitchLane.getHeight());
    g.setColour(text);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("NOTE EDITOR  /  " + rootNoteBox.getText() + " " + scaleBox.getText(),
               pitchLane.getX() + 14, pitchLane.getY() + 8, 270, 18, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("IN-SCALE = GREEN     OUT-OF-SCALE = RED     MONOPHONIC ESTIMATE",
               pitchLane.getX() + 14, pitchLane.getY() + 31, 360, 14, juce::Justification::centredLeft);
    if (pitchNotes.empty())
    {
        g.setColour(muted);
        g.setFont(11.0f);
        g.drawText("Load a vocal and press ANALYZE NOTES to estimate note blobs.",
                   pitchLane.getX() + 14, pitchLane.getY() + 68, pitchLane.getWidth() - 28, 24,
                   juce::Justification::centredLeft);
    }
    else
    {
        const double duration = juce::jmax(0.01, thumbnail.getTotalLength());
        const double visibleStart = viewStartSeconds;
        const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : duration;
        const double span = juce::jmax(0.001, visibleEnd - visibleStart);
        const int plotTop = pitchLane.getY() + 54;
        const int plotHeight = pitchLane.getHeight() - 64;
        for (int semitone = 0; semitone < 37; ++semitone)
        {
            const int y = plotTop + semitone * plotHeight / 36;
            g.setColour(juce::Colour(0xff243149));
            g.drawHorizontalLine(y, static_cast<float>(pitchLane.getX() + 8), static_cast<float>(pitchLane.getRight() - 8));
        }
        for (const auto& note : pitchNotes)
        {
            if (note.endSeconds < visibleStart || note.startSeconds > visibleEnd) continue;
            const int x1 = pitchLane.getX() + 8 + static_cast<int>((note.startSeconds - visibleStart) / span * (pitchLane.getWidth() - 16));
            const int x2 = pitchLane.getX() + 8 + static_cast<int>((note.endSeconds - visibleStart) / span * (pitchLane.getWidth() - 16));
            const int midiClamped = juce::jlimit(48, 84, note.midiNote);
            const int y = plotTop + (84 - midiClamped) * plotHeight / 36;
            const bool inScale = pitchClassAllowed(note.midiNote);
            const auto noteColour = inScale ? juce::Colour(0xff2dd4a7) : juce::Colour(0xfff06464);
            g.setColour(noteColour.withAlpha(0.22f));
            g.fillRoundedRectangle(static_cast<float>(x1), static_cast<float>(y - 3),
                                   static_cast<float>(juce::jmax(7, x2 - x1)), 8.0f, 3.0f);
            g.setColour(noteColour);
            g.drawRoundedRectangle(static_cast<float>(x1), static_cast<float>(y - 3),
                                   static_cast<float>(juce::jmax(7, x2 - x1)), 8.0f, 3.0f, 1.2f);
        }
    }

    // Bottom status strip, deliberately quiet.
    g.setColour(juce::Colour(0xff0b1220));
    g.fillRect(0, getHeight() - 34, getWidth(), 34);
    g.setColour(edge);
    g.drawHorizontalLine(getHeight() - 34, 0.0f, static_cast<float>(getWidth()));
    g.setColour(cyan);
    g.fillEllipse(18.0f, static_cast<float>(getHeight() - 21), 6.0f, 6.0f);
    g.setColour(muted);
    g.setFont(10.5f);
    g.drawText(status.getText(), 32, getHeight() - 28, getWidth() - 48, 20, juce::Justification::centredLeft);
}

void VocalDynamicsStandaloneEditor::resized()
{
    const int width = getWidth();
    const int height = getHeight();
    const auto wave = waveformBounds();
    const int inspectorX = wave.getRight() + 14;
    const int inspectorW = juce::jmax(220, width - inspectorX - 24);
    const int innerX = inspectorX + 14;
    const int innerW = juce::jmax(180, inspectorW - 28);

    // Responsive header and inspector: controls follow the available client area.
    status.setBounds(32, height - 28, juce::jmax(120, width - 48), 20);
    timeLabel.setBounds(285, 34, juce::jmin(250, juce::jmax(180, width - 650)), 28);

    importButton.setButtonText("IMPORT");
    playButton.setButtonText(transport.isPlaying() ? "PAUSE" : "PLAY");
    settingsButton.setButtonText("AUDIO DEVICE");
    whisperExeButton.setButtonText("CHOOSE SPEECH APP");
    whisperModelButton.setButtonText("CHOOSE MODEL");
    transcribeButton.setButtonText("DETECT WORDS");
    zoomOutButton.setButtonText("-");
    zoomInButton.setButtonText("+");

    const int toolbarRight = width - 24;
    zoomInButton.setBounds(toolbarRight - 30, 34, 30, 36);
    zoomLabel.setBounds(toolbarRight - 90, 34, 54, 36);
    zoomOutButton.setBounds(toolbarRight - 126, 34, 30, 36);
    stopButton.setBounds(toolbarRight - 194, 34, 58, 36);
    playButton.setBounds(toolbarRight - 276, 34, 74, 36);
    recordButton.setButtonText(recordingActive ? "STOP REC" : "RECORD");
    recordButton.setBounds(toolbarRight - 380, 34, 96, 36);
    importButton.setBounds(toolbarRight - 458, 34, 72, 36);

    selectedWordLabel.setBounds(innerX, 195, innerW, 30);
    gainValueLabel.setBounds(innerX, 264, innerW, 24);
    wordGainSlider.setBounds(innerX, 286, innerW, 66);
    eqTitle.setBounds(innerX, 358, innerW, 22);
    lowEqLabel.setBounds(innerX, 382, 42, 22);
    lowEqSlider.setBounds(innerX + 42, 382, innerW - 42, 22);
    midEqLabel.setBounds(innerX, 406, 42, 22);
    midEqSlider.setBounds(innerX + 42, 406, innerW - 42, 22);
    highEqLabel.setBounds(innerX, 430, 42, 22);
    highEqSlider.setBounds(innerX + 42, 430, innerW - 42, 22);
    settingsButton.setBounds(innerX, 462, innerW, 28);
    exportButton.setBounds(innerX, 494, innerW, 28);
    whisperExeButton.setBounds(innerX, 530, innerW, 26);
    whisperModelButton.setBounds(innerX, 558, innerW, 26);
    transcribeButton.setBounds(innerX, 586, innerW, 28);
    addWordButton.setVisible(false);

    analysisLabel.setBounds(wave.getX() + 14, 612, juce::jmax(100, wave.getWidth() - 205), 24);
    analysisButton.setBounds(wave.getRight() - 180, 598, 164, 48);
    rootNoteBox.setBounds(wave.getX() + 150, 678, 76, 28);
    scaleBox.setBounds(wave.getX() + 234, 678, 150, 28);
    pitchAnalysisButton.setBounds(wave.getX() + 394, 678, 140, 28);
}

