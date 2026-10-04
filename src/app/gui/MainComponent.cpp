#include "MainComponent.h"

#include "OfflinePipeline.h"
#include "WhisperTranscriptProvider.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

using namespace PodcastCleaner;

namespace
{

constexpr int denoiseMaxDb = 24;
constexpr int fileRowHeight = 30;

juce::File defaultOutputFor (const juce::File& input)
{
    return input.getSiblingFile (input.getFileNameWithoutExtension() + "_clean.wav");
}

/** Directories that may hold a ggml Whisper model, most specific first: the
    app bundle's Resources (an installed copy), the per-user Application
    Support folder, then every parent of the working directory and of the
    executable (a source or build tree). */
std::vector<juce::File> modelSearchDirectories()
{
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    std::vector<juce::File> directories {
        executable.getParentDirectory().getParentDirectory()
                  .getChildFile ("Resources").getChildFile ("models"),
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("Application Support")
                  .getChildFile ("Podcast Cleaner").getChildFile ("models")
    };

    const std::array<juce::File, 2> roots { juce::File::getCurrentWorkingDirectory(),
                                            executable.getParentDirectory() };

    for (auto root : roots)
    {
        for (int depth = 0; depth < 12 && root.exists(); ++depth)
        {
            directories.push_back (root.getChildFile ("models"));
            directories.push_back (root);

            const auto parent = root.getParentDirectory();
            if (parent == root)
                break;
            root = parent;
        }
    }

    return directories;
}

/** Looks for a ggml Whisper model in the bundled, per-user and build-tree
    locations found by modelSearchDirectories(). */
juce::File findDefaultModel()
{
    const auto directories = modelSearchDirectories();

    for (const auto& directory : directories)
    {
        const auto candidate = directory.getChildFile ("ggml-base.bin");
        if (candidate.existsAsFile())
            return candidate;
    }

    // Fall back to any other ggml model the user dropped in, VAD excepted.
    for (const auto& directory : directories)
    {
        if (! directory.isDirectory())
            continue;

        for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "ggml-*.bin"))
            if (! entry.getFile().getFileName().containsIgnoreCase ("silero"))
                return entry.getFile();
    }

    return {};
}

/** Silero VAD model shipped alongside the Whisper model (optional). */
juce::File findVadModelFor (const juce::File& model)
{
    if (! model.existsAsFile())
        return {};

    const auto directory = model.getParentDirectory();
    for (const char* name : { "ggml-silero-v6.2.0.bin", "ggml-silero-v5.1.2.bin" })
    {
        const auto candidate = directory.getChildFile (name);
        if (candidate.existsAsFile())
            return candidate;
    }

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*.bin"))
        if (entry.getFile().getFileName().containsIgnoreCase ("silero"))
            return entry.getFile();

    return {};
}

juce::String formatReport (const PipelineReport& report)
{
    juce::String text;
    text << "Loudness: " << juce::String (report.inputLufs, 2) << " -> "
         << juce::String (report.finalLufs, 2) << " LUFS\n";
    text << "True peak: " << juce::String (report.inputTruePeakDb, 2) << " -> "
         << juce::String (report.outputTruePeakDb, 2) << " dBFS\n";
    text << "Mastering gain: " << juce::String (report.appliedGainDb, 2)
         << " dB, limiter reduction: " << juce::String (report.limiterReductionDb, 2) << " dB\n";
    text << "Non-speech ratio: " << juce::String (report.nonSpeechRatio, 3) << "\n";
    text << "Censored ranges: " << static_cast<int> (report.censoredRanges.size()) << "\n";

    for (const auto& range : report.censoredRanges)
        text << "  [" << static_cast<juce::int64> (range.start)
             << ", " << static_cast<juce::int64> (range.end) << ") samples\n";

    if (! report.transcript.empty())
    {
        ProfanityDetector detector;
        text << "Transcript (" << static_cast<int> (report.transcript.size()) << " words):\n";
        for (const auto& word : report.transcript)
            text << (detector.isProfane (word.text) ? "  [*] " : "      ")
                 << "[" << static_cast<juce::int64> (word.startMs) << "-"
                 << static_cast<juce::int64> (word.endMs) << " ms] "
                 << juce::String (word.text) << "\n";
    }

    text << "Duration preserved: " << static_cast<juce::int64> (report.inputSamples) << " samples.";
    return text;
}

} // namespace

/** Shared state between the worker thread and the message thread. */
struct MainComponent::Job : public juce::ReferenceCountedObject
{
    std::atomic<bool> finished { false };
    std::atomic<double> progress { 0.0 };
    bool success = false;
    juce::String report;
};

