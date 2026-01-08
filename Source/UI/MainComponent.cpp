#include "MainComponent.h"
#include "../Utils/Constants.h"
#include "../Utils/MelSpectrogram.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#elif JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <dlfcn.h>
#endif

static juce::File getRuntimeBinaryDir()
{
#if defined(JucePlugin_Build_VST3) || defined(JucePlugin_Build_AU) || defined(JucePlugin_Build_AUv3) || defined(JucePlugin_Build_LV2)
   #if JUCE_WINDOWS
    HMODULE moduleHandle = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&getRuntimeBinaryDir),
                       &moduleHandle);

    wchar_t path[MAX_PATH]{};
    if (moduleHandle != nullptr && GetModuleFileNameW(moduleHandle, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0]))) > 0)
        return juce::File(juce::String(path)).getParentDirectory();
   #elif JUCE_MAC || JUCE_LINUX || JUCE_BSD
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&getRuntimeBinaryDir), &info) != 0 && info.dli_fname != nullptr)
        return juce::File(juce::String(info.dli_fname)).getParentDirectory();
   #endif
#endif

    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
}

MainComponent::MainComponent(bool enableAudioDevice)
    : enableAudioDeviceFlag(enableAudioDevice)
{
    DBG("MainComponent: Starting initialization...");
    setSize(1400, 900);
    
    DBG("MainComponent: Creating project and engines...");
    // Initialize components
    project = std::make_unique<Project>();
    if (enableAudioDeviceFlag)
        audioEngine = std::make_unique<AudioEngine>();
    pitchDetector = std::make_unique<PitchDetector>();
    fcpePitchDetector = std::make_unique<FCPEPitchDetector>();
    vocoder = std::make_unique<Vocoder>();
    undoManager = std::make_unique<PitchUndoManager>(100);
    
    DBG("MainComponent: Looking for FCPE model...");
    // Try to load FCPE model
    auto modelsDir = getRuntimeBinaryDir().getChildFile("models");
    
    auto fcpeModelPath = modelsDir.getChildFile("fcpe.onnx");
    auto melFilterbankPath = modelsDir.getChildFile("mel_filterbank.bin");
    auto centTablePath = modelsDir.getChildFile("cent_table.bin");
    
    if (fcpeModelPath.existsAsFile())
    {
        if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath, centTablePath))
        {
            DBG("FCPE pitch detector loaded successfully");
            useFCPE = true;
        }
        else
        {
            DBG("Failed to load FCPE model, falling back to YIN");
            useFCPE = false;
        }
    }
    else
    {
        DBG("FCPE model not found at: " + fcpeModelPath.getFullPathName());
        DBG("Using YIN pitch detector as fallback");
        useFCPE = false;
    }
    
    // Load vocoder settings
    applySettings();
    
    DBG("MainComponent: Initializing audio...");
    // Initialize audio (standalone app only)
    if (audioEngine)
        audioEngine->initializeAudio();
    
    DBG("MainComponent: Adding child components...");
    // Add child components
    addAndMakeVisible(toolbar);
    addAndMakeVisible(pianoRoll);
    addAndMakeVisible(waveform);
    addAndMakeVisible(parameterPanel);
    
    // Set undo manager for piano roll
    pianoRoll.setUndoManager(undoManager.get());
    
    DBG("MainComponent: Setting up callbacks...");
    // Setup toolbar callbacks
    toolbar.onOpenFile = [this]() { openFile(); };
    toolbar.onExportFile = [this]() { exportFile(); };
    toolbar.onPlay = [this]() { play(); };
    toolbar.onPause = [this]() { pause(); };
    toolbar.onStop = [this]() { stop(); };
    toolbar.onResynthesize = [this]() { resynthesize(); };
    toolbar.onSettings = [this]() { showSettings(); };
    toolbar.onZoomChanged = [this](float pps) { onZoomChanged(pps); };
    toolbar.onEditModeChanged = [this](EditMode mode) { setEditMode(mode); };
    
    // Setup piano roll callbacks
    pianoRoll.onSeek = [this](double time) { seek(time); };
    pianoRoll.onNoteSelected = [this](Note* note) { onNoteSelected(note); };
    pianoRoll.onPitchEdited = [this]() { onPitchEdited(); };
    pianoRoll.onPitchEditFinished = [this]() { resynthesizeIncremental(); };
    pianoRoll.onZoomChanged = [this](float pps) { onZoomChanged(pps); };
    pianoRoll.onScrollChanged = [this](double x) { onPianoRollScrollChanged(x); };
    
    // Setup waveform callbacks
    waveform.onSeek = [this](double time) { seek(time); };
    waveform.onZoomChanged = [this](float pps) { onZoomChanged(pps); };
    waveform.onScrollChanged = [this](double x) { onScrollChanged(x); };
    
    // Setup parameter panel callbacks
    parameterPanel.onParameterChanged = [this]() { onPitchEdited(); };
    parameterPanel.onParameterEditFinished = [this]() { resynthesizeIncremental(); };
    parameterPanel.onGlobalPitchChanged = [this]() { 
        pianoRoll.repaint();  // Update display
    };
    parameterPanel.setProject(project.get());
    
    DBG("MainComponent: Setting up audio engine callbacks...");
    // Setup audio engine callbacks
    if (audioEngine)
    {
        audioEngine->setPositionCallback([this](double position)
        {
            juce::MessageManager::callAsync([this, position]()
            {
                pianoRoll.setCursorTime(position);
                waveform.setCursorTime(position);
                toolbar.setCurrentTime(position);
            });
        });
        
        audioEngine->setFinishCallback([this]()
        {
            juce::MessageManager::callAsync([this]()
            {
                isPlaying = false;
                toolbar.setPlaying(false);
            });
        });
    }
    
    // Set initial project
    pianoRoll.setProject(project.get());
    waveform.setProject(project.get());
    
    DBG("MainComponent: Adding keyboard listener...");
    // Add keyboard listener
    addKeyListener(this);
    setWantsKeyboardFocus(true);
    
    DBG("MainComponent: Loading config...");
    // Load config
    if (enableAudioDeviceFlag)
        loadConfig();
    
    DBG("MainComponent: Starting timer...");
    // Start timer for UI updates
    startTimerHz(30);
    
    DBG("MainComponent: Initialization complete!");
}

