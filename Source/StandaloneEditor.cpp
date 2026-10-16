#include "StandaloneEditor.h"

VocalDynamicsStandaloneEditor::VocalDynamicsStandaloneEditor()
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);
    readAheadThread.startThread();

    const auto white = juce::Colour(0xffedf7ff);
    const auto cyan = juce::Colour(0xff39d9e9);
    const auto muted = juce::Colour(0xff8da6b9);

    title.setText("VOCAL DYNAMICS", juce::dontSendNotification);
    title.setFont(juce::Font(27.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, white);
    addAndMakeVisible(title);

    status.setText("DROP A VOCAL FILE HERE  /  OR CLICK IMPORT VOCAL", juce::dontSendNotification);
    status.setFont(juce::Font(12.0f));
    status.setColour(juce::Label::textColourId, cyan);
    addAndMakeVisible(status);

    timeLabel.setText("0.000 s / 0.000 s", juce::dontSendNotification);
    timeLabel.setColour(juce::Label::textColourId, muted);
    addAndMakeVisible(timeLabel);

    for (auto* button : { &importButton, &playButton, &stopButton })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff164252));
        button->setColour(juce::TextButton::textColourOffId, white);
        addAndMakeVisible(*button);
    }

    importButton.onClick = [this] { chooseFile(); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { stopPlayback(); };

    const auto setupResult = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (setupResult.isNotEmpty())
        status.setText("Audio device error: " + setupResult, juce::dontSendNotification);

    audioSourcePlayer.setSource(&transport);
    deviceManager.addAudioCallback(&audioSourcePlayer);
    startTimerHz(20);
    setSize(1100, 720);
}

VocalDynamicsStandaloneEditor::~VocalDynamicsStandaloneEditor()
{
    stopTimer();
    thumbnail.removeChangeListener(this);

    // Disconnect the device callback before destroying any audio source it can read.
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
        "Choose a vocal audio file", juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

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
        status.setText("Unsupported or unreadable audio file: " + file.getFileName(),
                       juce::dontSendNotification);
        return;
    }

    if (!juce::isPositiveAndBelow(reader->numChannels, 65) || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
    {
        status.setText("The audio file has invalid channel, sample-rate, or length information.",
                       juce::dontSendNotification);
        return;
    }

    const double sampleRate = reader->sampleRate;

    // Release the old source only after the transport has disconnected from it.
    transport.stop();
    transport.setSource(nullptr);
    readerSource.reset();

    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    transport.setSource(newSource.get(), 32768, &readAheadThread, sampleRate, 2);
    readerSource = std::move(newSource);

    currentFile = file;
    thumbnail.clear();
    const bool waveformStarted = thumbnail.setSource(new juce::FileInputSource(file));

    playButton.setButtonText("PLAY");
    status.setText(waveformStarted
        ? "Loaded: " + file.getFileName()
        : "Audio loaded; waveform preview could not be opened.",
        juce::dontSendNotification);
    repaint();
}

void VocalDynamicsStandaloneEditor::togglePlayback()
{
    if (readerSource == nullptr)
    {
        status.setText("Import or drop a vocal file first.", juce::dontSendNotification);
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

    juce::Rectangle<int> waveArea(30, 155, getWidth() - 60, 300);
    g.setColour(juce::Colour(0xff091521));
    g.fillRoundedRectangle(waveArea.toFloat(), 12.0f);
    g.setColour(isDraggingFile ? cyan : border);
    g.drawRoundedRectangle(waveArea.toFloat(), 12.0f, isDraggingFile ? 2.5f : 1.0f);

    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour(cyan);
        thumbnail.drawChannels(g, waveArea.reduced(12, 14), 0.0,
                               thumbnail.getTotalLength(), 1.0f);

        const auto duration = transport.getLengthInSeconds();
        if (duration > 0.0)
        {
            const auto x = waveArea.getX() + static_cast<int>(
                (transport.getCurrentPosition() / duration) * waveArea.getWidth());
            g.setColour(juce::Colour(0xfff5ffff));
            g.drawVerticalLine(x, static_cast<float>(waveArea.getY() + 8),
                               static_cast<float>(waveArea.getBottom() - 8));
        }
    }
    else
    {
        g.setColour(muted);
        g.setFont(15.0f);
        g.drawText("DROP WAV / AIFF / FLAC / MP3 HERE OR CLICK IMPORT VOCAL",
                   waveArea.reduced(20, 20), juce::Justification::centred);
    }

    juce::Rectangle<float> eventArea(30.0f, 475.0f,
                                     static_cast<float>(getWidth() - 60), 150.0f);
    g.setColour(juce::Colour(0xff0c1824));
    g.fillRoundedRectangle(eventArea, 10.0f);
    g.setColour(border);
    g.drawRoundedRectangle(eventArea, 10.0f, 1.0f);
    g.setColour(purple);
    g.setFont(14.0f);
    g.drawText("WORD-LEVEL DYNAMICS", eventArea.getX() + 16.0f,
               eventArea.getY() + 14.0f, eventArea.getWidth() - 32.0f, 24.0f,
               juce::Justification::centredLeft);
    g.setColour(white);
    g.setFont(12.0f);
    g.drawText("Word detection and individual word gain editing are not implemented yet.",
               eventArea.reduced(16.0f).withTrimmedTop(42.0f),
               juce::Justification::centredLeft);
}

void VocalDynamicsStandaloneEditor::resized()
{
    const int width = getWidth();
    title.setBounds(30, 22, 440, 38);
    status.setBounds(32, 62, 650, 25);
    timeLabel.setBounds(32, 112, 400, 24);
    importButton.setBounds(width - 360, 32, 150, 42);
    playButton.setBounds(width - 200, 32, 75, 42);
    stopButton.setBounds(width - 115, 32, 80, 42);
}
