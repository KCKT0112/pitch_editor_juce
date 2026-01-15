#include "MainComponent.h"
#include "../Utils/AppLogger.h"
#include "../Utils/Constants.h"
#include "../Utils/F0Smoother.h"
#include "../Utils/Localization.h"
#include "../Utils/MelSpectrogram.h"
#include "../Utils/PitchCurveProcessor.h"
#include "../Utils/PlatformPaths.h"
#include <atomic>
#include <iostream>
#include <climits>

MainComponent::MainComponent(bool enableAudioDevice)
    : enableAudioDeviceFlag(enableAudioDevice) {
  setSize(1400, 900);
  setOpaque(true); // Required for native title bar

  // Initialize components
  project = std::make_unique<Project>();
  if (enableAudioDeviceFlag)
    audioEngine = std::make_unique<AudioEngine>();
  pitchDetector = std::make_unique<PitchDetector>();
  fcpePitchDetector = std::make_unique<FCPEPitchDetector>();
  rmvpePitchDetector = std::make_unique<RMVPEPitchDetector>();
  vocoder = std::make_unique<Vocoder>();
  undoManager = std::make_unique<PitchUndoManager>(100);

  // Initialize new modular components
  fileManager = std::make_unique<AudioFileManager>();
  audioAnalyzer = std::make_unique<AudioAnalyzer>();
  incrementalSynth = std::make_unique<IncrementalSynthesizer>();
  playbackController = std::make_unique<PlaybackController>();
  menuHandler = std::make_unique<MenuHandler>();
  settingsManager = std::make_unique<SettingsManager>();

  // Try to load FCPE model
  auto modelsDir = PlatformPaths::getModelsDirectory();

  auto fcpeModelPath = modelsDir.getChildFile("fcpe.onnx");
  auto melFilterbankPath = modelsDir.getChildFile("mel_filterbank.bin");
  auto centTablePath = modelsDir.getChildFile("cent_table.bin");

  if (fcpeModelPath.existsAsFile()) {
#ifdef USE_DIRECTML
    if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath,
                                     centTablePath, GPUProvider::DirectML))
#elif defined(USE_CUDA)
    if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath,
                                     centTablePath, GPUProvider::CUDA))
#elif defined(__APPLE__)
    if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath,
                                     centTablePath, GPUProvider::CoreML))
#else
    if (fcpePitchDetector->loadModel(fcpeModelPath, melFilterbankPath,
                                     centTablePath, GPUProvider::CPU))
#endif
    {
      LOG("FCPE pitch detector loaded successfully");
    } else {
      LOG("Failed to load FCPE model");
    }
  } else {
    LOG("FCPE model not found at: " + fcpeModelPath.getFullPathName());
  }

  // Try to load RMVPE model
  auto rmvpeModelPath = modelsDir.getChildFile("rmvpe.onnx");
  if (rmvpeModelPath.existsAsFile()) {
#ifdef USE_DIRECTML
    if (rmvpePitchDetector->loadModel(rmvpeModelPath, GPUProvider::DirectML))
#elif defined(USE_CUDA)
    if (rmvpePitchDetector->loadModel(rmvpeModelPath, GPUProvider::CUDA))
#elif defined(__APPLE__)
    if (rmvpePitchDetector->loadModel(rmvpeModelPath, GPUProvider::CoreML))
#else
    if (rmvpePitchDetector->loadModel(rmvpeModelPath, GPUProvider::CPU))
#endif
    {
      LOG("RMVPE pitch detector loaded successfully");
    } else {
      LOG("Failed to load RMVPE model");
    }
  } else {
    LOG("RMVPE model not found at: " + rmvpeModelPath.getFullPathName());
  }

  // Initialize legacy SOME detector
  someDetector = std::make_unique<SOMEDetector>();
  auto someModelPath = modelsDir.getChildFile("some.onnx");
  if (someModelPath.existsAsFile()) {
    if (someDetector->loadModel(someModelPath)) {
      DBG("SOME detector loaded successfully");
    } else {
      DBG("Failed to load SOME model");
    }
  } else {
    DBG("SOME model not found at: " + someModelPath.getFullPathName());
  }

  // Wire up modular components (after all detectors are initialized)
  audioAnalyzer->setFCPEDetector(fcpePitchDetector.get());
  audioAnalyzer->setRMVPEDetector(rmvpePitchDetector.get());
  audioAnalyzer->setYINDetector(pitchDetector.get());
  audioAnalyzer->setSOMEDetector(someDetector.get());

  // Apply pitch detector type from settings
  audioAnalyzer->setPitchDetectorType(settingsManager->getPitchDetectorType());

  incrementalSynth->setVocoder(vocoder.get());
  playbackController->setAudioEngine(audioEngine.get());
  menuHandler->setUndoManager(undoManager.get());
  menuHandler->setPluginMode(isPluginMode());
  settingsManager->setVocoder(vocoder.get());

  // Load vocoder settings
  settingsManager->applySettings();

  // Initialize audio (standalone app only)
  if (audioEngine)
    audioEngine->initializeAudio();

  // Setup MenuHandler callbacks
  menuHandler->onOpenFile = [this]() { openFile(); };
  menuHandler->onSaveProject = [this]() { saveProject(); };
  menuHandler->onExportFile = [this]() { exportFile(); };
  menuHandler->onUndo = [this]() { undo(); };
  menuHandler->onRedo = [this]() { redo(); };
  menuHandler->onShowSettings = [this]() { showSettings(); };
  menuHandler->onQuit = [this]() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
  menuHandler->onExportSOMEDebug = [this]() {
    if (!project) return;

    auto& audioData = project->getAudioData();
    if (audioData.waveform.getNumSamples() == 0) {
      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "Export SOME Debug", "No audio loaded.");
      return;
    }

    if (!someDetector || !someDetector->isLoaded()) {
      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
        "Export SOME Debug", "SOME detector not loaded.");
      return;
    }

    // Run SOME inference directly
    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();

    auto someNotes = someDetector->detectNotes(samples, numSamples, SOMEDetector::SAMPLE_RATE);

    if (someNotes.empty()) {
      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "Export SOME Debug", "SOME detected no notes.");
      return;
    }

    // Build CSV with raw SOME output
    juce::String csv = "index,startFrame,endFrame,midiNote,isRest\n";
    int idx = 0;
    for (const auto& note : someNotes) {
      csv += juce::String(idx++) + ","
           + juce::String(note.startFrame) + ","
           + juce::String(note.endFrame) + ","
           + juce::String(note.midiNote, 6) + ","
           + juce::String(note.isRest ? 1 : 0) + "\n";
    }

    // Save to file
    auto debugFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                       .getChildFile("some_raw_output.csv");
    debugFile.replaceWithText(csv);

    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
      "Export SOME Debug", "SOME raw output: " + juce::String(someNotes.size()) + " notes\nSaved to:\n" + debugFile.getFullPathName());
  };

  // View menu callbacks
  menuHandler->setShowDeltaPitch(settingsManager->getShowDeltaPitch());
  menuHandler->setShowBasePitch(settingsManager->getShowBasePitch());
  pianoRoll.setShowDeltaPitch(settingsManager->getShowDeltaPitch());
  pianoRoll.setShowBasePitch(settingsManager->getShowBasePitch());

  menuHandler->onShowDeltaPitchChanged = [this](bool show) {
    pianoRoll.setShowDeltaPitch(show);
    settingsManager->setShowDeltaPitch(show);
    settingsManager->saveConfig();
  };
  menuHandler->onShowBasePitchChanged = [this](bool show) {
    pianoRoll.setShowBasePitch(show);
    settingsManager->setShowBasePitch(show);
    settingsManager->saveConfig();
  };

  // Add child components - macOS uses native menu, others use in-app menu bar
#if JUCE_MAC
  if (!isPluginMode())
    juce::MenuBarModel::setMacMainMenu(menuHandler.get());
#else
  menuBar.setModel(menuHandler.get());
  menuBar.setLookAndFeel(&menuBarLookAndFeel);
  addAndMakeVisible(menuBar);
