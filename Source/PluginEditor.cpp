
#include "PluginEditor.h"

VocalDynamicsAudioProcessorEditor::
VocalDynamicsAudioProcessorEditor(
    VocalDynamicsAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      audioProcessor(processor)
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);

    setSize(1100, 720);
    setResizable(true, true);
    setResizeLimits(900, 600, 1600, 1000);

    const auto white = juce::Colour(0xffedf7ff);
    const auto muted = juce::Colour(0xff8da6b9);
    const auto cyan = juce::Colour(0xff54f2c2);
    const auto panel = juce::Colour(0xff111f2d);

    titleLabel.setText(
        "VOCAL DYNAMICS",
        juce::dontSendNotification);
    titleLabel.setFont(
        juce::Font(27.0f, juce::Font::bold));
    titleLabel.setColour(
        juce::Label::textColourId, white);
    addAndMakeVisible(titleLabel);

    statusLabel.setText(
        "WORD-LEVEL DYNAMICS EDITOR  /  STAGE 1",
        juce::dontSendNotification);
    statusLabel.setFont(juce::Font(12.0f));
    statusLabel.setColour(
        juce::Label::textColourId, cyan);
    addAndMakeVisible(statusLabel);

    durationLabel.setText(
        "No audio loaded",
        juce::dontSendNotification);
    durationLabel.setFont(juce::Font(12.0f));
    durationLabel.setColour(
        juce::Label::textColourId, muted);
    addAndMakeVisible(durationLabel);

    importButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(0xff164252));
    importButton.setColour(
        juce::TextButton::textColourOffId, white);
    importButton.onClick = [this] {
        importAudio();
    };
    addAndMakeVisible(importButton);

    analyzeButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(0xff352a59));
    analyzeButton.setColour(
        juce::TextButton::textColourOffId, white);
    analyzeButton.onClick = [this] {
        statusLabel.setText(
            "Speech analysis engine is not connected yet",
            juce::dontSendNotification);
    };
    addAndMakeVisible(analyzeButton);

    playButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(0xff183a31));
    playButton.setColour(
        juce::TextButton::textColourOffId, white);
    playButton.onClick = [this] {
        statusLabel.setText(
            "Playback will be implemented in the editor engine",
            juce::dontSendNotification);
    };
    addAndMakeVisible(playButton);

    gainSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, 90, 24);
    gainSlider.setRange(-24.0, 24.0, 0.1);
    gainSlider.setColour(
        juce::Slider::rotarySliderFillColourId, cyan);
    gainSlider.setColour(
        juce::Slider::thumbColourId, white);
    gainSlider.setColour(
        juce::Slider::textBoxTextColourId, white);
    gainSlider.setColour(
        juce::Slider::textBoxBackgroundColourId, panel);
    addAndMakeVisible(gainSlider);

    gainAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "gain", gainSlider);

    targetLufsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    targetLufsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
    targetLufsSlider.setRange(-24.0, -9.0, 0.1);
    targetLufsSlider.setValue(-14.0);
    targetLufsSlider.setColour(juce::Slider::trackColourId, cyan);
    targetLufsSlider.setColour(juce::Slider::thumbColourId, white);
    targetLufsSlider.setColour(juce::Slider::textBoxTextColourId, white);
    targetLufsSlider.setColour(juce::Slider::textBoxBackgroundColourId, panel);
    addAndMakeVisible(targetLufsSlider);
    targetLufsAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "targetLufs", targetLufsSlider);

    const auto styleToggle = [this, white](juce::TextButton& button, juce::Colour colour)
    {
        button.setClickingTogglesState(true);
        button.setColour(juce::TextButton::buttonColourId, colour);
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1b665b));
        button.setColour(juce::TextButton::textColourOffId, white);
        button.setColour(juce::TextButton::textColourOnId, white);
        addAndMakeVisible(button);
    };
    styleToggle(engineButton, juce::Colour(0xff243751));
    styleToggle(riderButton, juce::Colour(0xff243751));
    styleToggle(deEsserButton, juce::Colour(0xff243751));
    styleToggle(bypassButton, juce::Colour(0xff563b73));
    engineAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "levelingEnabled", engineButton);
    riderAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "gainRiding", riderButton);
    deEsserAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "deEsserEnabled", deEsserButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "bypass", bypassButton);
}

VocalDynamicsAudioProcessorEditor::
~VocalDynamicsAudioProcessorEditor()
{
    thumbnail.removeChangeListener(this);
}

void VocalDynamicsAudioProcessorEditor::importAudio()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose a vocal audio file",
        juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<
            VocalDynamicsAudioProcessorEditor>(this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr)
                safeThis->loadAudioFile(chooser.getResult());
        });
}

void VocalDynamicsAudioProcessorEditor::loadAudioFile(
    const juce::File& file)
{
    if (!file.existsAsFile())
        return;

    if (!thumbnail.setSource(
            new juce::FileInputSource(file)))
    {
        statusLabel.setText(
            "Could not read this audio file",
            juce::dontSendNotification);
        return;
    }

    currentAudioFile = file;

    durationLabel.setText(
        file.getFileName(),
        juce::dontSendNotification);

    statusLabel.setText(
        "Audio loaded - word analysis is the next step",
        juce::dontSendNotification);

    repaint();
}

