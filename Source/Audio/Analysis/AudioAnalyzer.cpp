#include "AudioAnalyzer.h"
#include "../../Utils/PlatformPaths.h"

AudioAnalyzer::AudioAnalyzer() = default;

AudioAnalyzer::~AudioAnalyzer() {
    cancelFlag = true;
    if (analysisThread.joinable())
        analysisThread.join();
}

void AudioAnalyzer::initialize() {
    // Initialize pitch detectors
    pitchDetector = std::make_unique<PitchDetector>(SAMPLE_RATE, HOP_SIZE);

    // Try to load FCPE model
    auto fcpeModelPath = PlatformPaths::getModelsDirectory().getChildFile("fcpe.onnx");
    if (fcpeModelPath.existsAsFile()) {
        fcpeDetector = std::make_unique<FCPEPitchDetector>();
        if (!fcpeDetector->loadModel(fcpeModelPath)) {
            fcpeDetector.reset();
            DBG("Failed to load FCPE model");
        }
    }

    // Try to load SOME model
    auto someModelPath = PlatformPaths::getModelsDirectory().getChildFile("some.onnx");
    if (someModelPath.existsAsFile()) {
        someDetector = std::make_unique<SOMEDetector>();
        if (!someDetector->loadModel(someModelPath)) {
            someDetector.reset();
            DBG("Failed to load SOME model");
        }
    }
}

bool AudioAnalyzer::isFCPEAvailable() const {
    auto* detector = fcpeDetector ? fcpeDetector.get() : externalFCPEDetector;
    return detector && detector->isLoaded();
}

void AudioAnalyzer::analyze(Project& project, ProgressCallback onProgress, CompleteCallback onComplete) {
    auto& audioData = project.getAudioData();
    if (audioData.waveform.getNumSamples() == 0)
        return;

    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();

    // Compute mel spectrogram
    if (onProgress) onProgress(0.35, "Computing mel spectrogram...");
    MelSpectrogram melComputer(SAMPLE_RATE, N_FFT, HOP_SIZE, NUM_MELS, FMIN, FMAX);
    audioData.melSpectrogram = melComputer.compute(samples, numSamples);

    int targetFrames = static_cast<int>(audioData.melSpectrogram.size());

    if (cancelFlag.load()) return;

    // Extract F0
    if (onProgress) onProgress(0.55, "Extracting pitch (F0)...");
    if (useFCPE && isFCPEAvailable()) {
        extractF0WithFCPE(audioData, targetFrames);
    } else {
        extractF0WithYIN(audioData);
    }

    if (cancelFlag.load()) return;

    // Smooth F0
    if (onProgress) onProgress(0.65, "Smoothing pitch curve...");
    audioData.f0 = F0Smoother::smoothF0(audioData.f0, audioData.voicedMask);
    audioData.f0 = PitchCurveProcessor::interpolateWithUvMask(audioData.f0, audioData.voicedMask);

    if (cancelFlag.load()) return;

    // Segment into notes
    if (onProgress) onProgress(0.90, "Segmenting notes...");
    segmentIntoNotes(project);

    // Build dense base/delta curves
    PitchCurveProcessor::rebuildCurvesFromSource(project, audioData.f0);

    if (onComplete) onComplete();
}

void AudioAnalyzer::analyzeAsync(Project& project, ProgressCallback onProgress, CompleteCallback onComplete) {
    if (isRunning.load())
        return;

    cancelFlag = false;
    isRunning = true;

    if (analysisThread.joinable())
        analysisThread.join();

    analysisThread = std::thread([this, &project, onProgress, onComplete]() {
        analyze(project, onProgress, [this, onComplete]() {
            isRunning = false;
            if (onComplete) onComplete();
        });
        isRunning = false;
    });
}

void AudioAnalyzer::extractF0WithFCPE(AudioData& audioData, int targetFrames) {
    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();

    auto* detector = fcpeDetector ? fcpeDetector.get() : externalFCPEDetector;
    std::vector<float> fcpeF0 = detector->extractF0(samples, numSamples, SAMPLE_RATE);

    if (!fcpeF0.empty() && targetFrames > 0) {
        audioData.f0.resize(targetFrames);

        // Time per frame for each system
        const double fcpeFrameTime = 160.0 / 16000.0;    // 0.01 seconds
        const double vocoderFrameTime = 512.0 / 44100.0; // ~0.01161 seconds

        for (int i = 0; i < targetFrames; ++i) {
            double vocoderTime = i * vocoderFrameTime;
            double fcpeFramePos = vocoderTime / fcpeFrameTime;
            int srcIdx = static_cast<int>(fcpeFramePos);
            double frac = fcpeFramePos - srcIdx;

            if (srcIdx + 1 < static_cast<int>(fcpeF0.size())) {
                float f0_a = fcpeF0[srcIdx];
                float f0_b = fcpeF0[srcIdx + 1];

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
            } else if (srcIdx < static_cast<int>(fcpeF0.size())) {
                audioData.f0[i] = fcpeF0[srcIdx];
            } else {
                audioData.f0[i] = fcpeF0.back() > 0.0f ? fcpeF0.back() : 0.0f;
            }
        }
    } else {
        audioData.f0.clear();
    }

    // Create voiced mask
    audioData.voicedMask.resize(audioData.f0.size());
    for (size_t i = 0; i < audioData.f0.size(); ++i) {
        audioData.voicedMask[i] = audioData.f0[i] > 0;
    }
}