#endif
  addAndMakeVisible(toolbar);
  addAndMakeVisible(workspace);

  // Setup workspace with piano roll as main content
  workspace.setMainContent(&pianoRoll);

  // Add parameter panel to workspace sidebar
  // SVG icon for parameters panel (sliders icon)
  const juce::String paramIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="4" y1="21" x2="4" y2="14"/><line x1="4" y1="10" x2="4" y2="3"/><line x1="12" y1="21" x2="12" y2="12"/><line x1="12" y1="8" x2="12" y2="3"/><line x1="20" y1="21" x2="20" y2="16"/><line x1="20" y1="12" x2="20" y2="3"/><circle cx="4" cy="12" r="2"/><circle cx="12" cy="10" r="2"/><circle cx="20" cy="14" r="2"/></svg>)";
  workspace.addPanel("parameters", "Parameters", paramIconSvg, &parameterPanel, true);

  // Configure toolbar for plugin mode
  if (isPluginMode())
    toolbar.setPluginMode(true);

  // Set undo manager for piano roll
  pianoRoll.setUndoManager(undoManager.get());

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
  // Removed onRender callback - Melodyne-style: edits automatically trigger
  // real-time processing

  // Setup piano roll callbacks
  pianoRoll.onSeek = [this](double time) { seek(time); };
  pianoRoll.onNoteSelected = [this](Note *note) { onNoteSelected(note); };
  pianoRoll.onPitchEdited = [this]() { onPitchEdited(); };
  pianoRoll.onPitchEditFinished = [this]() {
    resynthesizeIncremental();
    // Melodyne-style: trigger real-time processor update in plugin mode
    if (isPluginMode() && onPitchEditFinished)
      onPitchEditFinished();
  };
  pianoRoll.onZoomChanged = [this](float pps) { onZoomChanged(pps); };

  // Setup parameter panel callbacks
  parameterPanel.onParameterChanged = [this]() { onPitchEdited(); };
  parameterPanel.onParameterEditFinished = [this]() {
    resynthesizeIncremental();
    // Melodyne-style: trigger real-time processor update in plugin mode
    if (isPluginMode() && onPitchEditFinished)
      onPitchEditFinished();
  };
  parameterPanel.onGlobalPitchChanged = [this]() {
    pianoRoll.repaint(); // Update display
  };
  parameterPanel.setProject(project.get());

  // Setup audio engine callbacks
  if (audioEngine) {
    audioEngine->setPositionCallback([this](double position) {
      // Throttle cursor updates - store position and let timer handle it
      pendingCursorTime.store(position);
      hasPendingCursorUpdate.store(true);
    });

    audioEngine->setFinishCallback([this]() {
      juce::MessageManager::callAsync([this]() {
        isPlaying = false;
        toolbar.setPlaying(false);
      });
    });
  }

  // Set initial project
  pianoRoll.setProject(project.get());

  // Add keyboard listener
  addKeyListener(this);
  setWantsKeyboardFocus(true);

  // Load config
  if (enableAudioDeviceFlag)
    settingsManager->loadConfig();

  // Start timer for UI updates
  startTimerHz(30);
}

MainComponent::~MainComponent() {
#if JUCE_MAC
  juce::MenuBarModel::setMacMainMenu(nullptr);
#else
  menuBar.setModel(nullptr);
  menuBar.setLookAndFeel(nullptr);
#endif
  removeKeyListener(this);
  stopTimer();

  cancelLoading = true;
  if (loaderThread.joinable())
    loaderThread.join();

  if (audioEngine) {
    audioEngine->clearCallbacks();
    audioEngine->shutdownAudio();
  }

  if (enableAudioDeviceFlag)
    settingsManager->saveConfig();
}

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(COLOR_BACKGROUND));
}

void MainComponent::resized() {
  auto bounds = getLocalBounds();

#if !JUCE_MAC
  // Menu bar at top for non-mac platforms
  menuBar.setBounds(bounds.removeFromTop(24));
#endif

  // Toolbar
  toolbar.setBounds(bounds.removeFromTop(52));

  // Workspace takes remaining space (includes piano roll, panels, and sidebar)
  workspace.setBounds(bounds);
}

void MainComponent::mouseDown(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
}

void MainComponent::mouseDrag(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
}

void MainComponent::mouseDoubleClick(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
}

void MainComponent::timerCallback() {
  // Handle throttled cursor updates (30Hz max)
  if (hasPendingCursorUpdate.load()) {
    double position = pendingCursorTime.load();
    hasPendingCursorUpdate.store(false);

    pianoRoll.setCursorTime(position);
    toolbar.setCurrentTime(position);

    // Follow playback: scroll to keep cursor visible
    if (isPlaying && toolbar.isFollowPlayback()) {
      float cursorX =
          static_cast<float>(position * pianoRoll.getPixelsPerSecond());
      float viewWidth = static_cast<float>(
          pianoRoll.getWidth() - 74); // minus piano keys and scrollbar
      float scrollX = static_cast<float>(pianoRoll.getScrollX());

      // If cursor is outside visible area, scroll to center it
      if (cursorX < scrollX || cursorX > scrollX + viewWidth) {
        double newScrollX =
            std::max(0.0, static_cast<double>(cursorX - viewWidth * 0.3f));
        pianoRoll.setScrollX(newScrollX);
      }
    }
  }

  if (isLoadingAudio.load()) {
    const auto progress = static_cast<float>(loadingProgress.load());
    toolbar.setProgress(progress);

    juce::String msg;
    {
      const juce::ScopedLock sl(loadingMessageLock);
      msg = loadingMessage;
    }

    if (msg.isNotEmpty() && msg != lastLoadingMessage) {
      toolbar.showProgress(msg);
      lastLoadingMessage = msg;
    }

    return;
  }

  if (lastLoadingMessage.isNotEmpty()) {
    toolbar.hideProgress();
    lastLoadingMessage.clear();
  }
}

bool MainComponent::keyPressed(const juce::KeyPress &key,
                               juce::Component * /*originatingComponent*/) {
  // Ctrl+S: Save project
  if (key == juce::KeyPress('s', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0)) {
    saveProject();
    return true;
  }

  // Ctrl+Z or Cmd+Z: Undo
  if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
    undo();
    return true;
  }

  // Ctrl+Y or Ctrl+Shift+Z or Cmd+Shift+Z: Redo
  if (key == juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 0) ||
      key == juce::KeyPress('z',
                            juce::ModifierKeys::ctrlModifier |
                                juce::ModifierKeys::shiftModifier,
                            0) ||
      key == juce::KeyPress('z',
                            juce::ModifierKeys::commandModifier |
                                juce::ModifierKeys::shiftModifier,
                            0)) {
    redo();
    return true;
  }

  // D: Toggle draw mode
  if (key == juce::KeyPress('d') || key == juce::KeyPress('D')) {
    if (pianoRoll.getEditMode() == EditMode::Draw)
      setEditMode(EditMode::Select);
    else
      setEditMode(EditMode::Draw);
    return true;
  }

  // Space bar: toggle play/pause
  if (key == juce::KeyPress::spaceKey) {
    if (isPlaying)
      pause();
    else
      play();
    return true;
  }

  // Escape: stop (or exit draw mode)
  if (key == juce::KeyPress::escapeKey) {
    if (pianoRoll.getEditMode() == EditMode::Draw) {
      setEditMode(EditMode::Select);
    } else {
      stop();
    }
    return true;
  }
  // Home: go to start
  if (key == juce::KeyPress::homeKey) {
    seek(0.0);
    return true;
  }

  if (key == juce::KeyPress::endKey) {
    if (project) {
      seek(project->getAudioData().getDuration());
    }
    return true;
  }

  return false;
}

