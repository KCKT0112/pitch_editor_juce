#include "MainComponent.h"
#include "../Utils/Constants.h"
#include "../Utils/MelSpectrogram.h"
#include "../Utils/F0Smoother.h"
#include "../Utils/PitchCurveProcessor.h"
#include "../Utils/PlatformPaths.h"
#include "../Utils/Localization.h"

#if JUCE_MAC
#include <Cocoa/Cocoa.h>
#endif

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
    someDetector = std::make_unique<SOMEDetector>();
    vocoder = std::make_unique<Vocoder>();
    undoManager = std::make_unique<PitchUndoManager>(100);
    
    DBG("MainComponent: Looking for FCPE model...");
    // Try to load FCPE model
    auto modelsDir = PlatformPaths::getModelsDirectory();

    auto fcpeModelPath = modelsDir.getChildFile("fcpe.onnx");
    auto melFilterbankPath = modelsDir.getChildFile("mel_filterbank.bin");
    auto centTablePath = modelsDir.getChildFile("cent_table.bin");

    if (fcpeModelPath.existsAsFile())
    {
#ifdef USE_DIRECTML
        if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath, centTablePath, GPUProvider::DirectML))
#elif defined(USE_CUDA)
        if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath, centTablePath, GPUProvider::CUDA))
#elif defined(__APPLE__)
        if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath, centTablePath, GPUProvider::CoreML))
#else
        if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath, centTablePath, GPUProvider::CPU))
#endif
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

    // Try to load SOME model for note segmentation
    auto someModelPath = modelsDir.getChildFile("some.onnx");
    if (someModelPath.existsAsFile())
    {
        if (someDetector->loadModel(someModelPath))
        {
            DBG("SOME note detector loaded successfully");
        }
        else
        {
            DBG("Failed to load SOME model");
        }
    }
    else
    {
        DBG("SOME model not found at: " + someModelPath.getFullPathName());
    }

    // Load vocoder settings
    applySettings();
    
    DBG("MainComponent: Initializing audio...");
    // Initialize audio (standalone app only)
    if (audioEngine)
        audioEngine->initializeAudio();
    
    DBG("MainComponent: Adding child components...");
    // Add child components
#if JUCE_MAC
    // Use native macOS menu bar (only in standalone mode)
    if (!isPluginMode())
        juce::MenuBarModel::setMacMainMenu(this);
#else
    addAndMakeVisible(titleBar);
    menuBar.setModel(this);
    menuBar.setLookAndFeel(&menuBarLookAndFeel);
    addAndMakeVisible(menuBar);
#endif
    addAndMakeVisible(toolbar);
    addAndMakeVisible(pianoRoll);
    addChildComponent(parameterPanel);  // Hidden by default

    // Configure toolbar for plugin mode
    if (isPluginMode())
        toolbar.setPluginMode(true);
    
    // Set undo manager for piano roll
    pianoRoll.setUndoManager(undoManager.get());
    
    DBG("MainComponent: Setting up callbacks...");
    // Setup toolbar callbacks
    toolbar.onPlay = [this]() { play(); };
    toolbar.onPause = [this]() { pause(); };
    toolbar.onStop = [this]() { stop(); };
    toolbar.onGoToStart = [this]() { seek(0.0); };
    toolbar.onGoToEnd = [this]() {
        if (project)
            seek(project->getAudioData().getDuration());
    };
    toolbar.onZoomChanged = [this](float pps) { onZoomChanged(pps); };
    toolbar.onEditModeChanged = [this](EditMode mode) { setEditMode(mode); };

    // Plugin mode callbacks
    toolbar.onReanalyze = [this]() {
        if (onReanalyzeRequested)
            onReanalyzeRequested();
    };
    toolbar.onRender = [this]() {
        renderProcessedAudio();
    };
    toolbar.onToggleSidebar = [this](bool visible) {
        parameterPanel.setVisible(visible);
        resized();
    };

    // Setup piano roll callbacks
    pianoRoll.onSeek = [this](double time) { seek(time); };
    pianoRoll.onNoteSelected = [this](Note* note) { onNoteSelected(note); };
    pianoRoll.onPitchEdited = [this]() { onPitchEdited(); };
    pianoRoll.onPitchEditFinished = [this]() { resynthesizeIncremental(); };
    pianoRoll.onZoomChanged = [this](float pps) { onZoomChanged(pps); };
    pianoRoll.onReinterpolateUV = [this](int startFrame, int endFrame) { reinterpolateUV(startFrame, endFrame); };

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
                toolbar.setCurrentTime(position);

                // Follow playback: scroll to keep cursor visible
                if (isPlaying && toolbar.isFollowPlayback())
                {
                    float cursorX = static_cast<float>(position * pianoRoll.getPixelsPerSecond());
                    float viewWidth = static_cast<float>(pianoRoll.getWidth() - 74);  // minus piano keys and scrollbar
                    float scrollX = static_cast<float>(pianoRoll.getScrollX());

                    // If cursor is outside visible area, scroll to center it
                    if (cursorX < scrollX || cursorX > scrollX + viewWidth)
                    {
                        double newScrollX = std::max(0.0, static_cast<double>(cursorX - viewWidth * 0.3f));
                        pianoRoll.setScrollX(newScrollX);
                    }
                }
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
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    menuBar.setLookAndFeel(nullptr);
#endif

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