MainComponent::~MainComponent()
{
    if (enableAudioDeviceFlag)
        saveConfig();
    removeKeyListener(this);
    stopTimer();

    cancelLoading = true;
    if (loaderThread.joinable())
        loaderThread.join();

    if (audioEngine)
        audioEngine->shutdownAudio();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(COLOR_BACKGROUND));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    
    // Toolbar at top
    toolbar.setBounds(bounds.removeFromTop(40));
    
    // Parameter panel on right
    parameterPanel.setBounds(bounds.removeFromRight(250));
    
    // Waveform at bottom
    waveform.setBounds(bounds.removeFromBottom(120));
    
    // Piano roll takes remaining space
    pianoRoll.setBounds(bounds);
}

void MainComponent::timerCallback()
{
    if (isLoadingAudio.load())
    {
        const auto progress = static_cast<float>(loadingProgress.load());
        toolbar.setProgress(progress);

        juce::String msg;
        {
            const juce::ScopedLock sl(loadingMessageLock);
            msg = loadingMessage;
        }

        if (msg.isNotEmpty() && msg != lastLoadingMessage)
        {
            toolbar.showProgress(msg);
            parameterPanel.setLoadingStatus(msg);
            lastLoadingMessage = msg;
        }

        return;
    }

    if (lastLoadingMessage.isNotEmpty())
    {
        toolbar.hideProgress();
        parameterPanel.clearLoadingStatus();
        lastLoadingMessage.clear();
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component* /*originatingComponent*/)
{
    // Ctrl+S: Save project
    if (key == juce::KeyPress('s', juce::ModifierKeys::ctrlModifier, 0) ||
        key == juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0))
    {
        saveProject();
        return true;
    }

    // Ctrl+Z: Undo
    if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0))
    {
        undo();
        return true;
    }
    
    // Ctrl+Y or Ctrl+Shift+Z: Redo
    if (key == juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 0) ||
        key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        redo();
        return true;
    }
    
    // D: Toggle draw mode
    if (key == juce::KeyPress('d') || key == juce::KeyPress('D'))
    {
        if (pianoRoll.getEditMode() == EditMode::Draw)
            setEditMode(EditMode::Select);
        else
            setEditMode(EditMode::Draw);
        return true;
    }
    
    // Space bar: toggle play/pause
    if (key == juce::KeyPress::spaceKey)
    {
        if (isPlaying)
            pause();
        else
            play();
        return true;
    }
    
    // Escape: stop (or exit draw mode)
    if (key == juce::KeyPress::escapeKey)
    {
        if (pianoRoll.getEditMode() == EditMode::Draw)
        {
            setEditMode(EditMode::Select);
        }
        else
        {
            stop();
        }
        return true;
    }
    // Home: go to start
    if (key == juce::KeyPress::homeKey)
    {
        seek(0.0);
        return true;
    }
    
    if (key == juce::KeyPress::endKey)
    {
        if (project)
        {
            seek(project->getAudioData().getDuration());
        }
        return true;
    }
    
    return false;
}

