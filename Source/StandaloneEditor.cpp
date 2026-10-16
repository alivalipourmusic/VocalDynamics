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
        : "Whisper finished without an SRT file (exit code " + juce::String(exitCode) + "). Check model compatibility and audio format.";

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
        for (const auto& event : *events)
        {
            if (time >= event.startSeconds && time < event.endSeconds)
                gain = juce::Decibels::decibelsToGain(event.gainDb);
        }

        if (gain != 1.0f)
            for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
                info.buffer->getWritePointer(channel, info.startSample)[sample] *= gain;
    }
}

VocalDynamicsStandaloneEditor::VocalDynamicsStandaloneEditor()
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);
    readAheadThread.startThread();
    eventGainSource.setEvents(&wordEvents);

    const auto white = juce::Colour(0xffedf7ff);
    const auto cyan = juce::Colour(0xff39d9e9);
    const auto muted = juce::Colour(0xff8da6b9);

    title.setText("VOCAL DYNAMICS", juce::dontSendNotification);
    title.setFont(juce::Font(27.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, white);
    addAndMakeVisible(title);

    status.setText("Import a vocal, set the offline Whisper engine/model, then detect words for gain editing.", juce::dontSendNotification);
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

    for (auto* button : { &importButton, &settingsButton, &playButton, &stopButton, &exportButton, &addWordButton, &zoomInButton, &zoomOutButton, &whisperExeButton, &whisperModelButton, &transcribeButton })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff164252));
        button->setColour(juce::TextButton::textColourOffId, white);
        addAndMakeVisible(*button);
    }

    importButton.onClick = [this] { chooseFile(); };
    analysisButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263b52));
    analysisButton.setColour(juce::TextButton::textColourOffId, white);
    analysisButton.onClick = [this] { analyzeTempoAndKey(); };
    analysisButton.setTooltip("Estimate tempo (BPM) and musical key from the loaded audio.");
    addAndMakeVisible(analysisButton);
    analysisLabel.setText("TEMPO  -- BPM     KEY  --", juce::dontSendNotification);
    analysisLabel.setColour(juce::Label::textColourId, cyan);
    analysisLabel.setFont(juce::Font(14.0f, juce::Font::bold));
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
    whisperExeButton.setButtonText("CHOOSE SPEECH ENGINE");
    whisperModelButton.setButtonText("CHOOSE LANGUAGE MODEL");
    transcribeButton.setButtonText("DETECT WORDS");
    whisperExeButton.setTooltip("Choose whisper-cli.exe, the offline speech-recognition program.");
    whisperModelButton.setTooltip("Choose a multilingual Whisper model file (.bin).");
    transcribeButton.setTooltip("Create timed word segments from the loaded vocal.");
    whisperExeButton.onClick = [this] { chooseWhisperExecutable(); };
    whisperModelButton.onClick = [this] { chooseWhisperModel(); };
    transcribeButton.onClick = [this] { startTranscription(); };
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);

    const auto setupResult = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (setupResult.isNotEmpty())
        status.setText("Audio device error: " + setupResult, juce::dontSendNotification);

    audioSourcePlayer.setSource(&eventGainSource);
    deviceManager.addAudioCallback(&audioSourcePlayer);
    refreshAudioStatus();
    startTimerHz(20);
    setSize(1240, 760);
}

VocalDynamicsStandaloneEditor::~VocalDynamicsStandaloneEditor()
{
    if (transcriptionWorker != nullptr)
        transcriptionWorker->stopThread(3000);
    stopTimer();
    thumbnail.removeChangeListener(this);
    deviceManager.removeAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(nullptr);
    transport.stop();
    transport.setSource(nullptr);
    readerSource.reset();
    readAheadThread.stopThread(2000);
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
    options.dialogTitle = "Vocal Dynamics - Audio Settings";
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
        "Select whisper-cli.exe (whisper.cpp)", juce::File{}, "*.exe");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr)
            {
                const auto f = chooser.getResult();
                if (f.existsAsFile())
                {
                    safeThis->whisperExecutable = f;
                    safeThis->status.setText("Whisper executable set: " + f.getFileName(), juce::dontSendNotification);
                }
            }
        });
}

void VocalDynamicsStandaloneEditor::chooseWhisperModel()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select a multilingual Whisper GGML model", juce::File{}, "*.bin");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<VocalDynamicsStandaloneEditor>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr)
            {
                const auto f = chooser.getResult();
                if (f.existsAsFile())
                {
                    safeThis->whisperModel = f;
                    safeThis->status.setText("Offline model set: " + f.getFileName(), juce::dontSendNotification);
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
        status.setText("Set whisper-cli.exe and a multilingual GGML model first.", juce::dontSendNotification);
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

    status.setText("Offline transcription running. Long vocals can take several minutes...", juce::dontSendNotification);
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
    return { 24, 142, juce::jmax(420, getWidth() - 360), 286 };
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
            gainValueLabel.setText(juce::String(e.gainDb, 1) + " dB", juce::dontSendNotification);
            // Selecting a detected word also places the playhead at its exact start time.
            if (readerSource != nullptr)
                transport.setPosition(juce::jlimit(0.0, transport.getLengthInSeconds(), e.startSeconds));
            repaint();
            return;
        }
    }
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

                for (int sample = 0; sample < count; ++sample)
                {
                    const double time = static_cast<double>(position + sample) / reader->sampleRate;
                    float gain = 1.0f;
                    for (const auto& e : safeThis->wordEvents)
                        if (time >= e.startSeconds && time < e.endSeconds)
                            gain = juce::Decibels::decibelsToGain(e.gainDb);
                    if (gain != 1.0f)
                        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                            buffer.getWritePointer(channel)[sample] *= gain;
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

void VocalDynamicsStandaloneEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &thumbnail)
        repaint();
}