void MainComponent::saveProject() {
  if (!project)
    return;

  auto target = project->getProjectFilePath();
  if (target == juce::File{}) {
    // Default next to audio if possible
    auto audio = project->getFilePath();
    if (audio.existsAsFile())
      target = audio.withFileExtension("peproj");
    else
      target =
          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
              .getChildFile("Untitled.peproj");

    fileChooser = std::make_unique<juce::FileChooser>("Save project...", target,
                                                      "*.peproj");

    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                        juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser &fc) {
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

void MainComponent::openFile() {
  fileChooser = std::make_unique<juce::FileChooser>(
      "Select an audio file...", juce::File{}, "*.wav;*.mp3;*.flac;*.aiff");

  auto chooserFlags = juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles;

  fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      loadAudioFile(file);
    }
  });
}

void MainComponent::loadAudioFile(const juce::File &file) {
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

  loaderThread = std::thread([this, file]() {
    juce::Component::SafePointer<MainComponent> safeThis(this);

    auto updateProgress = [this](double p, const juce::String &msg) {
      loadingProgress = juce::jlimit(0.0, 1.0, p);
      const juce::ScopedLock sl(loadingMessageLock);
      loadingMessage = msg;
    };

    updateProgress(0.05, "Loading audio...");

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));
    if (reader == nullptr || cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
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
    if (reader->numChannels == 1) {
      reader->read(&buffer, 0, numSamples, 0, true, false);
    } else {
      juce::AudioBuffer<float> stereoBuffer(2, numSamples);
      reader->read(&stereoBuffer, 0, numSamples, 0, true, true);

      const float *left = stereoBuffer.getReadPointer(0);
      const float *right = stereoBuffer.getReadPointer(1);
      float *mono = buffer.getWritePointer(0);

      for (int i = 0; i < numSamples; ++i)
        mono[i] = (left[i] + right[i]) * 0.5f;
    }

    if (cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->isLoadingAudio = false;
      });
      return;
    }

    // Resample if needed
    if (srcSampleRate != SAMPLE_RATE) {
      updateProgress(0.18, "Resampling...");
      const double ratio = static_cast<double>(srcSampleRate) / SAMPLE_RATE;
      const int newNumSamples = static_cast<int>(numSamples / ratio);

      juce::AudioBuffer<float> resampledBuffer(1, newNumSamples);
      const float *src = buffer.getReadPointer(0);
      float *dst = resampledBuffer.getWritePointer(0);

      for (int i = 0; i < newNumSamples; ++i) {
        const double srcPos = i * ratio;
        const int srcIndex = static_cast<int>(srcPos);
        const double frac = srcPos - srcIndex;

        if (srcIndex + 1 < numSamples)
          dst[i] = static_cast<float>(src[srcIndex] * (1.0 - frac) +
                                      src[srcIndex + 1] * frac);
        else
          dst[i] = src[srcIndex];
      }

      buffer = std::move(resampledBuffer);
    }

    updateProgress(0.22, "Preparing project...");
    auto newProject = std::make_shared<Project>();
    newProject->setFilePath(file);
    auto &audioData = newProject->getAudioData();
    audioData.waveform = std::move(buffer);
    audioData.sampleRate = SAMPLE_RATE;

    if (cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->isLoadingAudio = false;
      });
      return;
    }

    updateProgress(0.25, "Analyzing audio...");
    analyzeAudio(*newProject, updateProgress);

    if (cancelLoading.load()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->isLoadingAudio = false;
      });
      return;
    }

    updateProgress(0.95, "Finalizing...");

    juce::MessageManager::callAsync([safeThis, newProject]() mutable {
      if (safeThis == nullptr)
        return;

      safeThis->project = std::make_unique<Project>(std::move(*newProject));

      // Update UI
      safeThis->pianoRoll.setProject(safeThis->project.get());
      safeThis->parameterPanel.setProject(safeThis->project.get());
      safeThis->toolbar.setTotalTime(
          safeThis->project->getAudioData().getDuration());

      // Get audio data reference (used in multiple places below)
      auto &audioData = safeThis->project->getAudioData();

      // Set audio to engine (standalone mode only)
      // CRITICAL: In plugin mode, audioEngine is nullptr and should NEVER be
      // accessed
      if (safeThis) {
        // CRITICAL FIRST CHECK: Never call loadWaveform in plugin mode
        if (safeThis->isPluginMode()) {
          // This is expected in plugin mode - audioEngine doesn't exist
          // No need to log or call loadWaveform
        } else if (safeThis->audioEngine) {
          // Double-check audioEngine is still valid before calling
          AudioEngine *engine = safeThis->audioEngine.get();
          if (engine) {
            DBG("MainComponent::loadAudioFile - calling loadWaveform, "
                "engine="
                << juce::String::toHexString(
                       reinterpret_cast<uintptr_t>(engine)));
            try {
              engine->loadWaveform(audioData.waveform, audioData.sampleRate);
            } catch (...) {
              DBG("MainComponent::loadAudioFile - EXCEPTION in loadWaveform!");
            }
          } else {
            DBG("MainComponent::loadAudioFile - engine pointer is null!");
          }
        } else {
          DBG("MainComponent::loadAudioFile - audioEngine is null in "
              "standalone mode!");
        }
      }

      // Save original waveform for incremental synthesis
      safeThis->originalWaveform.makeCopyOf(audioData.waveform);
      safeThis->hasOriginalWaveform = true;

      // Center view on detected pitch range
      const auto &f0 = audioData.f0;
      if (!f0.empty()) {
        float minF0 = 10000.0f, maxF0 = 0.0f;
        for (float freq : f0) {
          if (freq > 50.0f) // Valid pitch
          {
            minF0 = std::min(minF0, freq);
            maxF0 = std::max(maxF0, freq);
          }
        }
        if (maxF0 > minF0) {
          float minMidi = freqToMidi(minF0) - 2.0f; // Add margin
          float maxMidi = freqToMidi(maxF0) + 2.0f;
          safeThis->pianoRoll.centerOnPitchRange(minMidi, maxMidi);
        }
      }

      // Ensure vocoder is loaded (shared logic - analyzeAudio should have
      // loaded it, but double-check)
      if (!safeThis->vocoder->isLoaded()) {
        DBG("MainComponent::loadAudioFile - vocoder not loaded, loading now");
        auto modelPath = PlatformPaths::getModelsDirectory().getChildFile(
            "pc_nsf_hifigan.onnx");
        if (modelPath.existsAsFile()) {
          if (safeThis->vocoder->loadModel(modelPath)) {
            DBG("MainComponent::loadAudioFile - vocoder model loaded "
                "successfully");
          } else {
            DBG("MainComponent::loadAudioFile - failed to load vocoder model");
          }
        } else {
          DBG("MainComponent::loadAudioFile - vocoder model not found");
        }
      }

      safeThis->repaint();
      safeThis->isLoadingAudio = false;

      // Notify plugin mode that project data is ready
      if (safeThis->isPluginMode() && safeThis->onProjectDataChanged)
        safeThis->onProjectDataChanged();
    });
  });
}

void MainComponent::analyzeAudio() {
  if (!project)
    return;

  // Run analysis in background thread to avoid blocking UI
  // All model inference (FCPE, YIN, SOME, vocoder) happens in background
  juce::Component::SafePointer<MainComponent> safeThis(this);

  if (loaderThread.joinable())
    loaderThread.join();

  loaderThread = std::thread([safeThis]() {
    if (safeThis == nullptr || !safeThis->project)
      return;

    // Create a copy for thread safety
    auto projectCopy = std::make_shared<Project>(*safeThis->project);

    // Perform analysis in background thread (all model inference here)
    safeThis->analyzeAudio(*projectCopy, [](double, const juce::String &) {});

    // Update main project on UI thread
    juce::MessageManager::callAsync([safeThis, projectCopy]() {
      if (safeThis == nullptr)
        return;

      // Copy analyzed data back
      safeThis->project->getAudioData().melSpectrogram =
          projectCopy->getAudioData().melSpectrogram;
      safeThis->project->getAudioData().f0 = projectCopy->getAudioData().f0;
      safeThis->project->getAudioData().voicedMask =
          projectCopy->getAudioData().voicedMask;
      safeThis->project->getAudioData().basePitch =
          projectCopy->getAudioData().basePitch;
      safeThis->project->getAudioData().deltaPitch =
          projectCopy->getAudioData().deltaPitch;

      // Update UI
      safeThis->pianoRoll.setProject(safeThis->project.get());
      safeThis->pianoRoll.repaint();

      // Trigger callbacks if needed
      if (safeThis->onProjectDataChanged)
        safeThis->onProjectDataChanged();
    });
  });
}