void MainComponent::saveProject()
{
    if (!project) return;

    auto target = project->getProjectFilePath();
    if (target == juce::File{})
    {
        // Default next to audio if possible
        auto audio = project->getFilePath();
        if (audio.existsAsFile())
            target = audio.withFileExtension("peproj");
        else
            target = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("Untitled.peproj");

        fileChooser = std::make_unique<juce::FileChooser>(
            "Save project...",
            target,
            "*.peproj");

        auto chooserFlags = juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension("peproj");

            toolbar.showProgress("Saving...");
            toolbar.setProgress(-1.0f);
            parameterPanel.setLoadingStatus("Saving...");

            const bool ok = project->saveToFile(file);
            if (ok)
                project->setProjectFilePath(file);

            toolbar.hideProgress();
            parameterPanel.clearLoadingStatus();
        });

        return;
    }

    toolbar.showProgress("Saving...");
    toolbar.setProgress(-1.0f);
    parameterPanel.setLoadingStatus("Saving...");

    const bool ok = project->saveToFile(target);
    juce::ignoreUnused(ok);

    toolbar.hideProgress();
    parameterPanel.clearLoadingStatus();
}

void MainComponent::openFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an audio file...",
        juce::File{},
        "*.wav;*.mp3;*.flac;*.aiff"
    );
    
    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
        {
            loadAudioFile(file);
        }
    });
}

void MainComponent::loadAudioFile(const juce::File& file)
{
    if (isLoadingAudio.load())
        return;

    cancelLoading = false;
    isLoadingAudio = true;
    loadingProgress = 0.0;
    {
        const juce::ScopedLock sl(loadingMessageLock);
        loadingMessage = "Loading audio...";
    }
    toolbar.showProgress("Loading audio...");
    toolbar.setProgress(0.0f);
    parameterPanel.setLoadingStatus("Loading audio...");

    if (loaderThread.joinable())
        loaderThread.join();

    loaderThread = std::thread([this, file]()
    {
        juce::Component::SafePointer<MainComponent> safeThis(this);

        auto updateProgress = [this](double p, const juce::String& msg)
        {
            loadingProgress = juce::jlimit(0.0, 1.0, p);
            const juce::ScopedLock sl(loadingMessageLock);
            loadingMessage = msg;
        };

        updateProgress(0.05, "Loading audio...");

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr || cancelLoading.load())
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (safeThis != nullptr)
                    safeThis->isLoadingAudio = false;
            });
            return;
        }

        // Read audio data
        const int numSamples = static_cast<int>(reader->lengthInSamples);
        const int srcSampleRate = static_cast<int>(reader->sampleRate);

        juce::AudioBuffer<float> buffer(1, numSamples);

        updateProgress(0.10, "Reading audio...");
        if (reader->numChannels == 1)
        {
            reader->read(&buffer, 0, numSamples, 0, true, false);
        }
        else
        {
            juce::AudioBuffer<float> stereoBuffer(2, numSamples);
            reader->read(&stereoBuffer, 0, numSamples, 0, true, true);

            const float* left = stereoBuffer.getReadPointer(0);
            const float* right = stereoBuffer.getReadPointer(1);
            float* mono = buffer.getWritePointer(0);

            for (int i = 0; i < numSamples; ++i)
                mono[i] = (left[i] + right[i]) * 0.5f;
        }

        if (cancelLoading.load())
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (safeThis != nullptr)
                    safeThis->isLoadingAudio = false;
            });
            return;
        }

        // Resample if needed
        if (srcSampleRate != SAMPLE_RATE)
        {
            updateProgress(0.18, "Resampling...");
            const double ratio = static_cast<double>(srcSampleRate) / SAMPLE_RATE;
            const int newNumSamples = static_cast<int>(numSamples / ratio);

            juce::AudioBuffer<float> resampledBuffer(1, newNumSamples);
            const float* src = buffer.getReadPointer(0);
            float* dst = resampledBuffer.getWritePointer(0);

            for (int i = 0; i < newNumSamples; ++i)
            {
                const double srcPos = i * ratio;
                const int srcIndex = static_cast<int>(srcPos);
                const double frac = srcPos - srcIndex;

                if (srcIndex + 1 < numSamples)
                    dst[i] = static_cast<float>(src[srcIndex] * (1.0 - frac) + src[srcIndex + 1] * frac);
                else
                    dst[i] = src[srcIndex];
            }

            buffer = std::move(resampledBuffer);
        }

        updateProgress(0.22, "Preparing project...");
        auto newProject = std::make_shared<Project>();
        newProject->setFilePath(file);
        auto& audioData = newProject->getAudioData();
        audioData.waveform = std::move(buffer);
        audioData.sampleRate = SAMPLE_RATE;

        if (cancelLoading.load())
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (safeThis != nullptr)
                    safeThis->isLoadingAudio = false;
            });
            return;
        }

        updateProgress(0.25, "Analyzing audio...");
        analyzeAudio(*newProject, updateProgress);

        if (cancelLoading.load())
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (safeThis != nullptr)
                    safeThis->isLoadingAudio = false;
            });
            return;
        }

        updateProgress(0.95, "Finalizing...");

        juce::MessageManager::callAsync([safeThis, newProject]() mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->project = std::make_unique<Project>(std::move(*newProject));

            // Update UI
            safeThis->pianoRoll.setProject(safeThis->project.get());
            safeThis->waveform.setProject(safeThis->project.get());
            safeThis->parameterPanel.setProject(safeThis->project.get());
            safeThis->toolbar.setTotalTime(safeThis->project->getAudioData().getDuration());

            // Set audio to engine
            auto& audioData = safeThis->project->getAudioData();
            if (safeThis->audioEngine)
                safeThis->audioEngine->loadWaveform(audioData.waveform, audioData.sampleRate);

            // Save original waveform for incremental synthesis
            safeThis->originalWaveform.makeCopyOf(audioData.waveform);
            safeThis->hasOriginalWaveform = true;

            safeThis->repaint();
            safeThis->isLoadingAudio = false;
        });
    });
}

