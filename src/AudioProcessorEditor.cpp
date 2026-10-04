#include "AudioProcessorEditor.h"

PodcastCleanerAudioProcessorEditor::PodcastCleanerAudioProcessorEditor(
    PodcastCleanerAudioProcessor& audioProcessor)
    : AudioProcessorEditor(audioProcessor), podcastProcessor(audioProcessor)
{
    setSize(620, 400);
    setResizable(true, true);
    setResizeLimits(480, 340, 900, 600);

    title.setText("Podcast Cleaner", juce::dontSendNotification);
    title.setFont(juce::Font(25.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colour(0xfff3f5f7));
    addAndMakeVisible(title);

    subtitle.setText("Streaming cleanup for mono and stereo tracks", juce::dontSendNotification);
    subtitle.setFont(juce::Font(14.0f));
    subtitle.setColour(juce::Label::textColourId, juce::Colour(0xffaab4bf));
    addAndMakeVisible(subtitle);

    constexpr std::array<const char*, 5> buttonNames{
        "High-pass 80 Hz", "Noise gate", "Compressor", "Podcast EQ", "Peak limiter"};
    constexpr std::array<const char*, 5> parameterIds{
        "hpf", "gate", "compressor", "eq", "limiter"};

    for (size_t index = 0; index < stageButtons.size(); ++index)
    {
        auto& button = stageButtons[index];
        button.setButtonText(buttonNames[index]);
        button.setClickingTogglesState(true);
        button.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffe7ebef));
        addAndMakeVisible(button);
        buttonAttachments[index] =
            std::make_unique<ButtonAttachment>(podcastProcessor.parameters, parameterIds[index], button);
    }

    const auto configureSlider = [this](juce::Slider& slider, juce::Label& label,
                                    const juce::String& labelText)
    {
        label.setText(labelText, juce::dontSendNotification);
        label.setFont(juce::Font(14.0f));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffd6dce2));
        addAndMakeVisible(label);

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xfff3f5f7));
        addAndMakeVisible(slider);
    };

    configureSlider(gateThreshold, gateLabel, "Gate threshold");
    configureSlider(compressionRatio, ratioLabel, "Compression ratio");
    configureSlider(outputGain, outputGainLabel, "Output gain");
    gateThresholdAttachment =
        std::make_unique<SliderAttachment>(podcastProcessor.parameters, "gateThreshold", gateThreshold);
    compressionRatioAttachment =
        std::make_unique<SliderAttachment>(podcastProcessor.parameters, "compressionRatio", compressionRatio);
    outputGainAttachment =
        std::make_unique<SliderAttachment>(podcastProcessor.parameters, "outputGain", outputGain);

    status.setText("In-place processing preserves the sample count. ML and full-track LUFS are not included.",
                   juce::dontSendNotification);
    status.setFont(juce::Font(12.0f));
    status.setColour(juce::Label::textColourId, juce::Colour(0xffaab4bf));
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);
}

void PodcastCleanerAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff171c22));
    graphics.setColour(juce::Colour(0xff29323b));
    graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 10.0f, 1.0f);
    graphics.setColour(juce::Colour(0xff58c6a7));
    graphics.fillRoundedRectangle(24.0f, 26.0f, 4.0f, 42.0f, 2.0f);
}

void PodcastCleanerAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(28, 20);
    title.setBounds(bounds.removeFromTop(36));
    subtitle.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);

    const int buttonWidth = (bounds.getWidth() - 16) / 3;
    for (size_t index = 0; index < stageButtons.size(); ++index)
    {
        const int row = static_cast<int>(index) / 3;
        const int column = static_cast<int>(index) % 3;
        stageButtons[index].setBounds(
            bounds.getX() + column * (buttonWidth + 8),
            bounds.getY() + row * 42,
            buttonWidth,
            34);
    }
    bounds.removeFromTop(96);

    const int labelWidth = 155;
    const int controlHeight = 42;
    const auto placeControl = [&](juce::Label& label, juce::Slider& slider)
    {
        label.setBounds(bounds.getX(), bounds.getY(), labelWidth, controlHeight);
        slider.setBounds(bounds.getX() + labelWidth, bounds.getY(),
                         bounds.getWidth() - labelWidth, controlHeight);
        bounds.removeFromTop(controlHeight + 4);
    };

    placeControl(gateLabel, gateThreshold);
    placeControl(ratioLabel, compressionRatio);
    placeControl(outputGainLabel, outputGain);

    status.setBounds(bounds.getX(), getHeight() - 42, bounds.getWidth(), 24);
}