void MainComponent::analyzeAudio(
    Project &targetProject,
    const std::function<void(double, const juce::String &)> &onProgress,
    std::function<void()> onComplete) {
  // NOTE: This function performs all model inference operations.
  // It should ONLY be called from background threads to avoid blocking UI.
  // All model inference (FCPE, YIN, SOME, vocoder, mel spectrogram) happens
  // here.

  auto &audioData = targetProject.getAudioData();
  if (audioData.waveform.getNumSamples() == 0)
    return;

  // Extract F0
  const float *samples = audioData.waveform.getReadPointer(0);
  int numSamples = audioData.waveform.getNumSamples();

  onProgress(0.35, "Computing mel spectrogram...");
  // Compute mel spectrogram first (to know target frame count)
  // This is computationally intensive and runs in background thread
  MelSpectrogram melComputer(SAMPLE_RATE, N_FFT, HOP_SIZE, NUM_MELS, FMIN,
                             FMAX);
  audioData.melSpectrogram = melComputer.compute(samples, numSamples);

  int targetFrames = static_cast<int>(audioData.melSpectrogram.size());

  onProgress(0.55, "Extracting pitch (F0)...");

  // Get pitch detector type from settings
  PitchDetectorType detectorType = settingsManager->getPitchDetectorType();
  LOG("========== PITCH DETECTOR SELECTION ==========");
  LOG("Selected detector: " + juce::String(pitchDetectorTypeToString(detectorType)));
  LOG("RMVPE loaded: " + juce::String(rmvpePitchDetector && rmvpePitchDetector->isLoaded() ? "YES" : "NO"));
  LOG("FCPE loaded: " + juce::String(fcpePitchDetector && fcpePitchDetector->isLoaded() ? "YES" : "NO"));

  // Extract F0 based on selected detector type
  std::vector<float> extractedF0;
  bool useNeuralDetector = false;
  bool isFallback = false;

  // Try selected detector first
  if (detectorType == PitchDetectorType::RMVPE && rmvpePitchDetector && rmvpePitchDetector->isLoaded()) {
    LOG(">>> USING RMVPE (selected)");
    extractedF0 = rmvpePitchDetector->extractF0(samples, numSamples, SAMPLE_RATE);
    useNeuralDetector = true;
  } else if (detectorType == PitchDetectorType::FCPE && fcpePitchDetector && fcpePitchDetector->isLoaded()) {
    LOG(">>> USING FCPE (selected)");
    extractedF0 = fcpePitchDetector->extractF0(samples, numSamples, SAMPLE_RATE);
    useNeuralDetector = true;
  } else {
    LOG("WARNING: Selected detector not available!");
    if (detectorType == PitchDetectorType::RMVPE)
      LOG("  RMVPE was selected but not loaded");
    else if (detectorType == PitchDetectorType::FCPE)
      LOG("  FCPE was selected but not loaded");
  }

  // Fallback chain if selected detector not available
  if (!useNeuralDetector) {
    isFallback = true;
    if (rmvpePitchDetector && rmvpePitchDetector->isLoaded()) {
      LOG(">>> FALLBACK: Using RMVPE");
      extractedF0 = rmvpePitchDetector->extractF0(samples, numSamples, SAMPLE_RATE);
      useNeuralDetector = true;
    } else if (fcpePitchDetector && fcpePitchDetector->isLoaded()) {
      LOG(">>> FALLBACK: Using FCPE");
      extractedF0 = fcpePitchDetector->extractF0(samples, numSamples, SAMPLE_RATE);
      useNeuralDetector = true;
    }
  }

  LOG("Final: useNeuralDetector=" + juce::String(useNeuralDetector ? "YES" : "NO") +
      ", isFallback=" + juce::String(isFallback ? "YES" : "NO"));
  LOG("==============================================");

  if (useNeuralDetector && !extractedF0.empty() && targetFrames > 0) {
    // Resample neural F0 (100 fps @ 16kHz) to vocoder frame rate (86.1 fps @ 44.1kHz)
    audioData.f0.resize(targetFrames);

    // Time per frame for each system
    const double neuralFrameTime = 160.0 / 16000.0;    // 0.01 seconds
    const double vocoderFrameTime = 512.0 / 44100.0;   // ~0.01161 seconds

    for (int i = 0; i < targetFrames; ++i) {
      double vocoderTime = i * vocoderFrameTime;
      double neuralFramePos = vocoderTime / neuralFrameTime;
      int srcIdx = static_cast<int>(neuralFramePos);
      double frac = neuralFramePos - srcIdx;

      if (srcIdx + 1 < static_cast<int>(extractedF0.size())) {
        float f0_a = extractedF0[srcIdx];
        float f0_b = extractedF0[srcIdx + 1];

        if (f0_a > 0.0f && f0_b > 0.0f) {
          // Log-domain interpolation for musical accuracy
          float logF0_a = std::log(f0_a);
          float logF0_b = std::log(f0_b);
          float logF0_interp = logF0_a * (1.0 - frac) + logF0_b * frac;
          audioData.f0[i] = std::exp(logF0_interp);
        } else if (f0_a > 0.0f) {
          audioData.f0[i] = f0_a;
        } else if (f0_b > 0.0f) {
          audioData.f0[i] = f0_b;
        } else {
          audioData.f0[i] = 0.0f;
        }
      } else if (srcIdx < static_cast<int>(extractedF0.size())) {
        audioData.f0[i] = extractedF0[srcIdx];
      } else {
        audioData.f0[i] = extractedF0.back() > 0.0f ? extractedF0.back() : 0.0f;
      }
    }

    // Create voiced mask
    audioData.voicedMask.resize(audioData.f0.size());
    for (size_t i = 0; i < audioData.f0.size(); ++i) {
      audioData.voicedMask[i] = audioData.f0[i] > 0;
    }

    // Apply F0 smoothing
    onProgress(0.65, "Smoothing pitch curve...");
    audioData.f0 = F0Smoother::smoothF0(audioData.f0, audioData.voicedMask);
    audioData.f0 = PitchCurveProcessor::interpolateWithUvMask(
        audioData.f0, audioData.voicedMask);
  } else {
    // Fallback to YIN
    DBG("Fallback: Using YIN pitch detector");
    auto [f0Values, voicedValues] =
        pitchDetector->extractF0(samples, numSamples);
    audioData.f0 = std::move(f0Values);
    audioData.voicedMask = std::move(voicedValues);

    // Apply F0 smoothing
    onProgress(0.65, "Smoothing pitch curve...");
    audioData.f0 = F0Smoother::smoothF0(audioData.f0, audioData.voicedMask);
    audioData.f0 = PitchCurveProcessor::interpolateWithUvMask(
        audioData.f0, audioData.voicedMask);
  }

  onProgress(0.75, "Loading vocoder...");
  // Load vocoder model (model loading happens in background thread)
  auto modelPath =
      PlatformPaths::getModelsDirectory().getChildFile("pc_nsf_hifigan.onnx");

  if (modelPath.existsAsFile() && !vocoder->isLoaded()) {
    if (vocoder->loadModel(modelPath)) {
      DBG("Vocoder model loaded successfully: " + modelPath.getFullPathName());
    } else {
      DBG("Failed to load vocoder model: " + modelPath.getFullPathName());
    }
  }

  onProgress(0.90, "Segmenting notes...");
  // Segment into notes (SOME model inference runs in background thread)
  segmentIntoNotes(targetProject);

  // Build dense base/delta curves from the detected pitch
  PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);

  // Call completion callback if provided
  if (onComplete)
    onComplete();
}