void MainComponent::analyzeAudio()
{
    if (!project) return;
    analyzeAudio(*project, [](double, const juce::String&) {});
}

void MainComponent::analyzeAudio(Project& targetProject, const std::function<void(double, const juce::String&)>& onProgress)
{
    auto& audioData = targetProject.getAudioData();
    if (audioData.waveform.getNumSamples() == 0) return;
    
    // Extract F0
    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();
    
    onProgress(0.35, "Computing mel spectrogram...");
    // Compute mel spectrogram first (to know target frame count)
    MelSpectrogram melComputer(SAMPLE_RATE, N_FFT, HOP_SIZE, NUM_MELS, FMIN, FMAX);
    audioData.melSpectrogram = melComputer.compute(samples, numSamples);
    
    int targetFrames = static_cast<int>(audioData.melSpectrogram.size());
    
    DBG("Computed mel spectrogram: " << audioData.melSpectrogram.size() << " frames x " 
        << (audioData.melSpectrogram.empty() ? 0 : audioData.melSpectrogram[0].size()) << " mels");
    
    onProgress(0.55, "Extracting pitch (F0)...");
    // Use FCPE if available, otherwise fall back to YIN
    if (useFCPE && fcpePitchDetector && fcpePitchDetector->isLoaded())
    {
        DBG("Using FCPE for pitch detection");
        std::vector<float> fcpeF0 = fcpePitchDetector->extractF0(samples, numSamples, SAMPLE_RATE);
        
        DBG("FCPE raw frames: " << fcpeF0.size() << ", target frames: " << targetFrames);
        
        // Resample FCPE F0 (100 fps @ 16kHz) to vocoder frame rate (86.1 fps @ 44.1kHz)
        // FCPE: sr=16000, hop=160 -> 100 fps
        // Vocoder: sr=44100, hop=512 -> 86.13 fps
        if (!fcpeF0.empty() && targetFrames > 0)
        {
            audioData.f0.resize(targetFrames);
            double ratio = static_cast<double>(fcpeF0.size()) / targetFrames;
            
            for (int i = 0; i < targetFrames; ++i)
            {
                double srcPos = i * ratio;
                int srcIdx = static_cast<int>(srcPos);
                double frac = srcPos - srcIdx;
                
                if (srcIdx + 1 < static_cast<int>(fcpeF0.size()))
                {
                    // Linear interpolation, but only between voiced frames
                    float f0_a = fcpeF0[srcIdx];
                    float f0_b = fcpeF0[srcIdx + 1];
                    
                    if (f0_a > 0 && f0_b > 0)
                    {
                        // Both voiced: interpolate
                        audioData.f0[i] = static_cast<float>(f0_a * (1.0 - frac) + f0_b * frac);
                    }
                    else if (f0_a > 0)
                    {
                        // Only first voiced
                        audioData.f0[i] = f0_a;
                    }
                    else if (f0_b > 0)
                    {
                        // Only second voiced
                        audioData.f0[i] = f0_b;
                    }
                    else
                    {
                        // Both unvoiced
                        audioData.f0[i] = 0.0f;
                    }
                }
                else if (srcIdx < static_cast<int>(fcpeF0.size()))
                {
                    audioData.f0[i] = fcpeF0[srcIdx];
                }
                else
                {
                    audioData.f0[i] = 0.0f;
                }
            }
        }
        else
        {
            audioData.f0.clear();
        }
        
        // Create voiced mask (non-zero F0 = voiced)
        audioData.voicedMask.resize(audioData.f0.size());
        for (size_t i = 0; i < audioData.f0.size(); ++i)
        {
            audioData.voicedMask[i] = audioData.f0[i] > 0;
        }
        
        DBG("Resampled F0 frames: " << audioData.f0.size());
    }
    else
    {
        DBG("Using YIN for pitch detection (fallback)");
        auto [f0Values, voicedValues] = pitchDetector->extractF0(samples, numSamples);
        audioData.f0 = std::move(f0Values);
        audioData.voicedMask = std::move(voicedValues);
    }
    
    onProgress(0.75, "Loading vocoder...");
    // Load vocoder model
    auto modelPath = getRuntimeBinaryDir()
                        .getChildFile("models")
                        .getChildFile("pc_nsf_hifigan.onnx");
    
    if (modelPath.existsAsFile() && !vocoder->isLoaded())
    {
        if (vocoder->loadModel(modelPath))
        {
            DBG("Vocoder model loaded successfully: " + modelPath.getFullPathName());
        }
        else
        {
            DBG("Failed to load vocoder model: " + modelPath.getFullPathName());
        }
    }
    
    onProgress(0.90, "Segmenting notes...");
    // Segment into notes
    segmentIntoNotes(targetProject);
    
    DBG("Loaded audio: " << audioData.waveform.getNumSamples() << " samples");
    DBG("Detected " << audioData.f0.size() << " F0 frames");
    DBG("Segmented into " << project->getNotes().size() << " notes");
}

