
#include "PluginEditor.h"

VocalDynamicsAudioProcessorEditor::
VocalDynamicsAudioProcessorEditor(
    VocalDynamicsAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      audioProcessor(processor)
{
    setSize(760, 430);

    titleLabel.setText(
        "VOCAL DYNAMICS",
        juce::dontSendNotification);
    titleLabel.setFont(
        juce::Font(27.0f, juce::Font::bold));
    titleLabel.setColour(
        juce::Label::textColourId,
        juce::Colour(0xffeaf7ff));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText(
        "VOCAL CONTROL / VERSION 0.1",
        juce::dontSendNotification);
    subtitleLabel.setFont(juce::Font(12.0f));
    subtitleLabel.setColour(
        juce::Label::textColourId,
        juce::Colour(0xff55dff2));
    addAndMakeVisible(subtitleLabel);

    auto setupSlider = [](juce::Slider& slider)
    {
        slider.setSliderStyle(
            juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(
            juce::Slider::TextBoxBelow,
            false, 100, 24);
        slider.setColour(
            juce::Slider::rotarySliderFillColourId,
            juce::Colour(0xff35d5e8));
        slider.setColour(
            juce::Slider::thumbColourId,
            juce::Colour(0xffeafaff));
        slider.setColour(
            juce::Slider::textBoxTextColourId,
            juce::Colours::white);
        slider.setColour(
            juce::Slider::textBoxBackgroundColourId,
            juce::Colour(0xff172633));
        slider.setColour(
            juce::Slider::textBoxOutlineColourId,
            juce::Colours::transparentBlack);
    };

    setupSlider(gainSlider);
    setupSlider(thresholdSlider);

    gainLabel.setText(
        "OUTPUT GAIN",
        juce::dontSendNotification);
    thresholdLabel.setText(
        "SILENCE THRESHOLD",
        juce::dontSendNotification);

    for (auto* label : {&gainLabel, &thresholdLabel})
    {
        label->setFont(
            juce::Font(14.0f, juce::Font::bold));
        label->setColour(
            juce::Label::textColourId,
            juce::Colour(0xffb6cbd8));
        label->setJustificationType(
            juce::Justification::centred);
        addAndMakeVisible(*label);
    }

    addAndMakeVisible(gainSlider);
    addAndMakeVisible(thresholdSlider);

    gateButton.setButtonText("Enable Silence Gate");
    gateButton.setColour(
        juce::ToggleButton::textColourId,
        juce::Colour(0xffeaf7ff));
    gateButton.setColour(
        juce::ToggleButton::tickColourId,
        juce::Colour(0xff35d5e8));
    addAndMakeVisible(gateButton);

    gainAttachment =
        std::make_unique<SliderAttachment>(
            audioProcessor.parameters,
            "gain", gainSlider);

    thresholdAttachment =
        std::make_unique<SliderAttachment>(
            audioProcessor.parameters,
            "threshold", thresholdSlider);

    gateAttachment =
        std::make_unique<ButtonAttachment>(
            audioProcessor.parameters,
            "gateEnabled", gateButton);
}

void VocalDynamicsAudioProcessorEditor::paint(
    juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0a111b));

    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xff111e2c));
    g.fillRoundedRectangle(
        bounds.reduced(14.0f), 16.0f);

    g.setColour(juce::Colour(0xff243747));
    g.drawRoundedRectangle(
        bounds.reduced(14.0f), 16.0f, 1.0f);

    g.setColour(juce::Colour(0xff35d5e8));
    g.fillRoundedRectangle(
        28.0f, 100.0f,
        getWidth() - 56.0f, 2.0f, 1.0f);

    g.setColour(juce::Colour(0xff162b3b));
    g.fillRoundedRectangle(
        30.0f, 125.0f,
        getWidth() - 60.0f, 220.0f, 12.0f);

    g.setColour(juce::Colour(0xff294253));
    g.drawRoundedRectangle(
        30.0f, 125.0f,
        getWidth() - 60.0f, 220.0f, 12.0f, 1.0f);

    g.setColour(juce::Colour(0xff7892a3));
    g.setFont(12.0f);
    g.drawText(
        "Gain control and basic low-level gating",
        32, 385, getWidth() - 64, 22,
        juce::Justification::centred);
}

void VocalDynamicsAudioProcessorEditor::resized()
{
    titleLabel.setBounds(30, 24, 360, 38);
    subtitleLabel.setBounds(32, 64, 350, 24);

    gainLabel.setBounds(90, 145, 230, 28);
    gainSlider.setBounds(95, 178, 220, 140);

    thresholdLabel.setBounds(405, 145, 260, 28);
    thresholdSlider.setBounds(425, 178, 220, 140);

    gateButton.setBounds(210, 342, 340, 28);
}