void MainComponent::exportFile() {
  if (!project)
    return;

  fileChooser = std::make_unique<juce::FileChooser>("Save audio file...",
                                                    juce::File{}, "*.wav");

  auto chooserFlags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::warnAboutOverwriting;

  fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file != juce::File{}) {
      auto &audioData = project->getAudioData();

      // Show progress
      toolbar.showProgress("Exporting audio...");
      toolbar.setProgress(0.0f);

      // Delete existing file if it exists (to ensure clean replacement)
      if (file.existsAsFile()) {
        if (!file.deleteFile()) {
          toolbar.hideProgress();
          StyledMessageBox::show(this, "Export Failed",
                                 "Could not delete existing file:\n" +
                                     file.getFullPathName(),
                                 StyledMessageBox::WarningIcon);
          return;
        }
      }

      toolbar.setProgress(0.3f);

      // Create output stream
      std::unique_ptr<juce::FileOutputStream> outputStream =
          std::make_unique<juce::FileOutputStream>(file);

      if (!outputStream->openedOk()) {
        toolbar.hideProgress();
        StyledMessageBox::show(this, "Export Failed",
                               "Could not open file for writing:\n" +
                                   file.getFullPathName(),
                               StyledMessageBox::WarningIcon);
        return;
      }

      toolbar.setProgress(0.5f);

      // Create writer
      juce::WavAudioFormat wavFormat;
      std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
          outputStream.release(), // Writer takes ownership of stream
          SAMPLE_RATE,
          1,  // mono
          16, // 16-bit
          {}, 0));

      if (writer == nullptr) {
        toolbar.hideProgress();
        StyledMessageBox::show(this, "Export Failed",
                               "Could not create audio writer for:\n" +
                                   file.getFullPathName(),
                               StyledMessageBox::WarningIcon);
        return;
      }

      toolbar.setProgress(0.7f);

      // Write audio data
      bool writeSuccess = writer->writeFromAudioSampleBuffer(
          audioData.waveform, 0, audioData.waveform.getNumSamples());

      toolbar.setProgress(0.9f);

      // Explicitly flush and close writer (destructor will also do this, but
      // explicit is better)
      writer->flush();
      writer.reset(); // Explicitly release writer and underlying stream

      toolbar.setProgress(1.0f);

      if (writeSuccess) {
        toolbar.hideProgress();
        StyledMessageBox::show(this, "Export Complete",
                               "Audio exported successfully to:\n" +
                                   file.getFullPathName(),
                               StyledMessageBox::InfoIcon);
      } else {
        toolbar.hideProgress();
        StyledMessageBox::show(this, "Export Failed",
                               "Failed to write audio data to:\n" +
                                   file.getFullPathName(),
                               StyledMessageBox::WarningIcon);
      }
    }
  });
}

void MainComponent::play() {
  if (!project)
    return;

  // In plugin mode, playback is controlled by the host
  // We only update UI state, but don't actually start playback
  if (isPluginMode()) {
    // In plugin mode, playback is handled by the host
    // We can't control playback directly, but we can update UI state
    // The host will call updatePlaybackPosition() to sync the cursor
    isPlaying = true;
    toolbar.setPlaying(true);
    return;
  }

  // Standalone mode: use AudioEngine for playback
  if (!audioEngine)
    return;

  isPlaying = true;
  toolbar.setPlaying(true);
  audioEngine->play();
}

void MainComponent::pause() {
  // In plugin mode, playback is controlled by the host
  if (isPluginMode()) {
    // In plugin mode, we can't pause playback directly
    // The host controls playback, we only update UI state
    isPlaying = false;
    toolbar.setPlaying(false);
    return;
  }

  // Standalone mode: use AudioEngine for playback
  if (!audioEngine)
    return;
  isPlaying = false;
  toolbar.setPlaying(false);
  audioEngine->pause();
}

void MainComponent::stop() {
  // In plugin mode, playback is controlled by the host
  if (isPluginMode()) {
    // In plugin mode, we can't stop playback directly
    // The host controls playback, we only update UI state
    isPlaying = false;
    toolbar.setPlaying(false);
    // Keep cursor at current position - user can press Home to go to start
    return;
  }

  // Standalone mode: use AudioEngine for playback
  if (!audioEngine)
    return;
  isPlaying = false;
  toolbar.setPlaying(false);
  audioEngine->stop();
  // Keep cursor at current position - user can press Home to go to start
}

void MainComponent::seek(double time) {
  // In plugin mode, seeking is controlled by the host
  // We only update UI cursor position, but don't actually seek in audio
  if (isPluginMode()) {
    // In plugin mode, we can't seek directly
    // The host controls playback position, we only update UI cursor
    pianoRoll.setCursorTime(time);
    toolbar.setCurrentTime(time);
    return;
  }

  // Standalone mode: use AudioEngine for seeking
  if (!audioEngine)
    return;
  audioEngine->seek(time);
  pianoRoll.setCursorTime(time);
  toolbar.setCurrentTime(time);
}

