#include "AudioFileManager.h"

AudioFileManager::AudioFileManager() = default;

AudioFileManager::~AudioFileManager() {
    cancelFlag = true;
    if (loaderThread.joinable())
        loaderThread.join();
}

void AudioFileManager::showOpenDialog(std::function<void(const juce::File&)> onFileSelected) {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an audio file...", juce::File{}, "*.wav;*.mp3;*.flac;*.aiff");

    auto chooserFlags = juce::FileBrowserComponent::openMode |
                        juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(chooserFlags, [onFileSelected](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file.existsAsFile() && onFileSelected)
            onFileSelected(file);
    });
}

void AudioFileManager::showSaveDialog(const juce::File& defaultPath,
                                      std::function<void(const juce::File&)> onFileSelected) {
    fileChooser = std::make_unique<juce::FileChooser>("Save project...", defaultPath, "*.peproj");

    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                        juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(chooserFlags, [onFileSelected](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{} && onFileSelected) {
            auto finalFile = file.getFileExtension().isEmpty()
                ? file.withFileExtension("peproj")
                : file;
            onFileSelected(finalFile);
        }
    });
}

void AudioFileManager::showExportDialog(const juce::File& defaultPath,
                                        std::function<void(const juce::File&)> onFileSelected) {
    fileChooser = std::make_unique<juce::FileChooser>("Export audio...", defaultPath, "*.wav");

    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                        juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(chooserFlags, [onFileSelected](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{} && onFileSelected) {
            auto finalFile = file.getFileExtension().isEmpty()
                ? file.withFileExtension("wav")
                : file;
            onFileSelected(finalFile);
        }
    });
}

void AudioFileManager::loadAudioFileAsync(const juce::File& file,
                                          ProgressCallback onProgress,
                                          LoadCompleteCallback onComplete) {
    if (isLoadingAudio.load())
        return;

    cancelFlag = false;
    isLoadingAudio = true;

    if (loaderThread.joinable())
        loaderThread.join();

    loaderThread = std::thread([this, file, onProgress, onComplete]() {
        if (onProgress)
            onProgress(0.05, "Loading audio...");

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr || cancelFlag.load()) {
            isLoadingAudio = false;
            return;
        }

        const int numSamples = static_cast<int>(reader->lengthInSamples);
        const int srcSampleRate = static_cast<int>(reader->sampleRate);

        if (onProgress)
            onProgress(0.10, "Reading audio...");

        // Read audio data
        juce::AudioBuffer<float> buffer;
        if (reader->numChannels == 1) {
            buffer.setSize(1, numSamples);
            reader->read(&buffer, 0, numSamples, 0, true, false);
        } else {
            juce::AudioBuffer<float> stereoBuffer(2, numSamples);
            reader->read(&stereoBuffer, 0, numSamples, 0, true, true);
            buffer = convertToMono(stereoBuffer);
        }

        if (cancelFlag.load()) {
            isLoadingAudio = false;
            return;
        }

        // Resample if needed
        if (srcSampleRate != SAMPLE_RATE) {
            if (onProgress)
                onProgress(0.18, "Resampling...");
            buffer = resampleIfNeeded(buffer, srcSampleRate, SAMPLE_RATE);
        }

        if (cancelFlag.load()) {
            isLoadingAudio = false;
            return;
        }

        if (onProgress)
            onProgress(0.22, "Audio loaded");

        isLoadingAudio = false;

        // Call completion on message thread
        juce::MessageManager::callAsync([onComplete, buffer = std::move(buffer), file]() mutable {
            if (onComplete)
                onComplete(std::move(buffer), SAMPLE_RATE, file);
        });
    });
}

void AudioFileManager::exportAudioFileAsync(const juce::File& file,
                                            const juce::AudioBuffer<float>& buffer,
                                            int sampleRate,
                                            ProgressCallback onProgress,
                                            ExportCompleteCallback onComplete) {
    if (onProgress)
        onProgress(0.0, "Exporting...");

    // Export synchronously for now (could be made async if needed)
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(new juce::FileOutputStream(file),
                                  sampleRate,
                                  static_cast<unsigned int>(buffer.getNumChannels()),
                                  16, {}, 0));

    bool success = false;
    if (writer) {
        success = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    if (onProgress)
        onProgress(1.0, success ? "Export complete" : "Export failed");

    if (onComplete)
        onComplete(success);
}

bool AudioFileManager::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        juce::File file(f);
        auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".peproj")
            return true;
    }
    return false;
}

juce::File AudioFileManager::getFirstAudioFile(const juce::StringArray& files) {
    for (const auto& f : files) {
        juce::File file(f);
        auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".peproj")
            return file;
    }
    return {};
}

juce::AudioBuffer<float> AudioFileManager::resampleIfNeeded(const juce::AudioBuffer<float>& buffer,
                                                            int srcSampleRate,
                                                            int targetSampleRate) {
    if (srcSampleRate == targetSampleRate)
        return buffer;

    const double ratio = static_cast<double>(srcSampleRate) / targetSampleRate;
    const int numSamples = buffer.getNumSamples();
    const int newNumSamples = static_cast<int>(numSamples / ratio);

    juce::AudioBuffer<float> resampledBuffer(1, newNumSamples);
    const float* src = buffer.getReadPointer(0);
    float* dst = resampledBuffer.getWritePointer(0);

    for (int i = 0; i < newNumSamples; ++i) {
        const double srcPos = i * ratio;
        const int srcIndex = static_cast<int>(srcPos);
        const double frac = srcPos - srcIndex;

        if (srcIndex + 1 < numSamples)
            dst[i] = static_cast<float>(src[srcIndex] * (1.0 - frac) + src[srcIndex + 1] * frac);
        else
            dst[i] = src[srcIndex];
    }

    return resampledBuffer;
}

juce::AudioBuffer<float> AudioFileManager::convertToMono(const juce::AudioBuffer<float>& stereoBuffer) {
    const int numSamples = stereoBuffer.getNumSamples();
    juce::AudioBuffer<float> monoBuffer(1, numSamples);

    const float* left = stereoBuffer.getReadPointer(0);
    const float* right = stereoBuffer.getReadPointer(1);
    float* mono = monoBuffer.getWritePointer(0);

    for (int i = 0; i < numSamples; ++i)
        mono[i] = (left[i] + right[i]) * 0.5f;

    return monoBuffer;
}