void VocalDynamicsAudioProcessorEditor::
changeListenerCallback(
    juce::ChangeBroadcaster* source)
{
    if (source == &thumbnail)
        repaint();
}

void VocalDynamicsAudioProcessorEditor::paint(
    juce::Graphics& g)
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
    g.fillRoundedRectangle(
        30.0f, 101.0f,
        static_cast<float>(getWidth() - 60),
        2.0f, 1.0f);

    // Main waveform panel.
    juce::Rectangle<int> waveArea(
        30, 150, getWidth() - 60, 280);

    g.setColour(juce::Colour(0xff091521));
    g.fillRoundedRectangle(
        waveArea.toFloat(), 12.0f);

    g.setColour(border);
    g.drawRoundedRectangle(
        waveArea.toFloat(), 12.0f, 1.0f);

    g.setColour(juce::Colour(0xff203747));
    g.drawHorizontalLine(
        waveArea.getCentreY(),
        static_cast<float>(waveArea.getX() + 12),
        static_cast<float>(waveArea.getRight() - 12));

    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour(cyan);

        thumbnail.drawChannels(
            g,
            waveArea.reduced(12, 14),
            0.0,
            thumbnail.getTotalLength(),
            1.0f);
    }
    else
    {
        g.setColour(muted);
        g.setFont(15.0f);
        g.drawText(
            "DROP A VOCAL FILE HERE OR CLICK IMPORT VOCAL",
            waveArea.reduced(20, 20),
            juce::Justification::centred);
    }

    // Event lane placeholder: no fake word detection.
    juce::Rectangle<float> eventArea(
        30.0f, 444.0f,
        static_cast<float>(getWidth() - 60),
        112.0f);

    g.setColour(juce::Colour(0xff0c1824));
    g.fillRoundedRectangle(eventArea, 10.0f);

    g.setColour(border);
    g.drawRoundedRectangle(eventArea, 10.0f, 1.0f);

    g.setColour(purple);
    g.setFont(13.0f);
    g.drawText(
        "WORD / SYLLABLE / PHRASE EVENTS",
        eventArea.getX() + 14.0f,
        eventArea.getY() + 10.0f,
        eventArea.getWidth() - 28.0f,
        22.0f,
        juce::Justification::centredLeft);

    g.setColour(muted);
    g.setFont(12.0f);
    g.drawText(
        "Word boundaries and editable gain events will appear here "
        "after the analysis engine is implemented.",
        eventArea.reduced(14.0f).withTrimmedTop(32.0f),
        juce::Justification::centredLeft);

    // Word editing panel.
    juce::Rectangle<float> editArea(
        30.0f, 570.0f,
        static_cast<float>(getWidth() - 60),
        90.0f);

    g.setColour(juce::Colour(0xff132332));
    g.fillRoundedRectangle(editArea, 10.0f);

    g.setColour(border);
    g.drawRoundedRectangle(editArea, 10.0f, 1.0f);

    g.setColour(white);
    g.setFont(14.0f);
    g.drawText(
        "SELECTED WORD",
        48, 583, 180, 22,
        juce::Justification::centredLeft);

    g.setColour(muted);
    g.setFont(12.0f);
    g.drawText(
        "No word selected",
        48, 610, 180, 22,
        juce::Justification::centredLeft);

    g.setColour(white);
    g.setFont(14.0f);
    g.drawText("INTELLIGENT LEVELING ENGINE", 48, 580, 320, 22, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(11.0f);
    g.drawText("TARGET LOUDNESS  /  APPROXIMATE RMS-BASED METERING", 48, 608, 360, 18, juce::Justification::centredLeft);
    g.setColour(cyan);
    g.setFont(11.0f);
    g.drawText(
        "STAGE 1  |  LEVELING DSP (LUFS METER IS APPROXIMATE)",
        30, getHeight() - 25,
        getWidth() - 60, 16,
        juce::Justification::centred);
}

void VocalDynamicsAudioProcessorEditor::resized()
{
    const int width = getWidth();

    titleLabel.setBounds(30, 22, 440, 38);
    statusLabel.setBounds(32, 62, 570, 25);
    durationLabel.setBounds(32, 112, width - 64, 24);

    importButton.setBounds(width - 360, 32, 150, 42);
    analyzeButton.setBounds(width - 200, 32, 155, 42);
    playButton.setBounds(width - 360, 75, 150, 30);

    gainSlider.setBounds(width - 190, 574, 145, 80);
    targetLufsSlider.setBounds(380, 582, 340, 38);
    engineButton.setBounds(380, 628, 150, 30);
    riderButton.setBounds(540, 628, 150, 30);
    deEsserButton.setBounds(700, 628, 150, 30);
    bypassButton.setBounds(860, 628, 150, 30);
}