void VocalDynamicsStandaloneEditor::paint(juce::Graphics& g)
{
    const auto bg = juce::Colour(0xff080b10);
    const auto surface = juce::Colour(0xff101720);
    const auto raised = juce::Colour(0xff151f2b);
    const auto edge = juce::Colour(0xff293747);
    const auto cyan = juce::Colour(0xff51e0d0);
    const auto violet = juce::Colour(0xffa68bff);
    const auto text = juce::Colour(0xffedf4fb);
    const auto muted = juce::Colour(0xff8193a7);
    const auto main = waveformBounds();

    g.fillAll(bg);
    // A purpose-built editorial layout: narrow utility rail, large waveform canvas,
    // event strip and a contextual inspector. Avoid the generic rounded-card look.
    g.setColour(juce::Colour(0xff0d131b));
    g.fillRect(0, 0, getWidth(), 112);
    g.setColour(cyan);
    g.fillRect(0, 0, 4, 112);
    g.setColour(text);
    g.setFont(juce::Font(21.0f, juce::Font::bold));
    g.drawText("VOCAL", 28, 17, 92, 26, juce::Justification::centredLeft);
    g.setColour(cyan);
    g.drawText("DYNAMICS", 28, 41, 118, 23, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(10.5f);
    g.drawText("EVENT-BASED VOCAL WORKSTATION", 28, 72, 240, 16, juce::Justification::centredLeft);

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
            g.setColour(juce::Colour(0xff1d2a37));
            g.drawVerticalLine(x, static_cast<float>(drawable.getY()), static_cast<float>(drawable.getBottom()));
            const double t = visibleStart + span * static_cast<double>(i) / 8.0;
            g.setColour(muted);
            g.setFont(9.5f);
            g.drawText(formatTimelineTime(t), x - 36, main.getY() + 24, 72, 13, juce::Justification::centred);
        }

        g.setColour(cyan.withAlpha(0.90f));
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
        g.drawText("No word events yet. Load audio, configure Whisper and run word detection.",
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

    // Right-side inspector follows the selected event; controls remain functional.
    const int sideX = main.getRight() + 14;
    const int sideW = juce::jmax(220, getWidth() - sideX - 28);
    juce::Rectangle<int> inspector(sideX, 145, sideW, 500);
    g.setColour(surface);
    g.fillRoundedRectangle(inspector.toFloat(), 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(inspector.toFloat(), 5.0f, 1.0f);
    g.setColour(cyan);
    g.fillRect(inspector.getX(), inspector.getY(), 3, 42);
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("INSPECTOR  /  ACTIVE REGION", inspector.getX() + 15, inspector.getY() + 13,
               inspector.getWidth() - 28, 16, juce::Justification::centredLeft);
    g.setColour(edge);
    g.drawHorizontalLine(inspector.getY() + 44, static_cast<float>(inspector.getX() + 14),
                         static_cast<float>(inspector.getRight() - 14));
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("EVENT GAIN", inspector.getX() + 16, inspector.getY() + 205,
               inspector.getWidth() - 32, 16, juce::Justification::centredLeft);
    g.setColour(cyan);
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText(gainValueLabel.getText(), inspector.getX() + 16, inspector.getY() + 222,
               inspector.getWidth() - 32, 28, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(10.0f);
    g.drawText("Affects playback + WAV export", inspector.getX() + 16, inspector.getBottom() - 28,
               inspector.getWidth() - 32, 15, juce::Justification::centredLeft);

    // Bottom status strip, deliberately quiet.
    g.setColour(juce::Colour(0xff0d131b));
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
    const int sideX = juce::jmax(600, width - 330);
    title.setBounds(28, 14, 170, 58);
    title.setVisible(false);
    status.setVisible(false);
    timeLabel.setVisible(false);
    status.setBounds(32, height - 32, width - 48, 24);
    timeLabel.setBounds(285, 34, 230, 27);

    importButton.setBounds(width - 420, 34, 118, 38);
    playButton.setBounds(width - 294, 34, 86, 38);
    stopButton.setBounds(width - 200, 34, 62, 38);
    zoomOutButton.setBounds(width - 128, 34, 30, 38);
    zoomLabel.setBounds(width - 95, 34, 54, 38);
    zoomInButton.setBounds(width - 38, 34, 30, 38);

    // Inspector is a compact vertical tool rack rather than a row of crowded buttons.
    const int inspectorX = sideX + 16;
    const int inspectorW = juce::jmax(180, width - inspectorX - 28);
    selectedWordLabel.setBounds(inspectorX, 205, inspectorW, 34);
    wordGainSlider.setBounds(inspectorX + 4, 414, inspectorW - 8, 104);
    gainValueLabel.setBounds(inspectorX, 382, inspectorW, 28);
    settingsButton.setBounds(inspectorX, 160, inspectorW, 30);
    exportButton.setBounds(inspectorX, 248, inspectorW, 30);
    whisperExeButton.setBounds(inspectorX, 286, inspectorW, 28);
    whisperModelButton.setBounds(inspectorX, 318, inspectorW, 28);
    transcribeButton.setBounds(inspectorX, 354, inspectorW, 30);
    addWordButton.setBounds(inspectorX, 526, inspectorW, 26);
}