#if JUCE_MAC
    // Paint the title bar area with toolbar color to match
    g.setColour(juce::Colour(0xFF1A1A24));
    g.fillRect(0, 0, getWidth(), 28);
#endif
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

#if JUCE_MAC
    // Leave space for native traffic lights (title bar area)
    bounds.removeFromTop(28);
#else
    // Custom title bar at top (Windows/Linux only)
    titleBar.setBounds(bounds.removeFromTop(CustomTitleBar::titleBarHeight));
    // Menu bar below title bar
    menuBar.setBounds(bounds.removeFromTop(24));
#endif

    // Toolbar at top
    toolbar.setBounds(bounds.removeFromTop(40));

    // Parameter panel on right (only if visible)
    if (parameterPanel.isVisible())
        parameterPanel.setBounds(bounds.removeFromRight(250));

    // Piano roll takes remaining space
    pianoRoll.setBounds(bounds);
}

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
#if JUCE_MAC
    // Allow dragging from the title bar area (top 28px)
    if (e.y < 28)
    {
        if (auto* window = getTopLevelComponent())
            dragger.startDraggingComponent(window, e.getEventRelativeTo(window));
    }
#else
    juce::ignoreUnused(e);
#endif
}

void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
#if JUCE_MAC
    if (e.y < 28 || e.mouseWasDraggedSinceMouseDown())
    {
        if (auto* window = getTopLevelComponent())
            dragger.dragComponent(window, e.getEventRelativeTo(window), nullptr);
    }
#else
    juce::ignoreUnused(e);
#endif
}

void MainComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
#if JUCE_MAC
    if (e.y < 28)
    {
        // Use native zoom (maximize to screen) instead of fullscreen
        if (auto* peer = getTopLevelComponent()->getPeer())
        {
            if (auto* nsView = (NSView*)peer->getNativeHandle())
            {
                if (auto* nsWindow = [nsView window])
                    [nsWindow zoom:nil];
            }
        }
    }
#else
    juce::ignoreUnused(e);