void MainComponent::exportFile()
{
    if (!project) return;
    
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save audio file...",
        juce::File{},
        "*.wav"
    );
    
    auto chooserFlags = juce::FileBrowserComponent::saveMode
                      | juce::FileBrowserComponent::canSelectFiles
                      | juce::FileBrowserComponent::warnAboutOverwriting;
    
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            auto& audioData = project->getAudioData();
            
            juce::WavAudioFormat wavFormat;
            auto* outputStream = new juce::FileOutputStream(file);
            
            if (outputStream->openedOk())
            {
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wavFormat.createWriterFor(
                        outputStream,
                        SAMPLE_RATE,
                        1,
                        16,
                        {},
                        0
                    )
                );
                
                if (writer != nullptr)
                {
                    writer->writeFromAudioSampleBuffer(
                        audioData.waveform, 0, audioData.waveform.getNumSamples());
                }
            }
            else
            {
                delete outputStream;
            }
        }
    });
}

void MainComponent::play()
{
    if (!project) return;
    if (!audioEngine) return;
    
    isPlaying = true;
    toolbar.setPlaying(true);
    audioEngine->play();
}

void MainComponent::pause()
{
    if (!audioEngine) return;
    isPlaying = false;
    toolbar.setPlaying(false);
    audioEngine->pause();
}

void MainComponent::stop()
{
    if (!audioEngine) return;
    isPlaying = false;
    toolbar.setPlaying(false);
    audioEngine->stop();
    
    pianoRoll.setCursorTime(0.0);
    waveform.setCursorTime(0.0);
    toolbar.setCurrentTime(0.0);
}

void MainComponent::seek(double time)
{
    if (!audioEngine) return;
    audioEngine->seek(time);
    pianoRoll.setCursorTime(time);
    waveform.setCursorTime(time);
    toolbar.setCurrentTime(time);
}