void MainComponent::resynthesizeIncremental() {
  DBG("resynthesizeIncremental() called");

  if (!project) {
    DBG("  Skipped: no project");
    return;
  }

  auto &audioData = project->getAudioData();
  if (audioData.melSpectrogram.empty() || audioData.f0.empty()) {
    DBG("  Skipped: mel or f0 empty");
    return;
  }
  if (!vocoder->isLoaded()) {
    DBG("  Skipped: vocoder not loaded");
    return;
  }

  // Check if there are dirty notes or F0 edits
  if (!project->hasDirtyNotes() && !project->hasF0DirtyRange()) {
    DBG("  Skipped: no dirty notes or F0 edits");
    return;
  }

  auto [dirtyStart, dirtyEnd] = project->getDirtyFrameRange();
  if (dirtyStart < 0 || dirtyEnd < 0) {
    DBG("  Skipped: invalid dirty range: " + juce::String(dirtyStart) + " to " +
        juce::String(dirtyEnd));
    return;
  }

  DBG("  Proceeding with synthesis: frames " + juce::String(dirtyStart) +
      " to " + juce::String(dirtyEnd));

  // Add padding frames for smooth transitions (vocoder needs context)
  // Increased padding for better quality and smoother transitions
  const int paddingFrames = 30; // Increased from 10 to 30 for better context
  int startFrame = std::max(0, dirtyStart - paddingFrames);
  int endFrame = std::min(static_cast<int>(audioData.melSpectrogram.size()),
                          dirtyEnd + paddingFrames);

  // Extract mel spectrogram range
  std::vector<std::vector<float>> melRange(
      audioData.melSpectrogram.begin() + startFrame,
      audioData.melSpectrogram.begin() + endFrame);

  // Get adjusted F0 for range
  std::vector<float> adjustedF0Range =
      project->getAdjustedF0ForRange(startFrame, endFrame);

  if (melRange.empty() || adjustedF0Range.empty())
    return;

  // Show progress during synthesis
  toolbar.showProgress(TR("progress.synthesizing"));
  toolbar.setProgress(-1.0f); // Indeterminate progress

  // Disable toolbar during synthesis
  toolbar.setEnabled(false);

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

  // Capture audioEngine pointer for async callback (only if not in plugin mode)
  // IMPORTANT: In plugin mode, audioEngine is nullptr, so we should not capture
  // it
  AudioEngine *audioEnginePtr = nullptr;
  if (!isPluginMode() && audioEngine) {
    audioEnginePtr = audioEngine.get();
  }

  // Cancel any previous in-flight incremental synthesis (drop its result)
  if (incrementalCancelFlag)
    incrementalCancelFlag->store(true);
  incrementalCancelFlag = std::make_shared<std::atomic<bool>>(false);
  uint64_t jobId = ++incrementalJobId;

  // Run vocoder inference asynchronously (coalesced)
  vocoder->inferAsync(
      melRange, adjustedF0Range,
      [safeThis, audioEnginePtr, capturedStartSample, capturedEndSample,
       capturedPaddingFrames, capturedHopSize,
       sampleRate = audioData.sampleRate,
       jobId](std::vector<float> synthesizedAudio) {
        // Check if component still exists
        if (safeThis == nullptr)
          return;

        // If a newer job is in flight, drop this result
        if (jobId != safeThis->incrementalJobId.load())
          return;

        safeThis->toolbar.setEnabled(true);

        if (synthesizedAudio.empty()) {
          safeThis->toolbar.hideProgress();
          return;
        }

        auto &audioData = safeThis->project->getAudioData();
        int totalSamples = audioData.waveform.getNumSamples();

        // CRITICAL: Verify vocoder output length matches expected length
        int expectedSamples = capturedEndSample - capturedStartSample;
        int actualSamples = static_cast<int>(synthesizedAudio.size());

        if (actualSamples != expectedSamples) {
          DBG("WARNING: Vocoder output length mismatch!");
          DBG("  Expected: " + juce::String(expectedSamples) + " samples");
          DBG("  Actual: " + juce::String(actualSamples) + " samples");
          DBG("  Difference: " + juce::String(actualSamples - expectedSamples) +
              " samples");

          // If the difference is too large, skip this synthesis to avoid
          // corruption
          if (std::abs(actualSamples - expectedSamples) > capturedHopSize * 2) {
            DBG("  Difference too large, skipping synthesis");
            safeThis->toolbar.hideProgress();
            return;
          }
        }

        // Direct replacement without crossfade to avoid phase issues
        // The vocoder output includes padding context, so we replace the entire
        // synthesized region directly

        // Replace entire synthesized range (including padding for seamless splice)
        int replaceStartSample = capturedStartSample;
        int replaceSamples = std::min(actualSamples, totalSamples - replaceStartSample);

        if (replaceSamples <= 0) {
          safeThis->toolbar.hideProgress();
          return;
        }

        // Apply crossfade at boundaries to avoid pops/clicks
        // Only replace the middle portion directly, crossfade at edges
        int numChannels = audioData.waveform.getNumChannels();
        int crossfadeSamples = capturedPaddingFrames * capturedHopSize / 2; // Half of padding for crossfade
        crossfadeSamples = std::min(crossfadeSamples, actualSamples / 4); // Don't exceed 1/4 of total

        for (int i = 0; i < replaceSamples && (replaceStartSample + i) < totalSamples; ++i) {
          int dstIdx = replaceStartSample + i;
          float srcVal = synthesizedAudio[i];

          // Calculate crossfade factor
          float factor = 1.0f;
          if (i < crossfadeSamples && replaceStartSample > 0) {
            // Fade in at start
            factor = static_cast<float>(i) / crossfadeSamples;
          } else if (i >= replaceSamples - crossfadeSamples && (replaceStartSample + replaceSamples) < totalSamples) {
            // Fade out at end
            factor = static_cast<float>(replaceSamples - 1 - i) / crossfadeSamples;
          }

          for (int ch = 0; ch < numChannels; ++ch) {
            float *dstCh = audioData.waveform.getWritePointer(ch);
            if (factor < 1.0f) {
              // Crossfade: blend original and synthesized
              dstCh[dstIdx] = dstCh[dstIdx] * (1.0f - factor) + srcVal * factor;
            } else {
              dstCh[dstIdx] = srcVal;
            }
          }
        }

        // Reload waveform in audio engine (standalone mode only)
        // CRITICAL: In plugin mode, audioEngine is nullptr and should NEVER be
        // accessed
        // IMPORTANT: Use captured audioEnginePtr instead of accessing through
        // safeThis This avoids accessing destroyed object through unique_ptr
        if (safeThis && audioEnginePtr) {
          // CRITICAL FIRST CHECK: Never call loadWaveform in plugin mode
          // This is the most important check - if we're in plugin mode,
          // audioEngine doesn't exist
          if (safeThis->isPluginMode()) {
            DBG("MainComponent::resynthesizeIncremental - CRITICAL: Attempted "
                "to "
                "call loadWaveform in plugin mode! This should never happen. "
                "audioEnginePtr="
                << juce::String::toHexString(
                       reinterpret_cast<uintptr_t>(audioEnginePtr)));
            jassertfalse; // This should never happen - helps catch bugs
            return;       // Early return to prevent crash
          }

          // Double-check audioEnginePtr is still valid (compare with current
          // audioEngine) Only proceed if we're not in plugin mode and
          // audioEngine still exists
          if (safeThis->audioEngine &&
              safeThis->audioEngine.get() == audioEnginePtr) {
            // CRITICAL: Validate pointer before calling
            // Check if the pointer is still valid by comparing addresses
            AudioEngine *currentEngine = safeThis->audioEngine.get();
            if (currentEngine == audioEnginePtr && currentEngine != nullptr) {
              DBG("MainComponent::resynthesizeIncremental - calling "
                  "loadWaveform, "
                  "engine="
                  << juce::String::toHexString(
                         reinterpret_cast<uintptr_t>(audioEnginePtr)));
              try {
                audioEnginePtr->loadWaveform(audioData.waveform,
                                             audioData.sampleRate, true);
              } catch (...) {
                DBG("MainComponent::resynthesizeIncremental - EXCEPTION in "
                    "loadWaveform!");
              }
            } else {
              DBG("MainComponent::resynthesizeIncremental - audioEnginePtr is "
                  "invalid! "
                  "currentEngine="
                  << juce::String::toHexString(
                         reinterpret_cast<uintptr_t>(currentEngine))
                  << " audioEnginePtr="
                  << juce::String::toHexString(
                         reinterpret_cast<uintptr_t>(audioEnginePtr)));
            }
          } else {
            DBG("MainComponent::resynthesizeIncremental - skipping "
                "loadWaveform: "
                "audioEngine exists="
                << (safeThis->audioEngine ? "true" : "false")
                << " pointers match="
                << (safeThis->audioEngine &&
                            safeThis->audioEngine.get() == audioEnginePtr
                        ? "true"
                        : "false"));
          }
        } else {
          // This is normal in plugin mode - audioEnginePtr should be nullptr
          if (safeThis && safeThis->isPluginMode()) {
            // This is expected - no need to log
          } else {
            DBG("MainComponent::resynthesizeIncremental - skipping "
                "loadWaveform: "
                "safeThis="
                << (safeThis ? "valid" : "null") << " audioEnginePtr="
                << (audioEnginePtr
                        ? juce::String::toHexString(
                              reinterpret_cast<uintptr_t>(audioEnginePtr))
                        : "null"));
          }
        }

        // Clear dirty flags after successful synthesis
        safeThis->project->clearAllDirty();

        // Hide progress bar
        safeThis->toolbar.hideProgress();

        // Repaint piano roll to show updated waveform
        safeThis->pianoRoll.repaint();

        // Notify plugin mode that project data changed
        if (safeThis->isPluginMode() && safeThis->onProjectDataChanged)
          safeThis->onProjectDataChanged();
      });
}

void MainComponent::onNoteSelected(Note *note) {
  parameterPanel.setSelectedNote(note);
}

void MainComponent::onPitchEdited() {
  pianoRoll.repaint();
  parameterPanel.updateFromNote();
}

void MainComponent::onZoomChanged(float pixelsPerSecond) {
  if (isSyncingZoom)
    return;

  isSyncingZoom = true;

  // Update all components with zoom centered on cursor
  pianoRoll.setPixelsPerSecond(pixelsPerSecond, true);
  toolbar.setZoom(pixelsPerSecond);

  isSyncingZoom = false;
}

void MainComponent::undo() {
  if (undoManager && undoManager->canUndo()) {
    undoManager->undo();
    pianoRoll.repaint();

    if (project) {
      // Don't mark all notes as dirty - let undo action callbacks handle
      // the specific dirty range. This avoids synthesizing the entire project.
      // The undo action's callback will set the correct F0 dirty range.
      resynthesizeIncremental();
    }
  }
}

void MainComponent::redo() {
  if (undoManager && undoManager->canRedo()) {
    undoManager->redo();
    pianoRoll.repaint();

    if (project) {
      // Don't mark all notes as dirty - let redo action callbacks handle
      // the specific dirty range. This avoids synthesizing the entire project.
      // The redo action's callback will set the correct F0 dirty range.
      resynthesizeIncremental();
    }
  }
}