MainComponent::MainComponent()
{
    setSize (640, 560);

    title.setText ("Podcast Cleaner", juce::dontSendNotification);
    title.setFont (juce::Font (24.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xfff3f5f7));
    addAndMakeVisible (title);

    subtitle.setText ("Denoise a recording and censor profanity. The duration never changes.",
                      juce::dontSendNotification);
    subtitle.setFont (juce::Font (13.0f));
    subtitle.setColour (juce::Label::textColourId, juce::Colour (0xffaab4bf));
    addAndMakeVisible (subtitle);

    const auto configureCaption = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (13.0f));
        label.setColour (juce::Label::textColourId, juce::Colour (0xffd6dce2));
        addAndMakeVisible (label);
    };

    const auto configurePath = [this] (juce::Label& label, const juce::String& placeholder)
    {
        label.setText (placeholder, juce::dontSendNotification);
        label.setFont (juce::Font (13.0f));
        label.setColour (juce::Label::textColourId, juce::Colour (0xffaab4bf));
        label.setColour (juce::Label::backgroundColourId, juce::Colour (0xff212932));
        label.setColour (juce::Label::outlineColourId, juce::Colour (0xff2f3a45));
        label.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
        label.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff212932));
        label.setJustificationType (juce::Justification::centredLeft);
        label.setMinimumHorizontalScale (0.8f);
        label.setEditable (false, true, false);
        addAndMakeVisible (label);
    };

    const auto configureButton = [this] (juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f3a45));
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe7ebef));
        addAndMakeVisible (button);
    };

    configureCaption (inputCaption, "Input file");
    configureCaption (outputCaption, "Output file");
    configurePath (inputPath, "Choose a WAV file...");
    configurePath (outputPath, "Output WAV file (auto-filled)...");

    configureButton (inputButton, "Choose...");
    configureButton (outputButton, "Choose...");
    inputButton.onClick = [this] { chooseInputFile(); };
    outputButton.onClick = [this] { chooseOutputFile(); };
    inputPath.onTextChange = [this] { maybeAutoFillOutput(); };

    censorButton.setButtonText ("Apply profanity censoring");
    censorButton.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe7ebef));
    censorButton.onClick = [this] { updateCensorControls(); };
    addAndMakeVisible (censorButton);

    configureCaption (languageCaption, "Language");
    languageBox.addItemList ({ "Auto", "Russian", "English" }, 1);
    languageBox.setSelectedId (1, juce::dontSendNotification);
    languageBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff212932));
    languageBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2f3a45));
    languageBox.setColour (juce::ComboBox::textColourId, juce::Colour (0xfff3f5f7));
    languageBox.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffaab4bf));
    addAndMakeVisible (languageBox);

    configureCaption (modelCaption, "Whisper model");
    configurePath (modelPath, "ggml model used for recognition...");
    configureButton (modelButton, "Choose...");
    modelButton.onClick = [this] { chooseModelFile(); };

    const auto detectedModel = findDefaultModel();
    if (detectedModel.existsAsFile())
    {
        modelPath.setText (detectedModel.getFullPathName(), juce::dontSendNotification);
        juce::Logger::writeToLog ("Podcast Cleaner: using speech recognition model "
                                  + detectedModel.getFullPathName());
    }
    else
    {
        juce::Logger::writeToLog ("Podcast Cleaner: no ggml speech recognition model found");
    }

    configureCaption (denoiseCaption, "Noise reduction");
    denoiseSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    denoiseSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 24);
    denoiseSlider.setRange (0.0, denoiseMaxDb, 0.5);
    denoiseSlider.setValue (12.0, juce::dontSendNotification);
    denoiseSlider.setTextValueSuffix (" dB");
    denoiseSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xfff3f5f7));
    denoiseSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff212932));
    denoiseSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff2f3a45));
    denoiseSlider.setTooltip ("0 dB turns noise reduction off. Higher values remove more noise.");
    addAndMakeVisible (denoiseSlider);

    processButton.setButtonText ("Process");
    processButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff58c6a7));
    processButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff10231d));
    processButton.onClick = [this] { startProcessing(); };
    addAndMakeVisible (processButton);

    progressBar.setPercentageDisplay (false);
    progressBar.setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xff212932));
    progressBar.setColour (juce::ProgressBar::foregroundColourId, juce::Colour (0xff58c6a7));
    progressBar.setVisible (false);
    addAndMakeVisible (progressBar);

    status.setText ("Ready.", juce::dontSendNotification);
    status.setFont (juce::Font (13.0f));
    status.setColour (juce::Label::textColourId, juce::Colour (0xffaab4bf));
    addAndMakeVisible (status);

    report.setMultiLine (true);
    report.setReadOnly (true);
    report.setScrollbarsShown (true);
    report.setCaretVisible (false);
    report.setPopupMenuEnabled (true);
    report.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    report.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff11161b));
    report.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2f3a45));
    report.setColour (juce::TextEditor::textColourId, juce::Colour (0xffcfd8e3));
    addAndMakeVisible (report);

    updateCensorControls();
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xff171c22));
    graphics.setColour (juce::Colour (0xff29323b));
    graphics.drawRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 10.0f, 1.0f);
    graphics.setColour (juce::Colour (0xff58c6a7));
    graphics.fillRoundedRectangle (24.0f, 26.0f, 4.0f, 42.0f, 2.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (24, 20);

    title.setBounds (area.removeFromTop (32));
    subtitle.setBounds (area.removeFromTop (22));
    area.removeFromTop (12);

    const auto layoutFileRow = [&area] (juce::Label& caption, juce::Label& path,
                                        juce::TextButton& button)
    {
        auto row = area.removeFromTop (fileRowHeight);
        caption.setBounds (row.removeFromLeft (96));
        button.setBounds (row.removeFromRight (96).reduced (2));
        path.setBounds (row.reduced (4, 2));
        area.removeFromTop (8);
    };

    layoutFileRow (inputCaption, inputPath, inputButton);
    layoutFileRow (outputCaption, outputPath, outputButton);
    area.removeFromTop (4);

    censorButton.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);

    if (censorButton.getToggleState())
    {
        auto row = area.removeFromTop (fileRowHeight);
        languageCaption.setBounds (row.removeFromLeft (96));
        languageBox.setBounds (row.removeFromLeft (130).reduced (2, 4));
        row.removeFromLeft (10);
        modelCaption.setBounds (row.removeFromLeft (104));
        modelButton.setBounds (row.removeFromRight (96).reduced (2));
        modelPath.setBounds (row.reduced (4, 2));
        area.removeFromTop (8);
    }

    auto denoiseRow = area.removeFromTop (34);
    denoiseCaption.setBounds (denoiseRow.removeFromLeft (120));
    denoiseSlider.setBounds (denoiseRow);
    area.removeFromTop (10);

    auto processRow = area.removeFromTop (36);
    processButton.setBounds (processRow.removeFromLeft (170));
    processRow.removeFromLeft (12);
    progressBar.setBounds (processRow.reduced (0, 10));

    area.removeFromTop (12);
    status.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (6);
    report.setBounds (area);
}