void MainComponent::resynthesize()
{
    if (!project) 
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Resynthesize",
            "No project loaded.");
        return;
    }
    
    auto& audioData = project->getAudioData();
    if (audioData.melSpectrogram.empty() || audioData.f0.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Resynthesize",
            "No mel spectrogram or F0 data. Please load an audio file first.");
        DBG("Cannot resynthesize: no mel spectrogram or F0 data");
        DBG("  melSpectrogram size: " << audioData.melSpectrogram.size());
        DBG("  f0 size: " << audioData.f0.size());
        return;
    }
    
    if (!vocoder->isLoaded())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Resynthesize",
            "Vocoder model not loaded. Check if models/pc_nsf_hifigan.onnx exists.");
        DBG("Cannot resynthesize: vocoder not loaded");
        return;
    }
    
    DBG("Starting resynthesis...");
    DBG("  Mel frames: " << audioData.melSpectrogram.size());
    DBG("  F0 frames: " << audioData.f0.size());
    
    // Show progress indicator
    toolbar.setEnabled(false);
    parameterPanel.setLoadingStatus("Synthesizing...");
    
    // Get adjusted F0
    std::vector<float> adjustedF0 = project->getAdjustedF0();
    
    DBG("  Adjusted F0 frames: " << adjustedF0.size());
    
    // Run vocoder inference asynchronously
    vocoder->inferAsync(audioData.melSpectrogram, adjustedF0, 
        [this](std::vector<float> synthesizedAudio)
        {
            // Re-enable toolbar
            toolbar.setEnabled(true);
            parameterPanel.clearLoadingStatus();
            
            if (synthesizedAudio.empty())
            {
                DBG("Resynthesis failed: empty output");
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Resynthesize",
                    "Synthesis failed - empty output from vocoder.");
                return;
            }
            
            DBG("Resynthesis complete: " << synthesizedAudio.size() << " samples");
            
            // Create audio buffer from synthesized audio
            juce::AudioBuffer<float> newBuffer(1, static_cast<int>(synthesizedAudio.size()));
            float* dst = newBuffer.getWritePointer(0);
            std::copy(synthesizedAudio.begin(), synthesizedAudio.end(), dst);
            
            // Update project audio data
            auto& audioData = project->getAudioData();
            audioData.waveform = std::move(newBuffer);
            
            // Reload waveform in audio engine
            audioEngine->loadWaveform(audioData.waveform, audioData.sampleRate);
            
            // Update UI
            waveform.repaint();
            
            DBG("Resynthesis applied to project");
            
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Resynthesize",
                "Synthesis complete! " + juce::String(synthesizedAudio.size()) + " samples generated.");
                
            // Clear dirty flags after full resynthesis
            project->clearAllDirty();
        });
}

void MainComponent::resynthesizeIncremental()
{
    if (!project) return;
    
    auto& audioData = project->getAudioData();
    if (audioData.melSpectrogram.empty() || audioData.f0.empty()) return;
    if (!vocoder->isLoaded()) return;
    
    // Check if there are dirty notes or F0 edits
    if (!project->hasDirtyNotes() && !project->hasF0DirtyRange())
    {
        DBG("No dirty notes or F0 edits, skipping incremental synthesis");
        return;
    }
    
    auto [dirtyStart, dirtyEnd] = project->getDirtyFrameRange();
    if (dirtyStart < 0 || dirtyEnd < 0)
    {
        DBG("Invalid dirty frame range");
        return;
    }
    
    // Add padding frames for smooth transitions (vocoder needs context)
    const int paddingFrames = 10;
    int startFrame = std::max(0, dirtyStart - paddingFrames);
    int endFrame = std::min(static_cast<int>(audioData.melSpectrogram.size()), 
                           dirtyEnd + paddingFrames);
    
    DBG("Incremental synthesis: frames " << startFrame << " to " << endFrame);
    
    // Extract mel spectrogram range
    std::vector<std::vector<float>> melRange(
        audioData.melSpectrogram.begin() + startFrame,
        audioData.melSpectrogram.begin() + endFrame);
    
    // Get adjusted F0 for range
    std::vector<float> adjustedF0Range = project->getAdjustedF0ForRange(startFrame, endFrame);
    
    if (melRange.empty() || adjustedF0Range.empty())
    {
        DBG("Empty mel or F0 range");
        return;
    }
    
    // Disable toolbar during synthesis
    toolbar.setEnabled(false);
    parameterPanel.setLoadingStatus("Preview...");
    
    // Calculate sample positions
    int hopSize = vocoder->getHopSize();
    int startSample = startFrame * hopSize;
    int endSample = endFrame * hopSize;
    
    // Store frame range for callback
    int capturedStartSample = startSample;
    int capturedEndSample = endSample;
    int capturedPaddingFrames = paddingFrames;
    int capturedHopSize = hopSize;
    
    // Run vocoder inference asynchronously
    vocoder->inferAsync(melRange, adjustedF0Range, 
        [this, capturedStartSample, capturedEndSample, capturedPaddingFrames, capturedHopSize]
        (std::vector<float> synthesizedAudio)
        {
            toolbar.setEnabled(true);
            parameterPanel.clearLoadingStatus();
            
            if (synthesizedAudio.empty())
            {
                DBG("Incremental synthesis failed: empty output");
                return;
            }
            
            DBG("Incremental synthesis complete: " << synthesizedAudio.size() << " samples");
            
            auto& audioData = project->getAudioData();
            float* dst = audioData.waveform.getWritePointer(0);
            int totalSamples = audioData.waveform.getNumSamples();
            
            // Calculate actual replace range (skip padding on both ends)
            int paddingSamples = capturedPaddingFrames * capturedHopSize;
            int replaceStartSample = capturedStartSample + paddingSamples;
            int replaceEndSample = capturedEndSample - paddingSamples;
            
            // Calculate source offset in synthesized audio
            int srcOffset = paddingSamples;
            int replaceSamples = replaceEndSample - replaceStartSample;
            
            // Apply crossfade at boundaries for smooth transitions
            const int crossfadeSamples = 256;
            
            // Copy synthesized audio with crossfade
            for (int i = 0; i < replaceSamples && (replaceStartSample + i) < totalSamples; ++i)
            {
                int dstIdx = replaceStartSample + i;
                int srcIdx = srcOffset + i;
                
                if (srcIdx >= 0 && srcIdx < static_cast<int>(synthesizedAudio.size()))
                {
                    float srcVal = synthesizedAudio[srcIdx];
                    
                    // Crossfade at start
                    if (i < crossfadeSamples)
                    {
                        float t = static_cast<float>(i) / crossfadeSamples;
                        dst[dstIdx] = dst[dstIdx] * (1.0f - t) + srcVal * t;
                    }
                    // Crossfade at end
                    else if (i >= replaceSamples - crossfadeSamples)
                    {
                        float t = static_cast<float>(replaceSamples - i) / crossfadeSamples;
                        dst[dstIdx] = dst[dstIdx] * (1.0f - t) + srcVal * t;
                    }
                    // Direct copy in the middle
                    else
                    {
                        dst[dstIdx] = srcVal;
                    }
                }
            }
            
            // Reload waveform in audio engine
            audioEngine->loadWaveform(audioData.waveform, audioData.sampleRate);
            
            // Update UI
            waveform.repaint();
            
            // Clear dirty flags after successful synthesis
            project->clearAllDirty();
            
            DBG("Incremental synthesis applied");
        });
}