void MainComponent::setEditMode(EditMode mode) {
  pianoRoll.setEditMode(mode);
  toolbar.setEditMode(mode);
}

void MainComponent::segmentIntoNotes() {
  if (!project)
    return;

  // Run segmentation in background thread to avoid blocking UI
  // SOME model inference happens in background
  juce::Component::SafePointer<MainComponent> safeThis(this);

  if (loaderThread.joinable())
    loaderThread.join();

  loaderThread = std::thread([safeThis]() {
    if (safeThis == nullptr || !safeThis->project)
      return;

    // Create a copy for thread safety
    auto projectCopy = std::make_shared<Project>(*safeThis->project);

    // Perform segmentation in background thread (SOME model inference here)
    safeThis->segmentIntoNotes(*projectCopy);

    // Update main project on UI thread
    juce::MessageManager::callAsync([safeThis, projectCopy]() {
      if (safeThis == nullptr)
        return;

      // Copy notes back
      safeThis->project->getNotes() = projectCopy->getNotes();

      // Update UI
      safeThis->pianoRoll.invalidateBasePitchCache();
      safeThis->pianoRoll.repaint();
    });
  });
}

void MainComponent::segmentIntoNotes(Project &targetProject) {
  // NOTE: This function performs SOME model inference.
  // It should ONLY be called from background threads to avoid blocking UI.

  auto &audioData = targetProject.getAudioData();
  auto &notes = targetProject.getNotes();
  notes.clear();

  if (audioData.f0.empty())
    return;

  // Try to use SOME model for segmentation if available
  // SOME model inference runs in background thread
  if (someDetector && someDetector->isLoaded() &&
      audioData.waveform.getNumSamples() > 0) {

    const float *samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();

    // audioData.f0 uses vocoder frame rate: 44100Hz / 512 hop = 86.13 fps
    // SOME uses 44100Hz / 512 hop = 86.13 fps (same!)
    // So SOME frames map directly to F0 frames
    const int f0Size = static_cast<int>(audioData.f0.size());

    // Use streaming detection to show notes as they're detected
    someDetector->detectNotesStreaming(
        samples, numSamples, SOMEDetector::SAMPLE_RATE,
        [&](const std::vector<SOMEDetector::NoteEvent> &chunkNotes) {
          for (const auto &someNote : chunkNotes) {
            if (someNote.isRest)
              continue;

            // SOME frames are already in the same frame rate as F0 (hop 512 at
            // 44100Hz)
            int f0Start = someNote.startFrame;
            int f0End = someNote.endFrame;

            f0Start = std::max(0, std::min(f0Start, f0Size - 1));
            f0End = std::max(f0Start + 1, std::min(f0End, f0Size));

            if (f0End - f0Start < 3)
              continue;

            // Use SOME's predicted MIDI value directly for note position
            // Delta pitch (from RMVPE/FCPE F0) will capture the pitch curve details
            Note note(f0Start, f0End, someNote.midiNote);
            std::vector<float> f0Values(audioData.f0.begin() + f0Start,
                                        audioData.f0.begin() + f0End);
            note.setF0Values(std::move(f0Values));
            notes.push_back(note);
          }

          // Update UI on main thread
          juce::MessageManager::callAsync([this]() {
            pianoRoll
                .invalidateBasePitchCache(); // Regenerate smoothed base pitch
            pianoRoll.repaint();
          });
        },
        nullptr // progress callback
    );

    // Wait a bit for streaming to complete (notes are added asynchronously)
    juce::Thread::sleep(100);

    DBG("SOME segmented into " << notes.size() << " notes");

    if (!audioData.f0.empty())
      PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);

    return;
  }

  // Fallback: segment based on F0 pitch changes

  // Helper to finalize a note
  auto finalizeNote = [&](int start, int end) {
    if (end - start < 5)
      return; // Minimum 5 frames

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
    if (midiCount == 0)
      return; // No voiced frames at all

    float midi = midiSum / midiCount;

    Note note(start, end, midi);
    std::vector<float> f0Values(audioData.f0.begin() + start,
                                audioData.f0.begin() + end);
    note.setF0Values(std::move(f0Values));
    notes.push_back(note);
  };

  // Segment F0 into notes, splitting on pitch changes > 0.5 semitones
  constexpr float pitchSplitThreshold = 0.5f; // semitones
  constexpr int minFramesForSplit =
      3; // require consecutive frames to confirm pitch change
  constexpr int maxUnvoicedGap =
      INT_MAX; // never break on unvoiced, only on pitch change

  bool inNote = false;
  int noteStart = 0;
  int currentMidiNote = 0; // quantized to nearest semitone
  int pitchChangeCount = 0;
  int pitchChangeStart = 0;
  int unvoicedCount = 0;

  for (size_t i = 0; i < audioData.f0.size(); ++i) {
    bool voiced = i < audioData.voicedMask.size() && audioData.voicedMask[i];

    if (voiced && !inNote) {
      // Start new note
      inNote = true;
      noteStart = static_cast<int>(i);
      currentMidiNote =
          static_cast<int>(std::round(freqToMidi(audioData.f0[i])));
      pitchChangeCount = 0;
      unvoicedCount = 0;
    } else if (voiced && inNote) {
      unvoicedCount = 0; // Reset unvoiced counter

      // Check for pitch change
      float currentMidi = freqToMidi(audioData.f0[i]);
      int quantizedMidi = static_cast<int>(std::round(currentMidi));

      if (quantizedMidi != currentMidiNote &&
          std::abs(currentMidi - currentMidiNote) > pitchSplitThreshold) {
        if (pitchChangeCount == 0)
          pitchChangeStart = static_cast<int>(i);
        pitchChangeCount++;

        // Confirm pitch change after consecutive frames
        if (pitchChangeCount >= minFramesForSplit) {
          // Finalize current note up to pitch change point
          finalizeNote(noteStart, pitchChangeStart);

          // Start new note from pitch change point
          noteStart = pitchChangeStart;
          currentMidiNote = quantizedMidi;
          pitchChangeCount = 0;
        }
      } else {
        pitchChangeCount = 0; // Reset if pitch returns
      }
    } else if (!voiced && inNote) {
      // Allow short unvoiced gaps within notes
      unvoicedCount++;
      if (unvoicedCount > maxUnvoicedGap) {
        // End note after long unvoiced gap
        finalizeNote(noteStart, static_cast<int>(i) - unvoicedCount);
        inNote = false;
        pitchChangeCount = 0;
        unvoicedCount = 0;
      }
    }
  }

  // Handle note at end
  if (inNote) {
    finalizeNote(noteStart, static_cast<int>(audioData.f0.size()));
  }

  // Update dense pitch curves after segmentation
  if (!audioData.f0.empty())
    PitchCurveProcessor::rebuildCurvesFromSource(targetProject, audioData.f0);
}

void MainComponent::showSettings() {
  if (!settingsDialog) {
    // Pass AudioDeviceManager only in standalone mode
    juce::AudioDeviceManager *deviceMgr = nullptr;
    if (!isPluginMode() && audioEngine)
      deviceMgr = &audioEngine->getDeviceManager();

    settingsDialog = std::make_unique<SettingsDialog>(deviceMgr);
    settingsDialog->getSettingsComponent()->onSettingsChanged = [this]() {
      settingsManager->applySettings();
    };
    settingsDialog->getSettingsComponent()->onPitchDetectorChanged = [this](PitchDetectorType type) {
      audioAnalyzer->setPitchDetectorType(type);
    };
  }

  settingsDialog->setVisible(true);
  settingsDialog->toFront(true);
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray &files) {
  for (const auto &file : files) {
    if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".mp3") ||
        file.endsWithIgnoreCase(".flac") || file.endsWithIgnoreCase(".aiff") ||
        file.endsWithIgnoreCase(".ogg") || file.endsWithIgnoreCase(".m4a"))
      return true;
  }
  return false;
}

void MainComponent::filesDropped(const juce::StringArray &files, int /*x*/,
                                 int /*y*/) {
  if (files.isEmpty())
    return;

  juce::File audioFile(files[0]);
  if (audioFile.existsAsFile())
    loadAudioFile(audioFile);
}

