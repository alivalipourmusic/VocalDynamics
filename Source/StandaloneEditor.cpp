#include "StandaloneEditor.h"

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

    status.setText("Import a vocal, select a time range, and create editable word regions.", juce::dontSendNotification);
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

    zoomLabel.setText("100%", juce::dontSendNotification);
    zoomLabel.setColour(juce::Label::textColourId, muted);
    zoomLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(zoomLabel);

    wordGainSlider.setRange(-24.0, 12.0, 0.1);
    wordGainSlider.setValue(0.0);
    wordGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    wordGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    wordGainSlider.setColour(juce::Slider::trackColourId, cyan);
    wordGainSlider.setColour(juce::Slider::thumbColourId, white);
    wordGainSlider.onValueChange = [this] { updateSelectedGain(); };
    addAndMakeVisible(wordGainSlider);

    for (auto* button : { &importButton, &playButton, &stopButton, &exportButton, &addWordButton, &zoomInButton, &zoomOutButton })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff164252));
        button->setColour(juce::TextButton::textColourOffId, white);
        addAndMakeVisible(*button);
    }

    importButton.onClick = [this] { chooseFile(); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { stopPlayback(); };
    exportButton.onClick = [this] { exportProcessedAudio(); };
    addWordButton.onClick = [this] { createWordEventFromSelection(); };
    zoomInButton.onClick = [this] { zoomBy(0.5); };
    zoomOutButton.onClick = [this] { zoomBy(2.0); };
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);

    const auto setupResult = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (setupResult.isNotEmpty())
        status.setText("Audio device error: " + setupResult, juce::dontSendNotification);

    audioSourcePlayer.setSource(&eventGainSource);
    deviceManager.addAudioCallback(&audioSourcePlayer);
    startTimerHz(20);
    setSize(1100, 720);
}

VocalDynamicsStandaloneEditor::~VocalDynamicsStandaloneEditor()
{
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
    thumbnail.clear();
    const bool waveformStarted = thumbnail.setSource(new juce::FileInputSource(file));
    playButton.setButtonText("PLAY");
    status.setText(waveformStarted ? "Loaded: " + file.getFileName()
                                   : "Audio loaded; waveform preview could not be opened.",
                   juce::dontSendNotification);
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
    const auto area = waveformBounds();
    const double fullDuration = thumbnail.getTotalLength();
    if (fullDuration <= 0.0 || area.getWidth() <= 0)
        return 0.0;
    const double start = viewEndSeconds > viewStartSeconds ? viewStartSeconds : 0.0;
    const double end = viewEndSeconds > start ? viewEndSeconds : fullDuration;
    return juce::jlimit(0.0, fullDuration,
        start + static_cast<double>(x - area.getX()) / static_cast<double>(area.getWidth()) * (end - start));
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
    const double newSpan = juce::jlimit(0.5, total, oldSpan * factor);
    const double anchor = juce::jlimit(0.0, total, transport.getCurrentPosition());
    const double anchorRatio = (anchor - viewStartSeconds) / juce::jmax(0.001, oldSpan);
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
    const double zoom = span > 0.0 ? total / span * 100.0 : 100.0;
    zoomLabel.setText(juce::String(static_cast<int>(zoom)) + "%", juce::dontSendNotification);
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
        zoomBy(0.5);
        return true;
    }
    if (key.getTextCharacter() == '-' || key.getTextCharacter() == '_' ||
        key.getKeyCode() == juce::KeyPress::numberPadSubtract)
    {
        zoomBy(2.0);
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
        isSelectingRange = true;
        mouseMovedOnWave = false;
        selectionStartX = juce::jlimit(area.getX(), area.getRight(), event.x);
        selectionCurrentX = selectionStartX;
        return;
    }

    if (event.y >= 450 && event.y <= 625)
        selectEventAt(event.getPosition());
}

void VocalDynamicsStandaloneEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (isSelectingRange)
    {
        if (std::abs(event.x - selectionStartX) > 3)
            mouseMovedOnWave = true;
        selectionCurrentX = juce::jlimit(waveformBounds().getX(), waveformBounds().getRight(), event.x);
        repaint();
    }
}

void VocalDynamicsStandaloneEditor::mouseUp(const juce::MouseEvent& event)
{
    if (!isSelectingRange)
        return;
    selectionCurrentX = juce::jlimit(waveformBounds().getX(), waveformBounds().getRight(), event.x);
    isSelectingRange = false;
    if (mouseMovedOnWave && std::abs(selectionCurrentX - selectionStartX) >= 5)
        createWordEventFromSelection();
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
    wordGainSlider.setValue(0.0, juce::dontSendNotification);
    status.setText("Region created. Rename it by clicking its name; adjust its gain below.", juce::dontSendNotification);
    repaint();
}