void MainComponent::updateCensorControls()
{
    const bool enabled = censorButton.getToggleState();
    languageCaption.setVisible (enabled);
    languageBox.setVisible (enabled);
    modelCaption.setVisible (enabled);
    modelPath.setVisible (enabled);
    modelButton.setVisible (enabled);
    resized();
}

void MainComponent::maybeAutoFillOutput()
{
    if (outputPath.getText().isNotEmpty())
        return;

    const juce::File input (inputPath.getText());
    if (input.existsAsFile())
        outputPath.setText (defaultOutputFor (input).getFullPathName(), juce::dontSendNotification);
}

void MainComponent::chooseInputFile()
{
    const juce::File start (inputPath.getText());
    chooser = std::make_unique<juce::FileChooser> (
        "Select the input audio",
        start.existsAsFile() ? start : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav");

    const juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fileChooser)
                          {
                              if (safe.getComponent() == nullptr)
                                  return;

                              const auto file = fileChooser.getResult();
                              if (! file.existsAsFile())
                                  return;

                              safe->inputPath.setText (file.getFullPathName(),
                                                       juce::dontSendNotification);
                              safe->maybeAutoFillOutput();
                              safe->showStatus ("Input: " + file.getFileName(), false);
                          });
}

void MainComponent::chooseOutputFile()
{
    const juce::File start (outputPath.getText());
    const auto initial = start.getFullPathName().isNotEmpty()
        ? start
        : defaultOutputFor (juce::File (inputPath.getText()));

    chooser = std::make_unique<juce::FileChooser> ("Save the cleaned audio as", initial, "*.wav");

    const juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fileChooser)
                          {
                              if (safe.getComponent() == nullptr)
                                  return;

                              auto file = fileChooser.getResult();
                              if (file == juce::File())
                                  return;

                              if (! file.hasFileExtension ("wav"))
                                  file = file.withFileExtension ("wav");

                              safe->outputPath.setText (file.getFullPathName(),
                                                        juce::dontSendNotification);
                          });
}

void MainComponent::chooseModelFile()
{
    const juce::File start (modelPath.getText());
    chooser = std::make_unique<juce::FileChooser> (
        "Select a ggml Whisper model",
        start.existsAsFile() ? start : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.bin");

    const juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fileChooser)
                          {
                              if (safe.getComponent() == nullptr)
                                  return;

                              const auto file = fileChooser.getResult();
                              if (file.existsAsFile())
                                  safe->modelPath.setText (file.getFullPathName(),
                                                           juce::dontSendNotification);
                          });
}