void AudioAnalyzer::extractF0WithYIN(AudioData& audioData) {
    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();

    auto* detector = pitchDetector ? pitchDetector.get() : externalPitchDetector;
    auto [f0Values, voicedValues] = detector->extractF0(samples, numSamples);
    audioData.f0 = std::move(f0Values);
    audioData.voicedMask = std::move(voicedValues);
}

void AudioAnalyzer::segmentIntoNotes(Project& project) {
    auto& audioData = project.getAudioData();
    auto& notes = project.getNotes();
    notes.clear();

    if (audioData.f0.empty())
        return;

    // Try SOME model first
    auto* detector = someDetector ? someDetector.get() : externalSOMEDetector;
    if (detector && detector->isLoaded() && audioData.waveform.getNumSamples() > 0) {
        segmentWithSOME(project);
        return;
    }

    // Fallback to F0-based segmentation
    segmentFallback(project);
}

void AudioAnalyzer::segmentWithSOME(Project& project) {
    auto& audioData = project.getAudioData();
    auto& notes = project.getNotes();

    const float* samples = audioData.waveform.getReadPointer(0);
    int numSamples = audioData.waveform.getNumSamples();
    const int f0Size = static_cast<int>(audioData.f0.size());

    auto* detector = someDetector ? someDetector.get() : externalSOMEDetector;
    detector->detectNotesStreaming(
        samples, numSamples, SOMEDetector::SAMPLE_RATE,
        [&](const std::vector<SOMEDetector::NoteEvent>& chunkNotes) {
            for (const auto& someNote : chunkNotes) {
                if (someNote.isRest)
                    continue;

                int f0Start = std::max(0, std::min(someNote.startFrame, f0Size - 1));
                int f0End = std::max(f0Start + 1, std::min(someNote.endFrame, f0Size));

                if (f0End - f0Start < 3)
                    continue;

                // Calculate average MIDI from actual F0 data
                float midiSum = 0.0f;
                int midiCount = 0;
                for (int j = f0Start; j < f0End; ++j) {
                    if (j < static_cast<int>(audioData.voicedMask.size()) &&
                        audioData.voicedMask[j] && audioData.f0[j] > 0) {
                        midiSum += freqToMidi(audioData.f0[j]);
                        midiCount++;
                    }
                }

                float midi = someNote.midiNote;
                if (midiCount > 0) {
                    midi = midiSum / midiCount;
                }

                Note note(f0Start, f0End, midi);
                std::vector<float> f0Values(audioData.f0.begin() + f0Start,
                                            audioData.f0.begin() + f0End);
                note.setF0Values(std::move(f0Values));
                notes.push_back(note);
            }
        },
        nullptr
    );

    juce::Thread::sleep(100);

    if (!audioData.f0.empty())
        PitchCurveProcessor::rebuildCurvesFromSource(project, audioData.f0);
}

void AudioAnalyzer::segmentFallback(Project& project) {
    auto& audioData = project.getAudioData();
    auto& notes = project.getNotes();

    auto finalizeNote = [&](int start, int end) {
        if (end - start < 5)
            return;

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
            return;

        float midi = midiSum / midiCount;
        Note note(start, end, midi);
        std::vector<float> f0Values(audioData.f0.begin() + start,
                                    audioData.f0.begin() + end);
        note.setF0Values(std::move(f0Values));
        notes.push_back(note);
    };

    constexpr float pitchSplitThreshold = 0.5f;
    constexpr int minFramesForSplit = 3;
    constexpr int maxUnvoicedGap = INT_MAX;

    bool inNote = false;
    int noteStart = 0;
    int currentMidiNote = 0;
    int pitchChangeCount = 0;
    int pitchChangeStart = 0;
    int unvoicedCount = 0;

    for (size_t i = 0; i < audioData.f0.size(); ++i) {
        bool voiced = i < audioData.voicedMask.size() && audioData.voicedMask[i];

        if (voiced && !inNote) {
            inNote = true;
            noteStart = static_cast<int>(i);
            currentMidiNote = static_cast<int>(std::round(freqToMidi(audioData.f0[i])));
            pitchChangeCount = 0;
            unvoicedCount = 0;
        } else if (voiced && inNote) {
            unvoicedCount = 0;
            float currentMidi = freqToMidi(audioData.f0[i]);
            int quantizedMidi = static_cast<int>(std::round(currentMidi));

            if (quantizedMidi != currentMidiNote &&
                std::abs(currentMidi - currentMidiNote) > pitchSplitThreshold) {
                if (pitchChangeCount == 0)
                    pitchChangeStart = static_cast<int>(i);
                pitchChangeCount++;

                if (pitchChangeCount >= minFramesForSplit) {
                    finalizeNote(noteStart, pitchChangeStart);
                    noteStart = pitchChangeStart;
                    currentMidiNote = quantizedMidi;
                    pitchChangeCount = 0;
                }
            } else {
                pitchChangeCount = 0;
            }
        } else if (!voiced && inNote) {
            unvoicedCount++;
            if (unvoicedCount > maxUnvoicedGap) {
                finalizeNote(noteStart, static_cast<int>(i) - unvoicedCount);
                inNote = false;
                pitchChangeCount = 0;
                unvoicedCount = 0;
            }
        }
    }

    if (inNote) {
        finalizeNote(noteStart, static_cast<int>(audioData.f0.size()));
    }

    if (!audioData.f0.empty())
        PitchCurveProcessor::rebuildCurvesFromSource(project, audioData.f0);
}
