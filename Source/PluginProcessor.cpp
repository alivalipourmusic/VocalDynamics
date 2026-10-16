#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <vector>

static VocalDynamicsAudioProcessor::Parameters::ParameterLayout createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;
    params.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"gain", 1}, "Gain",
        NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, "dB"));
    params.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"threshold", 1}, "Silence Threshold",
        NormalisableRange<float>(-90.0f, -20.0f, 1.0f), -60.0f, "dB"));
    params.push_back(std::make_unique<AudioParameterBool>(ParameterID{"gateEnabled", 1}, "Silence Gate", false));
    params.push_back(std::make_unique<AudioParameterBool>(ParameterID{"bypass", 1}, "Bypass", false));
    params.push_back(std::make_unique<AudioParameterBool>(ParameterID{"levelingEnabled", 1}, "Intelligent Leveling", true));
    params.push_back(std::make_unique<AudioParameterBool>(ParameterID{"gainRiding", 1}, "Automatic Gain Riding", true));
    params.push_back(std::make_unique<AudioParameterBool>(ParameterID{"deEsserEnabled", 1}, "De-esser", true));
    params.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"targetLufs", 1}, "Target Loudness",
        NormalisableRange<float>(-24.0f, -9.0f, 0.1f), -14.0f, "LUFS"));
    params.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"compressorAmount", 1}, "Compression",
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.35f));
    params.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"deEsserAmount", 1}, "De-esser Amount",
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.30f));
    params.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"outputCeiling", 1}, "Output Ceiling",
        NormalisableRange<float>(-6.0f, -0.1f, 0.1f), -1.0f, "dBTP"));
    return { params.begin(), params.end() };
}

VocalDynamicsAudioProcessor::VocalDynamicsAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", AudioChannelSet::stereo(), true)
        .withOutput("Output", AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

const juce::String VocalDynamicsAudioProcessor::getName() const { return "Vocal Dynamics"; }

void VocalDynamicsAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);
    gainRiderDb = 0.0f;
    smoothedCompressorGain = 1.0f;
    deEsserEnvelope = 0.0f;
    previousSamples = {{0.0f, 0.0f}};
    previousDeEssSamples = {{0.0f, 0.0f}};
    detectorEnvelope = 0.0f;
    loudnessEnergySum = 0.0;
    loudnessSamples = 0;
}

void VocalDynamicsAudioProcessor::releaseResources() {}

bool VocalDynamicsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

VocalDynamicsAudioProcessor::MeterSnapshot VocalDynamicsAudioProcessor::getMeterSnapshot() const noexcept
{
    MeterSnapshot m;
    m.inputLufs = measuredInputLufs.load(std::memory_order_relaxed);
    m.outputLufs = measuredOutputLufs.load(std::memory_order_relaxed);
    m.truePeakDbtp = measuredTruePeakDbtp.load(std::memory_order_relaxed);
    m.gainReductionDb = measuredGainReductionDb.load(std::memory_order_relaxed);
    return m;
}

void VocalDynamicsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();
    const int samples = buffer.getNumSamples();
    for (int ch = inputChannels; ch < outputChannels; ++ch)
        buffer.clear(ch, 0, samples);

    const auto param = [this](const char* id, float fallback = 0.0f)
    {
        if (auto* p = parameters.getRawParameterValue(id)) return p->load(std::memory_order_relaxed);
        return fallback;
    };
    const bool bypass = param("bypass") > 0.5f;
    if (bypass) { measuredTruePeakDbtp.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, samples)), std::memory_order_relaxed); return; }
    const bool enabled = param("levelingEnabled", 1.0f) > 0.5f;
    const bool riderEnabled = param("gainRiding", 1.0f) > 0.5f;
    const bool deEssEnabled = param("deEsserEnabled", 1.0f) > 0.5f;
    const bool gateEnabled = param("gateEnabled") > 0.5f;
    const float manualGain = juce::Decibels::decibelsToGain(param("gain"));
    const float gateThreshold = juce::Decibels::decibelsToGain(param("threshold", -60.0f));
    const float targetLufs = param("targetLufs", -14.0f);
    const float compression = param("compressorAmount", 0.35f);
    const float deEssAmount = param("deEsserAmount", 0.30f);
    const float ceiling = juce::Decibels::decibelsToGain(param("outputCeiling", -1.0f));

    const float riderAttack = std::exp(-1.0f / static_cast<float>(0.035 * currentSampleRate));
    const float riderRelease = std::exp(-1.0f / static_cast<float>(0.240 * currentSampleRate));
    const float detectorAttack = std::exp(-1.0f / static_cast<float>(0.025 * currentSampleRate));
    const float detectorRelease = std::exp(-1.0f / static_cast<float>(0.180 * currentSampleRate));
    const float compAttack = std::exp(-1.0f / static_cast<float>(0.008 * currentSampleRate));
    const float compRelease = std::exp(-1.0f / static_cast<float>(0.100 * currentSampleRate));
    const float essRelease = std::exp(-1.0f / static_cast<float>(0.045 * currentSampleRate));
    double inputEnergy = 0.0, outputEnergy = 0.0;
    float blockTruePeak = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        float detectorEnergy = 0.0f;
        for (int ch = 0; ch < inputChannels; ++ch)
        {
            const float x = buffer.getReadPointer(ch)[i];
            detectorEnergy += x * x;
            inputEnergy += static_cast<double>(x) * x;
            const size_t channel = static_cast<size_t>(juce::jmin(ch, 1));
            const float previous = previousSamples[channel];
            blockTruePeak = juce::jmax(blockTruePeak, std::abs(x));
            for (int phase = 1; phase < 4; ++phase)
            {
                const float fraction = static_cast<float>(phase) * 0.25f;
                blockTruePeak = juce::jmax(blockTruePeak, std::abs(previous + (x - previous) * fraction));
            }
            previousSamples[channel] = x;
        }
        const float instantLevel = std::sqrt(detectorEnergy / static_cast<float>(juce::jmax(1, inputChannels)));
        const float detectorCoefficient = instantLevel > detectorEnvelope ? detectorAttack : detectorRelease;
        detectorEnvelope = instantLevel + detectorCoefficient * (detectorEnvelope - instantLevel);
        const float level = detectorEnvelope;

        if (enabled && riderEnabled)
        {
            // Short-term RMS-style riding with capped correction and smoothed gain movement.
            const float desiredDb = juce::jlimit(-9.0f, 9.0f,
                targetLufs - (juce::Decibels::gainToDecibels(juce::jmax(level, 1.0e-5f)) - 0.691f));
            const float coefficient = desiredDb > gainRiderDb ? riderAttack : riderRelease;
            gainRiderDb = desiredDb + coefficient * (gainRiderDb - desiredDb);
        }
        else gainRiderDb = 0.0f;

        const float threshold = juce::Decibels::decibelsToGain(-18.0f);
        const float above = juce::jmax(0.0f, level - threshold);
        const float ratio = 1.0f + compression * 2.0f;
        const float compressed = threshold + above / ratio;
        const float targetCompGain = level > threshold ? compressed / juce::jmax(level, 1.0e-5f) : 1.0f;
        const float compCoefficient = targetCompGain < smoothedCompressorGain ? compAttack : compRelease;
        smoothedCompressorGain = targetCompGain + compCoefficient * (smoothedCompressorGain - targetCompGain);

        const float autoGain = enabled ? juce::Decibels::decibelsToGain(gainRiderDb) * smoothedCompressorGain : 1.0f;

        for (int ch = 0; ch < inputChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            float value = data[i];
            if (gateEnabled && std::abs(value) < gateThreshold) value = 0.0f;

            if (deEssEnabled && deEssAmount > 0.0f)
            {
                // Lightweight high-frequency transient detector. This is a basic
                // de-essing stage, not a multiband or phoneme-aware de-esser.
                const size_t channel = static_cast<size_t>(juce::jmin(ch, 1));
                const float high = value - previousDeEssSamples[channel];
                previousDeEssSamples[channel] = value;
                deEsserEnvelope = juce::jmax(std::abs(high), essRelease * deEsserEnvelope);
                const float reduction = 1.0f - juce::jlimit(0.0f, 0.35f,
                    deEssAmount * juce::jmax(0.0f, deEsserEnvelope - 0.035f) * 2.0f);
                value *= reduction;
            }

            value *= manualGain * autoGain;
            value = juce::jlimit(-ceiling, ceiling, value);
            data[i] = value;
            outputEnergy += static_cast<double>(value) * value;
        }
    }

    // Meter values are RMS-derived loudness estimates, not standards-compliant LUFS.
    loudnessEnergySum += inputEnergy;
    loudnessSamples += static_cast<int64_t>(samples) * juce::jmax(1, inputChannels);
    const double windowSeconds = static_cast<double>(loudnessSamples) /
        (currentSampleRate * juce::jmax(1, inputChannels));
    if (windowSeconds >= 3.0)
    {
        const float rms = static_cast<float>(std::sqrt(loudnessEnergySum / juce::jmax<int64_t>(1, loudnessSamples)));
        measuredInputLufs.store(juce::Decibels::gainToDecibels(rms) - 0.691f, std::memory_order_relaxed);
        measuredOutputLufs.store(juce::Decibels::gainToDecibels(
            static_cast<float>(std::sqrt(outputEnergy / juce::jmax(1, samples * inputChannels)))) - 0.691f,
            std::memory_order_relaxed);
        loudnessEnergySum = 0.0;
        loudnessSamples = 0;
    }
    measuredTruePeakDbtp.store(juce::Decibels::gainToDecibels(blockTruePeak), std::memory_order_relaxed);
    measuredGainReductionDb.store(juce::Decibels::gainToDecibels(juce::jmax(smoothedCompressorGain, 1.0e-5f)),
        std::memory_order_relaxed);
}

juce::AudioProcessorEditor* VocalDynamicsAudioProcessor::createEditor()
{
    return new VocalDynamicsAudioProcessorEditor(*this);
}

void VocalDynamicsAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destinationData);
}

void VocalDynamicsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalDynamicsAudioProcessor();
}