void VocalDynamicsStandaloneEditor::selectEventAt(juce::Point<int> point)
{
    if (wordEvents.empty() || thumbnail.getTotalLength() <= 0.0)
        return;
    const double duration = thumbnail.getTotalLength();
    const int laneX = 30;
    const int laneWidth = getWidth() - 60;
    for (int i = static_cast<int>(wordEvents.size()) - 1; i >= 0; --i)
    {
        const auto& e = wordEvents[static_cast<size_t>(i)];
        const int x1 = laneX + static_cast<int>(e.startSeconds / duration * laneWidth);
        const int x2 = laneX + static_cast<int>(e.endSeconds / duration * laneWidth);
        if (point.x >= x1 && point.x <= x2)
        {
            selectedEvent = i;
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
        repaint();
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
            const auto x = waveArea.getX() + static_cast<int>(
                ((transport.getCurrentPosition() - visibleStart) / juce::jmax(0.001, visibleEnd - visibleStart)) * waveArea.getWidth());
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

    juce::Rectangle<float> eventArea(30.0f, 450.0f, static_cast<float>(getWidth() - 60), 175.0f);
    g.setColour(juce::Colour(0xff0c1824));
    g.fillRoundedRectangle(eventArea, 10.0f);
    g.setColour(border);
    g.drawRoundedRectangle(eventArea, 10.0f, 1.0f);
    g.setColour(purple);
    g.setFont(14.0f);
    g.drawText("WORD / PHRASE REGIONS  •  DRAG ON WAVEFORM TO CREATE",
               eventArea.getX() + 16.0f, eventArea.getY() + 8.0f, eventArea.getWidth() - 32.0f, 24.0f,
               juce::Justification::centredLeft);

    if (wordEvents.empty() || thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour(muted);
        g.setFont(12.0f);
        g.drawText("Select a section of the waveform. Each region can have its own gain and is applied during playback/export.",
                   eventArea.reduced(16.0f).withTrimmedTop(38.0f), juce::Justification::centredLeft);
    }
    else
    {
        const double duration = thumbnail.getTotalLength();
        const double visibleStart = viewStartSeconds;
        const double visibleEnd = viewEndSeconds > visibleStart ? viewEndSeconds : duration;
        const double visibleSpan = juce::jmax(0.001, visibleEnd - visibleStart);
        const int laneX = 42;
        const int laneWidth = getWidth() - 84;
        for (int i = 0; i < static_cast<int>(wordEvents.size()); ++i)
        {
            const auto& e = wordEvents[static_cast<size_t>(i)];
            const int x1 = laneX + static_cast<int>((e.startSeconds - visibleStart) / visibleSpan * laneWidth);
            const int x2 = laneX + static_cast<int>((e.endSeconds - visibleStart) / visibleSpan * laneWidth);
            juce::Rectangle<float> block(static_cast<float>(x1), 500.0f,
                                         static_cast<float>(juce::jmax(8, x2 - x1)), 46.0f);
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

    g.setColour(muted);
    g.setFont(11.0f);
    g.drawText("Selected region name is editable. Gain changes are non-destructive until export.",
               44, 598, getWidth() - 88, 18, juce::Justification::centredLeft);
}

void VocalDynamicsStandaloneEditor::resized()
{
    const int width = getWidth();
    title.setBounds(30, 22, 400, 38);
    status.setBounds(32, 62, 600, 25);
    timeLabel.setBounds(32, 112, 220, 24);

    importButton.setBounds(width - 530, 32, 125, 42);
    playButton.setBounds(width - 395, 32, 105, 42);
    stopButton.setBounds(width - 280, 32, 65, 42);
    zoomOutButton.setBounds(width - 205, 32, 38, 42);
    zoomLabel.setBounds(width - 165, 32, 55, 42);
    zoomInButton.setBounds(width - 105, 32, 38, 42);
    exportButton.setBounds(width - 530, 112, 125, 30);

    selectedWordLabel.setBounds(44, 548, 180, 28);
    wordGainSlider.setBounds(235, 550, width - 405, 24);
    gainValueLabel.setBounds(width - 155, 548, 95, 28);
    addWordButton.setBounds(44, 584, 170, 30);
}