#endif
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".mp3") ||
            file.endsWithIgnoreCase(".flac") || file.endsWithIgnoreCase(".aiff") ||
            file.endsWithIgnoreCase(".ogg") || file.endsWithIgnoreCase(".hachi"))
            return true;
    }
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    for (const auto& file : files)
    {
        juce::File f(file);
        if (f.existsAsFile())
        {
            loadAudioFile(f);
            break;
        }
    }
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
            lastLoadingMessage = msg;
        }

        return;
    }

    if (lastLoadingMessage.isNotEmpty())
    {
        toolbar.hideProgress();
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

    // Ctrl+Z or Cmd+Z: Undo
    if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0) ||
        key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        undo();
        return true;
    }

    // Ctrl+Y or Ctrl+Shift+Z or Cmd+Shift+Z: Redo
    if (key == juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 0) ||
        key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) ||
        key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
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

            const bool ok = project->saveToFile(file);
            if (ok)
                project->setProjectFilePath(file);

            toolbar.hideProgress();
        });

        return;
    }

    toolbar.showProgress("Saving...");
    toolbar.setProgress(-1.0f);

    const bool ok = project->saveToFile(target);
    juce::ignoreUnused(ok);

    toolbar.hideProgress();
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
            safeThis->parameterPanel.setProject(safeThis->project.get());
            safeThis->toolbar.setTotalTime(safeThis->project->getAudioData().getDuration());

            // Set audio to engine
            auto& audioData = safeThis->project->getAudioData();
            if (safeThis->audioEngine)
                safeThis->audioEngine->loadWaveform(audioData.waveform, audioData.sampleRate);

            // Save original waveform for incremental synthesis
            safeThis->originalWaveform.makeCopyOf(audioData.waveform);
            safeThis->hasOriginalWaveform = true;

            // Center view on detected pitch range
            const auto& f0 = audioData.f0;
            if (!f0.empty())
            {
                float minF0 = 10000.0f, maxF0 = 0.0f;
                for (float freq : f0)
                {
                    if (freq > 50.0f)  // Valid pitch
                    {
                        minF0 = std::min(minF0, freq);
                        maxF0 = std::max(maxF0, freq);
                    }
                }
                if (maxF0 > minF0)
                {
                    float minMidi = freqToMidi(minF0) - 2.0f;  // Add margin
                    float maxMidi = freqToMidi(maxF0) + 2.0f;
                    safeThis->pianoRoll.centerOnPitchRange(minMidi, maxMidi);
                }
            }

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
        // FCPE: sr=16000, hop=160 -> 100 fps, frame time = hop/16000 = 0.01s
        // Vocoder: sr=44100, hop=512 -> 86.13 fps, frame time = hop/44100 = 0.01161s
        // Use time-based alignment for better accuracy
        if (!fcpeF0.empty() && targetFrames > 0)
        {
            audioData.f0.resize(targetFrames);
            
            // Time per frame for each system
            const double fcpeFrameTime = 160.0 / 16000.0;  // 0.01 seconds
            const double vocoderFrameTime = 512.0 / 44100.0;  // ~0.01161 seconds
            
            for (int i = 0; i < targetFrames; ++i)
            {
                // Calculate time position for vocoder frame
                double vocoderTime = i * vocoderFrameTime;
                
                // Find corresponding FCPE frame indices
                double fcpeFramePos = vocoderTime / fcpeFrameTime;
                int srcIdx = static_cast<int>(fcpeFramePos);
                double frac = fcpeFramePos - srcIdx;
                
                if (srcIdx + 1 < static_cast<int>(fcpeF0.size()))
                {
                    // Linear interpolation with voiced/unvoiced awareness
                    float f0_a = fcpeF0[srcIdx];
                    float f0_b = fcpeF0[srcIdx + 1];
                    
                    if (f0_a > 0.0f && f0_b > 0.0f)
                    {
                        // Both voiced: linear interpolation in log domain for better musical accuracy
                        float logF0_a = std::log(f0_a);
                        float logF0_b = std::log(f0_b);
                        float logF0_interp = logF0_a * (1.0 - frac) + logF0_b * frac;
                        audioData.f0[i] = std::exp(logF0_interp);
                    }
                    else if (f0_a > 0.0f)
                    {
                        // Only first voiced: use it
                        audioData.f0[i] = f0_a;
                    }
                    else if (f0_b > 0.0f)
                    {
                        // Only second voiced: use it
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
                else if (srcIdx >= static_cast<int>(fcpeF0.size()))
                {
                    // Beyond source range: use last value if voiced
                    audioData.f0[i] = fcpeF0.back() > 0.0f ? fcpeF0.back() : 0.0f;
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
        
        // Apply F0 smoothing for better quality
        onProgress(0.65, "Smoothing pitch curve...");
        audioData.f0 = F0Smoother::smoothF0(audioData.f0, audioData.voicedMask);
        audioData.f0 = PitchCurveProcessor::interpolateWithUvMask(audioData.f0, audioData.voicedMask);
        
        DBG("Resampled and smoothed F0 frames: " << audioData.f0.size());
    }
    else
    {
        DBG("Using YIN for pitch detection (fallback)");
        auto [f0Values, voicedValues] = pitchDetector->extractF0(samples, numSamples);
        audioData.f0 = std::move(f0Values);
        audioData.voicedMask = std::move(voicedValues);
        
        // Apply F0 smoothing for better quality
        onProgress(0.65, "Smoothing pitch curve...");
        audioData.f0 = F0Smoother::smoothF0(audioData.f0, audioData.voicedMask);
        audioData.f0 = PitchCurveProcessor::interpolateWithUvMask(audioData.f0, audioData.voicedMask);
    }
    
    onProgress(0.75, "Loading vocoder...");
    // Load vocoder model
    auto modelPath = PlatformPaths::getModelsDirectory()
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
    
    // Build dense base/delta curves from the detected pitch
    PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);
    
    DBG("Loaded audio: " << audioData.waveform.getNumSamples() << " samples");
    DBG("Detected " << audioData.f0.size() << " F0 frames");
    DBG("Segmented into " << targetProject.getNotes().size() << " notes");
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

            // Show progress
            toolbar.showProgress("Exporting audio...");
            toolbar.setProgress(0.0f);

            // Delete existing file if it exists (to ensure clean replacement)
            if (file.existsAsFile())
            {
                if (!file.deleteFile())
                {
                    toolbar.hideProgress();
                    StyledMessageBox::show(this, "Export Failed",
                        "Could not delete existing file:\n" + file.getFullPathName(),
                        StyledMessageBox::WarningIcon);
                    return;
                }
            }

            toolbar.setProgress(0.3f);

            // Create output stream
            std::unique_ptr<juce::FileOutputStream> outputStream = std::make_unique<juce::FileOutputStream>(file);

            if (!outputStream->openedOk())
            {
                toolbar.hideProgress();
                StyledMessageBox::show(this, "Export Failed",
                    "Could not open file for writing:\n" + file.getFullPathName(),
                    StyledMessageBox::WarningIcon);
                return;
            }

            toolbar.setProgress(0.5f);

            // Create writer
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(
                    outputStream.release(),  // Writer takes ownership of stream
                    SAMPLE_RATE,
                    1,  // mono
                    16, // 16-bit
                    {},
                    0
                )
            );

            if (writer == nullptr)
            {
                toolbar.hideProgress();
                StyledMessageBox::show(this, "Export Failed",
                    "Could not create audio writer for:\n" + file.getFullPathName(),
                    StyledMessageBox::WarningIcon);
                return;
            }

            toolbar.setProgress(0.7f);

            // Write audio data
            bool writeSuccess = writer->writeFromAudioSampleBuffer(
                audioData.waveform, 0, audioData.waveform.getNumSamples());

            toolbar.setProgress(0.9f);

            // Explicitly flush and close writer (destructor will also do this, but explicit is better)
            writer->flush();
            writer.reset();  // Explicitly release writer and underlying stream

            toolbar.setProgress(1.0f);

            if (writeSuccess)
            {
                toolbar.hideProgress();
                StyledMessageBox::show(this, "Export Complete",
                    "Audio exported successfully to:\n" + file.getFullPathName(),
                    StyledMessageBox::InfoIcon);
            }
            else
            {
                toolbar.hideProgress();
                StyledMessageBox::show(this, "Export Failed",
                    "Failed to write audio data to:\n" + file.getFullPathName(),
                    StyledMessageBox::WarningIcon);
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
    // Keep cursor at current position - user can press Home to go to start
}

void MainComponent::seek(double time)
{
    if (!audioEngine) return;
    audioEngine->seek(time);
    pianoRoll.setCursorTime(time);
    toolbar.setCurrentTime(time);
}

void MainComponent::resynthesizeIncremental()
{
    DBG("resynthesizeIncremental called");

    if (!project) {
        DBG("No project");
        return;
    }

    auto& audioData = project->getAudioData();
    if (audioData.melSpectrogram.empty() || audioData.f0.empty()) {
        DBG("No mel spectrogram or F0 data: mel=" << audioData.melSpectrogram.size() << " f0=" << audioData.f0.size());
        return;
    }
    if (!vocoder->isLoaded()) {
        DBG("Vocoder not loaded");
        return;
    }

    // Check if there are dirty notes or F0 edits
    if (!project->hasDirtyNotes() && !project->hasF0DirtyRange())
    {
        DBG("No dirty notes or F0 edits, skipping incremental synthesis");
        return;
    }

    DBG("Has dirty notes: " << (int)project->hasDirtyNotes() << " Has F0 dirty: " << (int)project->hasF0DirtyRange());
    
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
    toolbar.showProgress("Preview...");
    toolbar.setProgress(-1.0f);
    
    // Calculate sample positions
    int hopSize = vocoder->getHopSize();
    int startSample = startFrame * hopSize;
    int endSample = endFrame * hopSize;
    
    // Store frame range for callback
    int capturedStartSample = startSample;
    int capturedEndSample = endSample;
    int capturedPaddingFrames = paddingFrames;
    int capturedHopSize = hopSize;

    // Use SafePointer to prevent accessing destroyed component
    juce::Component::SafePointer<MainComponent> safeThis(this);

    // Run vocoder inference asynchronously
    vocoder->inferAsync(melRange, adjustedF0Range,
        [safeThis, capturedStartSample, capturedEndSample, capturedPaddingFrames, capturedHopSize]
        (std::vector<float> synthesizedAudio)
        {
            // Check if component still exists
            if (safeThis == nullptr)
            {
                DBG("MainComponent destroyed, skipping callback");
                return;
            }

            safeThis->toolbar.setEnabled(true);
            safeThis->toolbar.hideProgress();

            if (synthesizedAudio.empty())
            {
                DBG("Incremental synthesis failed: empty output");
                return;
            }

            DBG("Incremental synthesis complete: " << synthesizedAudio.size() << " samples");

            auto& audioData = safeThis->project->getAudioData();
            float* dst = audioData.waveform.getWritePointer(0);
            int totalSamples = audioData.waveform.getNumSamples();

            // Calculate actual replace range (skip padding on both ends)
            int paddingSamples = capturedPaddingFrames * capturedHopSize;
            int replaceStartSample = capturedStartSample + paddingSamples;
            int replaceEndSample = capturedEndSample - paddingSamples;

            // Calculate source offset in synthesized audio
            int srcOffset = paddingSamples;
            int replaceSamples = replaceEndSample - replaceStartSample;

            // Calculate original audio RMS in the replace region for volume matching
            float originalRms = 0.0f;
            int rmsCount = 0;
            for (int i = replaceStartSample; i < replaceEndSample && i < totalSamples; ++i)
            {
                originalRms += dst[i] * dst[i];
                rmsCount++;
            }
            if (rmsCount > 0)
                originalRms = std::sqrt(originalRms / rmsCount);

            // Calculate synthesized audio RMS
            float synthRms = 0.0f;
            int synthCount = 0;
            for (int i = srcOffset; i < srcOffset + replaceSamples && i < static_cast<int>(synthesizedAudio.size()); ++i)
            {
                synthRms += synthesizedAudio[i] * synthesizedAudio[i];
                synthCount++;
            }
            if (synthCount > 0)
                synthRms = std::sqrt(synthRms / synthCount);

            // Calculate volume scaling factor to match original
            float volumeScale = 1.0f;
            if (synthRms > 0.001f && originalRms > 0.001f)
            {
                volumeScale = originalRms / synthRms;
                // Limit scaling to reasonable range
                volumeScale = std::clamp(volumeScale, 0.1f, 3.0f);
                DBG("Volume scaling: " << volumeScale << " (original RMS: " << originalRms << ", synth RMS: " << synthRms << ")");
            }

            // Apply crossfade at boundaries for smooth transitions
            const int crossfadeSamples = 256;
            
            // Copy synthesized audio with crossfade
            for (int i = 0; i < replaceSamples && (replaceStartSample + i) < totalSamples; ++i)
            {
                int dstIdx = replaceStartSample + i;
                int srcIdx = srcOffset + i;
                
                if (srcIdx >= 0 && srcIdx < static_cast<int>(synthesizedAudio.size()))
                {
                    float srcVal = synthesizedAudio[srcIdx] * volumeScale;

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
            safeThis->audioEngine->loadWaveform(audioData.waveform, audioData.sampleRate);

            // Clear dirty flags after successful synthesis
            safeThis->project->clearAllDirty();
            
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

void MainComponent::reinterpolateUV(int startFrame, int endFrame)
{
    if (!project || !fcpePitchDetector || !fcpePitchDetector->isLoaded())
        return;

    auto& audioData = project->getAudioData();
    if (audioData.waveform.getNumSamples() == 0 || audioData.f0.empty())
        return;

    // Convert frame range to sample range
    int startSample = startFrame * HOP_SIZE;
    int endSample = endFrame * HOP_SIZE;

    // Add some padding for better context
    const int paddingSamples = HOP_SIZE * 5;
    startSample = std::max(0, startSample - paddingSamples);
    endSample = std::min(audioData.waveform.getNumSamples(), endSample + paddingSamples);

    int numSamples = endSample - startSample;
    if (numSamples <= 0)
        return;

    // Extract audio segment
    const float* samples = audioData.waveform.getReadPointer(0) + startSample;

    // Run FCPE on this segment
    std::vector<float> fcpeF0 = fcpePitchDetector->extractF0(
        samples, numSamples, SAMPLE_RATE, 0.05f);

    if (fcpeF0.empty())
        return;

    // Calculate frame range in vocoder frame rate
    int paddingFrames = paddingSamples / HOP_SIZE;
    int actualStartFrame = startFrame - paddingFrames;
    if (actualStartFrame < 0) actualStartFrame = 0;

    int targetFrames = endFrame - actualStartFrame + paddingFrames;

    // Resample FCPE F0 to vocoder frame rate
    double ratio = static_cast<double>(fcpeF0.size()) / targetFrames;

    for (int i = 0; i < targetFrames; ++i)
    {
        int dstFrame = actualStartFrame + i;
        if (dstFrame < 0 || dstFrame >= static_cast<int>(audioData.f0.size()))
            continue;

        double srcPos = i * ratio;
        int srcIdx = static_cast<int>(srcPos);
        double frac = srcPos - srcIdx;

        float fcpeValue = 0.0f;
        if (srcIdx + 1 < static_cast<int>(fcpeF0.size()))
        {
            float f0_a = fcpeF0[srcIdx];
            float f0_b = fcpeF0[srcIdx + 1];

            if (f0_a > 0 && f0_b > 0)
                fcpeValue = static_cast<float>(f0_a * (1.0 - frac) + f0_b * frac);
            else if (f0_a > 0)
                fcpeValue = f0_a;
            else if (f0_b > 0)
                fcpeValue = f0_b;
        }
        else if (srcIdx < static_cast<int>(fcpeF0.size()))
        {
            fcpeValue = fcpeF0[srcIdx];
        }

        // If FCPE detects unvoiced (UV), mark as unvoiced
        if (fcpeValue <= 0.0f)
        {
            audioData.f0[dstFrame] = 0.0f;
            if (dstFrame < static_cast<int>(audioData.voicedMask.size()))
                audioData.voicedMask[dstFrame] = false;
        }
        // If current frame is unvoiced but FCPE detects voiced, fill in
        else if (audioData.f0[dstFrame] <= 0.0f)
        {
            audioData.f0[dstFrame] = fcpeValue;
            if (dstFrame < static_cast<int>(audioData.voicedMask.size()))
                audioData.voicedMask[dstFrame] = true;
        }
    }

    DBG("Reinterpolated UV frames " << startFrame << " to " << endFrame);
}

void MainComponent::onZoomChanged(float pixelsPerSecond)
{
    if (isSyncingZoom) return;

    isSyncingZoom = true;

    // Update all components with zoom centered on cursor
    pianoRoll.setPixelsPerSecond(pixelsPerSecond, true);
    toolbar.setZoom(pixelsPerSecond);

    isSyncingZoom = false;
}

void MainComponent::undo()
{
    if (undoManager && undoManager->canUndo())
    {
        undoManager->undo();
        pianoRoll.repaint();
        
        if (project)
        {
            // Don't mark all notes as dirty - let undo action callbacks handle
            // the specific dirty range. This avoids synthesizing the entire project.
            // The undo action's callback will set the correct F0 dirty range.
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
            // Don't mark all notes as dirty - let redo action callbacks handle
            // the specific dirty range. This avoids synthesizing the entire project.
            // The redo action's callback will set the correct F0 dirty range.
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

    // Try to use SOME model for segmentation if available
    if (someDetector && someDetector->isLoaded() && audioData.waveform.getNumSamples() > 0)
    {
        DBG("Using SOME model for note segmentation (streaming)");

        const float* samples = audioData.waveform.getReadPointer(0);
        int numSamples = audioData.waveform.getNumSamples();

        // audioData.f0 uses vocoder frame rate: 44100Hz / 512 hop = 86.13 fps
        // SOME uses 44100Hz / 512 hop = 86.13 fps (same!)
        // So SOME frames map directly to F0 frames
        const int f0Size = static_cast<int>(audioData.f0.size());

        // Use streaming detection to show notes as they're detected
        someDetector->detectNotesStreaming(samples, numSamples, SAMPLE_RATE,
            [&](const std::vector<SOMEDetector::NoteEvent>& chunkNotes) {
                for (const auto& someNote : chunkNotes)
                {
                    if (someNote.isRest) continue;

                    // SOME frames are already in the same frame rate as F0 (hop 512 at 44100Hz)
                    int f0Start = someNote.startFrame;
                    int f0End = someNote.endFrame;

                    f0Start = std::max(0, std::min(f0Start, f0Size - 1));
                    f0End = std::max(f0Start + 1, std::min(f0End, f0Size));

                    if (f0End - f0Start < 3) continue;

                    // Calculate average MIDI from actual audio data (not SOME prediction)
                    // Important: average the MIDI values, not the frequencies, because
                    // freqToMidi is logarithmic and average(midi) != freqToMidi(average(freq))
                    float midiSum = 0.0f;
                    int midiCount = 0;
                    for (int j = f0Start; j < f0End; ++j) {
                        if (j < static_cast<int>(audioData.voicedMask.size()) &&
                            audioData.voicedMask[j] && audioData.f0[j] > 0) {
                            midiSum += freqToMidi(audioData.f0[j]);
                            midiCount++;
                        }
                    }

                    float midi = someNote.midiNote;  // Fallback to SOME prediction
                    if (midiCount > 0) {
                        midi = midiSum / midiCount;  // Average of MIDI values
                    }

                    Note note(f0Start, f0End, midi);
                    std::vector<float> f0Values(audioData.f0.begin() + f0Start,
                                                audioData.f0.begin() + f0End);
                    note.setF0Values(std::move(f0Values));
                    notes.push_back(note);
                }

                // Update UI on main thread
                juce::MessageManager::callAsync([this]() {
                    pianoRoll.invalidateBasePitchCache();  // Regenerate smoothed base pitch
                    pianoRoll.repaint();
                });
            },
            nullptr  // progress callback
        );

        // Wait a bit for streaming to complete (notes are added asynchronously)
        juce::Thread::sleep(100);
        
        DBG("SOME segmented into " << notes.size() << " notes");
        juce::Logger::writeToLog("SOME segmented into " + juce::String(notes.size()) + " notes");
        
        // Write to a debug file on desktop for visibility
        auto logFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("pitch_editor_some_debug.txt");
        logFile.appendText("SOME segmented into " + juce::String(notes.size()) + " notes\n");

        if (!audioData.f0.empty())
            PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);
        
        return;
    }

    // Fallback: segment based on F0 pitch changes

    // Helper to finalize a note
    auto finalizeNote = [&](int start, int end) {
        if (end - start < 5) return;  // Minimum 5 frames

        // Calculate average MIDI for this segment (only voiced frames)
        // Important: average the MIDI values, not the frequencies
        float midiSum = 0.0f;
        int midiCount = 0;
        for (int j = start; j < end; ++j) {
            if (j < static_cast<int>(audioData.voicedMask.size()) &&
                audioData.voicedMask[j] && audioData.f0[j] > 0) {
                midiSum += freqToMidi(audioData.f0[j]);
                midiCount++;
            }
        }
        if (midiCount == 0) return;  // No voiced frames at all

        float midi = midiSum / midiCount;

        Note note(start, end, midi);
        std::vector<float> f0Values(audioData.f0.begin() + start,
                                    audioData.f0.begin() + end);
        note.setF0Values(std::move(f0Values));
        notes.push_back(note);
    };

    // Segment F0 into notes, splitting on pitch changes > 0.5 semitones
    constexpr float pitchSplitThreshold = 0.5f;  // semitones
    constexpr int minFramesForSplit = 3;  // require consecutive frames to confirm pitch change
    constexpr int maxUnvoicedGap = INT_MAX;  // never break on unvoiced, only on pitch change

    bool inNote = false;
    int noteStart = 0;
    int currentMidiNote = 0;  // quantized to nearest semitone
    int pitchChangeCount = 0;
    int pitchChangeStart = 0;
    int unvoicedCount = 0;

    for (size_t i = 0; i < audioData.f0.size(); ++i)
    {
        bool voiced = i < audioData.voicedMask.size() && audioData.voicedMask[i];

        if (voiced && !inNote)
        {
            // Start new note
            inNote = true;
            noteStart = static_cast<int>(i);
            currentMidiNote = static_cast<int>(std::round(freqToMidi(audioData.f0[i])));
            pitchChangeCount = 0;
            unvoicedCount = 0;
        }
        else if (voiced && inNote)
        {
            // Check for pitch change
            int frameMidi = static_cast<int>(std::round(freqToMidi(audioData.f0[i])));
            float midiDiff = std::abs(frameMidi - currentMidiNote);

            if (midiDiff > pitchSplitThreshold)
            {
                if (pitchChangeCount == 0)
                {
                    pitchChangeStart = static_cast<int>(i);
                }
                pitchChangeCount++;
                unvoicedCount = 0;  // Reset unvoiced count on pitch change

                // Confirm pitch change after minFramesForSplit consecutive frames
                if (pitchChangeCount >= minFramesForSplit)
                {
                    finalizeNote(noteStart, pitchChangeStart);
                    noteStart = pitchChangeStart;
                    currentMidiNote = frameMidi;
                    pitchChangeCount = 0;
                }
            }
            else
            {
                // Pitch is stable, reset change counter
                pitchChangeCount = 0;
                unvoicedCount = 0;
            }
        }
        else if (!voiced && inNote)
        {
            unvoicedCount++;
            if (unvoicedCount > maxUnvoicedGap)
            {
                finalizeNote(noteStart, static_cast<int>(i) - maxUnvoicedGap);
                inNote = false;
            }
        }
    }

    // Handle note at end
    if (inNote)
    {
        finalizeNote(noteStart, static_cast<int>(audioData.f0.size()));
    }

    if (!audioData.f0.empty())
        PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);
}

void MainComponent::showSettings()
{
    if (!settingsDialog)
    {
        // Pass AudioDeviceManager only in standalone mode
        juce::AudioDeviceManager* deviceMgr = nullptr;
        if (!isPluginMode() && audioEngine)
            deviceMgr = &audioEngine->getDeviceManager();

        settingsDialog = std::make_unique<SettingsDialog>(deviceMgr);
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
                            .getChildFile("HachiTune")
                            .getChildFile("settings.xml");
    
    juce::String device = "CPU";

    if (settingsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(settingsFile);
        if (xml != nullptr)
        {
            device = xml->getStringAttribute("device", "CPU");
        }
    }

    DBG("Applying settings: device=" + device);

    // Apply to vocoder
    if (vocoder)
    {
        vocoder->setExecutionDevice(device);
        
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
    auto configFile = PlatformPaths::getConfigFile("config.json");
    
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
    auto configFile = PlatformPaths::getConfigFile("config.json");
    
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

// Menu IDs
enum MenuIDs
{
    menuOpen = 1,
    menuExport,
    menuUndo,
    menuRedo,
    menuSettings,
    menuQuit
};

juce::StringArray MainComponent::getMenuBarNames()
{
    return { TR("menu.file"), TR("menu.edit") };
}

juce::PopupMenu MainComponent::getMenuForIndex(int menuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)  // File menu
    {
        // Only show open/export in standalone mode
        if (!isPluginMode())
        {
            menu.addItem(menuOpen, TR("menu.open"), true, false);
            menu.addItem(menuExport, TR("menu.export"), project != nullptr, false);
        }
#if !JUCE_MAC
        if (!isPluginMode())
            menu.addSeparator();
        menu.addItem(menuQuit, TR("menu.quit"), true, false);
#endif
    }
    else if (menuIndex == 1)  // Edit menu
    {
        bool canUndo = undoManager && undoManager->canUndo();
        bool canRedo = undoManager && undoManager->canRedo();
        menu.addItem(menuUndo, TR("menu.undo"), canUndo, false);
        menu.addItem(menuRedo, TR("menu.redo"), canRedo, false);
        menu.addSeparator();
        menu.addItem(menuSettings, TR("menu.settings"), true, false);
    }

    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/)
{
    switch (menuItemID)
    {
        case menuOpen:
            openFile();
            break;
        case menuExport:
            exportFile();
            break;
        case menuUndo:
            undo();
            break;
        case menuRedo:
            redo();
            break;
        case menuSettings:
            showSettings();
            break;
        case menuQuit:
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            break;
        default:
            break;
    }
}

void MainComponent::setHostAudio(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (!isPluginMode())
        return;

    // Create project if needed
    if (!project)
        project = std::make_unique<Project>();

    // Store sample rate
    project->getAudioData().sampleRate = static_cast<int>(sampleRate);

    // Copy waveform to project
    project->getAudioData().waveform = buffer;

    // Store original waveform for synthesis
    originalWaveform = buffer;
    hasOriginalWaveform = true;

    // Trigger analysis
    analyzeAudio();
}

void MainComponent::renderProcessedAudio()
{
    if (!isPluginMode() || !hasOriginalWaveform)
        return;

    // Show progress
    toolbar.showProgress(TR("progress.rendering"));

    // Run synthesis in background thread
    std::thread([this]()
    {
        // Resynthesize with current edits
        auto& f0Array = project->getAudioData().f0;
        auto& voicedMask = project->getAudioData().voicedMask;

        if (f0Array.empty())
        {
            juce::MessageManager::callAsync([this]() {
                toolbar.hideProgress();
            });
            return;
        }

        // Apply global pitch offset
        std::vector<float> modifiedF0 = f0Array;
        float globalOffset = project->getGlobalPitchOffset();
        for (size_t i = 0; i < modifiedF0.size(); ++i)
        {
            if (voicedMask[i] && modifiedF0[i] > 0)
                modifiedF0[i] *= std::pow(2.0f, globalOffset / 12.0f);
        }

        // Get mel spectrogram
        auto& melSpec = project->getAudioData().melSpectrogram;
        if (melSpec.empty())
        {
            juce::MessageManager::callAsync([this]() {
                toolbar.hideProgress();
            });
            return;
        }

        // Synthesize
        auto synthesized = vocoder->infer(melSpec, modifiedF0);

        if (!synthesized.empty())
        {
            // Create output buffer
            juce::AudioBuffer<float> outputBuffer(originalWaveform.getNumChannels(),
                                                   static_cast<int>(synthesized.size()));

            // Copy to all channels
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
            {
                for (int i = 0; i < outputBuffer.getNumSamples(); ++i)
                    outputBuffer.setSample(ch, i, synthesized[i]);
            }

            juce::MessageManager::callAsync([this, buf = std::move(outputBuffer)]() mutable {
                toolbar.hideProgress();
                if (onRenderComplete)
                    onRenderComplete(buf);
            });
        }
        else
        {
            juce::MessageManager::callAsync([this]() {
                toolbar.hideProgress();
            });
        }
    }).detach();
}
