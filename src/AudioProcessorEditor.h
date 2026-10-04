#pragma once

#include "AudioProcessor.h"

#include <array>
#include <memory>

class PodcastCleanerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PodcastCleanerAudioProcessorEditor(PodcastCleanerAudioProcessor&);
    ~PodcastCleanerAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    PodcastCleanerAudioProcessor& podcastProcessor;
    juce::Label title;
    juce::Label subtitle;
    juce::Label gateLabel;
    juce::Label ratioLabel;
    juce::Label outputGainLabel;
    juce::Label status;
    std::array<juce::ToggleButton, 5> stageButtons;
    juce::Slider gateThreshold;
    juce::Slider compressionRatio;
    juce::Slider outputGain;
    std::array<std::unique_ptr<ButtonAttachment>, 5> buttonAttachments;
    std::unique_ptr<SliderAttachment> gateThresholdAttachment;
    std::unique_ptr<SliderAttachment> compressionRatioAttachment;
    std::unique_ptr<SliderAttachment> outputGainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PodcastCleanerAudioProcessorEditor)
};
