#include "StandaloneEditor.h"

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

    timeLabel.setText("0.000 s / 0.000 s", juce::dontSendNotification);
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

    zoomLabel.setText("ZOOM", juce::dontSendNotification);
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
    settingsButton.onClick = [this] { openAudioSettings(); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { stopPlayback(); };
    exportButton.onClick = [this] { exportProcessedAudio(); };
    addWordButton.setButtonText("DETECT WORDS FIRST");
    addWordButton.onClick = [this] { startTranscription(); };
    zoomInButton.setButtonText("+");
    zoomOutButton.setButtonText("-"); // ASCII hyphen avoids unsupported-glyph boxes on Windows.
    zoomInButton.setTooltip("Zoom in");
    zoomOutButton.setTooltip("Zoom out");
    zoomInButton.onClick = [this] { zoomBy(0.85); };
    zoomOutButton.onClick = [this] { zoomBy(1.1764705882); };
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
    setSize(1100, 720);
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
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.existsAsFile() && formatManager.findFormatForFileExtension(file.getFileExtension()) != nullptr)
            return true;
    }
    return false;
}

void VocalDynamicsStandaloneEditor::filesDropped(const juce::StringArray& files, int, int)
{
    isDraggingFile = false;
    if (!files.isEmpty())
        loadFile(juce::File(files[0]));
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
    return { 30, 155, getWidth() - 60, 280 };
}

double VocalDynamicsStandaloneEditor::timeAtX(int x) const
{
    // The waveform is drawn inside a 12 px horizontal inset. Map clicks to
    // that exact drawable area, not the outer rounded panel, otherwise seeks
    // drift away from the point the user clicked.
    const auto area = waveformBounds().reduced(12, 14);
    const double fullDuration = thumbnail.getTotalLength();
    if (fullDuration <= 0.0 || area.getWidth() <= 0)
        return 0.0;
    const double start = viewEndSeconds > viewStartSeconds ? viewStartSeconds : 0.0;
    const double end = viewEndSeconds > start ? viewEndSeconds : fullDuration;
    const double ratio = juce::jlimit(0.0, 1.0,
        static_cast<double>(x - area.getX()) / static_cast<double>(area.getWidth()));
    return juce::jlimit(0.0, fullDuration, start + ratio * (end - start));
}

void VocalDynamicsStandaloneEditor::seekFromMouse(int x)
{
    if (thumbnail.getTotalLength() <= 0.0 || readerSource == nullptr)
        return;
    transport.setPosition(timeAtX(x));
    if (!transport.isPlaying())
        playButton.setButtonText("PLAY / SPACE");
}

void VocalDynamicsStandaloneEditor::zoomBy(double factor)
{
    const double total = thumbnail.getTotalLength();
    if (total <= 0.0 || factor <= 0.0)
        return;
    const double oldSpan = viewEndSeconds > viewStartSeconds ? viewEndSeconds - viewStartSeconds : total;
    // Keep zoom steps controlled and prevent extreme magnifications.
    // Fit-to-window is 100% (the full file); maximum magnification is 100x.
    const double minimumSpan = juce::jmax(0.05, total / 100.0);
    const double newSpan = juce::jlimit(minimumSpan, total, oldSpan * factor);
    // Keep the center of the visible timeline stable while zooming. The old
    // implementation anchored to the playhead, which made the waveform appear
    // to jump relative to the mouse/click location.
    const double anchor = juce::jlimit(0.0, total, viewStartSeconds + oldSpan * 0.5);
    const double anchorRatio = 0.5;
    double newStart = anchor - anchorRatio * newSpan;
    newStart = juce::jlimit(0.0, juce::jmax(0.0, total - newSpan), newStart);
    viewStartSeconds = newStart;
    viewEndSeconds = juce::jmin(total, newStart + newSpan);
    updateZoomLabel();
    repaint();
}