void MainComponent::onNoteSelected(Note* note)
{
    parameterPanel.setSelectedNote(note);
}

void MainComponent::onPitchEdited()
{
    pianoRoll.repaint();
    parameterPanel.updateFromNote();
}

void MainComponent::onZoomChanged(float pixelsPerSecond)
{
    if (isSyncingZoom) return;
    
    isSyncingZoom = true;
    
    // Update all components with zoom centered on cursor
    pianoRoll.setPixelsPerSecond(pixelsPerSecond, true);
    waveform.setPixelsPerSecond(pixelsPerSecond);
    toolbar.setZoom(pixelsPerSecond);
    
    // Sync scroll positions after zoom
    waveform.setScrollX(pianoRoll.getScrollX());
    
    isSyncingZoom = false;
}

void MainComponent::onScrollChanged(double scrollX)
{
    // Called from waveform scroll change
    if (isSyncingScroll) return;
    
    isSyncingScroll = true;
    pianoRoll.setScrollX(scrollX);
    isSyncingScroll = false;
}

void MainComponent::onPianoRollScrollChanged(double scrollX)
{
    // Called from piano roll scroll change
    if (isSyncingScroll) return;
    
    isSyncingScroll = true;
    waveform.setScrollX(scrollX);
    isSyncingScroll = false;
}

void MainComponent::undo()
{
    if (undoManager && undoManager->canUndo())
    {
        undoManager->undo();
        pianoRoll.repaint();
        
        if (project)
        {
            // Mark all notes as dirty for resynthesis
            for (auto& note : project->getNotes())
                note.markDirty();
            
            resynthesizeIncremental();
        }
    }
}

void MainComponent::redo()
{
    if (undoManager && undoManager->canRedo())
    {
        undoManager->redo();
        pianoRoll.repaint();
        
        if (project)
        {
            // Mark all notes as dirty for resynthesis
            for (auto& note : project->getNotes())
                note.markDirty();
            
            resynthesizeIncremental();
        }
    }
}

void MainComponent::setEditMode(EditMode mode)
{
    pianoRoll.setEditMode(mode);
    toolbar.setEditMode(mode);
}

void MainComponent::segmentIntoNotes()
{
    if (!project) return;
    segmentIntoNotes(*project);
}