void MainComponent::setHostAudio(const juce::AudioBuffer<float> &buffer,
                                 double sampleRate) {
  if (!isPluginMode())
    return;

  DBG("MainComponent::setHostAudio called - starting async analysis");

  // Use SafePointer to prevent accessing destroyed component
  juce::Component::SafePointer<MainComponent> safeThis(this);

  // Create project if needed (on message thread)
  if (!project)
    project = std::make_unique<Project>();

  // Store sample rate and waveform (on message thread)
  project->getAudioData().sampleRate = static_cast<int>(sampleRate);
  project->getAudioData().waveform = buffer;

  // Store original waveform for synthesis
  originalWaveform = buffer;
  hasOriginalWaveform = true;

  // Show analyzing progress
  toolbar.showProgress("Analyzing...");

  // Run analysis in background thread to avoid blocking UI
  // Use the same analysis logic as loadAudioFile for code sharing
  if (loaderThread.joinable())
    loaderThread.join();

  loaderThread = std::thread([safeThis]() {
    if (safeThis == nullptr)
      return;

    // Create a copy of project data for analysis
    // This ensures thread safety
    auto projectCopy = std::make_shared<Project>();
    {
      // Copy project data (safe to read from background thread)
      projectCopy->getAudioData().waveform =
          safeThis->project->getAudioData().waveform;
      projectCopy->getAudioData().sampleRate =
          safeThis->project->getAudioData().sampleRate;
    }

    DBG("MainComponent::setHostAudio - analysis thread started");

    // Use shared analyzeAudio function (same as loadAudioFile)
    // This ensures code reuse and consistency
    auto updateProgress = [safeThis](double p, const juce::String &msg) {
      // Progress updates can be shown in toolbar if needed
      juce::MessageManager::callAsync([safeThis, msg, p]() {
        if (safeThis != nullptr) {
          // Could update progress bar here if needed
          DBG("MainComponent::setHostAudio - " << msg << " (" << (p * 100)
                                               << "%)");
        }
      });
    };

    // Perform analysis using shared function
    safeThis->analyzeAudio(*projectCopy, updateProgress);

    DBG("MainComponent::setHostAudio - analysis complete, updating UI");

    // Analysis complete - update main project on message thread
    // Use same UI update logic as loadAudioFile for consistency
    juce::MessageManager::callAsync([safeThis, projectCopy]() mutable {
      if (safeThis == nullptr)
        return;

      safeThis->project = std::make_unique<Project>(std::move(*projectCopy));

      // Update UI components (shared logic)
      safeThis->pianoRoll.setProject(safeThis->project.get());
      safeThis->parameterPanel.setProject(safeThis->project.get());
      safeThis->toolbar.setTotalTime(
          safeThis->project->getAudioData().getDuration());

      // Center view on detected pitch range (shared logic)
      const auto &f0 = safeThis->project->getAudioData().f0;
      if (!f0.empty()) {
        float minF0 = 10000.0f, maxF0 = 0.0f;
        for (float freq : f0) {
          if (freq > 50.0f) {
            minF0 = std::min(minF0, freq);
            maxF0 = std::max(maxF0, freq);
          }
        }
        if (maxF0 > minF0) {
          float minMidi = freqToMidi(minF0) - 2.0f;
          float maxMidi = freqToMidi(maxF0) + 2.0f;
          safeThis->pianoRoll.centerOnPitchRange(minMidi, maxMidi);
        }
      }

      safeThis->repaint();

      // Load vocoder if not already loaded (required for real-time processing)
      // This is done in analyzeAudio, but we ensure it's loaded here too
      if (!safeThis->vocoder->isLoaded()) {
        DBG("MainComponent::setHostAudio - loading vocoder model");
        auto modelPath = PlatformPaths::getModelsDirectory().getChildFile(
            "pc_nsf_hifigan.onnx");
        if (modelPath.existsAsFile()) {
          if (safeThis->vocoder->loadModel(modelPath)) {
            DBG("MainComponent::setHostAudio - vocoder model loaded "
                "successfully: "
                << modelPath.getFullPathName());
          } else {
            DBG("MainComponent::setHostAudio - failed to load vocoder model: "
                << modelPath.getFullPathName());
          }
        } else {
          DBG("MainComponent::setHostAudio - vocoder model not found at: "
              << modelPath.getFullPathName());
        }
      } else {
        DBG("MainComponent::setHostAudio - vocoder already loaded");
      }

      // Trigger real-time processor update (this will also set vocoder if
      // needed)
      if (safeThis->onProjectDataChanged)
        safeThis->onProjectDataChanged();

      // Hide progress bar
      safeThis->toolbar.hideProgress();

      DBG("MainComponent::setHostAudio - UI update complete");
    });
  });
}

void MainComponent::updatePlaybackPosition(double timeSeconds) {
  if (!isPluginMode())
    return;

  // Update cursor position using the same mechanism as AudioEngine
  pendingCursorTime.store(timeSeconds);
  hasPendingCursorUpdate.store(true);
}

bool MainComponent::isARAModeActive() const {
  // Check if we're in plugin mode and have project data from ARA
  // ARA mode is indicated by having project data but no manual capture
  // In ARA mode, audio comes from ARA document controller, not from
  // processBlock
  if (!isPluginMode())
    return false;

  // If we have project data and it wasn't captured manually, it's likely from
  // ARA This is a heuristic - in a real implementation, we'd track this
  // explicitly
  if (project && project->getAudioData().waveform.getNumSamples() > 0) {
    // Check if we're not currently capturing (which would indicate non-ARA
    // mode) Note: This requires access to PluginProcessor, which we don't have
    // here A better approach would be to set a flag when ARA audio is received
    return true; // Assume ARA if we have project data in plugin mode
  }

  return false;
}

void MainComponent::renderProcessedAudio() {
  if (!isPluginMode() || !hasOriginalWaveform)
    return;

  // Show progress
  toolbar.showProgress(TR("progress.rendering"));

  // Use SafePointer to prevent accessing destroyed component
  juce::Component::SafePointer<MainComponent> safeThis(this);

  // Run synthesis in background thread
  std::thread([safeThis]() {
    if (safeThis == nullptr)
      return;

    // Resynthesize with current edits
    auto &f0Array = safeThis->project->getAudioData().f0;
    auto &voicedMask = safeThis->project->getAudioData().voicedMask;

    if (f0Array.empty()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->toolbar.hideProgress();
      });
      return;
    }

    // Apply global pitch offset
    std::vector<float> modifiedF0 = f0Array;
    float globalOffset = safeThis->project->getGlobalPitchOffset();
    for (size_t i = 0; i < modifiedF0.size(); ++i) {
      if (voicedMask[i] && modifiedF0[i] > 0)
        modifiedF0[i] *= std::pow(2.0f, globalOffset / 12.0f);
    }

    // Get mel spectrogram
    auto &melSpec = safeThis->project->getAudioData().melSpectrogram;
    if (melSpec.empty()) {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->toolbar.hideProgress();
      });
      return;
    }

    // Synthesize
    auto synthesized = safeThis->vocoder->infer(melSpec, modifiedF0);

    if (!synthesized.empty()) {
      // Create output buffer
      juce::AudioBuffer<float> outputBuffer(
          safeThis->originalWaveform.getNumChannels(),
          static_cast<int>(synthesized.size()));

      // Copy to all channels
      for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) {
        for (int i = 0; i < outputBuffer.getNumSamples(); ++i)
          outputBuffer.setSample(ch, i, synthesized[i]);
      }

      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr) {
          safeThis->toolbar.hideProgress();
          // Melodyne-style: trigger real-time processor update instead of
          // manual render The real-time processor will handle playback
          // automatically
          if (safeThis->onProjectDataChanged)
            safeThis->onProjectDataChanged();
        }
      });
    } else {
      juce::MessageManager::callAsync([safeThis]() {
        if (safeThis != nullptr)
          safeThis->toolbar.hideProgress();
      });
    }
  }).detach();
}
