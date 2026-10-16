
#pragma once

#include <JuceHeader.h>
#include <array>

class VocalDynamicsAudioProcessor
    : public juce::AudioProcessor
{
public:
    VocalDynamicsAudioProcessor();
    ~VocalDynamicsAudioProcessor() override = default;

    void prepareToPlay(double sampleRate,
                       int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(
        const BusesLayout& layouts) const override;

    void processBlock(
        juce::AudioBuffer<float>&,
        juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(
        juce::MemoryBlock& destinationData) override;

    void setStateInformation(
        const void* data, int sizeInBytes) override;

    using Parameters =
        juce::AudioProcessorValueTreeState;

    struct MeterSnapshot
    {
        float inputLufs = -100.0f;
        float outputLufs = -100.0f;
        float truePeakDbtp = -100.0f;
        float gainReductionDb = 0.0f;
    };
    MeterSnapshot getMeterSnapshot() const noexcept;

    std::atomic<float> measuredInputLufs { -100.0f };
    std::atomic<float> measuredOutputLufs { -100.0f };
    std::atomic<float> measuredTruePeakDbtp { -100.0f };
    std::atomic<float> measuredGainReductionDb { 0.0f };
    double currentSampleRate = 44100.0;
    float gainRiderDb = 0.0f;
    float smoothedCompressorGain = 1.0f;
    float deEsserEnvelope = 0.0f;
    std::array<float, 2> previousSamples {{ 0.0f, 0.0f }};
    std::array<float, 2> previousDeEssSamples {{ 0.0f, 0.0f }};
    float detectorEnvelope = 0.0f;
    double loudnessEnergySum = 0.0;
    int64_t loudnessSamples = 0;


    Parameters parameters;

private:
    juce::AudioBuffer<float> scratchBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        VocalDynamicsAudioProcessor)
};