void MainComponent::segmentIntoNotes(Project& targetProject)
{
    auto& audioData = targetProject.getAudioData();
    auto& notes = targetProject.getNotes();
    notes.clear();
    
    if (audioData.f0.empty()) return;
    
    // Segment F0 into notes
    bool inNote = false;
    int noteStart = 0;
    float noteF0Sum = 0.0f;
    int noteF0Count = 0;
    
    for (size_t i = 0; i < audioData.f0.size(); ++i)
    {
        bool voiced = audioData.voicedMask[i];
        
        if (voiced && !inNote)
        {
            // Start new note
            inNote = true;
            noteStart = static_cast<int>(i);
            noteF0Sum = audioData.f0[i];
            noteF0Count = 1;
        }
        else if (voiced && inNote)
        {
            // Continue note
            noteF0Sum += audioData.f0[i];
            noteF0Count++;
        }
        else if (!voiced && inNote)
        {
            // End note
            int noteEnd = static_cast<int>(i);
            int duration = noteEnd - noteStart;
            
            if (duration >= 5)  // Minimum note length: 5 frames
            {
                float avgF0 = noteF0Sum / noteF0Count;
                float midi = freqToMidi(avgF0);
                
                Note note(noteStart, noteEnd, midi);  // Use noteEnd, not duration
                
                // Store F0 values for this note
                std::vector<float> f0Values(audioData.f0.begin() + noteStart,
                                            audioData.f0.begin() + noteEnd);
                note.setF0Values(std::move(f0Values));
                
                notes.push_back(note);
            }
            
            inNote = false;
        }
    }
    
    // Handle note at end
    if (inNote)
    {
        int noteEnd = static_cast<int>(audioData.f0.size());
        int duration = noteEnd - noteStart;
        
        if (duration >= 5)
        {
            float avgF0 = noteF0Sum / noteF0Count;
            float midi = freqToMidi(avgF0);
            
            Note note(noteStart, noteEnd, midi);  // Use noteEnd, not duration
            
            // Store F0 values for this note
            std::vector<float> f0Values(audioData.f0.begin() + noteStart,
                                        audioData.f0.begin() + noteEnd);
            note.setF0Values(std::move(f0Values));
            
            notes.push_back(note);
        }
    }
}

void MainComponent::showSettings()
{
    if (!settingsDialog)
    {
        settingsDialog = std::make_unique<SettingsDialog>();
        settingsDialog->getSettingsComponent()->onSettingsChanged = [this]()
        {
            applySettings();
        };
    }
    
    settingsDialog->setVisible(true);
    settingsDialog->toFront(true);
}

void MainComponent::applySettings()
{
    // Load settings from file
    auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("PitchEditor")
                            .getChildFile("settings.xml");
    
    juce::String device = "CPU";
    int threads = 0;
    
    if (settingsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(settingsFile);
        if (xml != nullptr)
        {
            device = xml->getStringAttribute("device", "CPU");
            threads = xml->getIntAttribute("threads", 0);
        }
    }
    
    DBG("Applying settings: device=" + device + ", threads=" + juce::String(threads));
    
    // Apply to vocoder
    if (vocoder)
    {
        vocoder->setExecutionDevice(device);
        vocoder->setNumThreads(threads);
        
        // Reload model if already loaded to apply new execution provider
        if (vocoder->isLoaded())
        {
            DBG("Reloading vocoder model with new settings...");
            vocoder->reloadModel();
        }
    }
}

void MainComponent::loadConfig()
{
    auto configFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                          .getParentDirectory()
                          .getChildFile("config.json");
    
    if (configFile.existsAsFile())
    {
        auto configText = configFile.loadFileAsString();
        auto config = juce::JSON::parse(configText);
        
        if (config.isObject())
        {
            auto configObj = config.getDynamicObject();
            if (configObj)
            {
                // Load last opened file path (for future use)
                // juce::String lastFile = configObj->getProperty("lastFile").toString();
                
                // Load window size (for future use)
                // int width = configObj->getProperty("windowWidth");
                // int height = configObj->getProperty("windowHeight");
                
                DBG("Config loaded from: " + configFile.getFullPathName());
            }
        }
    }
}

void MainComponent::saveConfig()
{
    auto configFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                          .getParentDirectory()
                          .getChildFile("config.json");
    
    auto config = new juce::DynamicObject();
    
    // Save last opened file path
    if (project && project->getFilePath().existsAsFile())
    {
        config->setProperty("lastFile", project->getFilePath().getFullPathName());
    }
    
    // Save window size
    config->setProperty("windowWidth", getWidth());
    config->setProperty("windowHeight", getHeight());
    
    // Write to file
    juce::String jsonText = juce::JSON::toString(juce::var(config));
    configFile.replaceWithText(jsonText);
    
    DBG("Config saved to: " + configFile.getFullPathName());
}