void VocalDynamicsStandaloneEditor::updateZoomLabel()
{
    const double total = thumbnail.getTotalLength();
    const double span = viewEndSeconds > viewStartSeconds ? viewEndSeconds - viewStartSeconds : total;
    // UI zoom meter is clamped to a clear 0–100 scale: 0 = full file fit,
    // 100 = maximum supported magnification. It never shows 200–800% values.
    const double fraction = total > 0.0 ? 1.0 - span / total : 0.0;
    const int percent = juce::jlimit(0, 100, static_cast<int>(std::round(fraction * 100.0)));
    zoomLabel.setText("ZOOM " + juce::String(percent) + "%", juce::dontSendNotification);
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
    if (thumbnail.getTotalLength() > 0.0 && area.contains(event.getPosition()))
    {
        // Follow the clicked position immediately. A drag scrubs the playhead;
        // it must not create a fake word region.
        seekFromMouse(event.x);
        isSelectingRange = true;
        mouseMovedOnWave = false;
        const auto drawable = area.reduced(12, 14);
        selectionStartX = juce::jlimit(drawable.getX(), drawable.getRight(), event.x);
        selectionCurrentX = selectionStartX;
        return;
    }

    if (event.y >= 450 && event.y <= 534)
        selectEventAt(event.getPosition());
}

void VocalDynamicsStandaloneEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (isSelectingRange)
    {
        if (std::abs(event.x - selectionStartX) > 3)
            mouseMovedOnWave = true;
        const auto drawable = waveformBounds().reduced(12, 14);
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
    timeLabel.setText(juce::String(position, 3) + " s / " + juce::String(length, 3) + " s",
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
    const auto background = juce::Colour(0xff080e17);
    const auto panel = juce::Colour(0xff111f2d);
    const auto border = juce::Colour(0xff294153);
    const auto cyan = juce::Colour(0xff39d9e9);
    const auto purple = juce::Colour(0xff9b83ff);
    const auto white = juce::Colour(0xffedf7ff);
    const auto muted = juce::Colour(0xff7893a7);

    g.fillAll(background);
    auto bounds = getLocalBounds().toFloat().reduced(14.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(bounds, 16.0f);
    g.setColour(border);
    g.drawRoundedRectangle(bounds, 16.0f, 1.0f);
    g.setColour(cyan);
    g.fillRoundedRectangle(30.0f, 101.0f, static_cast<float>(getWidth() - 60), 2.0f, 1.0f);

    const auto waveArea = waveformBounds();
    g.setColour(juce::Colour(0xff091521));
    g.fillRoundedRectangle(waveArea.toFloat(), 12.0f);
    g.setColour(isDraggingFile || isSelectingRange ? cyan : border);
    g.drawRoundedRectangle(waveArea.toFloat(), 12.0f, isSelectingRange ? 2.5f : 1.0f);

    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour(cyan);
        const double visibleStart = viewStartSeconds;
        const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : thumbnail.getTotalLength();
        thumbnail.drawChannels(g, waveArea.reduced(12, 14), visibleStart, visibleEnd, 1.0f);

        if (isSelectingRange)
        {
            const int x = juce::jmin(selectionStartX, selectionCurrentX);
            const int w = std::abs(selectionCurrentX - selectionStartX);
            g.setColour(juce::Colour(0x5539d9e9));
            g.fillRect(x, waveArea.getY() + 1, w, waveArea.getHeight() - 2);
        }

        const auto duration = transport.getLengthInSeconds();
        if (duration > 0.0)
        {
            const double visibleStart = viewStartSeconds;
            const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : duration;
            const auto drawable = waveArea.reduced(12, 14);
            const auto rawX = drawable.getX() + static_cast<int>(
                ((transport.getCurrentPosition() - visibleStart) / juce::jmax(0.001, visibleEnd - visibleStart)) * drawable.getWidth());
            const auto x = juce::jlimit(drawable.getX(), drawable.getRight(), rawX);
            g.setColour(white);
            g.drawVerticalLine(x, static_cast<float>(waveArea.getY() + 8), static_cast<float>(waveArea.getBottom() - 8));
        }
    }
    else
    {
        g.setColour(muted);
        g.setFont(15.0f);
        g.drawText("IMPORT A VOCAL FILE TO VIEW ITS WAVEFORM", waveArea.reduced(20, 20), juce::Justification::centred);
    }

    juce::Rectangle<float> eventArea(30.0f, 450.0f, static_cast<float>(getWidth() - 60), 84.0f);
    g.setColour(juce::Colour(0xff0c1824));
    g.fillRoundedRectangle(eventArea, 10.0f);
    g.setColour(border);
    g.drawRoundedRectangle(eventArea, 10.0f, 1.0f);
    g.setColour(purple);
    g.setFont(14.0f);
    g.drawText("DETECTED WORDS  /  SELECT A WORD TO EDIT ITS GAIN",
               eventArea.getX() + 16.0f, eventArea.getY() + 8.0f, eventArea.getWidth() - 32.0f, 24.0f,
               juce::Justification::centredLeft);

    if (wordEvents.empty() || thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour(muted);
        g.setFont(12.0f);
        g.drawText("No words detected yet. Set whisper-cli.exe and a multilingual model, then click TRANSCRIBE WORDS.",
                   eventArea.reduced(16.0f).withTrimmedTop(38.0f), juce::Justification::centredLeft);
    }
    else
    {
        const double duration = thumbnail.getTotalLength();
        const double visibleStart = viewStartSeconds;
        const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : duration;
        const double visibleSpan = juce::jmax(0.001, visibleEnd - visibleStart);
        const int laneX = waveformBounds().getX() + 12;
        const int laneWidth = waveformBounds().getWidth() - 24;
        for (int i = 0; i < static_cast<int>(wordEvents.size()); ++i)
        {
            const auto& e = wordEvents[static_cast<size_t>(i)];
            const int x1 = laneX + static_cast<int>((e.startSeconds - visibleStart) / visibleSpan * laneWidth);
            const int x2 = laneX + static_cast<int>((e.endSeconds - visibleStart) / visibleSpan * laneWidth);
            juce::Rectangle<float> block(static_cast<float>(x1), 488.0f,
                                         static_cast<float>(juce::jmax(8, x2 - x1)), 30.0f);
            g.setColour(i == selectedEvent ? juce::Colour(0xff246e7b) : juce::Colour(0xff263b54));
            g.fillRoundedRectangle(block, 5.0f);
            g.setColour(i == selectedEvent ? cyan : border);
            g.drawRoundedRectangle(block, 5.0f, 1.0f);
            g.setColour(white);
            g.setFont(11.0f);
            g.drawText(e.word + "  " + juce::String(e.gainDb, 1) + " dB",
                       block.reduced(4.0f), juce::Justification::centred);
        }
    }

    // Dedicated edit strip keeps the gain control clear of the event lane.
    juce::Rectangle<float> editArea(30.0f, 545.0f, static_cast<float>(getWidth() - 60), 122.0f);
    g.setColour(juce::Colour(0xff132332));
    g.fillRoundedRectangle(editArea, 10.0f);
    g.setColour(border);
    g.drawRoundedRectangle(editArea, 10.0f, 1.0f);
    g.setColour(muted);
    g.setFont(11.0f);
    g.drawText("SELECTED WORD / REGION", 44, 548, 180, 18, juce::Justification::centredLeft);
    g.drawText("GAIN", getWidth() - 278, 548, 150, 18, juce::Justification::centred);
    g.drawText("Changes affect playback and WAV export.", 44, 648, getWidth() - 88, 14,
               juce::Justification::centredLeft);
}

void VocalDynamicsStandaloneEditor::resized()
{
    const int width = getWidth();
    title.setBounds(30, 22, 400, 38);
    status.setBounds(32, 62, 600, 25);
    timeLabel.setBounds(32, 112, 220, 24);

    importButton.setBounds(width - 650, 32, 125, 42);
    playButton.setBounds(width - 515, 32, 105, 42);
    stopButton.setBounds(width - 400, 32, 65, 42);
    zoomOutButton.setBounds(width - 325, 32, 38, 42);
    zoomLabel.setBounds(width - 285, 32, 55, 42);
    zoomInButton.setBounds(width - 225, 32, 38, 42);
    settingsButton.setBounds(width - 180, 32, 150, 42);
    exportButton.setBounds(width - 650, 112, 125, 30);

    selectedWordLabel.setBounds(44, 568, 190, 30);
    wordGainSlider.setBounds(width - 290, 562, 145, 88);
    gainValueLabel.setBounds(width - 145, 568, 90, 28);
    addWordButton.setBounds(44, 606, 145, 30);
    whisperExeButton.setBounds(198, 606, 130, 30);
    whisperModelButton.setBounds(337, 606, 100, 30);
    transcribeButton.setBounds(446, 606, 160, 30);
}