void MainComponent::timerCallback()
{
    if (currentJob == nullptr)
    {
        stopTimer();
        return;
    }

    progressValue = currentJob->progress.load();

    if (! currentJob->finished.load())
        return;

    stopTimer();

    const bool success = currentJob->success;
    const juce::String text = currentJob->report;
    currentJob = nullptr;

    report.setText (text);
    setBusy (false);
    showStatus (success ? "Done." : "Failed - see the report below.", ! success);
}

void MainComponent::startProcessing()
{
    if (currentJob != nullptr)
        return;

    const juce::File input (inputPath.getText());
    if (! input.existsAsFile())
    {
        showStatus ("Choose an existing input WAV file first.", true);
        return;
    }

    if (outputPath.getText().isEmpty())
        maybeAutoFillOutput();

    juce::File output (outputPath.getText());
    if (output.getFullPathName().isEmpty())
    {
        showStatus ("Choose an output file first.", true);
        return;
    }

    if (! output.hasFileExtension ("wav"))
        output = output.withFileExtension ("wav");

    if (output == input)
    {
        showStatus ("The output file must differ from the input.", true);
        return;
    }

    const bool censor = censorButton.getToggleState();
    const juce::File model (modelPath.getText());
    if (censor && ! model.existsAsFile())
    {
        showStatus ("Censoring needs a ggml Whisper model - choose one.", true);
        return;
    }

    PipelineOptions options;
    const float reductionDb = static_cast<float> (denoiseSlider.getValue());
    options.denoise = reductionDb > 0.0f;
    options.denoiseSettings.reductionDb = reductionDb;
    options.censor = censor;
    options.censorMode = CensorMode::Mute;

    const auto languageText = languageBox.getText();
    const std::string language = languageText == "Russian" ? "ru"
                              : languageText == "English" ? "en"
                                                          : "auto";
    const juce::File vadModel = findVadModelFor (model);

    JobPtr job (new Job());
    currentJob = job;
    job->progress = 0.05;

    report.clear();
    progressValue = 0.05;
    setBusy (true);
    showStatus (censor ? "Transcribing and cleaning..." : "Cleaning...", false);
    startTimerHz (10);

    juce::Thread::launch ([job, input, output, options, censor, model, vadModel, language]
    {
        AudioData audio;
        std::string error;

        if (! readWav (input.getFullPathName().toStdString(), audio, error))
        {
            job->report = "Could not read the input file:\n" + juce::String (error);
            job->finished = true;
            return;
        }

        if (audio.getNumSamples() == 0)
        {
            job->report = "The input file contains no audio samples.";
            job->finished = true;
            return;
        }

        job->progress = 0.15;

        std::unique_ptr<WhisperTranscriptProvider> recogniser;
        if (censor && model.existsAsFile())
        {
            WhisperTranscriptProvider::Options whisperOptions;
            whisperOptions.modelPath = model.getFullPathName().toStdString();
            whisperOptions.vadModelPath = vadModel.existsAsFile()
                ? vadModel.getFullPathName().toStdString() : std::string();
            whisperOptions.language = language;
            whisperOptions.useVad = ! whisperOptions.vadModelPath.empty();

            recogniser = std::make_unique<WhisperTranscriptProvider> (whisperOptions);
            if (! recogniser->isReady())
            {
                job->report = "Could not load the Whisper model:\n" + model.getFullPathName();
                job->finished = true;
                return;
            }
        }

        job->progress = 0.35;

        OfflinePipeline pipeline;
        const PipelineReport result = pipeline.run (audio, options, recogniser.get());

        job->progress = 0.9;

        if (! writeWav (output.getFullPathName().toStdString(), audio, 16, error))
        {
            job->report = "Could not write the output file:\n" + juce::String (error);
            job->finished = true;
            return;
        }

        juce::String text = formatReport (result);
        text << "\nOutput: " << output.getFullPathName();
        if (result.inputSamples != result.outputSamples)
            text << "\n\nERROR: the sample count changed ("
                 << static_cast<juce::int64> (result.inputSamples) << " -> "
                 << static_cast<juce::int64> (result.outputSamples) << ").";

        job->report = text;
        job->success = true;
        job->progress = 1.0;
        job->finished = true;
    });
}

void MainComponent::setBusy (bool busy)
{
    processButton.setEnabled (! busy);
    processButton.setButtonText (busy ? "Processing..." : "Process");
    inputButton.setEnabled (! busy);
    outputButton.setEnabled (! busy);
    modelButton.setEnabled (! busy);
    censorButton.setEnabled (! busy);
    languageBox.setEnabled (! busy);
    denoiseSlider.setEnabled (! busy);
    progressBar.setVisible (busy);
}

void MainComponent::showStatus (const juce::String& message, bool isError)
{
    status.setText (message, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId,
                      isError ? juce::Colour (0xffe06c75) : juce::Colour (0xffaab4bf));
}




