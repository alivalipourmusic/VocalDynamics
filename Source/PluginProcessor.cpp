
#include "PluginProcessor.h"
#include "PluginEditor.h"

static VocalDynamicsAudioProcessor::Parameters::ParameterLayout
createParameterLayout()
{
    using namespace juce;

    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{"gain", 1},
        "Gain",
        NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        "dB"));

    params.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{"threshold", 1},
        "Silence Threshold",
        NormalisableRange<float>(-90.0f, -20.0f, 1.0f),
        -60.0f,
        "dB"));

    params.push_back(std::make_unique<AudioParameterBool>(
        ParameterID{"gateEnabled", 1},
        "Silence Gate",
        false));

    return {params.begin(), params.end()};
}

VocalDynamicsAudioProcessor::VocalDynamicsAudioProcessor()
    : AudioProcessor(
        BusesProperties()
            .withInput("Input", AudioChannelSet::stereo(), true)
            .withOutput("Output", AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS",
                 createParameterLayout())
{
}

const juce::String VocalDynamicsAudioProcessor::getName() const
{
    return "Vocal Dynamics";
}

void VocalDynamicsAudioProcessor::prepareToPlay(double, int) {}
void VocalDynamicsAudioProcessor::releaseResources() {}

bool VocalDynamicsAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    return input == output
        && (output == juce::AudioChannelSet::mono()
            || output == juce::AudioChannelSet::stereo());
}

void VocalDynamicsAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float gainDb =
        parameters.getRawParameterValue("gain")->load();

    const float thresholdDb =
        parameters.getRawParameterValue("threshold")->load();

    const bool gateEnabled =
        parameters.getRawParameterValue("gateEnabled")->load() > 0.5f;

    const float gain = juce::Decibels::decibelsToGain(gainDb);
    const float threshold =
        juce::Decibels::decibelsToGain(thresholdDb);

    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();

    for (int channel = inputChannels;
         channel < outputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    for (int channel = 0; channel < inputChannels; ++channel)
    {
        float* data = buffer.getWritePointer(channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float value = data[sample];

            if (gateEnabled && std::abs(value) < threshold)
                value = 0.0f;

            data[sample] = value * gain;
        }
    }
}

juce::AudioProcessorEditor*
VocalDynamicsAudioProcessor::createEditor()
{
    return new VocalDynamicsAudioProcessorEditor(*this);
}

void VocalDynamicsAudioProcessor::getStateInformation(
    juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destinationData);
}

void VocalDynamicsAudioProcessor::setStateInformation(
    const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(
        getXmlFromBinary(data, sizeInBytes));

    if (xml != nullptr &&
        xml->hasTagName(parameters.state.getType()))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalDynamicsAudioProcessor();
}
