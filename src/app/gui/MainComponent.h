#pragma once

#include <JuceHeader.h>

#include <memory>

/** Content of the Podcast Cleaner GUI window.

    The workflow is deliberately short: pick an input file, pick an output file,
    decide whether to censor profanity, set how much noise to remove and press
    Process. The pipeline runs on a background thread so the window stays
    responsive, and the full report is shown when the run finishes. */
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Job;
    using JobPtr = juce::ReferenceCountedObjectPtr<Job>;

    void chooseInputFile();
    void chooseOutputFile();
    void chooseModelFile();
    void updateCensorControls();
    void maybeAutoFillOutput();
    void startProcessing();
    void setBusy (bool busy);
    void showStatus (const juce::String& message, bool isError);
    void timerCallback() override;

    juce::Label title;
    juce::Label subtitle;

    juce::Label inputCaption;
    juce::Label inputPath;
    juce::TextButton inputButton;

    juce::Label outputCaption;
    juce::Label outputPath;
    juce::TextButton outputButton;

    juce::ToggleButton censorButton;

    juce::Label languageCaption;
    juce::ComboBox languageBox;

    juce::Label modelCaption;
    juce::Label modelPath;
    juce::TextButton modelButton;

    juce::Label denoiseCaption;
    juce::Slider denoiseSlider;

    juce::ToggleButton silenceButton;

    juce::TextButton processButton;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };

    juce::Label status;
    juce::TextEditor report;

    std::unique_ptr<juce::FileChooser> chooser;
    JobPtr currentJob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};